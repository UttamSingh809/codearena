-- ============================================================
-- CodeArena — Seed problems (development only)
-- ============================================================

BEGIN;

INSERT INTO problems
    (title, description, input_format, output_format, constraints,
     time_limit_ms, memory_limit_mb, difficulty, created_by)
VALUES
    ('Sum Two Numbers',
     'Read two integers from standard input and print their sum.',
     'Two integers a and b, separated by whitespace.',
     'A single integer: a + b.',
     '|a|, |b| <= 10^9',
     1000, 128, 'easy',
     (SELECT id FROM users WHERE username = 'admin')),

    ('Fibonacci Sequence',
     'Read n and print the n-th Fibonacci number (F(1) = 1, F(2) = 1).',
     'A single integer n.',
     'A single integer: F(n).',
     '1 <= n <= 90',
     2000, 256, 'easy',
     (SELECT id FROM users WHERE username = 'admin')),

    ('Palindrome Check',
     'Read a string and print "yes" if it is a palindrome, "no" otherwise.',
     'A single line containing lowercase letters.',
     'yes or no.',
     'String length <= 10^5',
     1500, 256, 'medium',
     (SELECT id FROM users WHERE username = 'admin'));

COMMIT;