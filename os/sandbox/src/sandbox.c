/*
 * sandbox.c — core sandbox execution.
 *
 * Step 4 adds:
 *   - cgroup v2 creation before fork
 *   - PID attach to cgroup after fork
 *   - memory.peak / cpu.stat read after wait4
 *   - memory.events based OOM-kill detection
 *   - cgroup_destroy() after result capture
 */

#include "sandbox.h"
#include "cgroup.h"
#include "namespace.h"
#include "seccomp_filter.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>

extern char **environ;

/* ------------------------------------------------------------------ */

static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

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

static void close_if_valid(int fd) { if (fd >= 0) close(fd); }

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
    rl.rlim_cur = rl.rlim_max = 0;
    if (setrlimit(RLIMIT_CORE, &rl) < 0) return -1;

    return 0;
}

/*
 * Read memory.events and return 1 if the cgroup was OOM-killed.
 * Format: "low 0\nhigh 0\nmax 0\noom 1\noom_kill 1\noom_group_kill 0\n"
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

    /* Look for "oom_kill N" where N > 0 */
    const char *p = strstr(buf, "oom_kill ");
    if (!p) return 0;
    p += strlen("oom_kill ");
    unsigned long long v = strtoull(p, NULL, 10);
    return (v > 0) ? 1 : 0;
}

/* ------------------------------------------------------------------ */

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

