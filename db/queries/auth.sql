-- ============================================================
-- CodeArena — Authentication queries
-- ============================================================
--
-- All queries use $N placeholders. Callers pass values via
-- parameterized statements (asyncpg, libpq PQexecParams).
--
-- Each query is annotated with:
--   - Purpose:    what the query does
--   - Parameters: what the caller must supply
--   - Returns:    what comes back
--   - Index:      which index the planner should pick
--   - Notes:      transaction/isolation considerations
-- ============================================================


-- ------------------------------------------------------------
-- register_user
-- ------------------------------------------------------------
-- Purpose:    Insert a new user.
-- Parameters: $1 username, $2 email, $3 password_hash
-- Returns:    The new user's id.
-- Index:      UNIQUE(username), UNIQUE(email) enforce duplicates.
-- Notes:      Caller hashes the password before calling. If the
--             username or email already exists, PostgreSQL raises
--             unique_violation (SQLSTATE 23505), which the API
--             translates to a 409 Conflict.
-- ------------------------------------------------------------
INSERT INTO users (username, email, password_hash)
VALUES ($1, $2, $3)
RETURNING id;


-- ------------------------------------------------------------
-- get_user_by_email
-- ------------------------------------------------------------
-- Purpose:    Look up a user for login.
-- Parameters: $1 email
-- Returns:    id, username, email, password_hash, role
-- Index:      users_email_key (UNIQUE on email)
-- Notes:      The password_hash is returned so the API can run
--             bcrypt.checkpw. Never log or return this field to
--             the client.
-- ------------------------------------------------------------
SELECT id, username, email, password_hash, role
FROM users
WHERE email = $1;


-- ------------------------------------------------------------
-- get_user_by_username
-- ------------------------------------------------------------
-- Purpose:    Look up a user by username.
-- Parameters: $1 username
-- Returns:    id, username, email, role, created_at
-- Index:      users_username_key (UNIQUE on username)
-- Notes:      Does not return password_hash.
-- ------------------------------------------------------------
SELECT id, username, email, role, created_at
FROM users
WHERE username = $1;


-- ------------------------------------------------------------
-- get_user_by_id
-- ------------------------------------------------------------
-- Purpose:    Fetch a user's profile for the current session.
-- Parameters: $1 user_id
-- Returns:    id, username, email, role, created_at, last_login_at
-- Index:      users_pkey (PRIMARY KEY on id)
-- ------------------------------------------------------------
SELECT id, username, email, role, created_at, last_login_at
FROM users
WHERE id = $1;


-- ------------------------------------------------------------
-- update_last_login
-- ------------------------------------------------------------
-- Purpose:    Update the last_login_at timestamp after successful auth.
-- Parameters: $1 user_id
-- Index:      users_pkey
-- Notes:      Uses NOW() so the timestamp comes from the DB server,
--             not the client. This keeps auditing consistent even if
--             a client clock is wrong.
-- ------------------------------------------------------------
UPDATE users
SET last_login_at = NOW()
WHERE id = $1;


-- ------------------------------------------------------------
-- update_password
-- ------------------------------------------------------------
-- Purpose:    Change a user's password hash.
-- Parameters: $1 user_id, $2 new_password_hash
-- Index:      users_pkey
-- ------------------------------------------------------------
UPDATE users
SET password_hash = $2
WHERE id = $1;