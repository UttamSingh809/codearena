/*
 * worker.c — main worker loop.
 *
 * The worker:
 *   1. Dequeues a job descriptor from the shared-memory queue.
 *   2. Claims the submission in the database (SKIP LOCKED).
 *   3. Fetches source code and test cases.
 *   4. Compiles (if necessary) into a temp directory.
 *   5. Runs the sandbox on each test case.
 *   6. Aggregates verdicts and score.
 *   7. Commits the atomic grading transaction.
 *   8. Repeats.
 *
 * If the database claim fails (submission already handled, or
 * cancelled), the worker drops the descriptor and continues.
 */

#include "worker_internal.h"
#include "job_processor.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* Signal handling                                                    */
/* ------------------------------------------------------------------ */

static volatile sig_atomic_t g_stop_requested = 0;

static void handle_signal(int sig)
{
    (void)sig;
    g_stop_requested = 1;
    /* Force exit: sem_wait() in queue_dequeue blocks indefinitely
     * even after a signal handler returns, because glibc's sem_wait
     * retries on EINTR. _exit() is async-signal-safe and terminates
     * the process immediately. The kernel reclaims all resources
     * (fds, mmaps, cgroup membership) on exit. */
    _exit(0);
}

static void install_signal_handlers(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT,  &sa, NULL);

    /* Ignore SIGPIPE — if the sandbox dies mid-write, we don't want
     * the worker killed by a broken pipe. */
    signal(SIGPIPE, SIG_IGN);
}


/* ------------------------------------------------------------------ */
/* Lifecycle                                                          */
/* ------------------------------------------------------------------ */

worker_t *worker_create(const worker_config_t *cfg)
{
    if (!cfg || !cfg->worker_id || !cfg->db_url || !cfg->queue_name) {
        errno = EINVAL;
        return NULL;
    }

    worker_t *w = calloc(1, sizeof(*w));
    if (!w) return NULL;

    /* Copy the config (shallow — caller keeps strings alive). */
    w->cfg = *cfg;

    /* Connect to the database. */
    w->db = db_connect(cfg->db_url);
    if (!w->db) {
        fprintf(stderr, "worker[%s]: db_connect failed\n", cfg->worker_id);
        free(w);
        return NULL;
    }

    /* Open the queue. Retry a few times because the API may not have
     * created it yet at worker start. */
    for (int attempt = 0; attempt < 30; attempt++) {
        w->queue = queue_open(cfg->queue_name);
        if (w->queue) break;
        fprintf(stderr, "worker[%s]: queue_open(%s) failed (attempt %d), retrying...\n",
                cfg->worker_id, cfg->queue_name, attempt + 1);
        sleep(1);
    }
    if (!w->queue) {
        fprintf(stderr, "worker[%s]: queue_open(%s) failed permanently\n",
                cfg->worker_id, cfg->queue_name);
        db_disconnect(w->db);
        free(w);
        return NULL;
    }

    install_signal_handlers();

    fprintf(stderr, "worker[%s]: connected, waiting for jobs\n", cfg->worker_id);
    return w;
}

void worker_stop(worker_t *w)
{
    if (w) w->stop = 1;
}

void worker_destroy(worker_t *w)
{
    if (!w) return;
    if (w->queue) queue_close(w->queue);
    if (w->db)    db_disconnect(w->db);
    free(w);
}

/* ------------------------------------------------------------------ */
/* Job processing                                                     */
/* ------------------------------------------------------------------ */

/*
 * Claim a submission in the DB. Returns 0 on success and populates
 * *out_user, *out_problem, *out_lang, *out_source (heap). Returns -1
 * if the submission is not actionable (already handled, cancelled).
 */
