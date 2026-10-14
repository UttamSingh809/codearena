-- ============================================================
-- CodeArena — Indexes (migration 002)
-- ============================================================
--
-- Every index exists to accelerate a specific query that the
-- application makes frequently. Adding an index without a query
-- is wasteful (write amplification with no read benefit).
--
-- Each index below is annotated with the query it optimizes.
-- Use EXPLAIN ANALYZE to verify that the planner actually
-- chooses each index for its target query.
-- ============================================================

BEGIN;

-- ------------------------------------------------------------
-- USERS
-- ------------------------------------------------------------

-- Login: SELECT * FROM users WHERE email = $1
-- (Implicitly satisfied by the UNIQUE constraint on email.)

-- Registration check: SELECT 1 FROM users WHERE username = $1
-- (Implicitly satisfied by the UNIQUE constraint on username.)

-- ------------------------------------------------------------
-- PROBLEMS
-- ------------------------------------------------------------

-- Active problem list: SELECT ... FROM problems WHERE is_active ORDER BY id
CREATE INDEX idx_problems_active
    ON problems (id)
    WHERE is_active = TRUE;

COMMENT ON INDEX idx_problems_active IS 'Partial index for active problem list';

-- ------------------------------------------------------------
-- TEST CASES
-- ------------------------------------------------------------

-- Fetching all test cases for a problem, ordered:
--   SELECT * FROM test_cases WHERE problem_id = $1 ORDER BY ordinal
-- (Implicitly satisfied by the UNIQUE(problem_id, ordinal) constraint.)

-- ------------------------------------------------------------
-- SUBMISSIONS
-- ------------------------------------------------------------

-- Worker job claim:
--   UPDATE submissions SET status='PROCESSING' ...
--   WHERE id = (SELECT id FROM submissions
--               WHERE status='PENDING'
--               ORDER BY submitted_at
--               FOR UPDATE SKIP LOCKED
--               LIMIT 1)
-- Partial index on PENDING keeps this fast even with millions of rows.
CREATE INDEX idx_submissions_pending_fifo
    ON submissions (submitted_at, id)
    WHERE status = 'PENDING';

COMMENT ON INDEX idx_submissions_pending_fifo
    IS 'Partial index for worker job claim (FIFO)';

-- Watchdog stale-job scan:
--   SELECT id, worker_id FROM submissions
--   WHERE status='PROCESSING'
--     AND processing_started_at < NOW() - INTERVAL '60 seconds'
-- Partial index on PROCESSING; secondary column on the timestamp.
CREATE INDEX idx_submissions_stale
    ON submissions (processing_started_at)
    WHERE status = 'PROCESSING';

COMMENT ON INDEX idx_submissions_stale
    IS 'Partial index for watchdog stale-job detection';

-- User history:
--   SELECT ... FROM submissions
--   WHERE user_id = $1
--   ORDER BY submitted_at DESC
--   LIMIT 20 OFFSET $2
CREATE INDEX idx_submissions_user_time
    ON submissions (user_id, submitted_at DESC);

COMMENT ON INDEX idx_submissions_user_time
    IS 'User submission history, newest first';

-- Problem submissions:
--   SELECT ... FROM submissions
--   WHERE problem_id = $1
--   ORDER BY submitted_at DESC
--   LIMIT 50
CREATE INDEX idx_submissions_problem_time
    ON submissions (problem_id, submitted_at DESC);

COMMENT ON INDEX idx_submissions_problem_time
    IS 'Per-problem submissions, newest first';

-- Global admin dashboard:
--   SELECT status, COUNT(*) FROM submissions GROUP BY status
-- A plain index on status helps the planner use an index-only scan
-- when it wants counts per status.
CREATE INDEX idx_submissions_status
    ON submissions (status);

COMMENT ON INDEX idx_submissions_status
    IS 'Status counts and status-filtered scans';

-- ------------------------------------------------------------
-- RESULTS
-- ------------------------------------------------------------

-- Per-submission lookup: SELECT * FROM results WHERE submission_id = $1
-- (Implicitly satisfied by the UNIQUE constraint on submission_id.)

-- Verdict counts: SELECT verdict, COUNT(*) FROM results GROUP BY verdict
CREATE INDEX idx_results_verdict
    ON results (verdict);

COMMENT ON INDEX idx_results_verdict
    IS 'Verdict distribution counts';

-- Recent graded: SELECT ... FROM results ORDER BY graded_at DESC LIMIT 100
CREATE INDEX idx_results_graded_at
    ON results (graded_at DESC);

COMMENT ON INDEX idx_results_graded_at
    IS 'Recent grading activity';

-- Score-based rankings per problem:
--   SELECT user_id, MAX(score) FROM results
--   JOIN submissions ON ...
--   WHERE problem_id = $1
--   GROUP BY user_id
--   ORDER BY MAX(score) DESC
-- This is expensive but the leaderboard materialized view
-- offloads it (see migration 003).
CREATE INDEX idx_results_score
    ON results (score DESC);

COMMENT ON INDEX idx_results_score
    IS 'Score ordering for ad-hoc ranking queries';

COMMIT;