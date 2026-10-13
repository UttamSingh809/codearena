#ifndef CODEARENA_SANDBOX_H
#define CODEARENA_SANDBOX_H

/*
 * CodeArena Sandbox — public API
 *
 * Step 3 adds rlimit-based resource controls: CPU time, file size,
 * process count, stack size, open file descriptors. Step 4 will add
 * cgroups v2 for cgroup-enforced limits (memory, cpu quota, pids).
 */

#include <stddef.h>
#include <sys/types.h>

/* Return codes from sandbox_run() */
#define SANDBOX_OK              0
#define SANDBOX_ERR_INTERNAL   -1
#define SANDBOX_ERR_FORK       -2
#define SANDBOX_ERR_EXEC       -3
#define SANDBOX_ERR_IO         -4
#define SANDBOX_ERR_ALLOC      -5
#define SANDBOX_ERR_TIMEOUT    -6   /* reserved for step 7 */
#define SANDBOX_ERR_RLIMIT     -7   /* setrlimit() failed in child */

/*
 * Configuration for one sandbox run.
 *
 * Pointer fields are borrowed. Zero-valued numeric fields mean
 * "use the system default / leave unlimited" unless documented.
 */
typedef struct {
    /* --- Execution --- */
    const char        *binary;
    char *const       *argv;
    const char        *stdin_data;
    size_t             stdin_len;

    /* --- Output capture --- */
    size_t             output_limit;    /* bytes captured per stream */

    /* --- Wall-clock timeout (step 7; ignored in step 3) --- */
    unsigned int       timeout_ms;

    /* --- rlimits (step 3) --- */
    unsigned int       cpu_seconds;     /* RLIMIT_CPU (hard); 0 = unlimited */
    size_t             max_file_size;   /* RLIMIT_FSIZE bytes; 0 = unlimited */
    unsigned int       max_pids;        /* RLIMIT_NPROC; 0 = unlimited */
    size_t             stack_size;      /* RLIMIT_STACK bytes; 0 = default */
    unsigned int       max_open_files;  /* RLIMIT_NOFILE; 0 = default */
} sandbox_config_t;

/*
 * Result of one sandbox run.
 *
 * stdout_buf and stderr_buf are heap-allocated and NUL-terminated.
 * Caller must release them with sandbox_result_free().
 */
typedef struct {
    int      exit_code;          /* WEXITSTATUS if exited normally, else -1 */
    int      signal;             /* WTERMSIG if killed by signal, else 0    */

    char    *stdout_buf;
    size_t   stdout_len;
    int      stdout_truncated;

    char    *stderr_buf;
    size_t   stderr_len;
    int      stderr_truncated;

    long     wall_time_ms;       /* measured wall-clock duration            */
    long     user_time_ms;       /* CPU time in user mode                   */
    long     sys_time_ms;        /* CPU time in kernel mode                 */
    long     cpu_time_ms;        /* user + sys                              */
    long     max_rss_kb;         /* peak resident set size (KB)             */
} sandbox_result_t;

/* Run a binary in a sandbox. Returns one of the SANDBOX_* codes. */
int sandbox_run(const sandbox_config_t *config, sandbox_result_t *result);

/* Release heap memory inside a sandbox_result_t. Safe to call twice. */
void sandbox_result_free(sandbox_result_t *result);

/* Internal: bounded dual-pipe reader (exposed for unit tests). */
int sandbox_capture_output(int stdout_fd, int stderr_fd, size_t limit,
                           char *out_buf, size_t *out_len, int *out_trunc,
                           char *err_buf, size_t *err_len, int *err_trunc);

#endif /* CODEARENA_SANDBOX_H */