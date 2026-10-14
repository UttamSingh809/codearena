#ifndef CODEARENA_QUEUE_PROTOCOL_H
#define CODEARENA_QUEUE_PROTOCOL_H

/*
 * CodeArena — IPC queue wire protocol.
 *
 * A queue consists of one shared-memory segment plus three POSIX
 * named semaphores. The segment layout is:
 *
 *     [ queue_header_t ][ job_descriptor_t x capacity ]
 *
 * Both producers and consumers map the same segment. Producers write
 * into slots at `tail`, consumers read from slots at `head`. Both
 * indices advance modulo capacity.
 *
 * Synchronization uses the classic three-semaphore bounded buffer:
 *   - mtx    : binary semaphore, protects head/tail/count
 *   - empty  : counting, initialized to capacity
 *   - full   : counting, initialized to 0
 *
 * All integers are host-endian. This queue is not designed for
 * cross-architecture communication — everything runs on one machine.
 */

#include <stdint.h>

/* Magic value to detect stale or invalid segments. */
#define CODEARENA_QUEUE_MAGIC   0xC0DEA2EAu

/* Protocol version. Bump when the layout changes incompatibly. */
#define CODEARENA_QUEUE_VERSION 1u

/* Maximum length of a queue name (POSIX shm_open limit is 255). */
#define CODEARENA_QUEUE_NAME_MAX 64

/*
 * Languages supported by the sandbox. Kept in the protocol so the
 * producer can validate before enqueueing and the consumer can
 * dispatch without parsing strings.
 */
typedef enum {
    CODEARENA_LANG_C        = 0,
    CODEARENA_LANG_CPP      = 1,
    CODEARENA_LANG_PYTHON   = 2,
    CODEARENA_LANG_JAVA     = 3,
    CODEARENA_LANG_GO       = 4,
    CODEARENA_LANG_RUST     = 5,
    CODEARENA_LANG_UNKNOWN  = 255,
} codearena_language_t;

/*
 * A single job. Fixed size — the queue stores these by value, not by
 * pointer. Any variable-length data (source code, test inputs) is
 * stored in the database, not in the queue. The queue carries only
 * enough for the worker to identify and dispatch the job.
 *
 * Total size: 8 + 4*6 + 8 + 4 + 4 + 4 = 56 bytes (packed).
 * With padding, 64 bytes — one cache line.
 */
typedef struct {
    uint64_t            submission_id;      /* primary key in DB       */
    uint32_t            problem_id;
    uint32_t            language;           /* codearena_language_t    */
    uint32_t            time_limit_ms;
    uint32_t            memory_limit_mb;
    uint32_t            output_limit_kb;
    uint32_t            retry_count;        /* incremented on requeue  */
    uint64_t            enqueue_time_ns;    /* CLOCK_MONOTONIC ns      */
    uint32_t            worker_id;          /* 0 = unclaimed           */
    uint32_t            magic;              /* CODEARENA_QUEUE_MAGIC   */
} job_descriptor_t;

/*
 * Fixed-layout header at offset 0 of the shared segment.
 *
 * `head` and `tail` are indices into the ring buffer (0..capacity-1).
 * `count` = (tail - head) mod capacity, cached for O(1) size queries
 * without touching both indices.
 *
 * All fields are written only under `mtx`. Readers outside the lock
 * (e.g., a monitoring tool) may see inconsistent values; they should
 * not rely on them for correctness.
 */
typedef struct {
    uint32_t magic;         /* CODEARENA_QUEUE_MAGIC on init         */
    uint32_t version;       /* CODEARENA_QUEUE_VERSION               */
    uint32_t capacity;      /* number of slots in the ring buffer    */
    uint32_t head;          /* next slot to dequeue from             */
    uint32_t tail;          /* next slot to enqueue into             */
    uint32_t count;         /* number of occupied slots              */
    uint64_t total_enqueued;/* cumulative stats                      */
    uint64_t total_dequeued;
    uint64_t total_dropped; /* enqueue attempts that failed          */
    uint8_t  reserved[32];  /* future expansion, zero-filled         */
} queue_header_t;

/*
 * Offsets:
 *   header    at offset 0
 *   ring[0]   at offset sizeof(queue_header_t)
 *   ring[i]   at offset sizeof(queue_header_t) + i * sizeof(job_descriptor_t)
 *
 * The size of one queue:
 *   sizeof(queue_header_t) + capacity * sizeof(job_descriptor_t)
 */

/* Maximum capacity we allow (prevents absurd allocations). */
#define CODEARENA_QUEUE_MAX_CAPACITY 65536u

#endif /* CODEARENA_QUEUE_PROTOCOL_H */