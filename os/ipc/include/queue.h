#ifndef CODEARENA_QUEUE_H
#define CODEARENA_QUEUE_H

/*
 * CodeArena — POSIX shared-memory work queue.
 *
 * Public API for producers and consumers. All functions are
 * process-safe: any number of processes may hold a `queue_t` handle
 * to the same named queue and call enqueue/dequeue concurrently.
 *
 * Lifecycle:
 *   1. One process calls queue_create() to allocate the segment and
 *      initialize the semaphores.
 *   2. Any number of processes call queue_open() to attach.
 *   3. Producers call queue_enqueue(). Consumers call queue_dequeue().
 *   4. When all processes are done, one process calls queue_unlink()
 *      to remove the segment and semaphores.
 *
 * queue_enqueue() blocks while the queue is full.
 * queue_dequeue() blocks while the queue is empty.
 *
 * The *_try variants return CODEARENA_QUEUE_FULL / CODEARENA_QUEUE_EMPTY
 * without blocking.
 */

#include "queue_protocol.h"
#include <stddef.h>
#include <stdint.h>

/* Return codes. */
#define CODEARENA_QUEUE_OK        0
#define CODEARENA_QUEUE_FULL     -1   /* try_enqueue on full queue    */
#define CODEARENA_QUEUE_EMPTY    -2   /* try_dequeue on empty queue   */
#define CODEARENA_QUEUE_ERR      -3   /* generic error (errno set)    */
#define CODEARENA_QUEUE_CORRUPT  -4   /* magic/version mismatch       */

/*
 * Opaque handle. Do not touch the fields directly — use the API.
 */
typedef struct queue queue_t;

/*
 * Create a new named queue.
 *
 *   name      — must start with '/', contain no other '/', length <=
 *               CODEARENA_QUEUE_NAME_MAX. E.g. "/codearena_queue".
 *   capacity  — number of slots (1..CODEARENA_QUEUE_MAX_CAPACITY).
 *
 * If the segment already exists (from a previous crash), it is
 * unlinked first, then created fresh. Semaphores are unlinked and
 * recreated as well.
 *
 * Returns a handle on success, NULL on failure (errno set).
 */
queue_t *queue_create(const char *name, uint32_t capacity);

/*
 * Open an existing named queue.
 *
 * The segment and all three semaphores must already exist (created
 * by queue_create). Validates magic and version; returns NULL with
 * CODEARENA_QUEUE_CORRUPT if they don't match.
 */
queue_t *queue_open(const char *name);

/*
 * Close a queue handle. Does not unlink the underlying resources —
 * use queue_unlink() for that.
 */
void queue_close(queue_t *q);

/*
 * Destroy a named queue: unmap, unlink the shared segment, unlink
 * the three semaphores.
 *
 * Should be called only when no other process is using the queue.
 * Safe to call on a name that doesn't exist (returns 0).
 */
int queue_unlink(const char *name);

/*
 * Enqueue a job. Blocks while the queue is full.
 * Returns CODEARENA_QUEUE_OK on success.
 */
int queue_enqueue(queue_t *q, const job_descriptor_t *job);

/*
 * Enqueue a job without blocking. Returns CODEARENA_QUEUE_FULL if
 * the queue is full.
 */
int queue_try_enqueue(queue_t *q, const job_descriptor_t *job);

/*
 * Dequeue a job into `out`. Blocks while the queue is empty.
 */
int queue_dequeue(queue_t *q, job_descriptor_t *out);

/*
 * Dequeue without blocking. Returns CODEARENA_QUEUE_EMPTY if the
 * queue is empty.
 */
int queue_try_dequeue(queue_t *q, job_descriptor_t *out);

/*
 * Read the current queue size and capacity without blocking.
 * Returns 0 on success, -1 on error.
 *
 * These are advisory: by the time the caller acts on the value, it
 * may have changed. Use only for monitoring.
 */
int queue_stats(queue_t *q, uint32_t *out_count, uint32_t *out_capacity,
                uint64_t *out_enqueued, uint64_t *out_dequeued);

#endif /* CODEARENA_QUEUE_H */