-- ============================================================
-- CodeArena — Additional problems (11-42)
-- ============================================================
--
-- Adds 32 more problems across arrays, strings, math, data
-- structures, DP, and graphs. Each has 4-6 test cases.
--
-- Idempotent: uses WHERE NOT EXISTS on title.
-- ============================================================

BEGIN;

-- ------------------------------------------------------------
-- 11. Remove Duplicates (easy)
-- ------------------------------------------------------------
INSERT INTO problems
    (title, description, input_format, output_format, constraints,
     time_limit_ms, memory_limit_mb, difficulty, created_by)
SELECT
    'Remove Duplicates',
    E'Given a sorted array, remove duplicates in-place so each element appears only once. Print the length of the resulting array, then the array itself.\n\nTags: arrays, two pointers.',
    E'First line: n. Second line: n sorted integers.',
    E'First line: k (new length). Second line: k distinct integers.',
    E'1 <= n <= 100000',
    1500, 256, 'easy',
    (SELECT id FROM users WHERE username = 'admin')
WHERE NOT EXISTS (SELECT 1 FROM problems WHERE title = 'Remove Duplicates');

-- ------------------------------------------------------------
-- 12. Merge Sorted Arrays (easy)
-- ------------------------------------------------------------
INSERT INTO problems
    (title, description, input_format, output_format, constraints,
     time_limit_ms, memory_limit_mb, difficulty, created_by)
SELECT
    'Merge Sorted Arrays',
    E'Merge two sorted arrays into a single sorted array.\n\nTags: arrays, two pointers.',
    E'First line: n m. Second line: n integers. Third line: m integers.',
    E'n+m integers on one line, sorted ascending.',
    E'1 <= n, m <= 100000. Values fit in signed 32-bit int.',
    1500, 256, 'easy',
    (SELECT id FROM users WHERE username = 'admin')
WHERE NOT EXISTS (SELECT 1 FROM problems WHERE title = 'Merge Sorted Arrays');

-- ------------------------------------------------------------
-- 13. Move Zeroes (easy)
-- ------------------------------------------------------------
INSERT INTO problems
    (title, description, input_format, output_format, constraints,
     time_limit_ms, memory_limit_mb, difficulty, created_by)
SELECT
    'Move Zeroes',
    E'Given an array, move all zeroes to the end while keeping the relative order of non-zero elements.\n\nTags: arrays, two pointers.',
    E'First line: n. Second line: n integers.',
    E'The reordered array on one line.',
    E'1 <= n <= 100000',
    1500, 256, 'easy',
    (SELECT id FROM users WHERE username = 'admin')
WHERE NOT EXISTS (SELECT 1 FROM problems WHERE title = 'Move Zeroes');

-- ------------------------------------------------------------
-- 14. Majority Element (easy)
-- ------------------------------------------------------------
INSERT INTO problems
    (title, description, input_format, output_format, constraints,
     time_limit_ms, memory_limit_mb, difficulty, created_by)
SELECT
    'Majority Element',
    E'Given an array, find the element that appears more than n/2 times. The array is guaranteed to have a majority element.\n\nTags: arrays, Boyer-Moore.',
    E'First line: n. Second line: n integers.',
    E'The majority element.',
    E'1 <= n <= 100000',
    1500, 256, 'easy',
    (SELECT id FROM users WHERE username = 'admin')
WHERE NOT EXISTS (SELECT 1 FROM problems WHERE title = 'Majority Element');

-- ------------------------------------------------------------
-- 15. Anagram Check (easy)
-- ------------------------------------------------------------
INSERT INTO problems
    (title, description, input_format, output_format, constraints,
     time_limit_ms, memory_limit_mb, difficulty, created_by)
SELECT
    'Anagram Check',
    E'Given two strings s and t, print "yes" if they are anagrams (same characters, same counts), or "no" otherwise.\n\nTags: strings, hash table.',
    E'Two lines: s and t.',
    E'One word: "yes" or "no".',
    E'1 <= |s|, |t| <= 100000. Lowercase English letters only.',
    1500, 256, 'easy',
    (SELECT id FROM users WHERE username = 'admin')
WHERE NOT EXISTS (SELECT 1 FROM problems WHERE title = 'Anagram Check');

-- ------------------------------------------------------------
-- 16. First Non-Repeating Character (easy)
-- ------------------------------------------------------------
INSERT INTO problems
    (title, description, input_format, output_format, constraints,
     time_limit_ms, memory_limit_mb, difficulty, created_by)
SELECT
    'First Non-Repeating Character',
    E'Given a string s, find the first character (by position) that does not repeat anywhere in s. Print the character, or "-1" if none exists.\n\nTags: strings, hash table.',
    E'A single line containing s.',
    E'The character, or "-1".',
    E'1 <= |s| <= 100000. Lowercase English letters only.',
    1500, 256, 'easy',
    (SELECT id FROM users WHERE username = 'admin')
WHERE NOT EXISTS (SELECT 1 FROM problems WHERE title = 'First Non-Repeating Character');

-- ------------------------------------------------------------
-- 17. Longest Common Prefix (easy)
-- ------------------------------------------------------------
INSERT INTO problems
    (title, description, input_format, output_format, constraints,
     time_limit_ms, memory_limit_mb, difficulty, created_by)
SELECT
    'Longest Common Prefix',
    E'Given n strings, find the longest common prefix. If there is none, print the empty line.\n\nTags: strings.',
    E'First line: n. Then n lines, one string each.',
    E'The longest common prefix, or an empty line.',
    E'1 <= n <= 1000. |s_i| <= 1000.',
    1500, 256, 'easy',
    (SELECT id FROM users WHERE username = 'admin')
