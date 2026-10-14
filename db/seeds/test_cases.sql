-- ============================================================
-- CodeArena — Seed test cases (development only)
-- ============================================================

BEGIN;

-- Problem 1: Sum Two Numbers
INSERT INTO test_cases (problem_id, ordinal, input_data, expected_output, is_sample, points)
SELECT p.id, v.ordinal, v.input_data, v.expected_output, v.is_sample, v.points
FROM problems p,
(VALUES
    (1, '3 5',    '8',   TRUE,  10),
    (2, '10 20',  '30',  TRUE,  10),
    (3, '-1 1',   '0',   FALSE, 20),
    (4, '999999999 1', '1000000000', FALSE, 20)
) AS v(ordinal, input_data, expected_output, is_sample, points)
WHERE p.title = 'Sum Two Numbers';

-- Problem 2: Fibonacci
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
WHERE p.title = 'Fibonacci Sequence';

-- Problem 3: Palindrome
INSERT INTO test_cases (problem_id, ordinal, input_data, expected_output, is_sample, points)
SELECT p.id, v.ordinal, v.input_data, v.expected_output, v.is_sample, v.points
FROM problems p,
(VALUES
    (1, 'racecar',  'yes', TRUE,  20),
    (2, 'hello',    'no',  TRUE,  20),
    (3, 'a',        'yes', FALSE, 20),
    (4, 'abccba',   'yes', FALSE, 20),
    (5, 'abccbd',   'no',  FALSE, 20)
) AS v(ordinal, input_data, expected_output, is_sample, points)
WHERE p.title = 'Palindrome Check';

COMMIT;