/*
 * db_client.c — libpq wrapper implementation.
 *
 * Wraps PGconn, PGresult, and the libpq query API. All parameters
 * are passed via PQexecParams, never interpolated into SQL text.
 * This is the only correct way to handle untrusted input.
 *
 * Ownership model:
 *   - db_conn_t owns PGconn and the most recent PGresult.
 *   - db_result_t is a NON-OWNING wrapper around the connection's
 *     last PGresult. It must not outlive the connection, and it
 *     must not be used after the next db_exec() on the same
 *     connection (which replaces the cached result).
 *   - db_result_free() releases only the wrapper, not the PGresult.
 *     The connection releases the PGresult on the next query or
 *     on db_disconnect().
 */

#include "db_client.h"

#include <errno.h>
#include <libpq-fe.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Connection                                                         */
/* ------------------------------------------------------------------ */

struct db_conn {
    PGconn   *pg;
    PGresult *last_result;   /* most recent PGresult, owned by us */
    char      last_error[512];
};

struct db_result {
    db_conn_t *conn;    /* borrowed pointer; used for error context */
    PGresult  *pg;      /* borrowed pointer into conn->last_result */
    int        rows;
    int        cols;
};

static void set_error(db_conn_t *c, const char *msg)
{
    if (!c) return;
    snprintf(c->last_error, sizeof(c->last_error), "%s", msg ? msg : "");
}

db_conn_t *db_connect(const char *conninfo)
{
    PGconn *pg = PQconnectdb(conninfo);
    if (!pg) {
        errno = ENOMEM;
        return NULL;
    }
    if (PQstatus(pg) != CONNECTION_OK) {
        fprintf(stderr, "db_connect failed: %s\n", PQerrorMessage(pg));
        PQfinish(pg);
        errno = ECONNREFUSED;
        return NULL;
    }

    db_conn_t *c = calloc(1, sizeof(*c));
    if (!c) {
        PQfinish(pg);
        return NULL;
    }
    c->pg          = pg;
    c->last_result = NULL;
    return c;
}

void db_disconnect(db_conn_t *c)
{
    if (!c) return;
    if (c->last_result) PQclear(c->last_result);
    if (c->pg)          PQfinish(c->pg);
    free(c);
}

const char *db_client_last_error(db_conn_t *c)
{
    if (!c) return "";
    return c->last_error;
}

/* ------------------------------------------------------------------ */
/* Transactions                                                       */
/* ------------------------------------------------------------------ */

static int exec_status(db_conn_t *c, const char *sql)
{
    PGresult *r = PQexec(c->pg, sql);
    if (!r) {
        set_error(c, "PQexec returned NULL");
        return -1;
    }
    ExecStatusType st = PQresultStatus(r);
    if (st != PGRES_COMMAND_OK) {
        set_error(c, PQresultErrorMessage(r));
        PQclear(r);
        return -1;
    }
    PQclear(r);
    return 0;
}

int db_begin(db_conn_t *c)    { return exec_status(c, "BEGIN"); }
int db_commit(db_conn_t *c)   { return exec_status(c, "COMMIT"); }
int db_rollback(db_conn_t *c) { return exec_status(c, "ROLLBACK"); }

/* ------------------------------------------------------------------ */
/* Result set                                                         */
/* ------------------------------------------------------------------ */

void db_result_free(db_result_t *r)
{
    if (!r) return;
    /* The PGresult is owned by the connection (c->last_result);
     * do NOT PQclear it here. */
    free(r);
}

int db_result_rows(db_result_t *r) { return r ? r->rows : 0; }
int db_result_cols(db_result_t *r) { return r ? r->cols : 0; }

const char *db_result_get(db_result_t *r, int row, int col)
{
    if (!r) return NULL;
    if (row < 0 || row >= r->rows) return NULL;
    if (col < 0 || col >= r->cols) return NULL;
    if (PQgetisnull(r->pg, row, col)) return NULL;
    return PQgetvalue(r->pg, row, col);
}

int db_result_is_null(db_result_t *r, int row, int col)
{
    if (!r) return 1;
    if (row < 0 || row >= r->rows) return 1;
    if (col < 0 || col >= r->cols) return 1;
    return PQgetisnull(r->pg, row, col);
}

int64_t db_result_get_int64(db_result_t *r, int row, int col)
{
    const char *s = db_result_get(r, row, col);
    if (!s) return 0;
    return (int64_t)strtoll(s, NULL, 10);
}

int32_t db_result_get_int32(db_result_t *r, int row, int col)
{
    return (int32_t)db_result_get_int64(r, row, col);
}

/* ------------------------------------------------------------------ */
/* Query execution                                                    */
/* ------------------------------------------------------------------ */

db_result_t *db_exec(db_conn_t *c, const char *sql,
                     int nparams, const char *const *values)
{
    if (!c || !c->pg) return NULL;

    /* Free the previously cached result — we're about to replace it. */
    if (c->last_result) {
        PQclear(c->last_result);
        c->last_result = NULL;
    }

    PGresult *r = PQexecParams(c->pg, sql,
                               nparams, NULL, values,
                               NULL, NULL, 0);
    if (!r) {
        set_error(c, "PQexecParams returned NULL");
        return NULL;
    }

    ExecStatusType st = PQresultStatus(r);
    if (st != PGRES_TUPLES_OK && st != PGRES_COMMAND_OK) {
        set_error(c, PQresultErrorMessage(r));
        PQclear(r);
        return NULL;
    }

    db_result_t *out = calloc(1, sizeof(*out));
    if (!out) {
        PQclear(r);
        set_error(c, "calloc failed");
        return NULL;
    }
    out->conn = c;
    out->pg   = r;
    out->rows = PQntuples(r);
    out->cols = PQnfields(r);

    /* Transfer ownership of the PGresult to the connection. */
    c->last_result = r;

    return out;
}

db_result_t *db_exec_simple(db_conn_t *c, const char *sql)
{
    return db_exec(c, sql, 0, NULL);
}

int64_t db_affected_rows(db_conn_t *c)
{
    if (!c || !c->last_result) return -1;
    const char *s = PQcmdTuples(c->last_result);
    if (!s || !*s) return -1;
    return (int64_t)strtoll(s, NULL, 10);
}
