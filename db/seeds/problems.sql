-- ============================================================
-- CodeArena — Problem seeds (10 problems)
-- ============================================================
--
-- Each problem has:
--   - title, description, input_format, output_format, constraints
--   - time_limit_ms, memory_limit_mb
--   - difficulty (easy / medium / hard)
--
-- Tags are folded into the description text since the schema
-- does not have a tags column. Format: "Tags: x, y, z."
-- ============================================================

BEGIN;

-- Helper: only insert a problem if its title is not already present.
-- We do this by INSERT ... SELECT ... WHERE NOT EXISTS.

-- ------------------------------------------------------------
-- 1. Sum Two Numbers (easy)
-- ------------------------------------------------------------
INSERT INTO problems
    (title, description, input_format, output_format, constraints,
     time_limit_ms, memory_limit_mb, difficulty, created_by)
SELECT
    'Sum Two Numbers',
    E'Read two integers from standard input and print their sum.\n\nTags: math, io.',
    E'Two integers a and b separated by whitespace.',
    E'A single integer: a + b.',
    E'|a|, |b| <= 10^9',
    1000, 128, 'easy',
    (SELECT id FROM users WHERE username = 'admin')
WHERE NOT EXISTS (SELECT 1 FROM problems WHERE title = 'Sum Two Numbers');

-- ------------------------------------------------------------
-- 2. Fibonacci Sequence (easy)
-- ------------------------------------------------------------
INSERT INTO problems
    (title, description, input_format, output_format, constraints,
     time_limit_ms, memory_limit_mb, difficulty, created_by)
SELECT
    'Fibonacci Sequence',
    E'The Fibonacci sequence is defined by:\n  F(1) = 1, F(2) = 1, F(n) = F(n-1) + F(n-2) for n > 2.\n\nRead n and print the n-th Fibonacci number.\n\nTags: math, dynamic programming, memoization.',
    E'A single integer n.',
    E'A single integer: F(n).',
    E'1 <= n <= 90. The answer fits in a signed 64-bit integer.',
    2000, 256, 'easy',
    (SELECT id FROM users WHERE username = 'admin')
WHERE NOT EXISTS (SELECT 1 FROM problems WHERE title = 'Fibonacci Sequence');

-- ------------------------------------------------------------
-- 3. Palindrome Check (easy)
-- ------------------------------------------------------------
INSERT INTO problems
    (title, description, input_format, output_format, constraints,
     time_limit_ms, memory_limit_mb, difficulty, created_by)
SELECT
    'Palindrome Check',
    E'Given a string s, print "yes" if it is a palindrome (reads the same forwards and backwards), or "no" otherwise. The comparison is case-sensitive.\n\nTags: strings, two pointers.',
    E'A single line containing the string s (no spaces).',
    E'One word: "yes" or "no".',
    E'1 <= |s| <= 100000. s contains only lowercase English letters.',
    1500, 256, 'easy',
    (SELECT id FROM users WHERE username = 'admin')
WHERE NOT EXISTS (SELECT 1 FROM problems WHERE title = 'Palindrome Check');

-- ------------------------------------------------------------
-- 4. Reverse a String (easy)
-- ------------------------------------------------------------
INSERT INTO problems
    (title, description, input_format, output_format, constraints,
     time_limit_ms, memory_limit_mb, difficulty, created_by)
SELECT
    'Reverse a String',
    E'Read a line of text and print it reversed.\n\nTags: strings, two pointers.',
    E'A single line containing printable ASCII characters (may include spaces).',
    E'The same line, reversed.',
    E'0 <= |s| <= 10000',
    1000, 128, 'easy',
    (SELECT id FROM users WHERE username = 'admin')
WHERE NOT EXISTS (SELECT 1 FROM problems WHERE title = 'Reverse a String');

-- ------------------------------------------------------------
-- 5. Maximum in Array (easy)
-- ------------------------------------------------------------
INSERT INTO problems
    (title, description, input_format, output_format, constraints,
     time_limit_ms, memory_limit_mb, difficulty, created_by)
SELECT
    'Maximum in Array',
    E'Read n integers and print the maximum value.\n\nTags: arrays, iteration.',
    E'First line: integer n. Second line: n integers separated by spaces.',
    E'A single integer: the maximum.',
    E'1 <= n <= 100000. Each value fits in a signed 32-bit integer.',
    1500, 256, 'easy',
    (SELECT id FROM users WHERE username = 'admin')
WHERE NOT EXISTS (SELECT 1 FROM problems WHERE title = 'Maximum in Array');

