/*
 * job_processor.c — compile, run, grade one submission.
 *
 * Responsibilities:
 *   1. Fetch test cases for the problem.
 *   2. Write the source code to a per-job temp directory.
 *   3. Compile (for compiled languages) with rlimits.
 *   4. For each test case:
 *        a. Run the sandbox.
 *        b. Determine the verdict.
 *        c. Aggregate into the overall result.
 *   5. Free resources.
 *
 * The sandbox itself handles isolation, resource limits, and the
 * wall-clock watchdog. This file orchestrates.
 */

#include "worker_internal.h"
#include "job_processor.h"
#include "sandbox.h"
#include "verdict.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* Verdict priority — higher wins the aggregate                       */
/* ------------------------------------------------------------------ */
/*
 * If any test case fails with a "worse" verdict, that verdict wins.
 * Order (worst to best):
 *   FAILED > CE > RE > TLE > MLE > OLE > WA > AC
 * This is a simplification: in real judges, CE blocks all tests.
 */
static int verdict_priority(const char *v)
{
    if (strcmp(v, "AC")   == 0) return 10;
    if (strcmp(v, "WA")   == 0) return 20;
    if (strcmp(v, "PE")   == 0) return 25;
    if (strcmp(v, "OLE")  == 0) return 30;
    if (strcmp(v, "MLE")  == 0) return 40;
    if (strcmp(v, "TLE")  == 0) return 50;
    if (strcmp(v, "RE")   == 0) return 60;
    if (strcmp(v, "CE")   == 0) return 70;
    if (strcmp(v, "FAILED")== 0) return 100;
    return 90;   /* unknown */
}

/* ------------------------------------------------------------------ */
/* Test cases                                                         */
/* ------------------------------------------------------------------ */

typedef struct {
    int     ordinal;
    char   *input;
    char   *expected;
    int     points;
} test_case_t;

typedef struct {
    test_case_t *cases;
    int          count;
} test_case_set_t;

static void test_case_set_free(test_case_set_t *ts)
{
    if (!ts) return;
    for (int i = 0; i < ts->count; i++) {
        free(ts->cases[i].input);
        free(ts->cases[i].expected);
    }
    free(ts->cases);
    ts->cases = NULL;
    ts->count = 0;
}

