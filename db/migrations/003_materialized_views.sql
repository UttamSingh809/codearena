-- ============================================================
-- CodeArena — Materialized views (migration 003)
-- ============================================================
--
-- A materialized view stores the result of a query as a table.
-- Reads are fast; writes are deferred to REFRESH. We use one for
-- the leaderboard because the underlying query requires grouping
-- and window functions over potentially millions of rows.
--
-- REFRESH strategies:
--   REFRESH MATERIALIZED VIEW name
--     — takes ACCESS EXCLUSIVE lock; blocks all reads.
--   REFRESH MATERIALIZED VIEW CONCURRENTLY name
--     — takes SHARE UPDATE EXCLUSIVE; reads continue.
--     — requires a UNIQUE index on the view.
--
-- We use CONCURRENTLY. The application refreshes after each
-- submission is graded (batched, at most once per second).
-- ============================================================

BEGIN;

DROP MATERIALIZED VIEW IF EXISTS leaderboard CASCADE;

-- ------------------------------------------------------------
-- LEADERBOARD
-- ------------------------------------------------------------
-- One row per (user_id, problem_id). Columns:
--   best_score         — highest score achieved
--   best_runtime_ms    — fastest AC runtime
--   best_memory_kb     — lowest AC memory
--   accepted_count     — number of AC submissions
--   total_submissions  — all submissions regardless of verdict
--   last_submission_at — most recent submission timestamp
--
-- The view is per-problem. A global leaderboard would aggregate
-- across problems; we generate it with a second view below.
-- ------------------------------------------------------------
CREATE MATERIALIZED VIEW leaderboard AS
SELECT
    s.user_id,
    u.username,
    s.problem_id,
    p.title AS problem_title,
    COALESCE(MAX(CASE WHEN r.verdict = 'AC' THEN r.score END), 0)   AS best_score,
    MIN(CASE WHEN r.verdict = 'AC' THEN r.runtime_ms END)          AS best_runtime_ms,
    MIN(CASE WHEN r.verdict = 'AC' THEN r.memory_kb END)           AS best_memory_kb,
    COUNT(*) FILTER (WHERE r.verdict = 'AC')                       AS accepted_count,
    COUNT(*)                                                       AS total_submissions,
    MAX(s.submitted_at)                                            AS last_submission_at
FROM submissions s
JOIN users    u ON u.id = s.user_id
JOIN problems p ON p.id = s.problem_id
LEFT JOIN results r ON r.submission_id = s.id
GROUP BY s.user_id, u.username, s.problem_id, p.title
WITH DATA;

COMMENT ON MATERIALIZED VIEW leaderboard
    IS 'Per-user-per-problem best scores; refreshed periodically';

-- Required for REFRESH MATERIALIZED VIEW CONCURRENTLY.
-- The unique index must be on columns that uniquely identify rows.
CREATE UNIQUE INDEX idx_leaderboard_unique
    ON leaderboard (user_id, problem_id);

-- Ranking queries: ORDER BY best_score DESC, best_runtime_ms ASC
CREATE INDEX idx_leaderboard_ranking
    ON leaderboard (problem_id, best_score DESC, best_runtime_ms ASC)
    WHERE best_score > 0;

COMMENT ON INDEX idx_leaderboard_ranking
    IS 'Per-problem ranking order';

-- User view: "show me my stats for every problem I've touched"
CREATE INDEX idx_leaderboard_user
    ON leaderboard (user_id);

COMMENT ON INDEX idx_leaderboard_user
    IS 'Look up a single user''s stats';

COMMIT;