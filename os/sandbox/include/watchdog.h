#ifndef CODEARENA_WATCHDOG_H
#define CODEARENA_WATCHDOG_H

/*
 * CodeArena — wall-clock watchdog for sandboxed processes.
 *
 * A hostile program can call sleep(), pause(), or block on a pipe
 * forever without consuming CPU. RLIMIT_CPU never fires in that case.
 * The watchdog kills any child that lives longer than timeout_ms of
 * real (monotonic) time.
 *
 * The mechanism is a polling loop, not a signal:
 *   1. Parent records a monotonic deadline = now + timeout_ms.
 *   2. Parent loops calling waitpid(child, WNOHANG) every 50 ms.
 *   3. If deadline is exceeded, send SIGTERM; wait grace_ms for exit.
 *   4. If still alive after grace, send SIGKILL.
 *   5. Once child exits, return; the caller inspects status.
 *
 * Polling beats signal-based watchdogs because:
 *   - No async-signal-safety hazards.
 *   - Same loop handles two-phase kill (TERM then KILL).
 *   - Sub-50 ms timing precision is far better than needed.
 */

#include <sys/types.h>
#include <sys/resource.h>

/* What happened during the wait. */
typedef enum {
    WATCHDOG_CHILD_EXITED = 0,   /* child exited on its own             */
    WATCHDOG_CHILD_KILLED_TERM,  /* child ignored SIGTERM, got SIGKILL  */
    WATCHDOG_CHILD_KILLED_KILL,  /* SIGKILL sent without prior SIGTERM  */
    WATCHDOG_WAIT_FAILED,        /* waitpid returned an error           */
} watchdog_outcome_t;

typedef struct {
    int      timed_out;          /* 1 if watchdog fired                 */
    int      term_sent;          /* 1 if SIGTERM was sent               */
    int      kill_sent;          /* 1 if SIGKILL was sent               */
    long     elapsed_ms;         /* actual wall-clock elapsed time      */
    long     timed_out_elapsed_ms;  /* elapsed_ms at the moment of deadline */
    int      exit_status;        /* raw status from waitpid(2)          */
    watchdog_outcome_t outcome;
} watchdog_result_t;

/*
 * Wait for child to exit, enforcing the wall-clock timeout.
 *
 * timeout_ms == 0 disables the watchdog (blocks until exit).
 *
 * On success returns 0 and fills `out`. On internal failure
 * (waitpid error other than EINTR), returns -1 and sets errno.
 */
int watchdog_wait(pid_t child, unsigned int timeout_ms,
                  unsigned int grace_ms, watchdog_result_t *out,
                  struct rusage *out_ru);

#endif /* CODEARENA_WATCHDOG_H */