/*
 * queue_producer.c — enqueue side of the CodeArena queue.
 *
 * Implements the producer half of the classic three-semaphore bounded
 * buffer:
 *
 *     sem_wait(empty)   // wait for a free slot
 *     sem_wait(mtx)     // acquire mutual exclusion
 *     ring[tail] = job
 *     tail = (tail + 1) % capacity
 *     count++
 *     header->total_enqueued++
 *     sem_post(mtx)     // release mutual exclusion
 *     sem_post(full)    // signal a filled slot
 *
 * The *_try variant replaces sem_wait(empty) with sem_trywait(empty).
 */

#include "queue_internal.h"
#include "queue.h"

#include <errno.h>
#include <string.h>

/* Validate a job descriptor before enqueueing. */
static int validate_job(const job_descriptor_t *job)
{
    if (!job) return -1;
    if (job->submission_id == 0) return -1;
    if (job->problem_id == 0) return -1;
    return 0;
}

/* Internal: perform the enqueue after we've acquired `empty`. */
static void do_enqueue_locked(queue_t *q, const job_descriptor_t *job)
{
    job_descriptor_t *slot = &q->ring[q->header->tail];

    /* Copy the descriptor into the ring slot. */
    *slot = *job;

    /* Stamp magic and time if the caller didn't. */
    if (slot->magic != CODEARENA_QUEUE_MAGIC) {
        queue_fill_common(slot, slot->worker_id);
    }

    /* Advance tail with wraparound. */
    q->header->tail = (q->header->tail + 1) % q->header->capacity;
    q->header->count++;
    q->header->total_enqueued++;
}

int queue_enqueue(queue_t *q, const job_descriptor_t *job)
{
    if (!q || validate_job(job) < 0) { errno = EINVAL; return CODEARENA_QUEUE_ERR; }

    /* Block until a slot is free. */
    while (sem_wait(q->empty) < 0) {
        if (errno == EINTR) continue;
        return CODEARENA_QUEUE_ERR;
    }

    /* Acquire the mutex. */
    while (sem_wait(q->mtx) < 0) {
        if (errno == EINTR) continue;
        /* If mutex wait fails, put back the empty slot to avoid
         * losing count on the semaphore. */
        sem_post(q->empty);
        return CODEARENA_QUEUE_ERR;
    }

    do_enqueue_locked(q, job);

    sem_post(q->mtx);
    sem_post(q->full);
    return CODEARENA_QUEUE_OK;
}

int queue_try_enqueue(queue_t *q, const job_descriptor_t *job)
{
    if (!q || validate_job(job) < 0) { errno = EINVAL; return CODEARENA_QUEUE_ERR; }

    if (sem_trywait(q->empty) < 0) {
        if (errno == EAGAIN) return CODEARENA_QUEUE_FULL;
        return CODEARENA_QUEUE_ERR;
    }

    while (sem_wait(q->mtx) < 0) {
        if (errno == EINTR) continue;
        sem_post(q->empty);
        return CODEARENA_QUEUE_ERR;
    }

    do_enqueue_locked(q, job);

    sem_post(q->mtx);
    sem_post(q->full);
    return CODEARENA_QUEUE_OK;
}