static int claim_and_fetch(worker_t *w, uint64_t submission_id,
                           uint64_t *out_user,
                           uint64_t *out_problem,
                           char    **out_lang,
                           char    **out_source)
{
    /*
     * Single atomic UPDATE with RETURNING. The WHERE clause uses
     * FOR UPDATE SKIP LOCKED on the inner SELECT so two workers
     * can't both claim the same row.
     *
     * Note: we use the submission_id from the descriptor, not a
     * FIFO scan. The descriptor is our hint; the DB is the source
     * of truth. If the row's status isn't PENDING, we don't claim.
     */
    const char *sql =
        "UPDATE submissions "
        "SET status = 'PROCESSING', "
        "    processing_started_at = NOW(), "
        "    worker_id = $2 "
        "WHERE id = $1 "
        "  AND status = 'PENDING' "
        "RETURNING user_id, problem_id, language, source_code";

    char sid[32];
    snprintf(sid, sizeof(sid), "%llu", (unsigned long long)submission_id);

    const char *params[2] = { sid, w->cfg.worker_id };

    db_result_t *r = db_exec(w->db, sql, 2, params);
    if (!r) {
        fprintf(stderr, "worker[%s]: claim query failed: %s\n",
                w->cfg.worker_id, db_client_last_error(w->db));
        return -1;
    }

    if (db_result_rows(r) == 0) {
        db_result_free(r);
        return -1;   /* not actionable */
    }

    *out_user    = (uint64_t)db_result_get_int64(r, 0, 0);
    *out_problem = (uint64_t)db_result_get_int64(r, 0, 1);
    const char *lang = db_result_get(r, 0, 2);
    const char *src  = db_result_get(r, 0, 3);

    *out_lang   = strdup(lang ? lang : "");
    *out_source = strdup(src  ? src  : "");

    db_result_free(r);

    if (!*out_lang || !*out_source) {
        free(*out_lang);
        free(*out_source);
        return -1;
    }
    return 0;
}

/*
 * Commit the grading result atomically.
 *
 * The transaction:
 *   BEGIN;
 *   SELECT id, status FROM submissions WHERE id = $1 FOR UPDATE;
 *   -- if status != PROCESSING → ROLLBACK
 *   INSERT INTO results (...) VALUES (...) ON CONFLICT DO NOTHING;
 *   UPDATE submissions SET status='COMPLETED' WHERE id = $1;
 *   COMMIT;
 *
 * Returns 0 on success, -1 on failure (already rolled back).
 */
