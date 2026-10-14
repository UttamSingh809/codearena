-- ============================================================
-- CodeArena — Initial schema (migration 001)
-- ============================================================
--
-- Creates the core tables: users, problems, test_cases,
-- submissions, results. Every table is in 3NF.
--
-- Naming conventions:
--   - Tables are plural (users, submissions)
--   - Primary key is always `id BIGSERIAL`
--   - Foreign keys are `<table_singular>_id`
--   - Timestamps are TIMESTAMPTZ with default NOW()
--   - Enum-like columns are VARCHAR + CHECK (not native ENUM)
--
-- This migration is idempotent within a transaction: it DROPs
-- existing objects before creating them. Run only on a fresh
-- database or accept that you're wiping data.
-- ============================================================

BEGIN;

-- ------------------------------------------------------------
-- Drop in dependency order (children first).
-- ------------------------------------------------------------
DROP MATERIALIZED VIEW IF EXISTS leaderboard CASCADE;
DROP TABLE IF EXISTS results CASCADE;
DROP TABLE IF EXISTS submissions CASCADE;
DROP TABLE IF EXISTS test_cases CASCADE;
DROP TABLE IF EXISTS problems CASCADE;
DROP TABLE IF EXISTS users CASCADE;

-- ------------------------------------------------------------
-- USERS
-- ------------------------------------------------------------
-- Stores registered users. `password_hash` holds a bcrypt digest.
-- `role` gates API access: students submit; admins manage problems.
-- ------------------------------------------------------------
CREATE TABLE users (
    id              BIGSERIAL PRIMARY KEY,
    username        VARCHAR(64)  NOT NULL UNIQUE,
    email           VARCHAR(255) NOT NULL UNIQUE,
    password_hash   VARCHAR(255) NOT NULL,
    role            VARCHAR(16)  NOT NULL DEFAULT 'student'
                    CHECK (role IN ('student', 'instructor', 'admin')),
    created_at      TIMESTAMPTZ  NOT NULL DEFAULT NOW(),
    last_login_at   TIMESTAMPTZ
);

COMMENT ON TABLE  users            IS 'Registered users of the platform';
COMMENT ON COLUMN users.password_hash IS 'bcrypt digest; never plaintext';

-- ------------------------------------------------------------
-- PROBLEMS
-- ------------------------------------------------------------
-- Each problem has a time limit, memory limit, and difficulty.
-- Limits are per-problem defaults; the sandbox can override.
-- ------------------------------------------------------------
CREATE TABLE problems (
    id              BIGSERIAL PRIMARY KEY,
    title           VARCHAR(255) NOT NULL,
    description     TEXT,
    input_format    TEXT,
    output_format   TEXT,
    constraints     TEXT,
    time_limit_ms   INTEGER      NOT NULL DEFAULT 2000
                    CHECK (time_limit_ms > 0 AND time_limit_ms <= 60000),
    memory_limit_mb INTEGER      NOT NULL DEFAULT 256
                    CHECK (memory_limit_mb > 0 AND memory_limit_mb <= 4096),
    difficulty      VARCHAR(16)  NOT NULL DEFAULT 'medium'
                    CHECK (difficulty IN ('easy', 'medium', 'hard')),
    is_active       BOOLEAN      NOT NULL DEFAULT TRUE,
    created_by      BIGINT REFERENCES users(id) ON DELETE SET NULL,
    created_at      TIMESTAMPTZ  NOT NULL DEFAULT NOW(),
    updated_at      TIMESTAMPTZ  NOT NULL DEFAULT NOW()
);

COMMENT ON TABLE  problems             IS 'Programming problems';
COMMENT ON COLUMN problems.time_limit_ms   IS 'Wall-clock limit; sandbox enforces';
COMMENT ON COLUMN problems.memory_limit_mb IS 'RSS limit; cgroup memory.max enforces';

