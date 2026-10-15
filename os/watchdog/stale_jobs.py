"""
Stale job detection and requeue.

A submission is "stale" if it's in PROCESSING state but its
processing_started_at is older than STALE_LEASE_SEC. This means the
worker that claimed it either crashed or stopped sending heartbeats.

For each stale submission:
  1. If retry_count >= MAX_RETRY_COUNT, mark FAILED and insert a
     result row with verdict='FAILED'.
  2. Otherwise, set status='PENDING', clear worker fields, increment
     retry_count, and enqueue a fresh job descriptor on the IPC queue.

Both paths run in a transaction with SELECT ... FOR UPDATE to
serialize against a worker that might be committing a result.
"""

import logging
import time
from typing import List, Tuple

import psycopg2

from . import config
from .queue_writer import enqueue_job

log = logging.getLogger("watchdog.stale_jobs")


# ------------------------------------------------------------------
# SQL
# ------------------------------------------------------------------

FIND_STALE_SQL = """
    SELECT id, worker_id, retry_count,
           EXTRACT(EPOCH FROM (NOW() - processing_started_at))::INT AS elapsed
    FROM submissions
    WHERE status = 'PROCESSING'
      AND processing_started_at < NOW() - (%s || ' seconds')::INTERVAL
    ORDER BY processing_started_at ASC
"""

# We lock the row FOR UPDATE to prevent a race with a worker that
# is about to commit. If the worker commits between our SELECT and
# our UPDATE, the SELECT will see status='PROCESSING' (its snapshot
# predates the commit), but the UPDATE will block on the worker's
# row lock. When we finally get the lock, we re-check status.
LOCK_SQL = """
    SELECT status, worker_id
    FROM submissions
    WHERE id = %s
    FOR UPDATE
"""

REQUEUE_SQL = """
    UPDATE submissions
    SET status = 'PENDING',
        processing_started_at = NULL,
        worker_id = NULL,
        retry_count = retry_count + 1
    WHERE id = %s
      AND status = 'PROCESSING'
    RETURNING retry_count
"""

MARK_FAILED_RESULT_SQL = """
    INSERT INTO results (submission_id, verdict, score, stderr, graded_by)
    VALUES (%s, 'FAILED', 0, %s, 'watchdog')
    ON CONFLICT (submission_id) DO NOTHING
"""

MARK_FAILED_SUBMISSION_SQL = """
    UPDATE submissions
    SET status = 'FAILED',
        processing_started_at = NULL,
        worker_id = NULL
    WHERE id = %s
      AND status = 'PROCESSING'
"""


# ------------------------------------------------------------------
# Detection and requeue
# ------------------------------------------------------------------

def _find_stale(conn) -> List[Tuple[int, str, int, int]]:
    """Return [(submission_id, worker_id, retry_count, elapsed_sec), ...]."""
    with conn.cursor() as cur:
        cur.execute(FIND_STALE_SQL, (config.STALE_LEASE_SEC,))
        rows = cur.fetchall()
    return rows


def _requeue_one(conn, submission_id: int, retry_count: int) -> bool:
    """
    Requeue a single stale submission. Returns True if requeued,
    False if it was no longer stale (a worker committed first).
    """
    with conn.cursor() as cur:
        # Lock the row and re-verify status.
        cur.execute(LOCK_SQL, (submission_id,))
        row = cur.fetchone()
        if not row:
            conn.rollback()
            return False

        status, worker_id = row
        if status != "PROCESSING":
            # A worker committed between our SELECT and now.
            conn.rollback()
            log.debug(
                "submission %d no longer PROCESSING (status=%s); skipping",
                submission_id, status,
            )
            return False

        cur.execute(REQUEUE_SQL, (submission_id,))
        result = cur.fetchone()
        if not result:
            conn.rollback()
            return False

        new_retry = result[0]
        conn.commit()
        log.info(
            "requeued submission %d (was worker=%s, retry=%d -> %d)",
            submission_id, worker_id, retry_count, new_retry,
        )
        return True


def _mark_failed_one(conn, submission_id: int, retry_count: int) -> bool:
    """
    Mark a stale submission FAILED after exhausting retries.
    Returns True if marked, False if no longer stale.
    """
    with conn.cursor() as cur:
        cur.execute(LOCK_SQL, (submission_id,))
        row = cur.fetchone()
        if not row:
            conn.rollback()
            return False

        status, worker_id = row
        if status != "PROCESSING":
            conn.rollback()
            return False

        err = f"exceeded max retries ({retry_count}); last worker={worker_id}"
        cur.execute(MARK_FAILED_RESULT_SQL, (submission_id, err))
        cur.execute(MARK_FAILED_SUBMISSION_SQL, (submission_id,))
        conn.commit()
        log.warning(
            "marked submission %d FAILED after %d retries (last worker=%s)",
            submission_id, retry_count, worker_id,
        )
        return True


def run_once(conn) -> Tuple[int, int]:
    """
    Scan for stale submissions and process them.

    Returns (requeued_count, failed_count).
    """
    stale = _find_stale(conn)
    if not stale:
        return (0, 0)

    log.info("found %d stale submission(s)", len(stale))

    requeued = 0
    failed = 0

    for submission_id, worker_id, retry_count, elapsed in stale:
        try:
            if retry_count >= config.MAX_RETRY_COUNT:
                if _mark_failed_one(conn, submission_id, retry_count):
                    failed += 1
            else:
                if _requeue_one(conn, submission_id, retry_count):
                    requeued += 1
                    # Enqueue a fresh job descriptor so a worker picks it up.
                    if not enqueue_job(submission_id):
                        log.error(
                            "enqueue failed for requeued submission %d; "
                            "row is PENDING in DB but no worker will see it",
                            submission_id,
                        )
        except psycopg2.Error as e:
            # A single failure shouldn't kill the loop. Log and continue.
            log.error("error processing stale submission %d: %s",
                      submission_id, e)
            try:
                conn.rollback()
            except Exception:
                pass

    return (requeued, failed)