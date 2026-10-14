-- ============================================================
-- CodeArena — Refresh the leaderboard materialized view
-- ============================================================
--
-- REFRESH MATERIALIZED VIEW CONCURRENTLY cannot run inside a
-- transaction block, so this file is deliberately a single
-- statement with no BEGIN/COMMIT.
--
-- The CONCURRENTLY variant requires a UNIQUE index on the view
-- (idx_leaderboard_unique, created in migration 003). It builds
-- the new view alongside the old and swaps atomically, taking
-- only a SHARE UPDATE EXCLUSIVE lock — reads continue during
-- the refresh.
--
-- Called by: a background process, after each grading batch,
-- at most once per second.
-- ============================================================

REFRESH MATERIALIZED VIEW CONCURRENTLY leaderboard;