static int commit_result(worker_t *w, uint64_t submission_id,
                         const grading_result_t *g)
{
    if (db_begin(w->db) < 0) {
        fprintf(stderr, "worker[%s]: BEGIN failed: %s\n",
                w->cfg.worker_id, db_client_last_error(w->db));
        return -1;
    }

    /* Step 1: lock and verify */
    char sid[32];
    snprintf(sid, sizeof(sid), "%llu", (unsigned long long)submission_id);
    const char *lock_sql =
        "SELECT status, user_id, problem_id FROM submissions WHERE id = $1 FOR UPDATE";
    const char *lock_params[1] = { sid };

    db_result_t *lr = db_exec(w->db, lock_sql, 1, lock_params);
    if (!lr) {
        db_rollback(w->db);
        return -1;
    }
    if (db_result_rows(lr) == 0) {
        db_result_free(lr);
        db_rollback(w->db);
        return -1;
    }
    const char *status = db_result_get(lr, 0, 0);
    if (!status || strcmp(status, "PROCESSING") != 0) {
        db_result_free(lr);
        db_rollback(w->db);
        return -1;
    }

    /* NEW: Extract user_id and problem_id while the row is still locked */
    uint64_t user_id = (uint64_t)db_result_get_int64(lr, 0, 1);
    uint64_t problem_id = (uint64_t)db_result_get_int64(lr, 0, 2);

    db_result_free(lr);

    /* Step 2: insert result */
    char score_buf[16], runtime_buf[16], memory_buf[16];
    char test_total_buf[16], test_passed_buf[16], first_failed_buf[16];

    snprintf(score_buf, sizeof(score_buf), "%d", g->score);
    snprintf(runtime_buf, sizeof(runtime_buf), "%d", g->runtime_ms);
    snprintf(memory_buf, sizeof(memory_buf), "%d", g->memory_kb);
    snprintf(test_total_buf, sizeof(test_total_buf), "%d", g->test_total);
    snprintf(test_passed_buf, sizeof(test_passed_buf), "%d", g->test_passed);
    snprintf(first_failed_buf, sizeof(first_failed_buf), "%d", g->first_failed);

    const char *ins_sql =
        "INSERT INTO results "
        "  (submission_id, verdict, score, runtime_ms, memory_kb, "
        "   stdout, stderr, graded_by, "
        "   test_total, test_passed, first_failed) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11) "
        "ON CONFLICT (submission_id) DO NOTHING";

    const char *ins_params[11] = {
        sid,
        g->verdict,
        score_buf,
        runtime_buf,
        memory_buf,
        g->stdout_capture ? g->stdout_capture : "",
        g->stderr_capture ? g->stderr_capture : "",
        w->cfg.worker_id,
        test_total_buf,
        test_passed_buf,
        first_failed_buf,
    };

    db_result_t *ir = db_exec(w->db, ins_sql, 11, ins_params);
    if (!ir)
    {
        fprintf(stderr, "worker[%s]: result insert failed: %s\n",
                w->cfg.worker_id, db_client_last_error(w->db));
        db_rollback(w->db);
        return -1;
    }
    db_result_free(ir);

    /* Step 2b: insert per-test-case results */
    if (g->cases && g->cases_count > 0)
    {
        for (int i = 0; i < g->cases_count; i++)
        {
            const test_case_result_t *c = &g->cases[i];

            char tcid[24], ord[16], rt[16], mem[16], tsub[24];
            snprintf(tcid, sizeof(tcid), "%ld", c->test_case_id);
            snprintf(ord, sizeof(ord), "%d", c->ordinal);
            snprintf(rt, sizeof(rt), "%d", c->runtime_ms);
            snprintf(mem, sizeof(mem), "%d", c->memory_kb);
            snprintf(tsub, sizeof(tsub), "%llu",
                     (unsigned long long)submission_id);

            const char *tc_params[7] = {
                tsub,
                tcid,
                ord,
                c->verdict,
                c->actual_output ? c->actual_output : "",
                rt,
                mem,
            };

            db_result_t *tcr = db_exec(w->db,
                                       "INSERT INTO test_case_results "
                                       "  (submission_id, test_case_id, ordinal, verdict, "
                                       "   actual_output, runtime_ms, memory_kb) "
                                       "VALUES ($1, $2, $3, $4, $5, $6, $7) "
                                       "ON CONFLICT (submission_id, test_case_id) DO NOTHING",
                                       7, tc_params);
            if (tcr)
            {
                db_result_free(tcr);
            }
            else
            {
                fprintf(stderr, "worker: failed to insert test_case_result for case %d\n", c->ordinal);
            }
        }
    }

    /* Step 3: mark completed */
    const char *upd_sql =
        "UPDATE submissions SET status = 'COMPLETED', "
        "  processing_started_at = NULL, worker_id = NULL, "
        "  test_current = NULL "
        "WHERE id = $1";
    db_result_t *ur = db_exec(w->db, upd_sql, 1, lock_params);
    if (!ur) {
        db_rollback(w->db);
        return -1;
    }
    db_result_free(ur);
    

    /* Step 3b: Award XP if the verdict is AC and this is the first AC for this problem. */
    if (strcmp(g->verdict, "AC") == 0)
    {
        // Check if there are any other ACs for this user+problem
        char user_id_buf[24], problem_id_buf[24];
        snprintf(user_id_buf, sizeof(user_id_buf), "%llu", (unsigned long long)user_id);
        snprintf(problem_id_buf, sizeof(problem_id_buf), "%llu", (unsigned long long)problem_id);

        const char *check_sql =
            "SELECT COUNT(*) FROM results r "
            "JOIN submissions s ON s.id = r.submission_id "
            "WHERE s.user_id = $1 AND s.problem_id = $2 AND r.verdict = 'AC' AND s.id != $3";

        const char *check_params[3] = {user_id_buf, problem_id_buf, sid};
        db_result_t *cr = db_exec(w->db, check_sql, 3, check_params);

        if (cr)
        {
            int64_t previous_acs = db_result_get_int64(cr, 0, 0);
            db_result_free(cr);

            if (previous_acs == 0)
            {
                // This is the first AC for this user+problem. Award XP.
                // We need the problem's difficulty to calculate XP.
                const char *diff_sql = "SELECT difficulty FROM problems WHERE id = $1";
                const char *diff_params[1] = {problem_id_buf};
                db_result_t *dr = db_exec(w->db, diff_sql, 1, diff_params);

                if (dr && db_result_rows(dr) > 0)
                {
                    const char *difficulty = db_result_get(dr, 0, 0);
                    int xp_to_award = 0;

                    if (difficulty)
                    {
                        if (strcmp(difficulty, "easy") == 0)
                            xp_to_award = 10;
                        else if (strcmp(difficulty, "medium") == 0)
                            xp_to_award = 30;
                        else if (difficulty == "hard" || strcmp(difficulty, "hard") == 0)
                            xp_to_award = 60;
                    }
                    db_result_free(dr);

                    if (xp_to_award > 0)
                    {
                        char xp_buf[16];
                        snprintf(xp_buf, sizeof(xp_buf), "%d", xp_to_award);
                        const char *xp_sql = "UPDATE users SET xp = xp + $1 WHERE id = $2";
                        const char *xp_params[2] = {xp_buf, user_id_buf};
                        db_result_t *xr = db_exec(w->db, xp_sql, 2, xp_params);
                        if (xr)
                            db_result_free(xr);
                    }
                }
            }
        }
    }

    if (db_commit(w->db) < 0) {
        fprintf(stderr, "worker[%s]: COMMIT failed: %s\n",
                w->cfg.worker_id, db_client_last_error(w->db));
        db_rollback(w->db);
        return -1;
    }

    return 0;
}

