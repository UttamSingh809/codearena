-- ============================================================
-- CodeArena — Analytical queries
-- ============================================================
--
-- These queries answer "how is the system doing?" questions.
-- They run against the base tables (not the matview) because
-- they need fresh data, and they run infrequently so their cost
-- is acceptable.
-- ============================================================


-- ------------------------------------------------------------
-- verdict_distribution
-- ------------------------------------------------------------
-- Purpose:    Count results by verdict.
-- Parameters: none
-- Returns:    verdict, count
-- Index:      idx_results_verdict
-- Notes:      A bitmap index scan over idx_results_verdict
--             gives O(distinct verdicts) aggregation.
-- ------------------------------------------------------------
SELECT verdict, COUNT(*) AS count
FROM results
GROUP BY verdict
ORDER BY count DESC;


-- ------------------------------------------------------------
-- submissions_per_day
-- ------------------------------------------------------------
-- Purpose:    Daily submission volume for the last 30 days.
-- Parameters: $1 days (default 30)
-- Returns:    day, count
-- Index:      idx_submissions_status (partial) — but the planner
--             may prefer a seq scan for the full table.
-- Notes:      date_trunc('day', submitted_at) groups by calendar day.
--             The result is ordered ascending so charts draw left-to-right.
-- ------------------------------------------------------------
SELECT
    date_trunc('day', submitted_at) AS day,
    COUNT(*) AS count
FROM submissions
WHERE submitted_at >= NOW() - ($1 || ' days')::INTERVAL
GROUP BY day
ORDER BY day ASC;


-- ------------------------------------------------------------
-- avg_runtime_per_problem
-- ------------------------------------------------------------
-- Purpose:    Average and median runtime for accepted solutions
--             per problem.
-- Parameters: none
-- Returns:    problem_id, title, accepted_count, avg_runtime_ms,
--             median_runtime_ms
-- Index:      idx_submissions_problem_time, idx_results_verdict
-- Notes:      PERCENTILE_CONT(0.5) computes the median. It's an
--             ordered-set aggregate and is more expensive than AVG,
--             but only runs for accepted results (verdict='AC').
-- ------------------------------------------------------------
SELECT
    p.id AS problem_id,
    p.title,
    COUNT(*) FILTER (WHERE r.verdict = 'AC')                       AS accepted_count,
    ROUND(AVG(r.runtime_ms) FILTER (WHERE r.verdict = 'AC'))       AS avg_runtime_ms,
    PERCENTILE_CONT(0.5) WITHIN GROUP (ORDER BY r.runtime_ms)
        FILTER (WHERE r.verdict = 'AC')                            AS median_runtime_ms
FROM problems p
LEFT JOIN submissions s ON s.problem_id = p.id
LEFT JOIN results r ON r.submission_id = s.id
GROUP BY p.id, p.title
ORDER BY p.id;


-- ------------------------------------------------------------
-- hardest_problems
-- ------------------------------------------------------------
-- Purpose:    Rank problems by their rejection rate (non-AC / total).
-- Parameters: $1 minimum submissions (default 10)
-- Returns:    problem_id, title, total_submissions, accepted,
--             rejection_rate
-- Index:      idx_submissions_problem_time, idx_results_verdict
-- Notes:      Only problems with at least $1 submissions are shown,
--             to filter out noise from barely-attempted problems.
-- ------------------------------------------------------------
SELECT
    p.id AS problem_id,
    p.title,
    COUNT(s.id)                                              AS total_submissions,
    COUNT(*) FILTER (WHERE r.verdict = 'AC')                 AS accepted,
    ROUND(
        1.0 - (COUNT(*) FILTER (WHERE r.verdict = 'AC')::NUMERIC
               / NULLIF(COUNT(s.id), 0)),
        3
    )                                                        AS rejection_rate
FROM problems p
LEFT JOIN submissions s ON s.problem_id = p.id
LEFT JOIN results r ON r.submission_id = s.id
GROUP BY p.id, p.title
HAVING COUNT(s.id) >= $1
ORDER BY rejection_rate DESC;


-- ------------------------------------------------------------
-- active_users
-- ------------------------------------------------------------
-- Purpose:    Users with the most submissions in the last 7 days.
-- Parameters: $1 days (default 7), $2 limit (default 20)
-- Returns:    user_id, username, submission_count, accepted_count,
--             accept_rate
-- Index:      idx_submissions_user_time
-- Notes:      The 7-day window filters to recent activity.
-- ------------------------------------------------------------
SELECT
    u.id AS user_id,
    u.username,
    COUNT(s.id)                                              AS submission_count,
    COUNT(*) FILTER (WHERE r.verdict = 'AC')                 AS accepted_count,
    ROUND(
        COUNT(*) FILTER (WHERE r.verdict = 'AC')::NUMERIC
        / NULLIF(COUNT(s.id), 0),
        3
    )                                                        AS accept_rate
FROM users u
JOIN submissions s ON s.user_id = u.id
LEFT JOIN results r ON r.submission_id = s.id
WHERE s.submitted_at >= NOW() - ($1 || ' days')::INTERVAL
GROUP BY u.id, u.username
ORDER BY submission_count DESC
LIMIT $2;


-- ------------------------------------------------------------
-- worker_throughput
-- ------------------------------------------------------------
-- Purpose:    Submissions graded per worker, ordered by volume.
-- Parameters: $1 hours (default 24)
-- Returns:    graded_by, graded_count, avg_runtime_ms
-- Index:      idx_results_graded_at (graded_at DESC)
-- Notes:      Uses graded_by which stores the worker_id. Useful for
--             detecting worker imbalance or death.
-- ------------------------------------------------------------
SELECT
    COALESCE(r.graded_by, '(unknown)')   AS worker_id,
    COUNT(*)                             AS graded_count,
    ROUND(AVG(r.runtime_ms))             AS avg_runtime_ms
FROM results r
WHERE r.graded_at >= NOW() - ($1 || ' hours')::INTERVAL
GROUP BY r.graded_by
ORDER BY graded_count DESC;