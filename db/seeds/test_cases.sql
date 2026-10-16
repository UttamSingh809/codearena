-- ============================================================
-- CodeArena — Test case seeds for 10 problems
-- ============================================================
--
-- Each insert is guarded by a NOT EXISTS so re-running is safe.
-- Test cases are inserted by problem title lookup, so the order
-- of `problems.sql` and this file does not matter as long as
-- `problems.sql` runs first.
-- ============================================================

BEGIN;

-- Helper function to insert a test case if it doesn't exist yet.
-- (PostgreSQL doesn't allow IF NOT EXISTS on INSERT, so we use a
-- WHERE NOT EXISTS subquery.)

-- ------------------------------------------------------------
-- 1. Sum Two Numbers
-- ------------------------------------------------------------
INSERT INTO test_cases (problem_id, ordinal, input_data, expected_output, is_sample, points)
SELECT p.id, v.ordinal, v.input_data, v.expected_output, v.is_sample, v.points
FROM problems p,
(VALUES
    (1, '3 5',         '8',           TRUE,  10),
    (2, '10 20',       '30',          TRUE,  10),
    (3, '-1 1',        '0',           FALSE, 20),
    (4, '999999999 1', '1000000000',  FALSE, 20),
    (5, '-1000000000 -1000000000', '-2000000000', FALSE, 40)
) AS v(ordinal, input_data, expected_output, is_sample, points)
WHERE p.title = 'Sum Two Numbers'
  AND NOT EXISTS (
    SELECT 1 FROM test_cases t
    WHERE t.problem_id = p.id AND t.ordinal = v.ordinal
  );

-- ------------------------------------------------------------
-- 2. Fibonacci Sequence
-- ------------------------------------------------------------
INSERT INTO test_cases (problem_id, ordinal, input_data, expected_output, is_sample, points)
SELECT p.id, v.ordinal, v.input_data, v.expected_output, v.is_sample, v.points
FROM problems p,
(VALUES
    (1, '1',  '1',                    TRUE,  10),
    (2, '2',  '1',                    TRUE,  10),
    (3, '10', '55',                   FALSE, 20),
    (4, '50', '12586269025',          FALSE, 30),
    (5, '90', '2880067194370816120',  FALSE, 30)
) AS v(ordinal, input_data, expected_output, is_sample, points)
WHERE p.title = 'Fibonacci Sequence'
  AND NOT EXISTS (
    SELECT 1 FROM test_cases t
    WHERE t.problem_id = p.id AND t.ordinal = v.ordinal
  );

-- ------------------------------------------------------------
-- 3. Palindrome Check
-- ------------------------------------------------------------
INSERT INTO test_cases (problem_id, ordinal, input_data, expected_output, is_sample, points)
SELECT p.id, v.ordinal, v.input_data, v.expected_output, v.is_sample, v.points
FROM problems p,
(VALUES
    (1, 'racecar',        'yes', TRUE,  15),
    (2, 'hello',          'no',  TRUE,  15),
    (3, 'a',              'yes', FALSE, 15),
    (4, 'abccba',         'yes', FALSE, 20),
    (5, 'abccbd',         'no',  FALSE, 15),
    (6, 'aaaaaaaaaaaaaaaa', 'yes', FALSE, 20)
) AS v(ordinal, input_data, expected_output, is_sample, points)
WHERE p.title = 'Palindrome Check'
  AND NOT EXISTS (
    SELECT 1 FROM test_cases t
    WHERE t.problem_id = p.id AND t.ordinal = v.ordinal
  );

-- ------------------------------------------------------------
-- 4. Reverse a String
-- ------------------------------------------------------------
INSERT INTO test_cases (problem_id, ordinal, input_data, expected_output, is_sample, points)
SELECT p.id, v.ordinal, v.input_data, v.expected_output, v.is_sample, v.points
FROM problems p,
(VALUES
    (1, 'hello',          'olleh',       TRUE,  20),
    (2, 'CodeArena',      'anerAedoC',   TRUE,  20),
    (3, 'a',              'a',           FALSE, 20),
    (4, '12345',          '54321',       FALSE, 20),
    (5, 'Hello World',    'dlroW olleH', FALSE, 20)
) AS v(ordinal, input_data, expected_output, is_sample, points)
WHERE p.title = 'Reverse a String'
  AND NOT EXISTS (
    SELECT 1 FROM test_cases t
    WHERE t.problem_id = p.id AND t.ordinal = v.ordinal
  );

-- ------------------------------------------------------------
-- 5. Maximum in Array
-- ------------------------------------------------------------
INSERT INTO test_cases (problem_id, ordinal, input_data, expected_output, is_sample, points)
SELECT p.id, v.ordinal, v.input_data, v.expected_output, v.is_sample, v.points
FROM problems p,
(VALUES
    (1, '5' || E'\n' || '1 2 3 4 5',       '5',  TRUE,  20),
    (2, '3' || E'\n' || '-1 -2 -3',       '-1',  TRUE,  20),
    (3, '1' || E'\n' || '42',             '42',  FALSE, 20),
    (4, '6' || E'\n' || '10 3 100 7 100 2','100',FALSE, 20),
    (5, '4' || E'\n' || '-1000000 0 1000000 -1', '1000000', FALSE, 20)
) AS v(ordinal, input_data, expected_output, is_sample, points)
WHERE p.title = 'Maximum in Array'
  AND NOT EXISTS (
    SELECT 1 FROM test_cases t
    WHERE t.problem_id = p.id AND t.ordinal = v.ordinal
  );

