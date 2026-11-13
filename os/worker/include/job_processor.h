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

/*
 * Represents the result of running a single test case.
 */
typedef struct
{
    int ordinal;
    long test_case_id;
    char verdict[16];
    char *actual_output; /* heap; may be NULL */
    int runtime_ms;
    int memory_kb;
} test_case_result_t;

typedef struct
{
    char verdict[16]; /* AC, WA, TLE, MLE, RE, CE, OLE, PE */
    int score;
    int runtime_ms;       /* max runtime across test cases */
    int memory_kb;        /* max memory across test cases */
    char *stdout_capture; /* heap; first failure's stdout, or NULL */
    char *stderr_capture; /* heap; first failure's stderr, or NULL */

    /* Test case accounting */
    int test_total;   /* total test cases considered */
    int test_passed;  /* number that passed (verdict=AC) */
    int first_failed; /* ordinal of first failure, or 0 if none */

    /* Per-test-case details */
    test_case_result_t *cases;
    int cases_count;
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

/*
 * Free memory allocated inside a grading_result_t.
 */
void grading_result_free(grading_result_t *g);

#endif /* CODEARENA_JOB_PROCESSOR_H */