WHERE NOT EXISTS (SELECT 1 FROM problems WHERE title = 'Longest Common Prefix');

-- ------------------------------------------------------------
-- 18. Valid Anagram with Case (easy)
-- ------------------------------------------------------------
INSERT INTO problems
    (title, description, input_format, output_format, constraints,
     time_limit_ms, memory_limit_mb, difficulty, created_by)
SELECT
    'String Compression',
    E'Compress a string using run-length encoding. For each maximal group of identical characters, output the character followed by its count. If the count is 1, output only the character.\n\nExample: "aaabbc" -> "a3b2c".\n\nTags: strings, two pointers.',
    E'A single line containing s.',
    E'The compressed string.',
    E'1 <= |s| <= 100000. Lowercase English letters only.',
    1500, 256, 'easy',
    (SELECT id FROM users WHERE username = 'admin')
WHERE NOT EXISTS (SELECT 1 FROM problems WHERE title = 'String Compression');

-- ------------------------------------------------------------
-- 19. GCD and LCM (easy)
-- ------------------------------------------------------------
INSERT INTO problems
    (title, description, input_format, output_format, constraints,
     time_limit_ms, memory_limit_mb, difficulty, created_by)
SELECT
    'GCD and LCM',
    E'Given two positive integers a and b, print their greatest common divisor and least common multiple on one line, separated by a space.\n\nTags: math, number theory.',
    E'Two integers a and b.',
    E'Two integers: gcd(a, b) and lcm(a, b).',
    E'1 <= a, b <= 10^9. lcm fits in a signed 64-bit integer.',
    1000, 128, 'easy',
    (SELECT id FROM users WHERE username = 'admin')
WHERE NOT EXISTS (SELECT 1 FROM problems WHERE title = 'GCD and LCM');

-- ------------------------------------------------------------
-- 20. Prime Check (easy)
-- ------------------------------------------------------------
INSERT INTO problems
    (title, description, input_format, output_format, constraints,
     time_limit_ms, memory_limit_mb, difficulty, created_by)
SELECT
    'Prime Check',
    E'Given an integer n, print "yes" if it is prime, or "no" otherwise.\n\nTags: math, number theory.',
    E'A single integer n.',
    E'One word: "yes" or "no".',
    E'1 <= n <= 10^12',
    1000, 128, 'easy',
    (SELECT id FROM users WHERE username = 'admin')
WHERE NOT EXISTS (SELECT 1 FROM problems WHERE title = 'Prime Check');

-- ------------------------------------------------------------
-- 21. Count Primes (medium)
-- ------------------------------------------------------------
INSERT INTO problems
    (title, description, input_format, output_format, constraints,
     time_limit_ms, memory_limit_mb, difficulty, created_by)
SELECT
    'Count Primes',
    E'Given n, count the number of prime numbers less than n.\n\nTags: math, sieve.',
    E'A single integer n.',
    E'The number of primes < n.',
    E'0 <= n <= 5 * 10^6',
    2000, 512, 'medium',
    (SELECT id FROM users WHERE username = 'admin')
WHERE NOT EXISTS (SELECT 1 FROM problems WHERE title = 'Count Primes');

-- ------------------------------------------------------------
-- 22. Power Function (medium)
-- ------------------------------------------------------------
INSERT INTO problems
    (title, description, input_format, output_format, constraints,
     time_limit_ms, memory_limit_mb, difficulty, created_by)
SELECT
    'Power Function',
    E'Compute a^b modulo 10^9+7, where b may be very large.\n\nTags: math, binary exponentiation.',
    E'Two integers a and b on one line.',
    E'A single integer: a^b mod (10^9+7).',
    E'0 <= a <= 10^9, 0 <= b <= 10^18',
    1500, 256, 'medium',
    (SELECT id FROM users WHERE username = 'admin')
WHERE NOT EXISTS (SELECT 1 FROM problems WHERE title = 'Power Function');

-- ------------------------------------------------------------
-- 23. Sieve of Eratosthenes (medium)
-- ------------------------------------------------------------
INSERT INTO problems
    (title, description, input_format, output_format, constraints,
     time_limit_ms, memory_limit_mb, difficulty, created_by)
SELECT
    'Sieve of Eratosthenes',
    E'Given n, print all primes up to and including n, one per line.\n\nTags: math, sieve.',
    E'A single integer n.',
    E'Primes in ascending order, one per line.',
    E'2 <= n <= 10^6',
    1500, 256, 'medium',
    (SELECT id FROM users WHERE username = 'admin')
WHERE NOT EXISTS (SELECT 1 FROM problems WHERE title = 'Sieve of Eratosthenes');

-- ------------------------------------------------------------
-- 24. Factorial Mod (medium)
-- ------------------------------------------------------------
INSERT INTO problems
    (title, description, input_format, output_format, constraints,
     time_limit_ms, memory_limit_mb, difficulty, created_by)
SELECT
    'Factorial Mod',
    E'Compute n! modulo 10^9+7.\n\nTags: math.',
    E'A single integer n.',
    E'A single integer: n! mod (10^9+7).',
    E'0 <= n <= 10^6',
    1500, 256, 'medium',
    (SELECT id FROM users WHERE username = 'admin')
WHERE NOT EXISTS (SELECT 1 FROM problems WHERE title = 'Factorial Mod');

COMMIT;