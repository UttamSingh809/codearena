#!/usr/bin/env bash
# ============================================================
# CodeArena — Database migration runner
# ============================================================
#
# Usage:
#   ./migrate.sh              # run all migrations in order
#   ./migrate.sh --seed       # run all migrations + seed data
#   ./migrate.sh --drop       # drop all CodeArena tables
#
# Reads DATABASE_URL from the environment, or from .env in the
# repo root. Fails fast on the first error.
# ============================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# Load .env if present
if [[ -z "${DATABASE_URL:-}" && -f "$ROOT_DIR/.env" ]]; then
    # shellcheck disable=SC1091
    set -a
    # shellcheck disable=SC1090
    source <(grep -E '^DATABASE_URL=' "$ROOT_DIR/.env")
    set +a
fi

if [[ -z "${DATABASE_URL:-}" ]]; then
    echo "error: DATABASE_URL is not set (env or .env)" >&2
    exit 1
fi

MIGRATION_DIR="$SCRIPT_DIR/migrations"
SEED_DIR="$SCRIPT_DIR/seeds"

run_sql_file() {
    local file="$1"
    echo "  -> $(basename "$file")"
    psql "$DATABASE_URL" -v ON_ERROR_STOP=1 -q -f "$file"
}

drop_all() {
    echo "Dropping all CodeArena tables..."
    psql "$DATABASE_URL" -v ON_ERROR_STOP=1 <<'SQL'
BEGIN;
DROP MATERIALIZED VIEW IF EXISTS leaderboard CASCADE;
DROP TABLE IF EXISTS results CASCADE;
DROP TABLE IF EXISTS submissions CASCADE;
DROP TABLE IF EXISTS test_cases CASCADE;
DROP TABLE IF EXISTS problems CASCADE;
DROP TABLE IF EXISTS users CASCADE;
COMMIT;
SQL
    echo "Done."
}

run_migrations() {
    echo "Running migrations..."
    shopt -s nullglob
    for f in "$MIGRATION_DIR"/*.sql; do
        run_sql_file "$f"
    done
    shopt -u nullglob
    echo "Migrations complete."
}

run_seeds() {
    echo "Running seed data..."
    shopt -s nullglob
    for f in "$SEED_DIR"/*.sql; do
        run_sql_file "$f"
    done
    shopt -u nullglob
    echo "Seeds complete."
}

case "${1:-}" in
    --drop)
        drop_all
        ;;
    --seed)
        run_migrations
        run_seeds
        ;;
    ""|--run)
        run_migrations
        ;;
    *)
        echo "usage: $0 [--run|--seed|--drop]" >&2
        exit 1
        ;;
esac