int worker_process_one(worker_t *w, const job_descriptor_t *job)
{
    if (!w || !job) return -1;

    uint64_t submission_id = job->submission_id;

    /* Step 1: claim in DB */
    uint64_t user_id = 0, problem_id = 0;
    char    *language = NULL, *source = NULL;

    if (claim_and_fetch(w, submission_id, &user_id, &problem_id,
                        &language, &source) < 0) {
        /* Not actionable — drop the descriptor silently. */
        return 0;
    }

    fprintf(stderr, "worker[%s]: processing submission %llu (user=%llu problem=%llu lang=%s)\n",
            w->cfg.worker_id,
            (unsigned long long)submission_id,
            (unsigned long long)user_id,
            (unsigned long long)problem_id,
            language);

    /* Step 2: run sandbox on every test case */
    grading_result_t g;
    memset(&g, 0, sizeof(g));
    strcpy(g.verdict, "FAILED");   /* default if something goes wrong */

    int rc = job_process(w, submission_id, problem_id, language, source, &g);
    free(language);
    free(source);

    if (rc < 0) {
        fprintf(stderr, "worker[%s]: job_process failed for %llu\n",
                w->cfg.worker_id, (unsigned long long)submission_id);
        /* On internal failure, we do NOT commit a result. The watchdog
         * will requeue the submission after the lease expires. */
        return -1;
    }

    /* Step 3: commit the result atomically */
    if (commit_result(w, submission_id, &g) < 0) {
        fprintf(stderr, "worker[%s]: commit failed for %llu: %s\n",
                w->cfg.worker_id, (unsigned long long)submission_id,
                db_client_last_error(w->db));
        return -1;
    }

    fprintf(stderr, "worker[%s]: submission %llu verdict=%s score=%d runtime=%dms memory=%dKB\n",
            w->cfg.worker_id,
            (unsigned long long)submission_id,
            g.verdict, g.score, g.runtime_ms, g.memory_kb);

    grading_result_free(&g);

    return 0;
}

/* ------------------------------------------------------------------ */
/* Main loop                                                          */
/* ------------------------------------------------------------------ */

int worker_run(worker_t *w)
{
    if (!w) return -1;

    while (!w->stop && !g_stop_requested) {
        job_descriptor_t job;

        /* Blocking dequeue. The queue returns only when a job is
         * available or the semaphore is interrupted (EINTR), which
         * we handle by checking the stop flag. */
        int rc = queue_dequeue(w->queue, &job);
        if (rc != CODEARENA_QUEUE_OK) {
            if (rc == CODEARENA_QUEUE_EMPTY) continue;
            fprintf(stderr, "worker[%s]: dequeue failed rc=%d\n",
                    w->cfg.worker_id, rc);
            break;
        }

        if (w->stop || g_stop_requested) break;

        (void)worker_process_one(w, &job);
    }

    fprintf(stderr, "worker[%s]: shutting down\n", w->cfg.worker_id);
    return 0;
}