/*
 * watchdog.c — wall-clock timeout enforcement via polling loop.
 *
 * The parent of a sandboxed child calls watchdog_wait() to reap the
 * child while enforcing a wall-clock deadline. If the deadline fires,
 * the watchdog escalates: SIGTERM first (gracious), then SIGKILL
 * after a short grace period (final).
 *
 * See watchdog.h for the design rationale.
 */

#include "watchdog.h"

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define POLL_INTERVAL_MS 50

/* ------------------------------------------------------------------ */
/* Utilities                                                          */
/* ------------------------------------------------------------------ */

static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void sleep_ms(long ms)
{
    if (ms <= 0) return;
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) < 0 && errno == EINTR) {
        /* continue with remaining time */
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

int watchdog_wait(pid_t child, unsigned int timeout_ms,
                  unsigned int grace_ms, watchdog_result_t *out,
                  struct rusage *out_ru)
{
    if (!out) { errno = EINVAL; return -1; }

    memset(out, 0, sizeof(*out));
    if (out_ru) memset(out_ru, 0, sizeof(*out_ru));

    long start_ms = now_ms();
    long deadline_ms = (timeout_ms > 0) ? start_ms + (long)timeout_ms : 0;

    int status = 0;
    struct rusage ru;

    for (;;) {
        memset(&ru, 0, sizeof(ru));
        pid_t rc = wait4(child, &status, WNOHANG, &ru);

        if (rc == child) {
            out->exit_status = status;
            out->elapsed_ms  = now_ms() - start_ms;
            out->outcome     = WATCHDOG_CHILD_EXITED;
            if (out_ru) *out_ru = ru;
            return 0;
        }

        if (rc < 0 && errno != EINTR) {
            out->outcome = WATCHDOG_WAIT_FAILED;
            return -1;
        }

        long now = now_ms();

        if (deadline_ms > 0 && now >= deadline_ms) {
            if (!out->kill_sent) {
                if (!out->term_sent) {
                    kill(child, SIGTERM);
                    out->term_sent = 1;
                    out->timed_out = 1;
                    out->timed_out_elapsed_ms = now - start_ms;
                    deadline_ms = now + (long)grace_ms;
                } else {
                    kill(child, SIGKILL);
                    out->kill_sent = 1;
                    deadline_ms = now + POLL_INTERVAL_MS * 2;
                }
            }
        }

        sleep_ms(POLL_INTERVAL_MS);
    }
}