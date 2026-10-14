#ifndef CODEARENA_SANDBOX_H
#define CODEARENA_SANDBOX_H

/*
 * CodeArena Sandbox — public API
 *
 * Step 4 adds cgroup v2 controls: memory.max, cpu.max, pids.max.
 * These complement the rlimits from Step 3 and are the enforced
 * limits for root and non-root processes alike.
 */

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <signal.h>

/* Return codes from sandbox_run() */
#define SANDBOX_OK              0
#define SANDBOX_ERR_INTERNAL   -1
#define SANDBOX_ERR_FORK       -2
#define SANDBOX_ERR_EXEC       -3
#define SANDBOX_ERR_IO         -4
#define SANDBOX_ERR_ALLOC      -5
#define SANDBOX_ERR_TIMEOUT    -6
#define SANDBOX_ERR_RLIMIT     -7
#define SANDBOX_ERR_CGROUP     -8   /* cgroup create/attach failed */
#define SANDBOX_ERR_NS         -9 
#define SANDBOX_ERR_SECCOMP   -10

typedef struct {
    /* --- Execution --- */
    const char        *binary;
    char *const       *argv;
    const char        *stdin_data;
    size_t             stdin_len;
    size_t             output_limit;
    unsigned int       timeout_ms;

    /* --- rlimits (step 3) --- */
    unsigned int       cpu_seconds;
    size_t             max_file_size;
    unsigned int       max_pids;
    size_t             stack_size;
    unsigned int       max_open_files;

    /* --- cgroups v2 (step 4) --- */
    const char        *cgroup_root;       /* e.g. "/sys/fs/cgroup/codearena" */
    const char        *cgroup_name;       /* unique per submission; NULL = skip */
    unsigned int       cgroup_cpu_pct;    /* % of one core; 0 = no limit */
    unsigned int       cgroup_memory_mb;  /* memory.max in MB; 0 = no limit */
    unsigned int       cgroup_pids_max;   /* pids.max; 0 = no limit */
    uint64_t           cgroup_io_read_bps;
    uint64_t           cgroup_io_write_bps;

        /* --- namespaces (step 5) --- */
    int                enable_pid_ns;
    int                enable_mount_ns;
    int                enable_net_ns;
    int                enable_user_ns;
    int                enable_uts_ns;
    int                enable_ipc_ns;

    const char        *rootfs;             /* NULL/"" = skip pivot_root */
    const char        *hostname;           /* inside UTS ns             */
    const char *const *bind_host_paths;
    const char *const *bind_guest_paths;
    size_t             bind_count;

    /* --- seccomp (step 6) --- */
    int                enable_seccomp;
    int                seccomp_profile;   /* 0 = C, 1 = Python (see seccomp_filter.h) */
} sandbox_config_t;

typedef struct {
    int      exit_code;
    int      signal;

    char    *stdout_buf;
    size_t   stdout_len;
    int      stdout_truncated;

    char    *stderr_buf;
    size_t   stderr_len;
    int      stderr_truncated;

    long     wall_time_ms;
    long     user_time_ms;
    long     sys_time_ms;
    long     cpu_time_ms;
    long     max_rss_kb;

    /* --- cgroup measurements (step 4) --- */
    uint64_t cgroup_memory_peak_bytes;
    uint64_t cgroup_cpu_usage_usec;
    uint64_t cgroup_pids_peak;
    int      oom_killed;        /* 1 if OOM kill detected via cgroup events */

    /* --- namespace diagnostics (step 5) --- */
    int      ns_setup_failed;   /* 1 if child exited with 125 (ns error) */
    int      ns_exit_125;       /* raw: did the sandbox see exit code 125? */

    /* --- seccomp diagnostics (step 6) --- */
    int      seccomp_violation;   /* 1 if child died from SIGSYS */
} sandbox_result_t;

int sandbox_run(const sandbox_config_t *config, sandbox_result_t *result);
void sandbox_result_free(sandbox_result_t *result);

int sandbox_capture_output(int stdout_fd, int stderr_fd, size_t limit,
                           char *out_buf, size_t *out_len, int *out_trunc,
                           char *err_buf, size_t *err_len, int *err_trunc);

#endif