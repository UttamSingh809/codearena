-- ============================================================
-- CodeArena — Leaderboard queries
-- ============================================================
--
-- All ranking queries read from the `leaderboard` materialized
-- view, not from the base tables. The view is refreshed after
-- each grading batch (see refresh_leaderboard.sql).
--
-- Staleness is bounded by the refresh interval (typically < 1s).
-- ============================================================


-- ------------------------------------------------------------
-- get_problem_leaderboard
-- ------------------------------------------------------------
-- Purpose:    Top N users for one problem, ranked by best score.
-- Parameters: $1 problem_id, $2 limit, $3 offset
-- Returns:    rank, username, best_score, best_runtime_ms,
--             best_memory_kb, accepted_count, last_submission_at
-- Index:      idx_leaderboard_ranking (problem_id, best_score DESC,
--             best_runtime_ms ASC) WHERE best_score > 0
-- Notes:      RANK() is computed over the filtered set, so ranks
--             are dense per-problem. Users with no AC submissions
--             are excluded by the WHERE clause.
-- ------------------------------------------------------------
SELECT
    RANK() OVER (ORDER BY best_score DESC, best_runtime_ms ASC) AS rank,
    username,
    best_score,
    best_runtime_ms,
    best_memory_kb,
    accepted_count,
    last_submission_at
FROM leaderboard
WHERE problem_id = $1 AND best_score > 0
ORDER BY best_score DESC, best_runtime_ms ASC
LIMIT $2 OFFSET $3;


-- ------------------------------------------------------------
-- get_user_problem_stats
-- ------------------------------------------------------------
-- Purpose:    One user's stats for one problem.
-- Parameters: $1 user_id, $2 problem_id
-- Returns:    best_score, best_runtime_ms, best_memory_kb,
--             accepted_count, total_submissions, last_submission_at
-- Index:      idx_leaderboard_unique (user_id, problem_id)
-- ------------------------------------------------------------
SELECT
    best_score,
    best_runtime_ms,
    best_memory_kb,
    accepted_count,
    total_submissions,
    last_submission_at
FROM leaderboard
WHERE user_id = $1 AND problem_id = $2;


-- ------------------------------------------------------------
-- get_user_all_stats
-- ------------------------------------------------------------
-- Purpose:    Aggregate stats across all problems for one user.
-- Parameters: $1 user_id
-- Returns:    total_solved, total_submissions, total_accepted,
--             total_score, last_submission_at
-- Index:      idx_leaderboard_user (user_id)
-- Notes:      Only counts problems where the user has at least
--             one AC submission for total_solved.
-- ------------------------------------------------------------
SELECT
    COUNT(*) FILTER (WHERE accepted_count > 0) AS total_solved,
    SUM(total_submissions)                     AS total_submissions,
    SUM(accepted_count)                        AS total_accepted,
    SUM(best_score)                            AS total_score,
    MAX(last_submission_at)                    AS last_submission_at
FROM leaderboard
WHERE user_id = $1;


-- ------------------------------------------------------------
-- get_global_leaderboard
-- ------------------------------------------------------------
-- Purpose:    Overall ranking across all problems.
-- Parameters: $1 limit, $2 offset
-- Returns:    rank, username, total_score, total_solved,
--             total_submissions
-- Index:      idx_leaderboard_user
-- Notes:      This query groups by user, summing across problems.
--             It's more expensive than the per-problem query and
--             runs less frequently. The planner may choose a seq
--             scan on the matview for this one — that's fine, the
--             view is small.
-- ------------------------------------------------------------
SELECT
    RANK() OVER (ORDER BY SUM(best_score) DESC,
                          COUNT(*) FILTER (WHERE accepted_count > 0) DESC) AS rank,
    username,
    SUM(best_score)                            AS total_score,
    COUNT(*) FILTER (WHERE accepted_count > 0) AS total_solved,
    SUM(total_submissions)                     AS total_submissions
FROM leaderboard
GROUP BY user_id, username
ORDER BY total_score DESC, total_solved DESC
LIMIT $1 OFFSET $2;