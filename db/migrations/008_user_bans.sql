BEGIN;

ALTER TABLE users
    ADD COLUMN IF NOT EXISTS is_banned BOOLEAN NOT NULL DEFAULT FALSE;

COMMENT ON COLUMN users.is_banned IS 'If true, the user cannot log in or submit';

COMMIT;