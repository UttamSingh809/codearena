#ifndef CODEARENA_QUEUE_INTERNAL_H
#define CODEARENA_QUEUE_INTERNAL_H

/*
 * Private handle layout for the CodeArena queue. Only used by
 * queue.c, queue_producer.c, and queue_consumer.c. Not installed.
 */

#include "queue_protocol.h"
#include <semaphore.h>
#include <time.h>

struct queue {
    char              name[CODEARENA_QUEUE_NAME_MAX + 1];
    int               shm_fd;
    void             *map_base;
    size_t            map_size;
    queue_header_t   *header;
    job_descriptor_t *ring;
    sem_t            *mtx;
    sem_t            *empty;
    sem_t            *full;
    int               owner;
};

/* Helpers used by both producer and consumer. */

static inline uint64_t queue_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static inline void queue_fill_common(job_descriptor_t *job,
                                     uint32_t worker_id)
{
    job->magic           = CODEARENA_QUEUE_MAGIC;
    job->enqueue_time_ns = queue_now_ns();
    job->worker_id       = worker_id;
}

#endif /* CODEARENA_QUEUE_INTERNAL_H */