static int fetch_test_cases(worker_t *w, uint64_t problem_id,
                            test_case_set_t *out)
{
    memset(out, 0, sizeof(*out));

    char pid[32];
    snprintf(pid, sizeof(pid), "%llu", (unsigned long long)problem_id);

    const char *sql =
        "SELECT ordinal, input_data, expected_output, points "
        "FROM test_cases WHERE problem_id = $1 ORDER BY ordinal";

    const char *params[1] = { pid };
    db_result_t *r = db_exec(w->db, sql, 1, params);
    if (!r) return -1;

    int n = db_result_rows(r);
    if (n == 0) {
        db_result_free(r);
        return -1;   /* a problem with no test cases is a config error */
    }

    out->cases = calloc((size_t)n, sizeof(test_case_t));
    if (!out->cases) {
        db_result_free(r);
        return -1;
    }

    for (int i = 0; i < n; i++) {
        out->cases[i].ordinal  = db_result_get_int32(r, i, 0);
        const char *inp = db_result_get(r, i, 1);
        const char *exp = db_result_get(r, i, 2);
        out->cases[i].input    = strdup(inp ? inp : "");
        out->cases[i].expected = strdup(exp ? exp : "");
        out->cases[i].points   = db_result_get_int32(r, i, 3);
        if (!out->cases[i].input || !out->cases[i].expected) {
            test_case_set_free(out);
            db_result_free(r);
            return -1;
        }
    }
    out->count = n;
    db_result_free(r);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Compilation                                                        */
/* ------------------------------------------------------------------ */
/*
 * Compile the source into a binary in the job's temp directory.
 *
 * We do NOT run the compiler inside the sandbox. The compiler reads
 * only our source file and writes only our output file, so it doesn't
 * need filesystem isolation. It DOES need CPU and memory limits, so
 * we set rlimits via setrlimit before exec.
 *
 * Returns 0 on success, -1 on compile error (stderr captured in
 * out_err, heap).
 */
static int compile_source(worker_t *w, const char *language,
                          const char *src_path, const char *out_path,
                          char **out_err)
{
    *out_err = NULL;

    const char *compiler = NULL;
    const char *args[16] = {0};
    int         nargs = 0;

    if (strcmp(language, "c") == 0) {
        compiler = w->cfg.c_compiler ? w->cfg.c_compiler : "gcc";
        args[nargs++] = compiler;
        args[nargs++] = "-O2";
        args[nargs++] = "-std=c11";
        args[nargs++] = "-o";
        args[nargs++] = out_path;
        args[nargs++] = src_path;
    } else if (strcmp(language, "cpp") == 0) {
        compiler = w->cfg.cpp_compiler ? w->cfg.cpp_compiler : "g++";
        args[nargs++] = compiler;
        args[nargs++] = "-O2";
        args[nargs++] = "-std=c++17";
        args[nargs++] = "-o";
        args[nargs++] = out_path;
        args[nargs++] = src_path;
    } else {
        /* Python and other interpreted languages don't compile. */
        return 0;
    }

    args[nargs] = NULL;

    /* Create pipes for capturing compiler stderr. */
    int err_pipe[2];
    if (pipe(err_pipe) < 0) return -1;

    pid_t child = fork();
    if (child < 0) {
        close(err_pipe[0]); close(err_pipe[1]);
        return -1;
    }

    if (child == 0) {
        /* Child: redirect stderr, set rlimits, exec compiler. */
        close(err_pipe[0]);
        dup2(err_pipe[1], STDERR_FILENO);
        close(err_pipe[1]);

        struct rlimit rl;
        rl.rlim_cur = rl.rlim_max = 10;      /* 10s CPU for compilation */
        setrlimit(RLIMIT_CPU, &rl);
        rl.rlim_cur = rl.rlim_max = 512 * 1024 * 1024;  /* 512 MB */
        setrlimit(RLIMIT_AS, &rl);
        rl.rlim_cur = rl.rlim_max = 32;      /* 32 processes */
        setrlimit(RLIMIT_NPROC, &rl);
        rl.rlim_cur = rl.rlim_max = 0;
        setrlimit(RLIMIT_CORE, &rl);

        execvp(compiler, (char *const *)args);
        _exit(127);
    }

    /* Parent: read compiler stderr, wait. */
    close(err_pipe[1]);

    char buf[4096];
    size_t total = 0, cap = 8192;
    char *err_buf = malloc(cap);
    if (!err_buf) {
        close(err_pipe[0]);
        waitpid(child, NULL, 0);
        return -1;
    }

    ssize_t n;
    while ((n = read(err_pipe[0], buf, sizeof(buf))) > 0) {
        if (total + (size_t)n >= cap - 1) {
            size_t newcap = cap * 2;
            char *nb = realloc(err_buf, newcap);
            if (!nb) break;
            err_buf = nb;
            cap = newcap;
        }
        memcpy(err_buf + total, buf, (size_t)n);
        total += (size_t)n;
    }
    close(err_pipe[0]);
    err_buf[total < cap ? total : cap - 1] = '\0';

    int status = 0;
    waitpid(child, &status, 0);

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        *out_err = err_buf;
        return -1;
    }

    free(err_buf);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Per-test-case execution                                            */
/* ------------------------------------------------------------------ */

/*
 * Run the sandbox on a single test case and return its verdict.
 * Populates *out_stdout, *out_stderr (heap, may be NULL).
 */
static void run_one_test(worker_t *w, const char *binary_path,
                         const char *language,
                         const test_case_t *tc,
                         unsigned int time_limit_ms,
                         unsigned int memory_limit_mb,
                         char *out_verdict, size_t out_verdict_sz,
                         char **out_stdout, char **out_stderr,
                         int *out_runtime_ms, int *out_memory_kb)
{
    /*
     * For Python, the "binary" is the Python interpreter; the source
     * path becomes argv[1]. For C/C++, the binary is the executable.
     */
    char *child_argv[4];
    if (strcmp(language, "python") == 0)
    {
        child_argv[0] = (char *)"/usr/bin/python3";
        child_argv[1] = (char *)binary_path;
        child_argv[2] = NULL;
    }
    else
    {
        child_argv[0] = (char *)binary_path;
        child_argv[1] = NULL;
    }

    /* Build a unique cgroup name for this run. */
    char cgroup_name[128];
    snprintf(cgroup_name, sizeof(cgroup_name), "%s_sub_%ld_%d",
             w->cfg.worker_id, (long)time(NULL), (int)getpid());

    /*
     * Namespace policy for the per-test-case sandbox.
     *
     * Copy the worker's namespace preferences, then apply the same
     * auto-upgrade/auto-disable logic the sandbox CLI uses:
     *
     *   - Running as root: force enable_user_ns = 0. Root already has
     *     all capabilities, and writing uid_map from a root process
     *     fails with EPERM (the kernel refuses to map uids the process
     *     doesn't own in the parent namespace).
     *
     *   - Non-root with mount or pid namespaces requested: force
     *     enable_user_ns = 1. Without it, unshare(CLONE_NEWNS) and
     *     unshare(CLONE_NEWPID) fail with EPERM.
     */
    int ns_user = w->cfg.enable_user_ns;
    int ns_mnt = w->cfg.enable_mount_ns;
    int ns_pid = w->cfg.enable_pid_ns;

    if (geteuid() == 0)
    {
        ns_user = 0;
    }
    else if ((ns_mnt || ns_pid) && !ns_user)
    {
        ns_user = 1;
    }

    sandbox_config_t scfg = {
        .binary = child_argv[0],
        .argv = child_argv,
        .stdin_data = tc->input,
        .stdin_len = strlen(tc->input),
        .output_limit = (size_t)w->cfg.output_limit_kb * 1024,
        .timeout_ms = time_limit_ms + 500,
        .grace_ms = 500,

        .cpu_seconds = (time_limit_ms / 1000) + 2,
        .max_file_size = 16 * 1024 * 1024,
        .max_pids = 64,
        .stack_size = 8 * 1024 * 1024,
        .max_open_files = 64,

        .cgroup_root = w->cfg.cgroup_root,
        .cgroup_name = cgroup_name,
        .cgroup_cpu_pct = 100,
        .cgroup_memory_mb = memory_limit_mb,
        .cgroup_pids_max = 64,
        .cgroup_io_read_bps = 0,
        .cgroup_io_write_bps = 0,

        .enable_pid_ns = ns_pid,
        .enable_mount_ns = ns_mnt,
        .enable_net_ns = w->cfg.enable_net_ns,
        .enable_user_ns = ns_user,
        .enable_uts_ns = w->cfg.enable_uts_ns,
        .enable_ipc_ns = w->cfg.enable_ipc_ns,
        .rootfs = w->cfg.rootfs,
        .hostname = "codearena",
        .bind_host_paths = NULL,
        .bind_guest_paths = NULL,
        .bind_count = 0,

        .enable_seccomp = w->cfg.enable_seccomp,
        .seccomp_profile = w->cfg.seccomp_profile,
    };

    sandbox_result_t res;
    int rc = sandbox_run(&scfg, &res);
    if (rc != SANDBOX_OK)
    {
        /* Diagnostic: log the sandbox's failure so we can debug. */
        fprintf(stderr,
                "run_one_test: sandbox_run rc=%d "
                "ns_failed=%d sec_viol=%d oom=%d timed_out=%d\n",
                rc, res.ns_setup_failed, res.seccomp_violation,
                res.oom_killed, res.timed_out);
        fprintf(stderr, "  stderr: %s\n",
                res.stderr_buf ? res.stderr_buf : "(null)");
        fprintf(stderr, "  binary: %s\n", scfg.binary);
        fprintf(stderr, "  cgroup: %s/%s\n",
                scfg.cgroup_root ? scfg.cgroup_root : "(null)",
                scfg.cgroup_name ? scfg.cgroup_name : "(null)");
        fflush(stderr);

        snprintf(out_verdict, out_verdict_sz, "FAILED");
        *out_stdout = strdup("");
        *out_stderr = strdup(res.stderr_buf ? res.stderr_buf
                                            : "sandbox_run failed");
        *out_runtime_ms = 0;
        *out_memory_kb = 0;
        sandbox_result_free(&res);
        return;
    }

    verdict_input_t vin = {
        .expected = tc->expected,
        .expected_len = strlen(tc->expected),
        .strict_whitespace = 0,
    };
    verdict_determine(&res, &vin, out_verdict, out_verdict_sz);

    *out_stdout = strdup(res.stdout_buf ? res.stdout_buf : "");
    *out_stderr = strdup(res.stderr_buf ? res.stderr_buf : "");
    *out_runtime_ms = (int)res.wall_time_ms;
    *out_memory_kb = (int)(res.cgroup_memory_peak_bytes / 1024);

    sandbox_result_free(&res);
}
/* ------------------------------------------------------------------ */
/* Main entry point                                                   */
/* ------------------------------------------------------------------ */

int job_process(worker_t *w, uint64_t submission_id,
                uint64_t problem_id, const char *language,
                const char *source, grading_result_t *g)
{
    memset(g, 0, sizeof(*g));
    strcpy(g->verdict, "FAILED");

    /* Fetch test cases. */
    test_case_set_t ts;
    if (fetch_test_cases(w, problem_id, &ts) < 0) {
        fprintf(stderr, "worker: no test cases for problem %llu\n",
                (unsigned long long)problem_id);
        return -1;
    }

    /* Create a per-job temp directory. */
    const char *tmp_root = w->cfg.sandbox_tmp ? w->cfg.sandbox_tmp : "/tmp/codearena";

    /* Ensure the temp root exists. mkdir() doesn't create parents,
     * and /tmp is often cleared on boot, so the root may be missing. */
    if (mkdir(tmp_root, 0755) < 0 && errno != EEXIST)
    {
        fprintf(stderr, "worker: cannot create temp root %s: %s\n",
                tmp_root, strerror(errno));
        test_case_set_free(&ts);
        return -1;
    }

    char tmpdir[256];
    snprintf(tmpdir, sizeof(tmpdir), "%s/sub_%llu_%d",
             tmp_root,
             (unsigned long long)submission_id, (int)getpid());

    if (mkdir(tmpdir, 0755) < 0 && errno != EEXIST)
    {
        fprintf(stderr, "worker: mkdir %s failed: %s\n", tmpdir, strerror(errno));
        test_case_set_free(&ts);
        return -1;
    }

    /* Write source to a file. */
    const char *src_name = NULL;
    if (strcmp(language, "c") == 0)         src_name = "sol.c";
    else if (strcmp(language, "cpp") == 0)  src_name = "sol.cpp";
    else if (strcmp(language, "python")==0) src_name = "sol.py";
    else {
        fprintf(stderr, "worker: unsupported language %s\n", language);
        test_case_set_free(&ts);
        return -1;
    }

    char src_path[512];
    snprintf(src_path, sizeof(src_path), "%s/%s", tmpdir, src_name);

    FILE *sf = fopen(src_path, "w");
    if (!sf) {
        fprintf(stderr, "worker: cannot write %s: %s\n", src_path, strerror(errno));
        test_case_set_free(&ts);
        return -1;
    }
    fputs(source, sf);
    fclose(sf);

    /* Compile (C/C++ only). */
    char bin_path[512];
    if (strcmp(language, "python") == 0) {
        snprintf(bin_path, sizeof(bin_path), "%s", src_path);
    } else {
        snprintf(bin_path, sizeof(bin_path), "%s/sol", tmpdir);

        char *compile_err = NULL;
        if (compile_source(w, language, src_path, bin_path, &compile_err) < 0) {
            strcpy(g->verdict, "CE");
            g->score = 0;
            g->stderr_capture = compile_err ? compile_err : strdup("compilation failed");
            g->stdout_capture = strdup("");
            test_case_set_free(&ts);
            return 0;   /* CE is a valid result, not a worker error */
        }
        free(compile_err);
    }

    /* Run every test case. */
    int total_points = 0;
    int earned_points = 0;
    int worst_priority = 0;
    char worst_verdict[16] = "AC";
    int max_runtime = 0, max_memory = 0;
    char *first_failure_stdout = NULL;
    char *first_failure_stderr = NULL;

    g->test_total = ts.count;
    g->test_passed = 0;
    g->first_failed = 0;

    for (int i = 0; i < ts.count; i++)
    {
        char this_verdict[16];
        char *out = NULL, *err = NULL;
        int rt = 0, mem = 0;

        /* Update live progress in the DB before running the case. */
        char progress_buf[32];
        snprintf(progress_buf, sizeof(progress_buf), "%d", i + 1);
        const char *progress_params[2] = {progress_buf, NULL};
        char sid_buf[32];
        snprintf(sid_buf, sizeof(sid_buf), "%llu",
                 (unsigned long long)submission_id);
        progress_params[1] = sid_buf;

        db_result_t *pr = db_exec(w->db,
                                  "UPDATE submissions SET test_current = $1, "
                                  "test_total = $3, test_passed = $4 WHERE id = $2",
                                  4, (const char *const[]){progress_buf, sid_buf, ({ static char b[16];
                                          snprintf(b, sizeof(b), "%d", ts.count); b; }), ({ static char b[16];
                                          snprintf(b, sizeof(b), "%d", g->test_passed); b; })});
        if (pr)
            db_result_free(pr);

        run_one_test(w, bin_path, language, &ts.cases[i],
                     2000, 256,
                     this_verdict, sizeof(this_verdict),
                     &out, &err, &rt, &mem);

        total_points += ts.cases[i].points;
        int prio = verdict_priority(this_verdict);
        if (prio > worst_priority)
        {
            worst_priority = prio;
            strncpy(worst_verdict, this_verdict, sizeof(worst_verdict) - 1);
        }

        if (strcmp(this_verdict, "AC") == 0)
        {
            earned_points += ts.cases[i].points;
            g->test_passed++;
        }
        else
        {
            if (!first_failure_stdout)
            {
                first_failure_stdout = out;
                first_failure_stderr = err;
                out = err = NULL;
                g->first_failed = ts.cases[i].ordinal;
            }
        }

        if (rt > max_runtime)
            max_runtime = rt;
        if (mem > max_memory)
            max_memory = mem;

        free(out);
        free(err);

        /* Short-circuit on terminal verdicts. */
        if (strcmp(this_verdict, "TLE") == 0 ||
            strcmp(this_verdict, "MLE") == 0 ||
            strcmp(this_verdict, "RE") == 0 ||
            strcmp(this_verdict, "CE") == 0)
        {
            break;
        }
    }

    /* Aggregate. */
    snprintf(g->verdict, sizeof(g->verdict), "%s", worst_verdict);
    g->score = total_points > 0 ? (earned_points * 100) / total_points : 0;
    g->runtime_ms   = max_runtime;
    g->memory_kb    = max_memory;
    g->stdout_capture = first_failure_stdout ? first_failure_stdout : strdup("");
    g->stderr_capture = first_failure_stderr ? first_failure_stderr : strdup("");

    /* Cleanup */
    test_case_set_free(&ts);
    /* Best-effort: remove tmpdir. If files remain, watchdog cleans later. */
    char rm[512];
    snprintf(rm, sizeof(rm), "rm -rf %s", tmpdir);
    if (system(rm) != 0) {
        /* ignore */
    }

    return 0;
}