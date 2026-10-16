-- ============================================================
-- CodeArena — Demo submissions for leaderboard filling
-- ============================================================
--
-- Generates synthetic submissions + results for all existing
-- users across all problems, with realistic-ish verdict
-- distributions. Then refreshes the leaderboard matview.
--
-- Idempotent: skips if submissions with the 'demo-seed' worker
-- marker already exist. To force reseeding, delete those rows
-- first:
--     DELETE FROM results WHERE graded_by = 'demo-seed';
--     DELETE FROM submissions WHERE worker_id = 'demo-seed';
-- ============================================================

BEGIN;

DO $seed$
DECLARE
    u        RECORD;
    p        RECORD;
    n_sub    INT;
    k        INT;
    lang     TEXT;
    verdict  TEXT;
    score    INT;
    rt_ms    INT;
    mem_kb   INT;
    roll     FLOAT;
    sub_id   BIGINT;
    v_list   TEXT[] := ARRAY['AC','AC','AC','WA','TLE','RE','WA','MLE'];
    lang_list TEXT[] := ARRAY['c','cpp','python'];
    seq      BIGINT := 0;
BEGIN
    -- Skip if already seeded
    IF EXISTS (SELECT 1 FROM submissions WHERE worker_id = 'demo-seed' LIMIT 1) THEN
        RAISE NOTICE 'demo submissions already seeded; skipping';
        RETURN;
    END IF;

    FOR u IN SELECT id, username FROM users WHERE role != 'admin' LOOP
        FOR p IN SELECT id, time_limit_ms, memory_limit_mb FROM problems WHERE is_active LOOP
            -- Deterministic "random" count based on user and problem id
            n_sub := 3 + ((u.id * 7 + p.id * 13) % 6);   -- 3..8 submissions

            FOR k IN 1..n_sub LOOP
                seq := seq + 1;

                -- Deterministic language pick
                lang := lang_list[1 + ((u.id + p.id + k) % 3)];

                -- Deterministic verdict pick: weighted so AC is common,
                -- and gets more likely as k increases (user "learns").
                roll := ((u.id * 11 + p.id * 5 + k * 3) % 100) / 100.0;
                IF k >= n_sub - 1 AND roll < 0.7 THEN
                    verdict := 'AC';
                ELSIF roll < 0.45 THEN
                    verdict := 'AC';
                ELSE
                    verdict := v_list[1 + ((u.id + p.id + k * 7) % array_length(v_list, 1))];
                END IF;

                IF verdict = 'AC' THEN
                    score := 100;
                    rt_ms := 20 + ((u.id * 3 + p.id * 11 + k) % 500);
                    mem_kb := 1024 + ((u.id + p.id * 5 + k) % 2048);
                ELSIF verdict = 'WA' THEN
                    score := (30 + (k * 7) % 40);
                    rt_ms := 15 + ((u.id + k) % 200);
                    mem_kb := 1024 + ((u.id + p.id) % 1024);
                ELSIF verdict = 'TLE' THEN
                    score := 0;
                    rt_ms := p.time_limit_ms + 100;
                    mem_kb := 2048;
                ELSIF verdict = 'MLE' THEN
                    score := 0;
                    rt_ms := 100;
                    mem_kb := p.memory_limit_mb * 1024;
                ELSE  -- RE
                    score := 0;
                    rt_ms := 30;
                    mem_kb := 1024;
                END IF;

                -- Insert submission, marked COMPLETED
                INSERT INTO submissions
                    (user_id, problem_id, language, source_code,
                     status, submitted_at,
                     processing_started_at, worker_id, retry_count)
                VALUES
                    (u.id, p.id, lang,
                     '// demo seed for ' || u.username || ' p' || p.id,
                     'COMPLETED',
                     NOW() - (seq || ' minutes')::INTERVAL * 10,
                     NOW() - (seq || ' minutes')::INTERVAL * 10,
                     'demo-seed', 0)
                RETURNING id INTO sub_id;

                -- Insert result
                INSERT INTO results
                    (submission_id, verdict, score, runtime_ms, memory_kb,
                     graded_at, graded_by,
                     test_total, test_passed, first_failed)
                VALUES
                    (sub_id, verdict, score, rt_ms, mem_kb,
                     NOW() - (seq || ' minutes')::INTERVAL * 10,
                     'demo-seed',
                     4,
                     CASE WHEN verdict = 'AC' THEN 4
                          WHEN verdict = 'WA' THEN 2
                          ELSE 1 END,
                     CASE WHEN verdict = 'AC' THEN 0
                          ELSE 1 + (seq % 4) END);
            END LOOP;
        END LOOP;
    END LOOP;

    RAISE NOTICE 'inserted % demo submissions', seq;
END
$seed$;

COMMIT;

-- Refresh the leaderboard matview outside the transaction.
-- CONCURRENTLY requires it to be a top-level statement.
REFRESH MATERIALIZED VIEW CONCURRENTLY leaderboard;