int sandbox_run(const sandbox_config_t *cfg, sandbox_result_t *res)
{
    if (!cfg || !res || !cfg->binary || !cfg->argv) return SANDBOX_ERR_INTERNAL;

    memset(res, 0, sizeof(*res));
    res->exit_code = -1;

    int in_pipe[2] = { -1, -1 }, out_pipe[2] = { -1, -1 }, err_pipe[2] = { -1, -1 };

    /* --- cgroup setup BEFORE fork --- */
    int cgroup_active = 0;
    if (cfg->cgroup_root && cfg->cgroup_name && *cfg->cgroup_root && *cfg->cgroup_name) {
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

    if (pipe(in_pipe) < 0) goto cleanup_io;
    if (pipe(out_pipe) < 0) goto cleanup_io;
    if (pipe(err_pipe) < 0) goto cleanup_io;

    long start_ms = now_ms();
    pid_t child = fork();

    if (child < 0) {
        close_if_valid(in_pipe[0]); close_if_valid(in_pipe[1]);
        close_if_valid(out_pipe[0]); close_if_valid(out_pipe[1]);
        close_if_valid(err_pipe[0]); close_if_valid(err_pipe[1]);
        if (cgroup_active) cgroup_destroy(cfg->cgroup_root, cfg->cgroup_name);
        return SANDBOX_ERR_FORK;
    }

    /* ===================== CHILD ===================== */
    if (child == 0) {
        if (dup2(in_pipe[0], STDIN_FILENO) < 0) _exit(126);
        if (dup2(out_pipe[1], STDOUT_FILENO) < 0) _exit(126);
        if (dup2(err_pipe[1], STDERR_FILENO) < 0) _exit(126);

        close(in_pipe[0]); close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);
        close(err_pipe[0]); close(err_pipe[1]);

        if (apply_rlimits(cfg) < 0) {
            int err = errno;
            char msg[160];
            int n = snprintf(msg, sizeof(msg),
                             "sandbox: setrlimit failed: %s\n", strerror(err));
            if (n > 0) write_all(STDERR_FILENO, msg, (size_t)n);
            _exit(126);
        }

        /* --- step 5: namespaces --- */
        ns_config_t nsc = {
            .enable_pid_ns   = cfg->enable_pid_ns,
            .enable_mount_ns = cfg->enable_mount_ns,
            .enable_net_ns   = cfg->enable_net_ns,
            .enable_user_ns  = cfg->enable_user_ns,
            .enable_uts_ns   = cfg->enable_uts_ns,
            .enable_ipc_ns   = cfg->enable_ipc_ns,
            .rootfs          = cfg->rootfs,
            .hostname        = cfg->hostname,
            .bind_host_paths = cfg->bind_host_paths,
            .bind_guest_paths= cfg->bind_guest_paths,
            .bind_count      = cfg->bind_count,
        };
        /* ns_setup only returns in the grandchild if PID ns was enabled.
         * Otherwise it returns in the same process. */
        ns_setup(&nsc);
        /* If we get here and PID ns was enabled, we are now PID 1 of a
         * fresh namespace. If PID ns was NOT enabled, ns_setup returned
         * normally and we continue in the same child process. */
        
        /* --- step 6: seccomp --- *
         * Must come after namespaces (a seccomp filter that blocks
         * unshare would prevent namespace setup) and before execve
         * (so the target program runs under the filter). */
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

        execve(cfg->binary, cfg->argv, environ);

        int err = errno;
        char msg[256];
        int n = snprintf(msg, sizeof(msg),
                         "sandbox: execve(%s) failed: %s\n",
                         cfg->binary, strerror(err));
        if (n > 0) write_all(STDERR_FILENO, msg, (size_t)n);
        _exit(127);
    }

    /* ===================== PARENT ==================== */
    close_if_valid(in_pipe[0]);
    close_if_valid(out_pipe[1]);
    close_if_valid(err_pipe[1]);

    /* Attach child to cgroup as soon as we know its PID. */
    if (cgroup_active) {
        if (cgroup_attach_pid(cfg->cgroup_root, cfg->cgroup_name, child) < 0) {
            /* Non-fatal but noteworthy — cgroup exists but child may not
             * be inside. We proceed and rely on rlimits as backstop. */
        }
    }

    /* Feed stdin */
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

    int rc = sandbox_capture_output(out_pipe[0], err_pipe[0], cfg->output_limit,
                                    res->stdout_buf, &res->stdout_len, &res->stdout_truncated,
                                    res->stderr_buf, &res->stderr_len, &res->stderr_truncated);
    close_if_valid(out_pipe[0]);
    close_if_valid(err_pipe[0]);

    int status = 0;
    struct rusage ru;
    memset(&ru, 0, sizeof(ru));
    pid_t w;
    do { w = wait4(child, &status, 0, &ru); } while (w < 0 && errno == EINTR);
    /* Namespace failure shows up as exit code 125 from ns_setup's die(). */
    res->ns_exit_125    = (res->exit_code == 125) ? 1 : 0;
    res->ns_setup_failed = res->ns_exit_125;
    /* Seccomp violations surface as SIGSYS. */
    res->seccomp_violation = (res->signal == SIGSYS) ? 1 : 0;

    res->wall_time_ms = now_ms() - start_ms;
    if (w < 0) {
        sandbox_result_free(res);
        if (cgroup_active) cgroup_destroy(cfg->cgroup_root, cfg->cgroup_name);
        return SANDBOX_ERR_INTERNAL;
    }

    if (WIFEXITED(status))      { res->exit_code = WEXITSTATUS(status); res->signal = 0; }
    else if (WIFSIGNALED(status)) { res->exit_code = -1; res->signal = WTERMSIG(status); }

    res->user_time_ms = ru.ru_utime.tv_sec * 1000L + ru.ru_utime.tv_usec / 1000L;
    res->sys_time_ms  = ru.ru_stime.tv_sec * 1000L + ru.ru_stime.tv_usec / 1000L;
    res->cpu_time_ms  = res->user_time_ms + res->sys_time_ms;
    res->max_rss_kb   = ru.ru_maxrss;

    /* --- read cgroup stats BEFORE destroying the cgroup --- */
    if (cgroup_active) {
        cgroup_stats_t cst;
        if (cgroup_read_stats(cfg->cgroup_root, cfg->cgroup_name, &cst) == 0) {
            res->cgroup_memory_peak_bytes = cst.memory_peak_bytes;
            res->cgroup_cpu_usage_usec    = cst.cpu_usage_usec;
            res->cgroup_pids_peak         = cst.pids_peak;
        }
        res->oom_killed = cgroup_was_oom_killed(cfg->cgroup_root, cfg->cgroup_name);
        (void)cgroup_destroy(cfg->cgroup_root, cfg->cgroup_name);
    }

    res->stdout_buf[res->stdout_len] = '\0';
    res->stderr_buf[res->stderr_len] = '\0';

    return (rc == SANDBOX_OK) ? SANDBOX_OK : rc;

cleanup_io:
    if (in_pipe[0]  >= 0) { close_if_valid(in_pipe[0]);  close_if_valid(in_pipe[1]); }
    if (out_pipe[0] >= 0) { close_if_valid(out_pipe[0]); close_if_valid(out_pipe[1]); }
    if (err_pipe[0] >= 0) { close_if_valid(err_pipe[0]); close_if_valid(err_pipe[1]); }
    if (cgroup_active) cgroup_destroy(cfg->cgroup_root, cfg->cgroup_name);
    return SANDBOX_ERR_IO;
}