/*
 * test_stress.c — spawn P producers and C consumers on one queue.
 *
 * Usage:
 *   test_stress <queue_name> <capacity> <num_producers>
 *               <num_consumers> <total_jobs>
 *
 * Each producer enqueues a disjoint range [start..end]. Each consumer
 * dequeues until it has drained its share. The test verifies that
 * exactly <total_jobs> jobs were produced and consumed.
 *
 * Uses fork() so no external harness is needed.
 */

#include "queue.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc < 6) {
        fprintf(stderr,
                "usage: %s <queue_name> <capacity> <P> <C> <total>\n",
                argv[0]);
        return 1;
    }

    const char *name     = argv[1];
    uint32_t    capacity = (uint32_t)strtoul(argv[2], NULL, 10);
    uint32_t    nprod    = (uint32_t)strtoul(argv[3], NULL, 10);
    uint32_t    ncons    = (uint32_t)strtoul(argv[4], NULL, 10);
    uint64_t    total    = strtoull(argv[5], NULL, 10);

    if (nprod == 0 || ncons == 0) {
        fprintf(stderr, "need at least one producer and one consumer\n");
        return 1;
    }

    queue_t *q = queue_create(name, capacity);
    if (!q) {
        fprintf(stderr, "queue_create failed: %s\n", strerror(errno));
        return 1;
    }

    printf("stress: capacity=%u P=%u C=%u total=%llu\n",
           capacity, nprod, ncons, (unsigned long long)total);

    pid_t prod_pids[64];
    pid_t cons_pids[64];

    /* Split the total job range among producers. */
    uint64_t per_prod = total / nprod;
    uint64_t remainder = total % nprod;

    /* Fork producers. */
    for (uint32_t i = 0; i < nprod; i++) {
        pid_t p = fork();
        if (p < 0) { perror("fork producer"); return 1; }
        if (p == 0) {
            queue_t *cq = queue_open(name);
            if (!cq) { _exit(1); }

            uint64_t count = per_prod + (i < remainder ? 1 : 0);
            uint64_t start = i * per_prod + (i < remainder ? i : remainder);

            for (uint64_t j = 0; j < count; j++) {
                job_descriptor_t job;
                memset(&job, 0, sizeof(job));
                job.submission_id = start + j + 1;
                job.problem_id    = 1;
                job.language      = CODEARENA_LANG_C;
                job.magic         = CODEARENA_QUEUE_MAGIC;
                job.worker_id     = i + 1;

                if (queue_enqueue(cq, &job) != CODEARENA_QUEUE_OK) {
                    _exit(1);
                }
            }
            queue_close(cq);
            _exit(0);
        }
        prod_pids[i] = p;
    }

    /* Fork consumers. Each drains until it has gotten its share of
     * the total. Since we don't know which jobs go to which consumer
     * in advance, we simply have each consumer dequeue until the
     * queue reports it has been drained (via queue_try_dequeue). */
    for (uint32_t i = 0; i < ncons; i++) {
        pid_t p = fork();
        if (p < 0) { perror("fork consumer"); return 1; }
        if (p == 0) {
            queue_t *cq = queue_open(name);
            if (!cq) { _exit(1); }

            uint64_t got = 0;
            for (;;) {
                job_descriptor_t job;
                int rc = queue_try_dequeue(cq, &job);
                if (rc == CODEARENA_QUEUE_OK) {
                    got++;
                    continue;
                }
                if (rc == CODEARENA_QUEUE_EMPTY) {
                    /* Check if all producers are done by trying again
                     * a few times with a small delay. */
                    usleep(1000);
                    rc = queue_try_dequeue(cq, &job);
                    if (rc == CODEARENA_QUEUE_OK) { got++; continue; }
                    /* Assume producers are done. */
                    break;
                }
                _exit(1);
            }
            printf("stress: consumer[%u] got %llu\n",
                   i, (unsigned long long)got);
            queue_close(cq);
            _exit(0);
        }
        cons_pids[i] = p;
    }

    /* Wait for producers first, then consumers. */
    int fail = 0;
    for (uint32_t i = 0; i < nprod; i++) {
        int st = 0; waitpid(prod_pids[i], &st, 0);
        if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) fail = 1;
    }
    for (uint32_t i = 0; i < ncons; i++) {
        int st = 0; waitpid(cons_pids[i], &st, 0);
        if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) fail = 1;
    }

    /* Verify the queue is empty and stats are consistent. */
    uint32_t cnt = 0, cap = 0;
    uint64_t enq = 0, deq = 0;
    queue_stats(q, &cnt, &cap, &enq, &deq);
    printf("stress: stats count=%u capacity=%u enqueued=%llu dequeued=%llu\n",
           cnt, cap, (unsigned long long)enq, (unsigned long long)deq);

    if (enq != total) {
        fprintf(stderr, "stress: enqueued %llu != total %llu\n",
                (unsigned long long)enq, (unsigned long long)total);
        fail = 1;
    }
    if (deq != total) {
        fprintf(stderr, "stress: dequeued %llu != total %llu\n",
                (unsigned long long)deq, (unsigned long long)total);
        fail = 1;
    }
    if (cnt != 0) {
        fprintf(stderr, "stress: queue not empty: count=%u\n", cnt);
        fail = 1;
    }

    queue_close(q);
    queue_unlink(name);

    printf("stress: %s\n", fail ? "FAIL" : "PASS");
    return fail ? 1 : 0;
}