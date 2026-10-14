/*
 * queue.c — POSIX shared-memory queue: lifecycle operations.
 *
 * This file handles the segment and semaphore lifecycle:
 *   - queue_create / queue_open / queue_close / queue_unlink
 *   - queue_stats
 *
 * Producer and consumer logic lives in queue_producer.c and
 * queue_consumer.c respectively. This file is the common base.
 */

#include "queue.h"

#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* Internal handle                                                    */
/* ------------------------------------------------------------------ */

struct queue {
    char             name[CODEARENA_QUEUE_NAME_MAX + 1];
    int              shm_fd;             /* fd for shm_open           */
    void            *map_base;           /* mmap'd region             */
    size_t           map_size;           /* bytes                     */
    queue_header_t  *header;             /* == map_base               */
    job_descriptor_t *ring;              /* == map_base + hdr size    */
    sem_t           *mtx;                /* binary                   */
    sem_t           *empty;              /* counting, init capacity  */
    sem_t           *full;               /* counting, init 0         */
    int              owner;              /* 1 if created by us        */
};

/* ------------------------------------------------------------------ */
/* Utilities                                                          */
/* ------------------------------------------------------------------ */

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* Build semaphore name "<base>_mtx" etc. Caller provides buffer. */
static void make_sem_name(char *dst, size_t dstsz,
                          const char *base, const char *suffix)
{
    snprintf(dst, dstsz, "%s_%s", base, suffix);
}

/* Validate a queue name: starts with '/', no other '/'. */
static int validate_name(const char *name)
{
    if (!name || name[0] != '/') return -1;
    if (strlen(name) > CODEARENA_QUEUE_NAME_MAX) return -1;
    if (strchr(name + 1, '/') != NULL) return -1;
    return 0;
}

/* Compute total mmap size for a given capacity. */
static size_t compute_size(uint32_t capacity)
{
    return sizeof(queue_header_t) +
           (size_t)capacity * sizeof(job_descriptor_t);
}

/* ------------------------------------------------------------------ */
/* Create                                                             */
/* ------------------------------------------------------------------ */

