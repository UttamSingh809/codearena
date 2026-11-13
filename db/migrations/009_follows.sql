BEGIN;

CREATE TABLE IF NOT EXISTS follows (
    follower_id BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    following_id BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    PRIMARY KEY (follower_id, following_id),
    CHECK (follower_id != following_id)
);

-- Index to quickly find who a user is following
CREATE INDEX IF NOT EXISTS idx_follows_follower ON follows(follower_id);
-- Index to quickly find a user's followers
CREATE INDEX IF NOT EXISTS idx_follows_following ON follows(following_id);

COMMENT ON TABLE follows IS 'User follow relationships';

COMMIT;
