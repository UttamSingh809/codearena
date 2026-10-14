/*
 * sandbox.c — core sandbox execution.
 *
 * This is the single entry point that ties together every isolation
 * layer built in Steps 2 through 7. Given a sandbox_config_t, it:
 *
 *   1. Creates a cgroup (if configured) and applies resource limits.
 *   2. Creates three pipes for stdin, stdout, stderr.
 *   3. Forks a child process.
 *   4. In the child:
 *        a. Redirects stdio onto the pipes.
 *        b. Applies rlimits (RLIMIT_CPU, FSIZE, NPROC, STACK, NOFILE, CORE).
 *        c. Applies namespaces (user, PID, mount, net, UTS, IPC).
 *        d. Pivots into a minimal rootfs with read-only bind mounts.
 *        e. Installs the seccomp-BPF whitelist.
 *        f. execve()'s the target binary.
 *   5. In the parent:
 *        a. Attaches the child PID to the cgroup.
 *        b. Feeds stdin and closes the write end.
 *        c. Reads stdout and stderr in parallel with bounded buffers.
 *        d. Waits for the child with a wall-clock watchdog.
 *        e. Reaps the child and collects rusage.
 *        f. Reads cgroup stats before destroying the cgroup.
 *        g. Frees the cgroup and returns a sandbox_result_t.
 *
 * The function never returns on internal failure; it returns a
 * negative SANDBOX_ERR_* code and leaves `result` partially initialized.
 * Callers must call sandbox_result_free() if the return code is
 * SANDBOX_OK.
 */

#include "sandbox.h"
#include "cgroup.h"
#include "namespace.h"
#include "seccomp_filter.h"
#include "watchdog.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/wait.h>

extern char **environ;

/* ------------------------------------------------------------------ */
/* Utilities                                                          */
/* ------------------------------------------------------------------ */

/*
 * Return monotonic time in milliseconds. Monotonic time is immune to
 * wall-clock adjustments (NTP steps, DST, admin sets the clock back).
 * We use it for both timeout measurements and wall_time_ms reporting.
 */
static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

/*
 * Write the entire buffer to fd, retrying on partial writes and EINTR.
 * Returns 0 on success, -1 on failure (errno set).
 */
static int write_all(int fd, const char *buf, size_t len)
{
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, buf + off, len - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        off += (size_t)n;
    }
    return 0;
}

/*
 * Close a file descriptor if it is valid. Silences warnings on paths
 * where a pipe end might have already been closed.
 */
static void close_if_valid(int fd)
{
    if (fd >= 0) close(fd);
}

/* ------------------------------------------------------------------ */
/* rlimits (step 3)                                                   */
/* ------------------------------------------------------------------ */

/*
 * Apply rlimits to the calling process. Called in the child after
 * dup2() and before namespaces/seccomp/execve().
 *
 * Returns 0 on success, -1 on failure (errno set). On failure the
 * child prints a diagnostic and exits 126.
 *
 * Notes:
 *   - We set rlim_cur == rlim_max so the FIRST violation is fatal.
 *     A soft limit would send a catchable signal; a hard limit
 *     escalates to SIGKILL if the process catches the soft one.
 *   - RLIMIT_NPROC is enforced per real UID across the whole system
 *     and has no effect on root. Full fork-bomb containment under
 *     root requires cgroup pids.max (Step 4).
 *   - RLIMIT_CORE is always disabled. Untrusted code must never
 *     produce a core dump.
 */
static int apply_rlimits(const sandbox_config_t *cfg)
{
    struct rlimit rl;

    if (cfg->cpu_seconds > 0) {
        rl.rlim_cur = rl.rlim_max = cfg->cpu_seconds;
        if (setrlimit(RLIMIT_CPU, &rl) < 0) return -1;
    }
    if (cfg->max_file_size > 0) {
        rl.rlim_cur = rl.rlim_max = (rlim_t)cfg->max_file_size;
        if (setrlimit(RLIMIT_FSIZE, &rl) < 0) return -1;
    }
    if (cfg->max_pids > 0) {
        rl.rlim_cur = rl.rlim_max = cfg->max_pids;
        if (setrlimit(RLIMIT_NPROC, &rl) < 0) return -1;
    }
    if (cfg->stack_size > 0) {
        rl.rlim_cur = rl.rlim_max = (rlim_t)cfg->stack_size;
        if (setrlimit(RLIMIT_STACK, &rl) < 0) return -1;
    }
    if (cfg->max_open_files > 0) {
        rl.rlim_cur = rl.rlim_max = cfg->max_open_files;
        if (setrlimit(RLIMIT_NOFILE, &rl) < 0) return -1;
    }

    /* Always disable core dumps. */
    rl.rlim_cur = rl.rlim_max = 0;
    if (setrlimit(RLIMIT_CORE, &rl) < 0) return -1;

    return 0;
}