queue_t *queue_create(const char *name, uint32_t capacity)
{
    if (validate_name(name) < 0) { errno = EINVAL; return NULL; }
    if (capacity == 0 || capacity > CODEARENA_QUEUE_MAX_CAPACITY) {
        errno = EINVAL;
        return NULL;
    }

    /* Remove any stale segment and semaphores from a previous run. */
    shm_unlink(name);

    char sem_mtx  [CODEARENA_QUEUE_NAME_MAX + 16];
    char sem_empty[CODEARENA_QUEUE_NAME_MAX + 16];
    char sem_full [CODEARENA_QUEUE_NAME_MAX + 16];
    make_sem_name(sem_mtx,   sizeof(sem_mtx),   name, "mtx");
    make_sem_name(sem_empty, sizeof(sem_empty), name, "empty");
    make_sem_name(sem_full,  sizeof(sem_full),  name, "full");
    sem_unlink(sem_mtx);
    sem_unlink(sem_empty);
    sem_unlink(sem_full);

    /* Create the shared-memory segment. */
    int fd = shm_open(name, O_CREAT | O_RDWR | O_EXCL, 0600);
    if (fd < 0) return NULL;

    size_t size = compute_size(capacity);
    if (ftruncate(fd, (off_t)size) < 0) {
        int saved = errno;
        close(fd);
        shm_unlink(name);
        errno = saved;
        return NULL;
    }

    void *base = mmap(NULL, size, PROT_READ | PROT_WRITE,
                      MAP_SHARED, fd, 0);
    if (base == MAP_FAILED) {
        int saved = errno;
        close(fd);
        shm_unlink(name);
        errno = saved;
        return NULL;
    }

    /* Initialize the header. */
    queue_header_t *h = (queue_header_t *)base;
    memset(h, 0, sizeof(*h));
    h->magic    = CODEARENA_QUEUE_MAGIC;
    h->version  = CODEARENA_QUEUE_VERSION;
    h->capacity = capacity;
    h->head     = 0;
    h->tail     = 0;
    h->count    = 0;

    /* Initialize the ring buffer to zero. */
    job_descriptor_t *ring =
        (job_descriptor_t *)((uint8_t *)base + sizeof(queue_header_t));
    memset(ring, 0, (size_t)capacity * sizeof(job_descriptor_t));

    /* Create the three semaphores. */
    sem_t *mtx   = sem_open(sem_mtx,   O_CREAT | O_EXCL, 0600, 1);
    sem_t *empty = sem_open(sem_empty, O_CREAT | O_EXCL, 0600, capacity);
    sem_t *full  = sem_open(sem_full,  O_CREAT | O_EXCL, 0600, 0);

    if (mtx == SEM_FAILED || empty == SEM_FAILED || full == SEM_FAILED) {
        int saved = errno;
        if (mtx   != SEM_FAILED) sem_close(mtx);
        if (empty != SEM_FAILED) sem_close(empty);
        if (full  != SEM_FAILED) sem_close(full);
        sem_unlink(sem_mtx);
        sem_unlink(sem_empty);
        sem_unlink(sem_full);
        munmap(base, size);
        close(fd);
        shm_unlink(name);
        errno = saved;
        return NULL;
    }

    /* Build the handle. */
    queue_t *q = calloc(1, sizeof(*q));
    if (!q) {
        int saved = errno;
        sem_close(mtx);   sem_unlink(sem_mtx);
        sem_close(empty); sem_unlink(sem_empty);
        sem_close(full);  sem_unlink(sem_full);
        munmap(base, size);
        close(fd);
        shm_unlink(name);
        errno = saved;
        return NULL;
    }

    snprintf(q->name, sizeof(q->name), "%s", name);
    q->shm_fd   = fd;
    q->map_base = base;
    q->map_size = size;
    q->header   = h;
    q->ring     = ring;
    q->mtx      = mtx;
    q->empty    = empty;
    q->full     = full;
    q->owner    = 1;

    return q;
}

/* ------------------------------------------------------------------ */
/* Open                                                               */
/* ------------------------------------------------------------------ */

queue_t *queue_open(const char *name)
{
    if (validate_name(name) < 0) { errno = EINVAL; return NULL; }

    int fd = shm_open(name, O_RDWR, 0600);
    if (fd < 0) return NULL;

    /* Determine the size via fstat — we need it for mmap. */
    struct stat st;
    if (fstat(fd, &st) < 0) {
        int saved = errno;
        close(fd);
        errno = saved;
        return NULL;
    }
    size_t size = (size_t)st.st_size;

    void *base = mmap(NULL, size, PROT_READ | PROT_WRITE,
                      MAP_SHARED, fd, 0);
    if (base == MAP_FAILED) {
        int saved = errno;
        close(fd);
        errno = saved;
        return NULL;
    }

    /* Validate the header before doing anything else. */
    queue_header_t *h = (queue_header_t *)base;
    if (h->magic != CODEARENA_QUEUE_MAGIC ||
        h->version != CODEARENA_QUEUE_VERSION ||
        h->capacity == 0 ||
        h->capacity > CODEARENA_QUEUE_MAX_CAPACITY ||
        size < compute_size(h->capacity)) {
        munmap(base, size);
        close(fd);
        errno = 0;
        return NULL;   /* caller can check errno == 0 for "corrupt" */
    }

    job_descriptor_t *ring =
        (job_descriptor_t *)((uint8_t *)base + sizeof(queue_header_t));

    /* Open the semaphores. */
    char sem_mtx  [CODEARENA_QUEUE_NAME_MAX + 16];
    char sem_empty[CODEARENA_QUEUE_NAME_MAX + 16];
    char sem_full [CODEARENA_QUEUE_NAME_MAX + 16];
    make_sem_name(sem_mtx,   sizeof(sem_mtx),   name, "mtx");
    make_sem_name(sem_empty, sizeof(sem_empty), name, "empty");
    make_sem_name(sem_full,  sizeof(sem_full),  name, "full");

    sem_t *mtx   = sem_open(sem_mtx,   0);
    sem_t *empty = sem_open(sem_empty, 0);
    sem_t *full  = sem_open(sem_full,  0);

    if (mtx == SEM_FAILED || empty == SEM_FAILED || full == SEM_FAILED) {
        int saved = errno;
        if (mtx   != SEM_FAILED) sem_close(mtx);
        if (empty != SEM_FAILED) sem_close(empty);
        if (full  != SEM_FAILED) sem_close(full);
        munmap(base, size);
        close(fd);
        errno = saved;
        return NULL;
    }

    queue_t *q = calloc(1, sizeof(*q));
    if (!q) {
        int saved = errno;
        sem_close(mtx); sem_close(empty); sem_close(full);
        munmap(base, size);
        close(fd);
        errno = saved;
        return NULL;
    }

    snprintf(q->name, sizeof(q->name), "%s", name);
    q->shm_fd   = fd;
    q->map_base = base;
    q->map_size = size;
    q->header   = h;
    q->ring     = ring;
    q->mtx      = mtx;
    q->empty    = empty;
    q->full     = full;
    q->owner    = 0;

    return q;
}

