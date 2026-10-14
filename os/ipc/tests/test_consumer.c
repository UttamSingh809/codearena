/*
 * test_consumer.c — dequeue N jobs from a queue and verify them.
 *
 * Usage:
 *   test_consumer <queue_name> <num_jobs> [<worker_id>]
 *
 * Opens an existing queue, dequeues exactly <num_jobs>, and verifies
 * that submission_id values form a contiguous range [1..num_jobs]
 * with no gaps and no duplicates (if a single consumer is run).
 *
 * With multiple consumers, the verification is weaker (each consumer
 * counts what it got), but the aggregate across all consumers should
 * sum to <num_jobs> with no duplicates.
 *
 * Prints "consumer: got N jobs" to stdout; exits 0 on success.
 */

#include "queue.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s <queue_name> <num_jobs> [<worker_id>]\n",
                argv[0]);
        return 1;
    }

    const char *name    = argv[1];
    uint64_t    expected = strtoull(argv[2], NULL, 10);
    uint32_t    worker  = (argc > 3) ? (uint32_t)strtoul(argv[3], NULL, 10) : 1;

    queue_t *q = queue_open(name);
    if (!q) {
        fprintf(stderr, "consumer: open failed: %s\n", strerror(errno));
        return 1;
    }

    uint64_t got = 0;
    uint64_t last_id = 0;
    int gaps = 0;
    int order_ok = 1;

    for (uint64_t i = 0; i < expected; i++) {
        job_descriptor_t job;
        int rc = queue_dequeue(q, &job);
        if (rc != CODEARENA_QUEUE_OK) {
            fprintf(stderr, "consumer: dequeue failed at %llu: rc=%d errno=%s\n",
                    (unsigned long long)i, rc, strerror(errno));
            queue_close(q);
            return 1;
        }

        if (job.magic != CODEARENA_QUEUE_MAGIC) {
            fprintf(stderr, "consumer: bad magic on job %llu: 0x%08x\n",
                    (unsigned long long)i, job.magic);
            queue_close(q);
            return 1;
        }

        if (job.submission_id == 0) {
            fprintf(stderr, "consumer: zero submission_id on job %llu\n",
                    (unsigned long long)i);
            queue_close(q);
            return 1;
        }

        /* Check monotonic ordering across a single consumer. */
        if (last_id != 0 && job.submission_id <= last_id) {
            order_ok = 0;
        }
        if (last_id != 0 && job.submission_id != last_id + 1) {
            gaps++;
        }
        last_id = job.submission_id;
        got++;
    }

    printf("consumer[%u]: got %llu jobs, order_ok=%d, gaps=%d\n",
           worker, (unsigned long long)got, order_ok, gaps);

    queue_close(q);
    return (order_ok && gaps == 0) ? 0 : 2;
}