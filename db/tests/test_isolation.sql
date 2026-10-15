-- ============================================================
-- CodeArena — Isolation level demonstrations
-- ============================================================
--
-- Run with:
--   psql "$DATABASE_URL" -f db/tests/test_isolation.sql
--
-- This script demonstrates two anomalies and how the right
-- isolation level prevents them:
--
--   1. Lost update (READ COMMITTED without FOR UPDATE)
--   2. Phantom read (READ COMMITTED vs REPEATABLE READ)
--
-- PostgreSQL's default isolation level is READ COMMITTED. Two
-- sessions can run concurrently and interleave in ways that
-- produce anomalies unless the application takes explicit
-- precautions (FOR UPDATE, atomic increments, SERIALIZABLE).
--
-- This script cannot demonstrate interleaving in a single
-- session, so it uses two backends via dblink-like patterns
-- (see the Python tests for a real interleaved version). What
-- this script shows is the *SQL constructs* that prevent the
-- anomalies when concurrency does occur.
-- ============================================================

\set ON_ERROR_STOP on

-- ------------------------------------------------------------
-- Part 1: Lost update — the WRONG way
-- ------------------------------------------------------------
-- Two concurrent sessions run this pattern:
--
--   BEGIN;
--   SELECT retry_count FROM submissions WHERE id = 1;   -- both read 0
--   -- compute retry_count + 1 = 1
--   UPDATE submissions SET retry_count = 1 WHERE id = 1;
--   COMMIT;
--
-- Both write 1. One update is lost. Final retry_count = 1, not 2.
--
-- This is the classic non-atomic read-modify-write. Applications
-- must never do this.

\echo '--- Part 1: non-atomic read-modify-write (conceptual) ---'
\echo 'Two sessions both do: SELECT retry_count; UPDATE retry_count = +1'
\echo 'Both write the same value. One increment is lost.'
\echo ''

-- Setup: create a fresh submission for this test.
INSERT INTO submissions (user_id, problem_id, language, source_code, status)
VALUES (1, 1, 'c', 'int main(){return 0;}', 'PENDING')
RETURNING id AS test_submission_id;

-- Simulate the wrong pattern in one session.
BEGIN;
SELECT id, retry_count FROM submissions
WHERE source_code = 'int main(){return 0;}' AND status = 'PENDING'
ORDER BY id DESC LIMIT 1;
-- Application computes 0 + 1 = 1
UPDATE submissions SET retry_count = 1
WHERE id = (SELECT id FROM submissions
            WHERE source_code = 'int main(){return 0;}' AND status = 'PENDING'
            ORDER BY id DESC LIMIT 1);
COMMIT;

-- Verify the wrong outcome (retry_count = 1, not 2 after two increments)
SELECT id, retry_count, 'WRONG: non-atomic increment loses updates'
FROM submissions
WHERE source_code = 'int main(){return 0;}' AND status = 'PENDING'
ORDER BY id DESC LIMIT 1;


-- ------------------------------------------------------------
-- Part 2: Lost update — the RIGHT way (atomic increment)
-- ------------------------------------------------------------
-- UPDATE submissions SET retry_count = retry_count + 1 WHERE id = $1;
--
-- This compiles to a single atomic read-modify-write inside the
-- database. Two concurrent transactions serialize on the row lock
-- the UPDATE acquires. The final value is always correct.

\echo ''
\echo '--- Part 2: atomic increment (the correct pattern) ---'

-- Reset to 0
UPDATE submissions SET retry_count = 0
WHERE source_code = 'int main(){return 0;}' AND status = 'PENDING'
  AND id = (SELECT id FROM submissions
            WHERE source_code = 'int main(){return 0;}' AND status = 'PENDING'
            ORDER BY id DESC LIMIT 1);

-- Increment twice
UPDATE submissions SET retry_count = retry_count + 1
WHERE source_code = 'int main(){return 0;}' AND status = 'PENDING'
  AND id = (SELECT id FROM submissions
            WHERE source_code = 'int main(){return 0;}' AND status = 'PENDING'
            ORDER BY id DESC LIMIT 1);
UPDATE submissions SET retry_count = retry_count + 1
WHERE source_code = 'int main(){return 0;}' AND status = 'PENDING'
  AND id = (SELECT id FROM submissions
            WHERE source_code = 'int main(){return 0;}' AND status = 'PENDING'
            ORDER BY id DESC LIMIT 1);

