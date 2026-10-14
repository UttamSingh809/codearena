-- ============================================================
-- CodeArena — Watchdog queries
-- ============================================================
--
-- The watchdog runs every few seconds and:
--   1. Finds submissions stuck in PROCESSING beyond the lease.
--   2. Requeues them (PENDING) or marks them FAILED.
--   3. Cleans up cgroups and temp dirs (not a DB concern).
-- ============================================================


-- ------------------------------------------------------------
-- find_stale_processing
-- ------------------------------------------------------------
-- Purpose:    Find submissions stuck in PROCESSING beyond the lease.
-- Parameters: $1 lease_seconds (default 60)
-- Returns:    id, worker_id, processing_started_at, retry_count
-- Index:      idx_submissions_stale (processing_started_at)
--             WHERE status = 'PROCESSING'
-- Notes:      The partial index makes this O(k) where k is the
--             number of stale rows, not O(n) over all PROCESSING
--             rows. Since stale rows are rare, this is fast.
--
--             Lease is measured from processing_started_at. If the
--             worker is still alive but slow (a legitimate long
--             submission), it will be requeued, which is a bug we
--             accept: the new worker will re-run and overwrite.
--             To avoid this, the worker bumps processing_started_at
--             every 30 seconds while running (heartbeat).
-- ------------------------------------------------------------
SELECT
    id,
    worker_id,
    processing_started_at,
    retry_count,
    EXTRACT(EPOCH FROM (NOW() - processing_started_at))::INT AS elapsed_seconds
FROM submissions
WHERE status = 'PROCESSING'
  AND processing_started_at < NOW() - ($1 || ' seconds')::INTERVAL
ORDER BY processing_started_at ASC;


-- ------------------------------------------------------------
-- requeue_stale
-- ------------------------------------------------------------
-- Purpose:    Move a stale submission back to PENDING for retry.
-- Parameters: $1 submission_id, $2 max_retry
-- Returns:    Number of rows updated (0 or 1).
-- Index:      submissions_pkey
-- Notes:      Only requeues if retry_count < max_retry. If the
--             limit is reached, mark_submission_failed is used
--             instead. The WHERE clause includes the status check
--             so that if a worker commits between our SELECT and
--             our UPDATE, we don't clobber a COMPLETED row.
-- ------------------------------------------------------------
UPDATE submissions
SET status = 'PENDING',
    processing_started_at = NULL,
    worker_id = NULL,
    retry_count = retry_count + 1
WHERE id = $1
  AND status = 'PROCESSING'
  AND retry_count < $2
RETURNING id, retry_count;


-- ------------------------------------------------------------
-- mark_stale_failed
-- ------------------------------------------------------------
-- Purpose:    Give up on a stale submission that exceeded max retries.
-- Parameters: $1 submission_id
-- Returns:    The submission id.
-- Index:      submissions_pkey; results_submission_id_key
-- Notes:      Wrapped in a transaction with FOR UPDATE to prevent
--             a race with a worker that's about to commit. The
--             INSERT ... ON CONFLICT handles the case where a
--             worker committed between our SELECT and INSERT.
-- ------------------------------------------------------------
-- BEGIN;
-- SELECT id FROM submissions WHERE id = $1 AND status = 'PROCESSING' FOR UPDATE;
-- INSERT INTO results (submission_id, verdict, score, stderr)
-- VALUES ($1, 'FAILED', 0, 'exceeded max retries')
-- ON CONFLICT (submission_id) DO NOTHING;
-- UPDATE submissions
-- SET status = 'FAILED',
--     processing_started_at = NULL,
--     worker_id = NULL
-- WHERE id = $1;
-- COMMIT;


-- ------------------------------------------------------------
-- bump_heartbeat
-- ------------------------------------------------------------
-- Purpose:    Worker updates processing_started_at to signal liveness.
-- Parameters: $1 submission_id, $2 worker_id
-- Returns:    Number of rows updated (0 or 1).
-- Index:      submissions_pkey
-- Notes:      The worker calls this every 30 seconds while running
--             a long submission. The WHERE clause ensures the worker
--             still owns the submission; if the watchdog has already
--             requeued it, this update is a no-op.
-- ------------------------------------------------------------
UPDATE submissions
SET processing_started_at = NOW()
WHERE id = $1
  AND status = 'PROCESSING'
  AND worker_id = $2;


-- ------------------------------------------------------------
-- processing_count
-- ------------------------------------------------------------
-- Purpose:    Count of currently PROCESSING submissions.
-- Parameters: none
-- Returns:    count
-- Index:      idx_submissions_stale (partial index on PROCESSING)
-- Notes:      Used by monitoring to detect worker starvation or
--             runaway loops.
-- ------------------------------------------------------------
SELECT COUNT(*) AS processing_count
FROM submissions
WHERE status = 'PROCESSING';