-- ------------------------------------------------------------
-- 6. Count Vowels (easy)
-- ------------------------------------------------------------
INSERT INTO problems
    (title, description, input_format, output_format, constraints,
     time_limit_ms, memory_limit_mb, difficulty, created_by)
SELECT
    'Count Vowels',
    E'Given a string s, count how many vowels it contains (a, e, i, o, u). Count both lowercase and uppercase vowels.\n\nTags: strings, iteration.',
    E'A single line containing the string s.',
    E'A single integer: the number of vowels.',
    E'0 <= |s| <= 100000. s may contain spaces and punctuation.',
    1000, 128, 'easy',
    (SELECT id FROM users WHERE username = 'admin')
WHERE NOT EXISTS (SELECT 1 FROM problems WHERE title = 'Count Vowels');

-- ------------------------------------------------------------
-- 7. Two Sum (medium)
-- ------------------------------------------------------------
INSERT INTO problems
    (title, description, input_format, output_format, constraints,
     time_limit_ms, memory_limit_mb, difficulty, created_by)
SELECT
    'Two Sum',
    E'Given an array of n integers and a target t, find two distinct indices i and j (0-based, i < j) such that a[i] + a[j] = t.\n\nIf multiple answers exist, print any one. If no such pair exists, print "no solution".\n\nTags: arrays, hash table, two pointers.',
    E'First line: two integers n and t.\nSecond line: n integers separated by spaces.',
    E'Either "i j" (two 0-based indices separated by a space), or "no solution".',
    E'2 <= n <= 100000. |a[i]|, |t| <= 10^9. Exactly one solution may or may not exist; if multiple exist, any is accepted.',
    2000, 256, 'medium',
    (SELECT id FROM users WHERE username = 'admin')
WHERE NOT EXISTS (SELECT 1 FROM problems WHERE title = 'Two Sum');

-- ------------------------------------------------------------
-- 8. Balanced Parentheses (medium)
-- ------------------------------------------------------------
INSERT INTO problems
    (title, description, input_format, output_format, constraints,
     time_limit_ms, memory_limit_mb, difficulty, created_by)
SELECT
    'Balanced Parentheses',
    E'Given a string s consisting only of the characters ''('' and '')'', print "yes" if the parentheses are balanced, or "no" otherwise.\n\nA string is balanced if:\n  - Every opening parenthesis has a matching closing parenthesis that comes after it.\n  - At no point does a prefix have more closing than opening parentheses.\n\nTags: strings, stack.',
    E'A single line containing the string s.',
    E'One word: "yes" or "no".',
    E'1 <= |s| <= 100000. s contains only ''('' and '')''.',
    1500, 256, 'medium',
    (SELECT id FROM users WHERE username = 'admin')
WHERE NOT EXISTS (SELECT 1 FROM problems WHERE title = 'Balanced Parentheses');

-- ------------------------------------------------------------
-- 9. Longest Increasing Subsequence (medium)
-- ------------------------------------------------------------
INSERT INTO problems
    (title, description, input_format, output_format, constraints,
     time_limit_ms, memory_limit_mb, difficulty, created_by)
SELECT
    'Longest Increasing Subsequence',
    E'Given an array of n integers, find the length of the longest strictly increasing subsequence.\n\nA subsequence is a sequence obtained by deleting zero or more elements without changing the order of the remaining elements.\n\nTags: arrays, dynamic programming, binary search.',
    E'First line: integer n.\nSecond line: n integers separated by spaces.',
    E'A single integer: the length of the LIS.',
    E'1 <= n <= 100000. |a[i]| <= 10^9.',
    2500, 512, 'medium',
    (SELECT id FROM users WHERE username = 'admin')
WHERE NOT EXISTS (SELECT 1 FROM problems WHERE title = 'Longest Increasing Subsequence');

-- ------------------------------------------------------------
-- 10. Edit Distance (hard)
-- ------------------------------------------------------------
INSERT INTO problems
    (title, description, input_format, output_format, constraints,
     time_limit_ms, memory_limit_mb, difficulty, created_by)
SELECT
    'Edit Distance',
    E'Given two strings s and t, compute the minimum number of single-character operations required to transform s into t.\n\nAllowed operations:\n  - Insert a character.\n  - Delete a character.\n  - Replace a character.\n\nTags: strings, dynamic programming.',
    E'First line: string s.\nSecond line: string t.',
    E'A single integer: the minimum number of operations.',
    E'0 <= |s|, |t| <= 2000. Both strings contain only lowercase English letters.',
    3000, 512, 'hard',
    (SELECT id FROM users WHERE username = 'admin')
WHERE NOT EXISTS (SELECT 1 FROM problems WHERE title = 'Edit Distance');

COMMIT;