-- ------------------------------------------------------------
-- TEST CASES
-- ------------------------------------------------------------
-- Each problem has one or more test cases. `ordinal` gives a
-- stable order for reporting (sample #1, sample #2, hidden #1...).
-- `is_sample` marks cases whose I/O is shown to the user.
-- ------------------------------------------------------------
CREATE TABLE test_cases (
    id              BIGSERIAL PRIMARY KEY,
    problem_id      BIGINT NOT NULL REFERENCES problems(id) ON DELETE CASCADE,
    ordinal         INTEGER NOT NULL,
    input_data      TEXT,
    expected_output TEXT NOT NULL,
    is_sample       BOOLEAN NOT NULL DEFAULT FALSE,
    points          INTEGER NOT NULL DEFAULT 10
                    CHECK (points >= 0),
    UNIQUE (problem_id, ordinal)
);

COMMENT ON TABLE  test_cases              IS 'Input/output pairs for grading';
COMMENT ON COLUMN test_cases.ordinal      IS 'Stable order within a problem';
COMMENT ON COLUMN test_cases.is_sample    IS 'Visible to users as example';

-- ------------------------------------------------------------
-- SUBMISSIONS
-- ------------------------------------------------------------
-- Every code submission is one row. `status` tracks the lifecycle:
--   PENDING    — accepted by API, awaiting a worker
--   PROCESSING — claimed by a worker (has processing_started_at)
--   COMPLETED  — graded (has a matching row in results)
--   FAILED     — gave up after max retries
--   CANCELLED  — user or admin aborted
--
-- `worker_id` identifies which worker claimed the job.
-- `retry_count` increments each time the watchdog requeues.
-- ------------------------------------------------------------
CREATE TABLE submissions (
    id                      BIGSERIAL PRIMARY KEY,
    user_id                 BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    problem_id              BIGINT NOT NULL REFERENCES problems(id) ON DELETE CASCADE,
    language                VARCHAR(16) NOT NULL
                            CHECK (language IN
                              ('c', 'cpp', 'python', 'java', 'go', 'rust')),
    source_code             TEXT NOT NULL
                            CHECK (length(source_code) <= 65536),
    status                  VARCHAR(16) NOT NULL DEFAULT 'PENDING'
                            CHECK (status IN
                              ('PENDING', 'PROCESSING', 'COMPLETED',
                               'FAILED', 'CANCELLED')),
    submitted_at            TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    processing_started_at   TIMESTAMPTZ,
    worker_id               VARCHAR(64),
    retry_count             INTEGER NOT NULL DEFAULT 0
                            CHECK (retry_count >= 0 AND retry_count <= 10),
    ip_address              INET,
    CHECK (
        (status = 'PROCESSING' AND processing_started_at IS NOT NULL AND worker_id IS NOT NULL)
     OR (status <> 'PROCESSING')
    )
);

COMMENT ON TABLE  submissions                    IS 'One row per code submission';
COMMENT ON COLUMN submissions.status             IS 'Lifecycle state';
COMMENT ON COLUMN submissions.processing_started_at IS 'Watchdog uses this for stale detection';
COMMENT ON COLUMN submissions.source_code        IS 'Max 64 KB enforced by CHECK';

-- ------------------------------------------------------------
-- RESULTS
-- ------------------------------------------------------------
-- One result per submission (1:1 enforced by UNIQUE). Populated
-- by the worker inside the atomic grading transaction. The UNIQUE
-- constraint is the schema-level guarantee against double-grading.
-- ------------------------------------------------------------
CREATE TABLE results (
    id              BIGSERIAL PRIMARY KEY,
    submission_id   BIGINT NOT NULL UNIQUE
                    REFERENCES submissions(id) ON DELETE CASCADE,
    verdict         VARCHAR(16) NOT NULL
                    CHECK (verdict IN
                      ('AC', 'WA', 'TLE', 'MLE', 'RE', 'CE', 'OLE',
                       'PE', 'FAILED')),
    score           INTEGER NOT NULL DEFAULT 0
                    CHECK (score >= 0),
    runtime_ms      INTEGER
                    CHECK (runtime_ms IS NULL OR runtime_ms >= 0),
    memory_kb       INTEGER
                    CHECK (memory_kb IS NULL OR memory_kb >= 0),
    stdout          TEXT,
    stderr          TEXT,
    graded_at       TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    graded_by       VARCHAR(64)
);

COMMENT ON TABLE  results              IS 'Grading outcome for one submission';
COMMENT ON COLUMN results.submission_id IS 'UNIQUE — enforces 1:1 with submissions';
COMMENT ON COLUMN results.verdict       IS 'Standard competitive-programming verdict';
COMMENT ON COLUMN results.graded_by     IS 'worker_id that produced this result';

COMMIT;