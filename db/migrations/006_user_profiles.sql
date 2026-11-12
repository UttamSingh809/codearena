-- ============================================================
-- CodeArena — User profiles (avatar + bio)
-- ============================================================

BEGIN;

ALTER TABLE users
    ADD COLUMN IF NOT EXISTS avatar_url TEXT,
    ADD COLUMN IF NOT EXISTS bio TEXT;

COMMENT ON COLUMN users.avatar_url IS 'URL to the user avatar image';
COMMENT ON COLUMN users.bio IS 'Short user biography, max 200 chars';

COMMIT;