-- ============================================================
-- CodeArena — Test cases for problems 11-24
-- ============================================================
-- Uses the same pattern as test_cases.sql: a VALUES block per
-- problem, joined against problems by title, with NOT EXISTS
-- guards so re-running is safe.
-- ============================================================

BEGIN;

-- ---------- 11. Remove Duplicates ----------
INSERT INTO test_cases (problem_id, ordinal, input_data, expected_output, is_sample, points)
SELECT p.id, v.ordinal, v.input_data, v.expected_output, v.is_sample, v.points
FROM problems p,
(VALUES
    (1, '3' || E'\n' || '1 1 2',           '2' || E'\n' || '1 2',       TRUE,  20),
    (2, '5' || E'\n' || '0 0 1 1 1 2',     '3' || E'\n' || '0 1 2',     TRUE,  20),
    (3, '1' || E'\n' || '5',               '1' || E'\n' || '5',         FALSE, 20),
    (4, '6' || E'\n' || '1 2 3 4 5 6',     '6' || E'\n' || '1 2 3 4 5 6', FALSE, 20),
    (5, '4' || E'\n' || '7 7 7 7',         '1' || E'\n' || '7',         FALSE, 20)
) AS v(ordinal, input_data, expected_output, is_sample, points)
WHERE p.title = 'Remove Duplicates'
  AND NOT EXISTS (SELECT 1 FROM test_cases t WHERE t.problem_id = p.id AND t.ordinal = v.ordinal);

-- ---------- 12. Merge Sorted Arrays ----------
INSERT INTO test_cases (problem_id, ordinal, input_data, expected_output, is_sample, points)
SELECT p.id, v.ordinal, v.input_data, v.expected_output, v.is_sample, v.points
FROM problems p,
(VALUES
    (1, '3 3' || E'\n' || '1 3 5' || E'\n' || '2 4 6', '1 2 3 4 5 6',   TRUE,  20),
    (2, '1 1' || E'\n' || '1' || E'\n' || '2',          '1 2',           TRUE,  20),
    (3, '2 3' || E'\n' || '5 10' || E'\n' || '1 2 3',  '1 2 3 5 10',    FALSE, 20),
    (4, '1 4' || E'\n' || '100' || E'\n' || '1 2 3 4','1 2 3 4 100',    FALSE, 20),
    (5, '4 4' || E'\n' || '-3 -1 0 5' || E'\n' || '-2 1 4 6', '-3 -2 -1 0 1 4 5 6', FALSE, 20)
) AS v(ordinal, input_data, expected_output, is_sample, points)
WHERE p.title = 'Merge Sorted Arrays'
  AND NOT EXISTS (SELECT 1 FROM test_cases t WHERE t.problem_id = p.id AND t.ordinal = v.ordinal);

-- ---------- 13. Move Zeroes ----------
INSERT INTO test_cases (problem_id, ordinal, input_data, expected_output, is_sample, points)
SELECT p.id, v.ordinal, v.input_data, v.expected_output, v.is_sample, v.points
FROM problems p,
(VALUES
    (1, '5' || E'\n' || '0 1 0 3 12',    '1 3 12 0 0',       TRUE,  20),
    (2, '3' || E'\n' || '0 0 1',         '1 0 0',            TRUE,  20),
    (3, '4' || E'\n' || '1 2 3 4',       '1 2 3 4',          FALSE, 20),
    (4, '4' || E'\n' || '0 0 0 0',       '0 0 0 0',          FALSE, 20),
    (5, '6' || E'\n' || '5 0 3 0 1 0',   '5 3 1 0 0 0',      FALSE, 20)
) AS v(ordinal, input_data, expected_output, is_sample, points)
WHERE p.title = 'Move Zeroes'
  AND NOT EXISTS (SELECT 1 FROM test_cases t WHERE t.problem_id = p.id AND t.ordinal = v.ordinal);

-- ---------- 14. Majority Element ----------
INSERT INTO test_cases (problem_id, ordinal, input_data, expected_output, is_sample, points)
SELECT p.id, v.ordinal, v.input_data, v.expected_output, v.is_sample, v.points
FROM problems p,
(VALUES
    (1, '3' || E'\n' || '3 2 3',           '3',  TRUE,  20),
    (2, '1' || E'\n' || '5',               '5',  TRUE,  20),
    (3, '7' || E'\n' || '2 2 1 1 1 2 2',   '2',  FALSE, 20),
    (4, '5' || E'\n' || '9 9 9 9 9',       '9',  FALSE, 20),
    (5, '9' || E'\n' || '1 2 2 2 2 2 3 4 5', '2', FALSE, 20)
) AS v(ordinal, input_data, expected_output, is_sample, points)
WHERE p.title = 'Majority Element'
  AND NOT EXISTS (SELECT 1 FROM test_cases t WHERE t.problem_id = p.id AND t.ordinal = v.ordinal);

