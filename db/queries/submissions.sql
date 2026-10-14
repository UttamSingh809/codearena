-- ============================================================
-- CodeArena — Submission lifecycle queries
-- ============================================================
--
-- A submission moves through: PENDING → PROCESSING → COMPLETED.
-- On failure: PROCESSING → PENDING (retry) or FAILED (give up).
--
-- The claim query uses FOR UPDATE SKIP LOCKED for safe concurrent
-- worker dequeue without blocking.
-- ============================================================


-- ------------------------------------------------------------
-- create_submission
-- ------------------------------------------------------------
-- Purpose:    Insert a new submission in PENDING state.
-- Parameters: $1 user_id, $2 problem_id, $3 language, $4 source_code
-- Returns:    The new submission id, submitted_at, status.
-- Index:      Foreign keys are checked; no user-defined index is used.
-- Notes:      The API calls this BEFORE enqueueing the job. If the
--             enqueue fails, the submission stays PENDING and the
--             watchdog will requeue it. If the API crashes after
--             insert but before enqueue, the watchdog detects the
--             staleness after the lease timeout.
-- ------------------------------------------------------------
INSERT INTO submissions (user_id, problem_id, language, source_code)
VALUES ($1, $2, $3, $4)
RETURNING id, status, submitted_at;


-- ------------------------------------------------------------
-- claim_job
-- ------------------------------------------------------------
-- Purpose:    Atomically claim the oldest PENDING submission.
-- Parameters: $1 worker_id
-- Returns:    id, user_id, problem_id, language, source_code,
--             retry_count
-- Index:      idx_submissions_pending_fifo (submitted_at, id)
--             WHERE status = 'PENDING'
-- Notes:      Uses FOR UPDATE SKIP LOCKED. If two workers call this
--             at the same time, they get different rows. The WHERE
--             clause combines the partial-index predicate with the
--             status check so the planner can use the index-only
--             scan path.
--
--             Called inside a transaction. If the transaction
--             commits, the row is marked PROCESSING with the
--             worker's ID and a timestamp. If the worker crashes
--             before commit, the row reverts to PENDING (the
--             UPDATE never happened).
-- ------------------------------------------------------------
UPDATE submissions
SET status = 'PROCESSING',
    processing_started_at = NOW(),
    worker_id = $1
WHERE id = (
    SELECT id FROM submissions
    WHERE status = 'PENDING'
    ORDER BY submitted_at, id
    LIMIT 1
    FOR UPDATE SKIP LOCKED
)
RETURNING id, user_id, problem_id, language, source_code, retry_count;


-- ------------------------------------------------------------
-- get_submission
-- ------------------------------------------------------------
-- Purpose:    Fetch a submission with its result (if graded).
-- Parameters: $1 submission_id
-- Returns:    All submission fields + result fields (NULLs if not graded).
-- Index:      submissions_pkey; results_submission_id_key
-- Notes:      LEFT JOIN because a PENDING submission has no result yet.
-- ------------------------------------------------------------
SELECT
    s.id,
    s.user_id,
    s.problem_id,
    s.language,
    s.source_code,
    s.status,
    s.submitted_at,
    s.processing_started_at,
    s.worker_id,
    s.retry_count,
    r.verdict,
    r.score,
    r.runtime_ms,
    r.memory_kb,
    r.stderr,
    r.graded_at
FROM submissions s
LEFT JOIN results r ON r.submission_id = s.id
WHERE s.id = $1;


-- ------------------------------------------------------------
-- list_user_submissions
-- ------------------------------------------------------------
-- Purpose:    Paginated submission history for one user.
-- Parameters: $1 user_id, $2 limit, $3 offset
-- Returns:    Submission summaries + verdict.
-- Index:      idx_submissions_user_time (user_id, submitted_at DESC)
-- Notes:      The index is DESC on submitted_at, so the ORDER BY
--             matches without a sort.
-- ------------------------------------------------------------
SELECT
    s.id,
    s.problem_id,
    p.title AS problem_title,
    s.language,
    s.status,
    s.submitted_at,
    r.verdict,
    r.score,
    r.runtime_ms,
    r.memory_kb
FROM submissions s
JOIN problems p ON p.id = s.problem_id
LEFT JOIN results r ON r.submission_id = s.id
WHERE s.user_id = $1
ORDER BY s.submitted_at DESC, s.id DESC
LIMIT $2 OFFSET $3;


-- ------------------------------------------------------------
-- count_user_submissions
-- ------------------------------------------------------------
-- Purpose:    Total submission count for pagination metadata.
-- Parameters: $1 user_id
-- Index:      idx_submissions_user_time
-- ------------------------------------------------------------
SELECT COUNT(*) AS total
FROM submissions
WHERE user_id = $1;


-- ------------------------------------------------------------
-- list_problem_submissions
-- ------------------------------------------------------------
-- Purpose:    Paginated submissions for one problem (public view).
-- Parameters: $1 problem_id, $2 limit, $3 offset
-- Returns:    Submission summaries with username.
-- Index:      idx_submissions_problem_time (problem_id, submitted_at DESC)
-- Notes:      Only shows verdict and runtime — not source code — since
--             this view is visible to all users.
-- ------------------------------------------------------------
SELECT
    s.id,
    u.username,
    s.language,
    s.status,
    s.submitted_at,
    r.verdict,
    r.score,
    r.runtime_ms,
    r.memory_kb
FROM submissions s
JOIN users u ON u.id = s.user_id
LEFT JOIN results r ON r.submission_id = s.id
WHERE s.problem_id = $1
ORDER BY s.submitted_at DESC, s.id DESC
LIMIT $2 OFFSET $3;


-- ------------------------------------------------------------
-- cancel_submission
-- ------------------------------------------------------------
-- Purpose:    User cancels a PENDING submission.
-- Parameters: $1 submission_id, $2 user_id
-- Returns:    Number of rows updated (0 or 1).
-- Index:      submissions_pkey
-- Notes:      The user_id check prevents one user from cancelling
--             another's submission. Only PENDING rows can be
--             cancelled; PROCESSING rows have already been claimed.
-- ------------------------------------------------------------
UPDATE submissions
SET status = 'CANCELLED'
WHERE id = $1 AND user_id = $2 AND status = 'PENDING';