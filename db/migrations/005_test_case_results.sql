-- ============================================================
-- CodeArena — Per-test-case results
-- ============================================================
--
-- Stores one row per (submission, test case) with the verdict
-- and the program's actual output. Used to render a per-case
-- breakdown on the submission page, including "which case
-- failed and what did the program output".
--
-- Not stored:
--   - input_data        (identical to test_cases.input_data)
--   - expected_output   (identical to test_cases.expected_output)
-- Those are joined at query time from the test_cases table.
-- ============================================================

BEGIN;

CREATE TABLE IF NOT EXISTS test_case_results (
    id                BIGSERIAL PRIMARY KEY,
    submission_id     BIGINT NOT NULL
                      REFERENCES submissions(id) ON DELETE CASCADE,
    test_case_id      BIGINT NOT NULL
                      REFERENCES test_cases(id) ON DELETE CASCADE,
    ordinal           INTEGER NOT NULL,
    verdict           VARCHAR(16) NOT NULL
                      CHECK (verdict IN
                        ('AC', 'WA', 'TLE', 'MLE', 'RE', 'CE',
                         'OLE', 'PE', 'FAILED')),
    actual_output     TEXT,
    runtime_ms        INTEGER,
    memory_kb         INTEGER,
    created_at        TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    UNIQUE (submission_id, test_case_id)
);

-- Primary access pattern: fetch all cases for one submission,
-- ordered by ordinal.
CREATE INDEX IF NOT EXISTS idx_tcr_submission_ordinal
    ON test_case_results (submission_id, ordinal);

-- Analytical: which test cases fail most often?
CREATE INDEX IF NOT EXISTS idx_tcr_verdict
    ON test_case_results (verdict);

COMMENT ON TABLE test_case_results
    IS 'Per-test-case verdicts and outputs for a submission';

COMMIT;