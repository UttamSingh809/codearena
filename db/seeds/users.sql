-- ============================================================
-- CodeArena — Seed users (development only)
-- ============================================================
--
-- All seed users have the password "password". Hashes below are
-- bcrypt cost factor 12, generated with the bcrypt Python library.
--
-- NEVER use these in production. Real passwords come from the
-- /api/auth/register endpoint, which hashes at request time.
-- ============================================================

BEGIN;

INSERT INTO users (username, email, password_hash, role) VALUES
    ('alice',  'alice@example.com',
     '$2b$12$Gelhsu/MHBKwt4gpDy3Zp..kNubN9VLdlY8T3DexX1YuuF3T5o99m',
     'student'),
    ('bob',    'bob@example.com',
     '$2b$12$bvID0nqqsrzWurNG9J6zyew5/rjcP/GJXIc/tpd9qUlwTSU/biz9S',
     'student'),
    ('carol',  'carol@example.com',
     '$2b$12$PsiSN.DO694mPZqpoEd3wOCAK5r4rUvH/p05gecxxNk6HcXk23AGO',
     'student'),
    ('admin',  'admin@example.com',
     '$2b$12$K4ZwQru4Vebigk9UAjZrwOEeEoSNme/NByUuaXvUMYf8XPhfphen.',
     'admin')
ON CONFLICT (username) DO UPDATE
    SET password_hash = EXCLUDED.password_hash,
        email         = EXCLUDED.email,
        role          = EXCLUDED.role;

COMMIT;