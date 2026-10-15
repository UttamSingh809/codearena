#ifndef CODEARENA_WORKER_INTERNAL_H
#define CODEARENA_WORKER_INTERNAL_H

/*
 * Private worker layout. Included only by worker.c and
 * job_processor.c — never by code outside the worker module.
 *
 * The public header (worker.h) keeps struct worker opaque via a
 * forward declaration, so callers can pass worker_t* around without
 * knowing or depending on the layout.
 */

#include "worker.h"
#include "db_client.h"
#include "queue.h"

struct worker
{
    worker_config_t cfg;
    db_conn_t *db;
    queue_t *queue;
    int stop;
};

#endif /* CODEARENA_WORKER_INTERNAL_H */