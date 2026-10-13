/*
 * sandbox.c — core sandbox execution.
 *
 * Step 3 adds:
 *   - apply_rlimits(): sets RLIMIT_CPU/FSIZE/NPROC/STACK/NOFILE/CORE
 *     inside the child between dup2() and execve()
 *   - wait4() instead of waitpid(): per-child rusage for CPU/memory
 */

#define _GNU_SOURCE
#include "sandbox.h"

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

/* --- helpers --------------------------------------------------------- */

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

static void close_if_valid(int fd)
{
    if (fd >= 0) close(fd);
}

/*
 * Apply rlimits to the calling process. Called in the child after
 * dup2() and before execve(). Returns 0 on success, -1 on failure
 * (with errno set). On failure the child should _exit(126).
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

    /* Always disable core dumps — untrusted code must not write them. */
    rl.rlim_cur = rl.rlim_max = 0;
    if (setrlimit(RLIMIT_CORE, &rl) < 0) return -1;

    return 0;
}

/* --- public API ------------------------------------------------------ */

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
    res->signal    = 0;

    int in_pipe[2]  = { -1, -1 };
    int out_pipe[2] = { -1, -1 };
    int err_pipe[2] = { -1, -1 };

    if (pipe(in_pipe)  < 0) return SANDBOX_ERR_IO;
    if (pipe(out_pipe) < 0) { close_if_valid(in_pipe[0]);  close_if_valid(in_pipe[1]); return SANDBOX_ERR_IO; }
    if (pipe(err_pipe) < 0) {
        close_if_valid(in_pipe[0]);  close_if_valid(in_pipe[1]);
        close_if_valid(out_pipe[0]); close_if_valid(out_pipe[1]);
        return SANDBOX_ERR_IO;
    }

    long start_ms = now_ms();

    pid_t child = fork();
    if (child < 0) {
        close_if_valid(in_pipe[0]);  close_if_valid(in_pipe[1]);
        close_if_valid(out_pipe[0]); close_if_valid(out_pipe[1]);
        close_if_valid(err_pipe[0]); close_if_valid(err_pipe[1]);
        return SANDBOX_ERR_FORK;
    }

    /* ===================== CHILD ===================== */
    if (child == 0) {
        if (dup2(in_pipe[0],  STDIN_FILENO)  < 0) _exit(126);
        if (dup2(out_pipe[1], STDOUT_FILENO) < 0) _exit(126);
        if (dup2(err_pipe[1], STDERR_FILENO) < 0) _exit(126);

        close(in_pipe[0]);  close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);
        close(err_pipe[0]); close(err_pipe[1]);

        /* --- step 3: apply rlimits before exec --- */
        if (apply_rlimits(cfg) < 0) {
            int err = errno;
            char msg[160];
            int n = snprintf(msg, sizeof(msg),
                             "sandbox: setrlimit failed: %s\n", strerror(err));
            if (n > 0) write_all(STDERR_FILENO, msg, (size_t)n);
            _exit(126);
        }

        /* ------------------------------------------------------------
         * Later steps inject here:
         *   Step 4: cgroup setup + cgroup.procs write
         *   Step 5: unshare(CLONE_NEW*), pivot_root, uid_map/gid_map
         *   Step 6: seccomp_load(), capset()
         * ------------------------------------------------------------ */

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

    if (cfg->stdin_len > 0 && cfg->stdin_data) {
        if (write_all(in_pipe[1], cfg->stdin_data, cfg->stdin_len) < 0) {
            close_if_valid(in_pipe[1]);
            close_if_valid(out_pipe[0]);
            close_if_valid(err_pipe[0]);
            waitpid(child, NULL, 0);
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
        return SANDBOX_ERR_ALLOC;
    }
    res->stdout_buf[0] = '\0';
    res->stderr_buf[0] = '\0';

    int rc = sandbox_capture_output(out_pipe[0], err_pipe[0], cfg->output_limit,
                                    res->stdout_buf, &res->stdout_len, &res->stdout_truncated,
                                    res->stderr_buf, &res->stderr_len, &res->stderr_truncated);
    close_if_valid(out_pipe[0]);
    close_if_valid(err_pipe[0]);

    /* --- wait4 for per-child rusage --- */
    int status = 0;
    struct rusage ru;
    memset(&ru, 0, sizeof(ru));

    pid_t w;
    do {
        w = wait4(child, &status, 0, &ru);
    } while (w < 0 && errno == EINTR);

    res->wall_time_ms = now_ms() - start_ms;

    if (w < 0) {
        sandbox_result_free(res);
        return SANDBOX_ERR_INTERNAL;
    }

    if (WIFEXITED(status)) {
        res->exit_code = WEXITSTATUS(status);
        res->signal    = 0;
    } else if (WIFSIGNALED(status)) {
        res->exit_code = -1;
        res->signal    = WTERMSIG(status);
    }

    res->user_time_ms = ru.ru_utime.tv_sec * 1000L + ru.ru_utime.tv_usec / 1000L;
    res->sys_time_ms  = ru.ru_stime.tv_sec * 1000L + ru.ru_stime.tv_usec / 1000L;
    res->cpu_time_ms  = res->user_time_ms + res->sys_time_ms;
    res->max_rss_kb   = ru.ru_maxrss;   /* KB on Linux */

    res->stdout_buf[res->stdout_len] = '\0';
    res->stderr_buf[res->stderr_len] = '\0';

    return (rc == SANDBOX_OK) ? SANDBOX_OK : rc;
}