/* ------------------------------------------------------------------ */
/* Close & unlink                                                     */
/* ------------------------------------------------------------------ */

void queue_close(queue_t *q)
{
    if (!q) return;
    if (q->mtx   && q->mtx   != SEM_FAILED) sem_close(q->mtx);
    if (q->empty && q->empty != SEM_FAILED) sem_close(q->empty);
    if (q->full  && q->full  != SEM_FAILED) sem_close(q->full);
    if (q->map_base && q->map_base != MAP_FAILED) munmap(q->map_base, q->map_size);
    if (q->shm_fd >= 0) close(q->shm_fd);
    free(q);
}

int queue_unlink(const char *name)
{
    if (validate_name(name) < 0) { errno = EINVAL; return -1; }

    char sem_mtx  [CODEARENA_QUEUE_NAME_MAX + 16];
    char sem_empty[CODEARENA_QUEUE_NAME_MAX + 16];
    char sem_full [CODEARENA_QUEUE_NAME_MAX + 16];
    make_sem_name(sem_mtx,   sizeof(sem_mtx),   name, "mtx");
    make_sem_name(sem_empty, sizeof(sem_empty), name, "empty");
    make_sem_name(sem_full,  sizeof(sem_full),  name, "full");

    int rc = 0;
    if (shm_unlink(name) < 0 && errno != ENOENT) rc = -1;
    if (sem_unlink(sem_mtx)   < 0 && errno != ENOENT) rc = -1;
    if (sem_unlink(sem_empty) < 0 && errno != ENOENT) rc = -1;
    if (sem_unlink(sem_full)  < 0 && errno != ENOENT) rc = -1;
    return rc;
}

/* ------------------------------------------------------------------ */
/* Stats                                                              */
/* ------------------------------------------------------------------ */

int queue_stats(queue_t *q, uint32_t *out_count, uint32_t *out_capacity,
                uint64_t *out_enqueued, uint64_t *out_dequeued)
{
    if (!q) { errno = EINVAL; return -1; }
    if (out_count)    *out_count    = q->header->count;
    if (out_capacity) *out_capacity = q->header->capacity;
    if (out_enqueued) *out_enqueued = q->header->total_enqueued;
    if (out_dequeued) *out_dequeued = q->header->total_dequeued;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Internal helpers used by producer/consumer translation units       */
/* ------------------------------------------------------------------ */

/*
 * The producer and consumer share access to these fields. We expose
 * them via internal inline functions declared in a private header
 * (queue_internal.h) so that queue_producer.c and queue_consumer.c
 * can access the handle's fields without redeclaring the struct.
 */