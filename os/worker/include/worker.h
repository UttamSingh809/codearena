#ifndef CODEARENA_WORKER_H
#define CODEARENA_WORKER_H

/*
 * CodeArena worker — public types and API.
 *
 * The worker is a long-running process that:
 *   1. Dequeues jobs from the shared-memory IPC queue.
 *   2. Claims the submission in PostgreSQL.
 *   3. Fetches source code and test cases.
 *   4. Runs the sandbox on each test case.
 *   5. Aggregates verdicts.
 *   6. Commits the atomic grading transaction.
 *   7. Repeats.
 *
 * Multiple workers run in parallel, each with its own DB connection.
 * They coordinate through the queue and the database (SKIP LOCKED).
 */

#include "queue.h"
#include "sandbox.h"

#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Configuration                                                      */
/* ------------------------------------------------------------------ */

typedef struct {
    /* Identity */
    const char *worker_id;        /* e.g. "worker-1"; must be unique       */

    /* Database */
    const char *db_url;           /* libpq connection string               */

    /* IPC queue */
    const char *queue_name;       /* e.g. "/codearena_queue"               */

    /* Sandbox defaults (per-problem values override) */
    const char *cgroup_root;      /* e.g. "/sys/fs/cgroup/codearena"       */
    const char *sandbox_tmp;      /* e.g. "/tmp/codearena"                 */
    const char *rootfs;           /* minimal rootfs path, may be NULL      */

    /* Seccomp profile: 0 = C, 1 = Python */
    int         seccomp_profile;

    /* Heartbeat interval in seconds */
    unsigned int heartbeat_seconds;

    /* Per-test-case output limit (KB); overrides problem default */
    unsigned int output_limit_kb;

    /* Whether to enable namespaces for sandbox runs */
    int         enable_pid_ns;
    int         enable_mount_ns;
    int         enable_net_ns;
    int         enable_user_ns;
    int         enable_uts_ns;
    int         enable_ipc_ns;

    /* Whether to enable seccomp */
    int         enable_seccomp;

    /* Compilation: host compiler for C/C++. Set to "gcc" or "cc". */
    const char *c_compiler;
    const char *cpp_compiler;
} worker_config_t;

/* ------------------------------------------------------------------ */
/* Runtime state                                                      */
/* ------------------------------------------------------------------ */

typedef struct worker worker_t;

/*
 * Create a worker. Opens a DB connection and attaches to the queue.
 * Returns NULL on failure (errno set, or DB error logged).
 */
worker_t *worker_create(const worker_config_t *cfg);

/*
 * Run the main loop. Blocks forever, processing jobs one at a time,
 * until worker_stop() is called (from a signal handler) or the queue
 * is closed.
 *
 * Returns 0 on clean shutdown, -1 on fatal error.
 */
int worker_run(worker_t *w);

/*
 * Signal the worker to stop after the current job.
 * Async-signal-safe: may be called from a signal handler.
 */
void worker_stop(worker_t *w);

/*
 * Destroy a worker. Closes DB connection and queue handle.
 */
void worker_destroy(worker_t *w);

/*
 * Process a single job descriptor. Returns 0 on success (including
 * "job not actionable, skipped"), -1 on error.
 *
 * Exposed so tests can drive the worker one job at a time.
 */
int worker_process_one(worker_t *w, const job_descriptor_t *job);

#endif /* CODEARENA_WORKER_H */