/* ------------------------------------------------------------------ */
/* cgroup OOM detection (step 4)                                      */
/* ------------------------------------------------------------------ */

/*
 * Read /sys/fs/cgroup/codearena/<name>/memory.events and return 1 if
 * the cgroup was OOM-killed during this run.
 *
 * Format: "low 0\nhigh 0\nmax 0\noom 1\noom_kill 1\noom_group_kill 0\n"
 * We look for "oom_kill N" where N > 0.
 */
static int cgroup_was_oom_killed(const char *root, const char *name)
{
    if (!root || !name) return 0;

    char path[512];
    int n = snprintf(path, sizeof(path), "%s/%s/memory.events", root, name);
    if (n < 0 || n >= (int)sizeof(path)) return 0;

    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;

    char buf[512];
    ssize_t r = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (r <= 0) return 0;
    buf[r] = '\0';

    const char *p = strstr(buf, "oom_kill ");
    if (!p) return 0;
    p += strlen("oom_kill ");
    unsigned long long v = strtoull(p, NULL, 10);
    return (v > 0) ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* sandbox_result_free                                                */
/* ------------------------------------------------------------------ */

/*
 * Free the heap buffers inside a sandbox_result_t. Safe to call
 * multiple times. Does not free the struct itself.
 */
void sandbox_result_free(sandbox_result_t *r)
{
    if (!r) return;
    free(r->stdout_buf);
    free(r->stderr_buf);
    r->stdout_buf = NULL;
    r->stderr_buf = NULL;
    r->stdout_len = 0;
    r->stderr_len = 0;
}

/* ------------------------------------------------------------------ */
/* sandbox_run                                                        */
/* ------------------------------------------------------------------ */

int sandbox_run(const sandbox_config_t *cfg, sandbox_result_t *res)
{
    if (!cfg || !res || !cfg->binary || !cfg->argv)
        return SANDBOX_ERR_INTERNAL;

    /* Zero the result struct so any early error path is safe. */
    memset(res, 0, sizeof(*res));
    res->exit_code = -1;

    int in_pipe[2]  = { -1, -1 };
    int out_pipe[2] = { -1, -1 };
    int err_pipe[2] = { -1, -1 };

    /* ================================================================
     * STEP 4a — create the cgroup BEFORE fork.
     *
     * The cgroup must exist before the child starts, so that when the
     * parent attaches the child PID immediately after fork(), the
     * cgroup is ready. If creation fails, we bail out with
     * SANDBOX_ERR_CGROUP rather than run without limits.
     * ================================================================ */
    int cgroup_active = 0;
    if (cfg->cgroup_root && cfg->cgroup_name &&
        *cfg->cgroup_root && *cfg->cgroup_name) {
        cgroup_config_t cg = {
            .cpu_quota_pct   = cfg->cgroup_cpu_pct,
            .memory_limit_mb = cfg->cgroup_memory_mb,
            .pids_max        = cfg->cgroup_pids_max,
            .io_read_bps     = cfg->cgroup_io_read_bps,
            .io_write_bps    = cfg->cgroup_io_write_bps,
        };
        if (cgroup_create(cfg->cgroup_root, cfg->cgroup_name, &cg) < 0) {
            return SANDBOX_ERR_CGROUP;
        }
        cgroup_active = 1;
    }

    /* ================================================================
     * STEP 2a — create the three I/O pipes.
     * ================================================================ */
    if (pipe(in_pipe) < 0) goto cleanup_io;
    if (pipe(out_pipe) < 0) goto cleanup_io;
    if (pipe(err_pipe) < 0) goto cleanup_io;

    long start_ms = now_ms();

    /* ================================================================
     * STEP 2b — fork the child.
     * ================================================================ */
    pid_t child = fork();

    if (child < 0) {
        close_if_valid(in_pipe[0]);  close_if_valid(in_pipe[1]);
        close_if_valid(out_pipe[0]); close_if_valid(out_pipe[1]);
        close_if_valid(err_pipe[0]); close_if_valid(err_pipe[1]);
        if (cgroup_active) cgroup_destroy(cfg->cgroup_root, cfg->cgroup_name);
        return SANDBOX_ERR_FORK;
    }

    /* ================================================================
     * CHILD PROCESS
     * ================================================================ */
    if (child == 0) {
        /*
         * Redirect the three standard streams onto the pipe ends.
         * dup2 makes a copy; the original pipe fds are closed below.
         */
        if (dup2(in_pipe[0],  STDIN_FILENO)  < 0) _exit(126);
        if (dup2(out_pipe[1], STDOUT_FILENO) < 0) _exit(126);
        if (dup2(err_pipe[1], STDERR_FILENO) < 0) _exit(126);

        close(in_pipe[0]);  close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);
        close(err_pipe[0]); close(err_pipe[1]);

        /* ----------------------------------------------------------
         * STEP 3 — apply rlimits.
         *
         * Must come before namespaces and seccomp because some of
         * those operations require specific privileges that a tight
         * rlimit might interfere with (e.g., RLIMIT_NOFILE could
         * starve the mount code of file descriptors).
         * ---------------------------------------------------------- */
        if (apply_rlimits(cfg) < 0) {
            int err = errno;
            char msg[160];
            int n = snprintf(msg, sizeof(msg),
                             "sandbox: setrlimit failed: %s\n", strerror(err));
            if (n > 0) write_all(STDERR_FILENO, msg, (size_t)n);
            _exit(126);
        }

        /* ----------------------------------------------------------
         * STEP 5 — namespaces.
         *
         * Order matters: user ns first (grants caps inside), then
         * PID (affects future children — ns_setup() forks internally
         * so the grandchild becomes PID 1), then mount/network/UTS/
         * IPC, then mount propagation, then pivot_root.
         *
         * On success with PID ns enabled, ns_setup() returns only in
         * the grandchild. Otherwise it returns in the same process.
         * ---------------------------------------------------------- */
        ns_config_t nsc = {
            .enable_pid_ns    = cfg->enable_pid_ns,
            .enable_mount_ns  = cfg->enable_mount_ns,
            .enable_net_ns    = cfg->enable_net_ns,
            .enable_user_ns   = cfg->enable_user_ns,
            .enable_uts_ns    = cfg->enable_uts_ns,
            .enable_ipc_ns    = cfg->enable_ipc_ns,
            .rootfs           = cfg->rootfs,
            .hostname         = cfg->hostname,
            .bind_host_paths  = cfg->bind_host_paths,
            .bind_guest_paths = cfg->bind_guest_paths,
            .bind_count       = cfg->bind_count,
        };
        ns_setup(&nsc);

        /* ----------------------------------------------------------
         * STEP 6 — seccomp-BPF.
         *
         * Must come after namespaces (a filter that blocked unshare
         * would prevent namespace setup) and before execve (so the
         * target program runs under the filter from its first
         * instruction).
         *
         * On failure the child prints a diagnostic and exits 124.
         * ---------------------------------------------------------- */
        if (cfg->enable_seccomp) {
            if (seccomp_apply((seccomp_profile_t)cfg->seccomp_profile) < 0) {
                int err = errno;
                char msg[192];
                int n = snprintf(msg, sizeof(msg),
                                 "sandbox: seccomp_apply failed: %s\n",
                                 strerror(err));
                if (n > 0) write_all(STDERR_FILENO, msg, (size_t)n);
                _exit(124);
            }
        }

        /* ----------------------------------------------------------
         * STEP 2c — execve the target binary.
         *
         * If execve returns, it failed. Print the diagnostic to
         * stderr (which is piped back to the parent) and exit 127.
         * ---------------------------------------------------------- */
        execve(cfg->binary, cfg->argv, environ);

        int err = errno;
        char msg[256];
        int n = snprintf(msg, sizeof(msg),
                         "sandbox: execve(%s) failed: %s\n",
                         cfg->binary, strerror(err));
        if (n > 0) write_all(STDERR_FILENO, msg, (size_t)n);
        _exit(127);
    }

    /* ================================================================
     * PARENT PROCESS
     * ================================================================ */

    /* Close the pipe ends the child owns. */
    close_if_valid(in_pipe[0]);
    close_if_valid(out_pipe[1]);
    close_if_valid(err_pipe[1]);

    /* --------------------------------------------------------------
     * STEP 4b — attach the child PID to the cgroup.
     *
     * This must happen after fork (we need the PID) and before the
     * child can spawn any of its own processes (they inherit the
     * cgroup from the child, so if we attach the child, its
     * descendants are automatically in the cgroup too).
     *
     * There is a tiny race window: the child may exec before the
     * parent writes its PID. Even then, any process the child
     * spawns inherits the cgroup from the parent process's *current*
     * cgroup at spawn time. Since the parent process (the child of
     * our fork) is briefly outside the cgroup, any grandchild it
     * spawns in that window would also be outside. We accept this
     * because the window is sub-millisecond and rlimits still bound
     * the child.
     * -------------------------------------------------------------- */
    if (cgroup_active) {
        if (cgroup_attach_pid(cfg->cgroup_root, cfg->cgroup_name, child) < 0) {
            /* Non-fatal — rlimits remain in place. */
        }
    }

    /* --------------------------------------------------------------
     * STEP 2d — feed stdin and close the write end so the child
     * sees EOF if it's reading until end-of-input.
     * -------------------------------------------------------------- */
    if (cfg->stdin_len > 0 && cfg->stdin_data) {
        if (write_all(in_pipe[1], cfg->stdin_data, cfg->stdin_len) < 0) {
            close_if_valid(in_pipe[1]);
            close_if_valid(out_pipe[0]);
            close_if_valid(err_pipe[0]);
            kill(child, SIGKILL);
            waitpid(child, NULL, 0);
            if (cgroup_active) cgroup_destroy(cfg->cgroup_root, cfg->cgroup_name);
            return SANDBOX_ERR_IO;
        }
    }
    close_if_valid(in_pipe[1]);

    /* --------------------------------------------------------------
     * STEP 2e — allocate bounded capture buffers.
     *
     * limit + 1 so there's always room for a NUL terminator, even
     * when output is exactly at the limit.
     * -------------------------------------------------------------- */
    size_t cap = cfg->output_limit + 1;
    res->stdout_buf = malloc(cap);
    res->stderr_buf = malloc(cap);
    if (!res->stdout_buf || !res->stderr_buf) {
        close_if_valid(out_pipe[0]);
        close_if_valid(err_pipe[0]);
        kill(child, SIGKILL);
        waitpid(child, NULL, 0);
        sandbox_result_free(res);
        if (cgroup_active) cgroup_destroy(cfg->cgroup_root, cfg->cgroup_name);
        return SANDBOX_ERR_ALLOC;
    }
    res->stdout_buf[0] = '\0';
    res->stderr_buf[0] = '\0';

    /* --------------------------------------------------------------
     * STEP 2f — capture stdout and stderr in parallel.
     *
     * poll() on both pipe read ends prevents the classic deadlock
     * where a child blocked writing to stderr is never read because
     * the parent is blocked reading stdout (or vice versa).
     * -------------------------------------------------------------- */
    int rc = sandbox_capture_output(out_pipe[0], err_pipe[0], cfg->output_limit,
                                    res->stdout_buf, &res->stdout_len,
                                    &res->stdout_truncated,
                                    res->stderr_buf, &res->stderr_len,
                                    &res->stderr_truncated);
    close_if_valid(out_pipe[0]);
    close_if_valid(err_pipe[0]);

    /* --------------------------------------------------------------
     * STEP 7a — wall-clock watchdog.
     *
     * watchdog_wait() polls wait4(WNOHANG) in a loop until the child
     * exits or the deadline fires. On deadline, it sends SIGTERM
     * first, waits grace_ms, then escalates to SIGKILL. It returns
     * the child's exit status and (optionally) its rusage.
     * -------------------------------------------------------------- */
    watchdog_result_t wd;
    struct rusage ru;
    memset(&ru, 0, sizeof(ru));
    unsigned int grace = cfg->grace_ms ? cfg->grace_ms : 500;

    int wd_rc = watchdog_wait(child, cfg->timeout_ms, grace, &wd, &ru);

    res->wall_time_ms         = now_ms() - start_ms;
    res->timed_out            = wd.timed_out;
    res->term_sent            = wd.term_sent;
    res->kill_sent            = wd.kill_sent;
    res->timed_out_elapsed_ms = wd.timed_out_elapsed_ms;

    if (wd_rc < 0) {
        sandbox_result_free(res);
        if (cgroup_active) cgroup_destroy(cfg->cgroup_root, cfg->cgroup_name);
        return SANDBOX_ERR_WATCHDOG;
    }

    /* --------------------------------------------------------------
     * STEP 2g — decode the exit status.
     *
     * WIFEXITED   → normal termination; exit_code is the low 8 bits.
     * WIFSIGNALED → killed by signal; signal is the signal number,
     *               exit_code is -1 (sentinel).
     * -------------------------------------------------------------- */
    int status = wd.exit_status;
    if (WIFEXITED(status)) {
        res->exit_code = WEXITSTATUS(status);
        res->signal    = 0;
    } else if (WIFSIGNALED(status)) {
        res->exit_code = -1;
        res->signal    = WTERMSIG(status);
    }

    /* --------------------------------------------------------------
     * STEP 3b — copy rusage fields into the result.
     *
     * wait4()'s rusage is per-child, unlike getrusage(RUSAGE_CHILDREN)
     * which accumulates across all reaped children of the worker.
     * -------------------------------------------------------------- */
    res->user_time_ms = ru.ru_utime.tv_sec * 1000L + ru.ru_utime.tv_usec / 1000L;
    res->sys_time_ms  = ru.ru_stime.tv_sec * 1000L + ru.ru_stime.tv_usec / 1000L;
    res->cpu_time_ms  = res->user_time_ms + res->sys_time_ms;
    res->max_rss_kb   = ru.ru_maxrss;    /* KB on Linux */

    /* --------------------------------------------------------------
     * STEP 4c — read cgroup stats BEFORE destroying the cgroup.
     *
     * Once the cgroup is removed, its memory.peak and cpu.stat files
     * vanish. We read them now and store the values in the result.
     *
     * memory.peak is more accurate than ru_maxrss for programs that
     * fork subprocesses — it captures the peak across every task in
     * the cgroup.
     * -------------------------------------------------------------- */
    if (cgroup_active) {
        cgroup_stats_t cst;
        if (cgroup_read_stats(cfg->cgroup_root, cfg->cgroup_name, &cst) == 0) {
            res->cgroup_memory_peak_bytes = cst.memory_peak_bytes;
            res->cgroup_cpu_usage_usec    = cst.cpu_usage_usec;
            res->cgroup_pids_peak         = cst.pids_peak;
        }
        res->oom_killed = cgroup_was_oom_killed(cfg->cgroup_root,
                                                cfg->cgroup_name);
        (void)cgroup_destroy(cfg->cgroup_root, cfg->cgroup_name);
    }

    /* --------------------------------------------------------------
     * STEP 5b — decode namespace and seccomp diagnostics.
     *
     * Exit 125 is our sentinel for "ns_setup() called die()".
     * Exit 124 is our sentinel for "seccomp_apply() failed".
     * SIGSYS (31) means the seccomp filter killed the process.
     * -------------------------------------------------------------- */
    res->ns_exit_125       = (res->exit_code == 125) ? 1 : 0;
    res->ns_setup_failed   = res->ns_exit_125;
    res->seccomp_violation = (res->signal == SIGSYS) ? 1 : 0;

    /* NUL-terminate the capture buffers for convenience. */
    res->stdout_buf[res->stdout_len] = '\0';
    res->stderr_buf[res->stderr_len] = '\0';

    return (rc == SANDBOX_OK) ? SANDBOX_OK : rc;

cleanup_io:
    /* Cleanup path for pipe creation failures. */
    if (in_pipe[0]  >= 0) { close_if_valid(in_pipe[0]);  close_if_valid(in_pipe[1]); }
    if (out_pipe[0] >= 0) { close_if_valid(out_pipe[0]); close_if_valid(out_pipe[1]); }
    if (err_pipe[0] >= 0) { close_if_valid(err_pipe[0]); close_if_valid(err_pipe[1]); }
    if (cgroup_active) cgroup_destroy(cfg->cgroup_root, cfg->cgroup_name);
    return SANDBOX_ERR_IO;
}