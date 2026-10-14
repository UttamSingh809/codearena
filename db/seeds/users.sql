-- ============================================================
-- CodeArena — Seed users (development only)
-- ============================================================
--
-- Passwords are bcrypt hashes of the literal string "password".
-- DO NOT use these in production. Production passwords are set
-- through the registration endpoint.
-- ============================================================

BEGIN;

INSERT INTO users (username, email, password_hash, role) VALUES
    ('alice',  'alice@example.com',
     '$2b$12$LQv3c1yqBWVHxkd0LHAkCOYz6TtxMQJqhN8/LewdBPj2FpQ1c6P8m',
     'student'),
    ('bob',    'bob@example.com',
     '$2b$12$LQv3c1yqBWVHxkd0LHAkCOYz6TtxMQJqhN8/LewdBPj2FpQ1c6P8m',
     'student'),
    ('carol',  'carol@example.com',
     '$2b$12$LQv3c1yqBWVHxkd0LHAkCOYz6TtxMQJqhN8/LewdBPj2FpQ1c6P8m',
     'student'),
    ('admin',  'admin@example.com',
     '$2b$12$LQv3c1yqBWVHxkd0LHAkCOYz6TtxMQJqhN8/LewdBPj2FpQ1c6P8m',
     'admin')
ON CONFLICT (username) DO NOTHING;

COMMIT;