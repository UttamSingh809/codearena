-- ============================================================
-- CodeArena — Deadlock demonstration
-- ============================================================
--
-- Run with:
--   psql "$DATABASE_URL" -f db/tests/test_deadlock.sql
--
-- This script demonstrates:
--   1. How deadlock is detected by PostgreSQL
--   2. How the application should recover (retry)
--   3. How to avoid deadlock by consistent lock ordering
--
-- A single psql session cannot deadlock with itself, so this
-- script illustrates the pattern with comments and shows the
-- correct query ordering. The Python tests demonstrate actual
-- deadlock with two threads.
--
-- KEY FACTS:
--   - PostgreSQL detects deadlocks after deadlock_timeout
--     (default 1 second).
--   - The transaction that has done LESS work is aborted.
--   - The abort produces SQLSTATE 40P01 ('deadlock_detected').
--   - The application MUST retry the aborted transaction.
-- ============================================================

\set ON_ERROR_STOP off

-- ------------------------------------------------------------
-- Setup: two submissions to lock in opposite orders.
-- ------------------------------------------------------------
INSERT INTO submissions (user_id, problem_id, language, source_code, status)
VALUES (1, 1, 'c', 'int main(){return 0;}', 'PENDING')
RETURNING id AS submission_a;

INSERT INTO submissions (user_id, problem_id, language, source_code, status)
VALUES (1, 1, 'c', 'int main(){return 0;}', 'PENDING')
RETURNING id AS submission_b;


-- ------------------------------------------------------------
-- The deadlock scenario (two backends required)
-- ------------------------------------------------------------
-- Backend 1:                       Backend 2:
--   BEGIN;                           BEGIN;
--   SELECT ... FOR UPDATE            SELECT ... FOR UPDATE
--     WHERE id = A;                    WHERE id = B;
--   -- holds lock on A                -- holds lock on B
--                                   
--   SELECT ... FOR UPDATE            SELECT ... FOR UPDATE
--     WHERE id = B;                    WHERE id = A;
--   -- waits for B                    -- waits for A
--
-- Both transactions wait for a lock the other holds. Neither
-- can proceed. This is a deadlock.
--
-- After deadlock_timeout (1s), PostgreSQL detects the cycle
-- and aborts one transaction with:
--   ERROR:  deadlock detected
--   DETAIL: Process X waits for ShareLock on transaction Y; blocked by process Z.
--           Process Z waits for ShareLock on transaction X; blocked by process Y.
--   SQLSTATE: 40P01
--
-- The other transaction succeeds.
--
-- The application retries the aborted one.
\echo ''
\echo '--- Deadlock scenario (requires two backends) ---'
\echo 'Backend 1: BEGIN; SELECT FOR UPDATE id=A; SELECT FOR UPDATE id=B;'
\echo 'Backend 2: BEGIN; SELECT FOR UPDATE id=B; SELECT FOR UPDATE id=A;'
\echo 'One backend is aborted with SQLSTATE 40P01.'
\echo ''
\echo 'This script cannot interleave two backends, so we demonstrate'
\echo 'the correct alternative: consistent lock ordering.'
\echo ''


-- ------------------------------------------------------------
-- The FIX: consistent lock ordering
-- ------------------------------------------------------------
-- Always acquire locks in ascending id order. Then two
-- transactions can never wait for each other in a cycle.
--
-- Backend 1:                       Backend 2:
--   BEGIN;                           BEGIN;
--   SELECT ... FOR UPDATE            SELECT ... FOR UPDATE
--     WHERE id IN (A, B)               WHERE id IN (A, B)
--     ORDER BY id;                     ORDER BY id;
--
-- Both acquire A's lock first. One waits for the other, then
-- proceeds. No deadlock.

\echo '--- Consistent lock ordering: both lock A first, then B ---'

BEGIN;

-- Both rows locked in a single statement with ORDER BY id.
-- PostgreSQL locks them in that order.
SELECT id, status
FROM submissions
WHERE source_code = 'int main(){return 0;}'
ORDER BY id
FOR UPDATE;

-- Simulate processing
UPDATE submissions
SET retry_count = retry_count + 1
WHERE source_code = 'int main(){return 0;}';

COMMIT;

SELECT id, retry_count, 'Consistent lock ordering succeeded'
FROM submissions
WHERE source_code = 'int main(){return 0;}'
ORDER BY id;


-- ------------------------------------------------------------
-- Retry pattern (application-level)
-- ------------------------------------------------------------
-- When a transaction fails with SQLSTATE 40P01 ('deadlock_detected'),
-- the application should:
--
--   1. Roll back the transaction.
--   2. Wait a short randomized delay (to break the symmetry).
--   3. Retry the transaction, up to N times.
--   4. If all retries fail, mark the operation FAILED.
--
-- Example pseudo-code (Python):
--
--   for attempt in range(MAX_RETRIES):
--       try:
--           with conn.transaction():
--               do_grading_work(conn)
--           return  # success
--       except psycopg2.errors.DeadlockDetected:
--           time.sleep(random.uniform(0.01, 0.1) * (2 ** attempt))
--   raise Failed('deadlock retries exhausted')
--
-- See db/tests/test_concurrent_grading.py for a real implementation.


-- ------------------------------------------------------------
-- Cleanup
-- ------------------------------------------------------------
DELETE FROM submissions WHERE source_code = 'int main(){return 0;}';

\echo ''
\echo 'Deadlock test complete.'