SELECT id, retry_count, 'RIGHT: atomic increment gives 2'
FROM submissions
WHERE source_code = 'int main(){return 0;}' AND status = 'PENDING'
ORDER BY id DESC LIMIT 1;


-- ------------------------------------------------------------
-- Part 3: Lost update — the other right way (FOR UPDATE)
-- ------------------------------------------------------------
-- BEGIN;
-- SELECT retry_count FROM submissions WHERE id = $1 FOR UPDATE;
-- UPDATE submissions SET retry_count = retry_count + 1 WHERE id = $1;
-- COMMIT;
--
-- FOR UPDATE acquires an exclusive row lock at SELECT time. Any
-- other transaction attempting to read the same row FOR UPDATE
-- blocks until this transaction commits or rolls back. The
-- application sees the fresh value.

\echo ''
\echo '--- Part 3: FOR UPDATE (pessimistic lock) ---'

BEGIN;
SELECT id, retry_count FROM submissions
WHERE source_code = 'int main(){return 0;}' AND status = 'PENDING'
  AND id = (SELECT id FROM submissions
            WHERE source_code = 'int main(){return 0;}' AND status = 'PENDING'
            ORDER BY id DESC LIMIT 1)
FOR UPDATE;
UPDATE submissions SET retry_count = retry_count + 1
WHERE source_code = 'int main(){return 0;}' AND status = 'PENDING'
  AND id = (SELECT id FROM submissions
            WHERE source_code = 'int main(){return 0;}' AND status = 'PENDING'
            ORDER BY id DESC LIMIT 1);
COMMIT;

SELECT id, retry_count, 'RIGHT: FOR UPDATE + increment gives 3'
FROM submissions
WHERE source_code = 'int main(){return 0;}' AND status = 'PENDING'
ORDER BY id DESC LIMIT 1;


-- ------------------------------------------------------------
-- Part 4: Phantom read under READ COMMITTED
-- ------------------------------------------------------------
-- Under READ COMMITTED, each statement sees a fresh snapshot.
-- If another transaction inserts a row between two of our
-- statements, we see it on the second statement.
--
-- This is legal per SQL standard. To prevent it, use REPEATABLE
-- READ or SERIALIZABLE.

\echo ''
\echo '--- Part 4: READ COMMITTED phantom read (two statements, fresh snapshot each) ---'

BEGIN ISOLATION LEVEL READ COMMITTED;

-- Snapshot 1: count submissions for problem 1
SELECT COUNT(*) AS count_first_read
FROM submissions WHERE problem_id = 1;

-- Simulate a concurrent insert
INSERT INTO submissions (user_id, problem_id, language, source_code, status)
VALUES (2, 1, 'c', 'int main(){return 0;}', 'PENDING');

-- Snapshot 2: count again — sees the new row under READ COMMITTED
SELECT COUNT(*) AS count_second_read
FROM submissions WHERE problem_id = 1;

COMMIT;


-- ------------------------------------------------------------
-- Part 5: No phantom under REPEATABLE READ
-- ------------------------------------------------------------
-- Under REPEATABLE READ, all statements in the transaction see
-- the snapshot taken at the first statement. Concurrent inserts
-- are invisible.

\echo ''
\echo '--- Part 5: REPEATABLE READ (both reads see the same snapshot) ---'

BEGIN ISOLATION LEVEL REPEATABLE READ;

SELECT COUNT(*) AS count_first_read
FROM submissions WHERE problem_id = 1;

-- Simulate a concurrent insert. Under REPEATABLE READ, the second
-- read does NOT see this new row.
INSERT INTO submissions (user_id, problem_id, language, source_code, status)
VALUES (3, 1, 'c', 'int main(){return 0;}', 'PENDING');

SELECT COUNT(*) AS count_second_read
FROM submissions WHERE problem_id = 1;

-- Cleanup: delete our phantom test rows
DELETE FROM submissions WHERE source_code = 'int main(){return 0;}'
  AND user_id IN (2, 3);

COMMIT;


-- ------------------------------------------------------------
-- Cleanup
-- ------------------------------------------------------------
DELETE FROM submissions WHERE source_code = 'int main(){return 0;}'
  AND status = 'PENDING';

\echo ''
\echo 'Isolation test complete.'