-- ============================================================
-- CodeArena — Test case progress tracking
-- ============================================================
--
-- Adds columns to track how many test cases a submission
-- passed, and which one (if any) was the first failure.
--
-- Used to render "40 / 63 testcases passed" and "first failure
-- at case 41" in the UI. Not used for grading itself — the
-- verdict is still determined by the worker's aggregate.
-- ============================================================

BEGIN;

-- On submissions: track live progress for the "Running test N
-- of M" indicator shown while the worker is grading.
ALTER TABLE submissions
    ADD COLUMN IF NOT EXISTS test_total    INTEGER,
    ADD COLUMN IF NOT EXISTS test_passed   INTEGER,
    ADD COLUMN IF NOT EXISTS test_current  INTEGER;

COMMENT ON COLUMN submissions.test_total
    IS 'Total number of test cases for this submission''s problem';
COMMENT ON COLUMN submissions.test_passed
    IS 'Number of test cases that passed (verdict=AC)';
COMMENT ON COLUMN submissions.test_current
    IS 'Index of the test case currently being graded (1-based)';

-- On results: record the final pass count so the UI can show
-- it after grading completes without re-joining test_cases.
ALTER TABLE results
    ADD COLUMN IF NOT EXISTS test_total    INTEGER,
    ADD COLUMN IF NOT EXISTS test_passed   INTEGER,
    ADD COLUMN IF NOT EXISTS first_failed  INTEGER;

COMMENT ON COLUMN results.test_total
    IS 'Total test cases considered for this submission';
COMMENT ON COLUMN results.test_passed
    IS 'Number of test cases that passed';
COMMENT ON COLUMN results.first_failed
    IS 'Ordinal of the first failing test case (NULL if all passed)';

COMMIT;