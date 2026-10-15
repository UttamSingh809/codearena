#ifndef CODEARENA_DB_CLIENT_H
#define CODEARENA_DB_CLIENT_H

/*
 * CodeArena — libpq wrapper.
 *
 * A thin layer over libpq that provides:
 *   - Connection lifecycle
 *   - Parameterized query execution (never string interpolation)
 *   - Transaction helpers with RAII-like cleanup
 *   - Error extraction
 *
 * Every function returns 0 on success, -1 on failure. On failure,
 * db_client_last_error() returns a human-readable message.
 */

#include <stddef.h>
#include <stdint.h>

typedef struct db_conn db_conn_t;

/*
 * Connect to PostgreSQL. `conninfo` is a libpq connection string
 * like "host=localhost dbname=codearena user=codearena password=x".
 * Returns NULL on failure.
 */
db_conn_t *db_connect(const char *conninfo);

void db_disconnect(db_conn_t *conn);

/*
 * Return the last error message, or an empty string if none.
 * Valid until the next db_* call on the same connection.
 */
const char *db_client_last_error(db_conn_t *conn);

/*
 * Begin / commit / rollback. All return 0 on success, -1 on failure.
 */
int db_begin(db_conn_t *conn);
int db_commit(db_conn_t *conn);
int db_rollback(db_conn_t *conn);

/* ------------------------------------------------------------------ */
/* Query execution                                                    */
/* ------------------------------------------------------------------ */

/*
 * Result set. Valid until db_result_free() or the next query.
 * The number of columns is fixed by the query.
 */
typedef struct db_result db_result_t;

void db_result_free(db_result_t *r);

int db_result_rows(db_result_t *r);
int db_result_cols(db_result_t *r);

/* Column getters. Return NULL on out-of-bounds. */
const char *db_result_get(db_result_t *r, int row, int col);
int64_t     db_result_get_int64(db_result_t *r, int row, int col);
int32_t     db_result_get_int32(db_result_t *r, int row, int col);

/* Is the value at (row, col) NULL? */
int db_result_is_null(db_result_t *r, int row, int col);

/*
 * Execute a parameterized query.
 *
 * Parameters:
 *   conn     — connection
 *   sql      — SQL text with $1, $2, ... placeholders
 *   nparams  — number of parameters
 *   values   — array of C strings (NULL means SQL NULL)
 *
 * Returns a result set on success, NULL on failure.
 * Even INSERT/UPDATE/DELETE return a result (with 0 rows).
 */
db_result_t *db_exec(db_conn_t *conn, const char *sql,
                     int nparams, const char *const *values);

/* Convenience: run a query with no parameters. */
db_result_t *db_exec_simple(db_conn_t *conn, const char *sql);

/*
 * Return the number of rows affected by the last INSERT/UPDATE/DELETE.
 * Returns -1 if the query wasn't a data-modifying statement.
 */
int64_t db_affected_rows(db_conn_t *conn);

#endif /* CODEARENA_DB_CLIENT_H */