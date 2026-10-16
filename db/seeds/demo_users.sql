-- ============================================================
-- CodeArena — Additional demo users
-- ============================================================
-- Idempotent. Password for all demo users is "password".
-- ============================================================

BEGIN;

INSERT INTO users (username, email, password_hash, role)
SELECT v.username,
       v.username || '@example.com',
       '$2b$12$LQv3c1yqBWVHxkd0LHAkCOYz6TtxMQJqhN8/LewdBPj2FpQ1c6P8m',
       'student'
FROM (VALUES
    ('dave'),  ('eve'),   ('frank'),  ('grace'),
    ('heidi'), ('ivan'),  ('judy'),   ('karl'),
    ('linda'), ('mike'),  ('nancy'),  ('oscar'),
    ('peter'), ('quinn'), ('rachel'), ('steve'),
    ('tina'),  ('ursula'),('victor'), ('wendy')
) AS v(username)
WHERE NOT EXISTS (SELECT 1 FROM users WHERE users.username = v.username);

COMMIT;