-- ---------- 15. Anagram Check ----------
INSERT INTO test_cases (problem_id, ordinal, input_data, expected_output, is_sample, points)
SELECT p.id, v.ordinal, v.input_data, v.expected_output, v.is_sample, v.points
FROM problems p,
(VALUES
    (1, 'listen' || E'\n' || 'silent', 'yes', TRUE,  20),
    (2, 'hello'  || E'\n' || 'world',  'no',  TRUE,  20),
    (3, 'a'      || E'\n' || 'a',      'yes', FALSE, 20),
    (4, 'abc'    || E'\n' || 'cba',    'yes', FALSE, 20),
    (5, 'aabb'   || E'\n' || 'abab',   'yes', FALSE, 20)
) AS v(ordinal, input_data, expected_output, is_sample, points)
WHERE p.title = 'Anagram Check'
  AND NOT EXISTS (SELECT 1 FROM test_cases t WHERE t.problem_id = p.id AND t.ordinal = v.ordinal);

-- ---------- 16. First Non-Repeating Character ----------
INSERT INTO test_cases (problem_id, ordinal, input_data, expected_output, is_sample, points)
SELECT p.id, v.ordinal, v.input_data, v.expected_output, v.is_sample, v.points
FROM problems p,
(VALUES
    (1, 'leetcode',  'l',  TRUE,  20),
    (2, 'loveleetcode', 'v', TRUE, 20),
    (3, 'aabb',      '-1', FALSE, 20),
    (4, 'abcdef',    'a',  FALSE, 20),
    (5, 'aabbc',     'c',  FALSE, 20)
) AS v(ordinal, input_data, expected_output, is_sample, points)
WHERE p.title = 'First Non-Repeating Character'
  AND NOT EXISTS (SELECT 1 FROM test_cases t WHERE t.problem_id = p.id AND t.ordinal = v.ordinal);

-- ---------- 17. Longest Common Prefix ----------
INSERT INTO test_cases (problem_id, ordinal, input_data, expected_output, is_sample, points)
SELECT p.id, v.ordinal, v.input_data, v.expected_output, v.is_sample, v.points
FROM problems p,
(VALUES
    (1, '3' || E'\n' || 'flower' || E'\n' || 'flow' || E'\n' || 'flight', 'fl', TRUE, 20),
    (2, '3' || E'\n' || 'dog' || E'\n' || 'racecar' || E'\n' || 'car',     '',   TRUE, 20),
    (3, '1' || E'\n' || 'a',  'a',  FALSE, 20),
    (4, '2' || E'\n' || 'abc' || E'\n' || 'ab', 'ab', FALSE, 20),
    (5, '3' || E'\n' || 'test' || E'\n' || 'testing' || E'\n' || 'tested', 'test', FALSE, 20)
) AS v(ordinal, input_data, expected_output, is_sample, points)
WHERE p.title = 'Longest Common Prefix'
  AND NOT EXISTS (SELECT 1 FROM test_cases t WHERE t.problem_id = p.id AND t.ordinal = v.ordinal);

-- ---------- 18. String Compression ----------
INSERT INTO test_cases (problem_id, ordinal, input_data, expected_output, is_sample, points)
SELECT p.id, v.ordinal, v.input_data, v.expected_output, v.is_sample, v.points
FROM problems p,
(VALUES
    (1, 'aaabbc',  'a3b2c',   TRUE,  20),
    (2, 'abc',     'abc',     TRUE,  20),
    (3, 'a',       'a',       FALSE, 20),
    (4, 'aaaaaa',  'a6',      FALSE, 20),
    (5, 'aabbccdd','a2b2c2d2',FALSE, 20)
) AS v(ordinal, input_data, expected_output, is_sample, points)
WHERE p.title = 'String Compression'
  AND NOT EXISTS (SELECT 1 FROM test_cases t WHERE t.problem_id = p.id AND t.ordinal = v.ordinal);

-- ---------- 19. GCD and LCM ----------
INSERT INTO test_cases (problem_id, ordinal, input_data, expected_output, is_sample, points)
SELECT p.id, v.ordinal, v.input_data, v.expected_output, v.is_sample, v.points
FROM problems p,
(VALUES
    (1, '12 18',   '6 36',        TRUE,  20),
    (2, '7 13',    '1 91',        TRUE,  20),
    (3, '1 1',     '1 1',         FALSE, 20),
    (4, '100 75',  '25 300',      FALSE, 20),
    (5, '1000000000 999999999', '1 999999999000000000', FALSE, 20)
) AS v(ordinal, input_data, expected_output, is_sample, points)
WHERE p.title = 'GCD and LCM'
  AND NOT EXISTS (SELECT 1 FROM test_cases t WHERE t.problem_id = p.id AND t.ordinal = v.ordinal);

