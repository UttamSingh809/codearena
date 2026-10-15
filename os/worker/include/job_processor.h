#ifndef CODEARENA_JOB_PROCESSOR_H
#define CODEARENA_JOB_PROCESSOR_H

/*
 * CodeArena — job processor.
 *
 * Given a claimed submission, run the sandbox on each test case,
 * aggregate verdicts, and produce a grading_result_t ready for
 * the atomic commit.
 */

#include "worker.h"
#include <stdint.h>

typedef struct {
    char    verdict[16];       /* AC, WA, TLE, MLE, RE, CE, OLE, PE */
    int     score;
    int     runtime_ms;        /* max runtime across test cases */
    int     memory_kb;         /* max memory across test cases */
    char   *stdout_capture;    /* heap; first failure's stdout, or NULL */
    char   *stderr_capture;    /* heap; first failure's stderr, or NULL */
} grading_result_t;

/*
 * Process one job. Fetches test cases from the DB, compiles the
 * source (if needed), runs the sandbox per test case, aggregates.
 *
 * Returns 0 on success (g populated), -1 on internal error.
 * Internal error means the worker should NOT commit a result; the
 * watchdog will requeue.
 */
int job_process(worker_t *w, uint64_t submission_id,
                uint64_t problem_id, const char *language,
                const char *source, grading_result_t *g);

#endif /* CODEARENA_JOB_PROCESSOR_H */