-- ============================================================
-- CodeArena — Atomic grading transaction
-- ============================================================
--
-- This is the most important query in the system. It must be
-- correct under concurrent load: no double-grades, no lost
-- updates, no partial state.
--
-- The transaction:
--   1. Locks the submission row FOR UPDATE.
--   2. Verifies it's still PROCESSING and owned by this worker.
--   3. Inserts the result (UNIQUE on submission_id prevents dup).
--   4. Updates the submission status to COMPLETED.
--   5. Commits.
--
-- If any step fails, ROLLBACK leaves the submission untouched.
-- The watchdog will requeue it after the lease timeout.
--
-- Called by: worker, once per submission, after running the
-- sandbox on every test case and determining the final verdict.
-- ============================================================

BEGIN;

-- ------------------------------------------------------------
-- Step 1: Lock the submission row.
-- ------------------------------------------------------------
-- FOR UPDATE acquires a row-level exclusive lock. Any other
-- transaction trying to lock the same row blocks until this one
-- commits or rolls back. This serializes concurrent graders on
-- the same submission.
--
-- We also verify the status and worker_id match what we expect.
-- If another worker already graded this submission (status is
-- now COMPLETED), or the watchdog requeued it (status is PENDING
-- again), we must not proceed. The application checks these
-- fields after the SELECT and issues ROLLBACK if they don't match.
-- ------------------------------------------------------------
SELECT id, status, worker_id, retry_count
FROM submissions
WHERE id = $1
FOR UPDATE;


-- ------------------------------------------------------------
-- Step 2: Insert the result.
-- ------------------------------------------------------------
-- The UNIQUE constraint on results.submission_id is the ultimate
-- guard against double-grading. Even if two workers somehow both
-- reached this point (they can't, thanks to Step 1's lock, but
-- defense in depth), only one INSERT would succeed.
--
-- ON CONFLICT DO NOTHING makes the second insert a no-op rather
-- than an error. The application checks the affected row count
-- and rolls back if zero rows were inserted.
-- ------------------------------------------------------------
INSERT INTO results
    (submission_id, verdict, score, runtime_ms, memory_kb,
     stdout, stderr, graded_by)
VALUES
    ($1, $2, $3, $4, $5, $6, $7, $8)
ON CONFLICT (submission_id) DO NOTHING;


-- ------------------------------------------------------------
-- Step 3: Mark the submission COMPLETED.
-- ------------------------------------------------------------
-- Clear processing_started_at and worker_id: they're no longer
-- meaningful once grading is done. The row is now terminal.
-- ------------------------------------------------------------
UPDATE submissions
SET status = 'COMPLETED',
    processing_started_at = NULL,
    worker_id = NULL
WHERE id = $1;


COMMIT;


-- ============================================================
-- FAILURE PATH — the application ROLLBACKs instead of COMMITting
-- when:
--   - The SELECT in Step 1 shows status != 'PROCESSING'
--   - The SELECT shows worker_id != this worker
--   - The INSERT in Step 2 affected 0 rows (conflict)
--   - Any database error occurs
--
-- After ROLLBACK, the submission remains PROCESSING with the
-- original worker_id. The watchdog detects it as stale after the
-- lease timeout and requeues it.
-- ============================================================


-- ------------------------------------------------------------
-- mark_submission_failed
-- ------------------------------------------------------------
-- Purpose:    Move a submission to FAILED after max retries.
-- Parameters: $1 submission_id, $2 verdict (always 'FAILED' for now),
--             $3 stderr (the last error message)
-- Index:      submissions_pkey
-- Notes:      Called by the watchdog when retry_count exceeds the
--             limit. Uses a transaction with FOR UPDATE to prevent
--             a race with a worker that's about to commit.
-- ------------------------------------------------------------
-- BEGIN;
-- SELECT id FROM submissions WHERE id = $1 FOR UPDATE;
-- INSERT INTO results (submission_id, verdict, score, stderr)
-- VALUES ($1, 'FAILED', 0, $3)
-- ON CONFLICT (submission_id) DO NOTHING;
-- UPDATE submissions SET status = 'FAILED' WHERE id = $1;
-- COMMIT;