-- ---------- 20. Prime Check ----------
INSERT INTO test_cases (problem_id, ordinal, input_data, expected_output, is_sample, points)
SELECT p.id, v.ordinal, v.input_data, v.expected_output, v.is_sample, v.points
FROM problems p,
(VALUES
    (1, '2',         'yes', TRUE,  20),
    (2, '17',        'yes', TRUE,  20),
    (3, '1',         'no',  FALSE, 20),
    (4, '100',       'no',  FALSE, 20),
    (5, '999999999989', 'yes', FALSE, 20)
) AS v(ordinal, input_data, expected_output, is_sample, points)
WHERE p.title = 'Prime Check'
  AND NOT EXISTS (SELECT 1 FROM test_cases t WHERE t.problem_id = p.id AND t.ordinal = v.ordinal);

-- ---------- 21. Count Primes ----------
INSERT INTO test_cases (problem_id, ordinal, input_data, expected_output, is_sample, points)
SELECT p.id, v.ordinal, v.input_data, v.expected_output, v.is_sample, v.points
FROM problems p,
(VALUES
    (1, '10',       '4',   TRUE,  20),
    (2, '0',        '0',   TRUE,  20),
    (3, '1',        '0',   FALSE, 20),
    (4, '100',      '25',  FALSE, 20),
    (5, '1000000',  '78498', FALSE, 20)
) AS v(ordinal, input_data, expected_output, is_sample, points)
WHERE p.title = 'Count Primes'
  AND NOT EXISTS (SELECT 1 FROM test_cases t WHERE t.problem_id = p.id AND t.ordinal = v.ordinal);

-- ---------- 22. Power Function ----------
INSERT INTO test_cases (problem_id, ordinal, input_data, expected_output, is_sample, points)
SELECT p.id, v.ordinal, v.input_data, v.expected_output, v.is_sample, v.points
FROM problems p,
(VALUES
    (1, '2 10',       '1024',       TRUE,  20),
    (2, '3 0',        '1',          TRUE,  20),
    (3, '0 5',        '0',          FALSE, 20),
    (4, '2 1000000000000000000', '835275493', FALSE, 20),
    (5, '10 9',       '999999937',  FALSE, 20)
) AS v(ordinal, input_data, expected_output, is_sample, points)
WHERE p.title = 'Power Function'
  AND NOT EXISTS (SELECT 1 FROM test_cases t WHERE t.problem_id = p.id AND t.ordinal = v.ordinal);

-- ---------- 23. Sieve of Eratosthenes ----------
INSERT INTO test_cases (problem_id, ordinal, input_data, expected_output, is_sample, points)
SELECT p.id, v.ordinal, v.input_data, v.expected_output, v.is_sample, v.points
FROM problems p,
(VALUES
    (1, '10',  E'2\n3\n5\n7',           TRUE,  20),
    (2, '2',   '2',                     TRUE,  20),
    (3, '20',  E'2\n3\n5\n7\n11\n13\n17\n19', FALSE, 20),
    (4, '30',  E'2\n3\n5\n7\n11\n13\n17\n19\n23\n29', FALSE, 20),
    (5, '50',  E'2\n3\n5\n7\n11\n13\n17\n19\n23\n29\n31\n37\n41\n43\n47', FALSE, 20)
) AS v(ordinal, input_data, expected_output, is_sample, points)
WHERE p.title = 'Sieve of Eratosthenes'
  AND NOT EXISTS (SELECT 1 FROM test_cases t WHERE t.problem_id = p.id AND t.ordinal = v.ordinal);

-- ---------- 24. Factorial Mod ----------
INSERT INTO test_cases (problem_id, ordinal, input_data, expected_output, is_sample, points)
SELECT p.id, v.ordinal, v.input_data, v.expected_output, v.is_sample, v.points
FROM problems p,
(VALUES
    (1, '0',  '1',        TRUE,  20),
    (2, '5',  '120',      TRUE,  20),
    (3, '10', '3628800',  FALSE, 20),
    (4, '20', '146326063',FALSE, 20),
    (5, '1000000', '641102369', FALSE, 20)
) AS v(ordinal, input_data, expected_output, is_sample, points)
WHERE p.title = 'Factorial Mod'
  AND NOT EXISTS (SELECT 1 FROM test_cases t WHERE t.problem_id = p.id AND t.ordinal = v.ordinal);

COMMIT;