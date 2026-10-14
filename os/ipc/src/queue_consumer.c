/*
 * queue_consumer.c — dequeue side of the CodeArena queue.
 *
 * Implements the consumer half of the classic three-semaphore
 * bounded buffer:
 *
 *     sem_wait(full)    // wait for a filled slot
 *     sem_wait(mtx)     // acquire mutual exclusion
 *     job = ring[head]
 *     head = (head + 1) % capacity
 *     count--
 *     header->total_dequeued++
 *     sem_post(mtx)     // release mutual exclusion
 *     sem_post(empty)   // signal a free slot
 *
 * The *_try variant replaces sem_wait(full) with sem_trywait(full).
 */

#include "queue_internal.h"
#include "queue.h"

#include <errno.h>
#include <string.h>

/* Internal: perform the dequeue after we've acquired `full`. */
static void do_dequeue_locked(queue_t *q, job_descriptor_t *out)
{
    job_descriptor_t *slot = &q->ring[q->header->head];

    *out = *slot;

    /* Zero the slot so a future enqueue doesn't leave stale data
     * visible in a core dump or via /dev/mem. */
    memset(slot, 0, sizeof(*slot));

    q->header->head = (q->header->head + 1) % q->header->capacity;
    q->header->count--;
    q->header->total_dequeued++;
}

int queue_dequeue(queue_t *q, job_descriptor_t *out)
{
    if (!q || !out) { errno = EINVAL; return CODEARENA_QUEUE_ERR; }

    while (sem_wait(q->full) < 0) {
        if (errno == EINTR) continue;
        return CODEARENA_QUEUE_ERR;
    }

    while (sem_wait(q->mtx) < 0) {
        if (errno == EINTR) continue;
        sem_post(q->full);
        return CODEARENA_QUEUE_ERR;
    }

    do_dequeue_locked(q, out);

    sem_post(q->mtx);
    sem_post(q->empty);
    return CODEARENA_QUEUE_OK;
}

int queue_try_dequeue(queue_t *q, job_descriptor_t *out)
{
    if (!q || !out) { errno = EINVAL; return CODEARENA_QUEUE_ERR; }

    if (sem_trywait(q->full) < 0) {
        if (errno == EAGAIN) return CODEARENA_QUEUE_EMPTY;
        return CODEARENA_QUEUE_ERR;
    }

    while (sem_wait(q->mtx) < 0) {
        if (errno == EINTR) continue;
        sem_post(q->full);
        return CODEARENA_QUEUE_ERR;
    }

    do_dequeue_locked(q, out);

    sem_post(q->mtx);
    sem_post(q->empty);
    return CODEARENA_QUEUE_OK;
}