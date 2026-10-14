/*
 * test_producer.c — enqueue N jobs into a queue.
 *
 * Usage:
 *   test_producer <queue_name> <capacity> <num_jobs> [<sleep_us>]
 *
 * If <capacity> > 0, creates the queue (and removes it on exit).
 * If <capacity> == 0, opens an existing queue (does not remove it).
 *
 * The optional <sleep_us> inserts a microsecond delay between jobs.
 * Default 0 (no delay) for maximum throughput.
 */

#include "queue.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr,
                "usage: %s <queue_name> <capacity> <num_jobs> [<sleep_us>]\n",
                argv[0]);
        return 1;
    }

    const char *name     = argv[1];
    uint32_t    capacity = (uint32_t)strtoul(argv[2], NULL, 10);
    uint64_t    num_jobs = strtoull(argv[3], NULL, 10);
    long        sleep_us = (argc > 4) ? strtol(argv[4], NULL, 10) : 0;

    queue_t *q = NULL;
    int owner = 0;
    if (capacity > 0) {
        q = queue_create(name, capacity);
        owner = 1;
    } else {
        q = queue_open(name);
    }
    if (!q) {
        fprintf(stderr, "queue %s failed: %s\n",
                owner ? "create" : "open", strerror(errno));
        return 1;
    }

    fprintf(stderr, "producer: starting, %llu jobs\n",
            (unsigned long long)num_jobs);

    for (uint64_t i = 0; i < num_jobs; i++) {
        job_descriptor_t job;
        memset(&job, 0, sizeof(job));
        job.submission_id = i + 1;
        job.problem_id    = 1;
        job.language      = CODEARENA_LANG_C;
        job.time_limit_ms = 2000;
        job.memory_limit_mb = 256;
        job.output_limit_kb = 64;
        job.retry_count   = 0;
        job.worker_id     = 0;
        job.magic         = CODEARENA_QUEUE_MAGIC;

        int rc = queue_enqueue(q, &job);
        if (rc != CODEARENA_QUEUE_OK) {
            fprintf(stderr, "enqueue failed at job %llu: rc=%d errno=%s\n",
                    (unsigned long long)i, rc, strerror(errno));
            if (owner) queue_unlink(name);
            queue_close(q);
            return 1;
        }

        if (sleep_us > 0) usleep((useconds_t)sleep_us);
    }

    fprintf(stderr, "producer: all %llu jobs enqueued\n",
            (unsigned long long)num_jobs);

    if (owner) {
        /* Do NOT unlink here — consumers may still be draining the
         * queue. Only close our handle. */
        queue_close(q);
    } else {
        queue_close(q);
    }
    return 0;
}