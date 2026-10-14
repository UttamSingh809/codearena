#ifndef CODEARENA_CGROUP_H
#define CODEARENA_CGROUP_H

/*
 * CodeArena — cgroup v2 management
 *
 * Every sandboxed submission runs inside its own cgroup under
 * CGROUP_ROOT (typically /sys/fs/cgroup/codearena).
 *
 * Lifecycle:
 *   cgroup_init_parent()   — once at startup; enables controllers
 *   cgroup_create()        — per submission; mkdir + write limits
 *   cgroup_attach_pid()    — after fork(); write PID to cgroup.procs
 *   cgroup_read_peak()     — after wait4(); read memory.peak + cpu.stat
 *   cgroup_destroy()       — cleanup; rmdir
 *
 * All functions return 0 on success, -1 on failure (errno set).
 * Failure in create/attach is fatal for the submission. Failure in
 * read_peak leaves the out-params zeroed. Failure in destroy is
 * non-fatal (watchdog will clean up later).
 */

#include <stdint.h>
#include <sys/types.h>

/* Configuration for a per-submission cgroup. */
typedef struct {
    /* CPU quota as percentage of one core (100 = 1 core, 200 = 2 cores).
     * 0 means "no CPU limit configured" — leave cpu.max at default. */
    unsigned int cpu_quota_pct;

    /* Memory limit in megabytes. 0 means "no memory limit". */
    unsigned int memory_limit_mb;

    /* Max processes/threads. 0 means "no pids limit". */
    unsigned int pids_max;

    /* I/O bandwidth cap (bytes/sec) for the root block device, if nonzero.
     * Step 4 sets this optionally; 0 means "skip io.max". */
    uint64_t io_read_bps;
    uint64_t io_write_bps;
} cgroup_config_t;

/* Stats read from a cgroup after the process has exited. */
typedef struct {
    uint64_t memory_peak_bytes;  /* memory.peak */
    uint64_t cpu_usage_usec;     /* sum of usage_usec in cpu.stat */
    uint64_t pids_peak;          /* pids.peak (if available) */
} cgroup_stats_t;

/*
 * Initialize the parent cgroup (e.g. /sys/fs/cgroup/codearena).
 *
 * - Creates the directory if it does not exist.
 * - Enables cpu, memory, pids (and optionally io) on subtree_control.
 * - Idempotent: safe to call multiple times.
 *
 * Returns 0 on success, -1 on failure.
 */
int cgroup_init_parent(const char *root);

/*
 * Create a per-submission cgroup and apply limits.
 *
 * The cgroup name must be unique (use the submission ID as a string).
 * Writes cpu.max, memory.max, pids.max as configured. Any zero-valued
 * field is skipped (leaves the kernel default).
 *
 * Returns 0 on success, -1 on failure.
 */
int cgroup_create(const char *root, const char *name,
                  const cgroup_config_t *cfg);

/*
 * Attach a PID to a cgroup by writing to cgroup.procs.
 *
 * Must be called after fork() in the parent, once the child PID is known.
 * Writes the PID followed by a newline, as required by the kernel.
 *
 * Returns 0 on success, -1 on failure.
 */
int cgroup_attach_pid(const char *root, const char *name, pid_t pid);

/*
 * Read stats from a cgroup. Must be called BEFORE cgroup_destroy().
 * Missing files are treated as zeros (non-fatal).
 *
 * Returns 0 on success (even if some stats were zero), -1 on open failure.
 */
int cgroup_read_stats(const char *root, const char *name, cgroup_stats_t *out);

/*
 * Destroy a per-submission cgroup.
 *
 * Attempts rmdir. If the cgroup still has processes (EBUSY), we first
 * try to read cgroup.procs and SIGKILL each one, then retry rmdir.
 * Failures are logged by the caller; the watchdog will clean up later.
 *
 * Returns 0 on success, -1 on failure.
 */
int cgroup_destroy(const char *root, const char *name);

#endif /* CODEARENA_CGROUP_H */