-- ------------------------------------------------------------
-- 6. Count Vowels
-- ------------------------------------------------------------
INSERT INTO test_cases (problem_id, ordinal, input_data, expected_output, is_sample, points)
SELECT p.id, v.ordinal, v.input_data, v.expected_output, v.is_sample, v.points
FROM problems p,
(VALUES
    (1, 'hello',                 '2', TRUE,  20),
    (2, 'AEIOU',                 '5', TRUE,  20),
    (3, 'xyz',                   '0', FALSE, 20),
    (4, 'The quick brown fox',   '5', FALSE, 20),
    (5, 'Programming is fun!',   '5', FALSE, 20)
) AS v(ordinal, input_data, expected_output, is_sample, points)
WHERE p.title = 'Count Vowels'
  AND NOT EXISTS (
    SELECT 1 FROM test_cases t
    WHERE t.problem_id = p.id AND t.ordinal = v.ordinal
  );

-- ------------------------------------------------------------
-- 7. Two Sum
-- ------------------------------------------------------------
-- Because multiple valid answers may exist, the expected outputs
-- below show one valid index pair. The worker uses byte-exact
-- comparison after whitespace normalization, so the specific
-- pair matters for grading. We pick the pair the standard
-- hash-table solution produces first.
INSERT INTO test_cases (problem_id, ordinal, input_data, expected_output, is_sample, points)
SELECT p.id, v.ordinal, v.input_data, v.expected_output, v.is_sample, v.points
FROM problems p,
(VALUES
    (1, '4 9' || E'\n' || '2 7 11 15',    '0 1',  TRUE,  15),
    (2, '3 6' || E'\n' || '3 2 4',         '1 2',  TRUE,  15),
    (3, '2 6' || E'\n' || '3 3',           '0 1',  FALSE, 15),
    (4, '5 0' || E'\n' || '-1 0 1 2 -1',   '0 2',  FALSE, 20),
    (5, '4 100' || E'\n' || '1 2 3 4',     'no solution', FALSE, 15),
    (6, '6 8' || E'\n' || '1 5 3 6 2 7',   '2 3',  FALSE, 20)
) AS v(ordinal, input_data, expected_output, is_sample, points)
WHERE p.title = 'Two Sum'
  AND NOT EXISTS (
    SELECT 1 FROM test_cases t
    WHERE t.problem_id = p.id AND t.ordinal = v.ordinal
  );

-- ------------------------------------------------------------
-- 8. Balanced Parentheses
-- ------------------------------------------------------------
INSERT INTO test_cases (problem_id, ordinal, input_data, expected_output, is_sample, points)
SELECT p.id, v.ordinal, v.input_data, v.expected_output, v.is_sample, v.points
FROM problems p,
(VALUES
    (1, '()',        'yes', TRUE,  15),
    (2, '()()',      'yes', TRUE,  15),
    (3, '(())',      'yes', FALSE, 20),
    (4, '(()',       'no',  FALSE, 20),
    (5, '())',       'no',  FALSE, 15),
    (6, '((()))()', 'yes',  FALSE, 15)
) AS v(ordinal, input_data, expected_output, is_sample, points)
WHERE p.title = 'Balanced Parentheses'
  AND NOT EXISTS (
    SELECT 1 FROM test_cases t
    WHERE t.problem_id = p.id AND t.ordinal = v.ordinal
  );

-- ------------------------------------------------------------
-- 9. Longest Increasing Subsequence
-- ------------------------------------------------------------
INSERT INTO test_cases (problem_id, ordinal, input_data, expected_output, is_sample, points)
SELECT p.id, v.ordinal, v.input_data, v.expected_output, v.is_sample, v.points
FROM problems p,
(VALUES
    (1, '5' || E'\n' || '1 2 3 4 5',       '5',   TRUE,  15),
    (2, '5' || E'\n' || '5 4 3 2 1',       '1',   TRUE,  15),
    (3, '8' || E'\n' || '10 9 2 5 3 7 101 18', '4', FALSE, 20),
    (4, '1' || E'\n' || '7',               '1',   FALSE, 15),
    (5, '6' || E'\n' || '3 3 3 3 3 3',     '1',   FALSE, 15),
    (6, '7' || E'\n' || '1 3 6 7 9 4 10 5 5 6', '6', FALSE, 20)
) AS v(ordinal, input_data, expected_output, is_sample, points)
WHERE p.title = 'Longest Increasing Subsequence'
  AND NOT EXISTS (
    SELECT 1 FROM test_cases t
    WHERE t.problem_id = p.id AND t.ordinal = v.ordinal
  );

-- ------------------------------------------------------------
-- 10. Edit Distance
-- ------------------------------------------------------------
INSERT INTO test_cases (problem_id, ordinal, input_data, expected_output, is_sample, points)
SELECT p.id, v.ordinal, v.input_data, v.expected_output, v.is_sample, v.points
FROM problems p,
(VALUES
    (1, 'kitten' || E'\n' || 'sitting', '3',  TRUE,  20),
    (2, 'flaw'   || E'\n' || 'lawn',    '2',  TRUE,  20),
    (3, 'abc'    || E'\n' || 'abc',     '0',  FALSE, 15),
    (4, ''       || E'\n' || 'abc',     '3',  FALSE, 20),
    (5, 'abc'    || E'\n' || '',        '3',  FALSE, 15),
    (6, 'intention' || E'\n' || 'execution', '5', FALSE, 10)
) AS v(ordinal, input_data, expected_output, is_sample, points)
WHERE p.title = 'Edit Distance'
  AND NOT EXISTS (
    SELECT 1 FROM test_cases t
    WHERE t.problem_id = p.id AND t.ordinal = v.ordinal
  );

COMMIT;