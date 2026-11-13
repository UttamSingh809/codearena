BEGIN;

CREATE TABLE IF NOT EXISTS discussions (
    id              BIGSERIAL PRIMARY KEY,
    problem_id      BIGINT NOT NULL REFERENCES problems(id) ON DELETE CASCADE,
    user_id         BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    parent_id       BIGINT REFERENCES discussions(id) ON DELETE CASCADE,
    content         TEXT NOT NULL CHECK (length(content) > 0 AND length(content) <= 2000),
    upvotes         INTEGER NOT NULL DEFAULT 0,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- Track who upvoted what to prevent double-voting
CREATE TABLE IF NOT EXISTS discussion_upvotes (
    discussion_id   BIGINT NOT NULL REFERENCES discussions(id) ON DELETE CASCADE,
    user_id         BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    PRIMARY KEY (discussion_id, user_id)
);

CREATE INDEX IF NOT EXISTS idx_discussions_problem ON discussions(problem_id, created_at DESC);

COMMENT ON TABLE discussions IS 'Problem discussion threads';
COMMENT ON TABLE discussion_upvotes IS 'Tracks who upvoted a comment';

COMMIT;
