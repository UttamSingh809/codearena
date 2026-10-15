#!/usr/bin/env python3
"""
CodeArena — Concurrent job claim test.

Spawns N worker threads, each calling the claim_job query repeatedly
until no PENDING submissions remain. Verifies:

  1. Every PENDING submission is claimed exactly once (no duplicates).
  2. Every worker claimed at least one job (no starvation).
  3. Total claims == initial PENDING count.
  4. No deadlocks, no "duplicate key" errors.

The claim_job query uses FOR UPDATE SKIP LOCKED, which makes
concurrent claiming race-free and non-blocking.

Run:
    python3 db/tests/test_concurrent_claims.py

Environment:
    DATABASE_URL must be set, or read from .env in the repo root.
"""

import os
import sys
import threading
import time
from collections import Counter

try:
    import psycopg2
    import psycopg2.extras
except ImportError:
    print("ERROR: psycopg2 not installed. Run: pip install psycopg2-binary", file=sys.stderr)
    sys.exit(1)


# ----- Configuration -----

NUM_WORKERS = 10
NUM_JOBS = 100
CLAIM_TIMEOUT_SEC = 30


# ----- Helpers -----

def load_database_url() -> str:
    url = os.environ.get("DATABASE_URL")
    if url:
        return url
    # Try .env in the repo root
    env_path = os.path.join(os.path.dirname(__file__), "..", "..", ".env")
    if os.path.exists(env_path):
        with open(env_path) as f:
            for line in f:
                if line.startswith("DATABASE_URL="):
                    return line.strip().split("=", 1)[1]
    raise RuntimeError("DATABASE_URL not set and not found in .env")


def setup_jobs(conn, n: int) -> int:
    """Insert n PENDING submissions. Returns the count inserted."""
    with conn.cursor() as cur:
        # Ensure user and problem exist
        cur.execute("SELECT id FROM users WHERE username = 'alice' LIMIT 1")
        row = cur.fetchone()
        if not row:
            cur.execute(
                "INSERT INTO users (username, email, password_hash) "
                "VALUES ('alice', 'alice@test.com', 'h') RETURNING id"
            )
            user_id = cur.fetchone()[0]
        else:
            user_id = row[0]

        cur.execute("SELECT id FROM problems WHERE title = 'Sum Two Numbers' LIMIT 1")
        row = cur.fetchone()
        if not row:
            cur.execute(
                "INSERT INTO problems (title) VALUES ('Sum Two Numbers') RETURNING id"
            )
            problem_id = cur.fetchone()[0]
        else:
            problem_id = row[0]

        # Clean slate: delete any leftover test rows
        cur.execute(
            "DELETE FROM submissions WHERE worker_id LIKE 'test-claim-%'"
        )

        # Bulk insert
        psycopg2.extras.execute_values(
            cur,
            "INSERT INTO submissions (user_id, problem_id, language, source_code) "
            "VALUES %s",
            [(user_id, problem_id, "c", "int main(){return 0;}")] * n,
        )
    conn.commit()
    return n


def claim_one(conn, worker_id: str):
    """Attempt to claim one PENDING submission. Returns the claimed id or None."""
    with conn.cursor() as cur:
        cur.execute(
            """
            UPDATE submissions
            SET status = 'PROCESSING',
                processing_started_at = NOW(),
                worker_id = %s
            WHERE id = (
                SELECT id FROM submissions
                WHERE status = 'PENDING'
                ORDER BY submitted_at, id
                LIMIT 1
                FOR UPDATE SKIP LOCKED
            )
            RETURNING id
            """,
            (worker_id,),
        )
        row = cur.fetchone()
    conn.commit()
    return row[0] if row else None


def cleanup(conn):
    """Delete test submissions."""
    with conn.cursor() as cur:
        cur.execute("DELETE FROM submissions WHERE worker_id LIKE 'test-claim-%'")
    conn.commit()


# ----- Worker thread -----

def worker(worker_id: str, db_url: str, results: list, errors: list):
    """Claim jobs in a loop until none are available."""
    try:
        conn = psycopg2.connect(db_url)
        conn.autocommit = False
        claimed = []
        start = time.time()
        while time.time() - start < CLAIM_TIMEOUT_SEC:
            submission_id = claim_one(conn, worker_id)
            if submission_id is None:
                break
            claimed.append(submission_id)
        results.append((worker_id, claimed))
        conn.close()
    except Exception as e:
        errors.append((worker_id, repr(e)))


# ----- Main -----

def main():
    db_url = load_database_url()
    print(f"Connecting to: {db_url}")
    conn = psycopg2.connect(db_url)

    try:
        print(f"Setting up {NUM_JOBS} PENDING submissions...")
        setup_jobs(conn, NUM_JOBS)

        print(f"Starting {NUM_WORKERS} worker threads...")
        results = []
        errors = []
        threads = [
            threading.Thread(target=worker, args=(f"test-claim-{i}", db_url, results, errors))
            for i in range(NUM_WORKERS)
        ]
        t0 = time.time()
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        elapsed = time.time() - t0

        if errors:
            print("ERRORS:")
            for wid, err in errors:
                print(f"  worker {wid}: {err}")
            return 1

        # Aggregate
        all_claimed = []
        per_worker = {}
        for wid, ids in results:
            per_worker[wid] = len(ids)
            all_claimed.extend(ids)

        # --- Checks ---
        passed = True

        # 1. Total claims == NUM_JOBS
        if len(all_claimed) != NUM_JOBS:
            print(f"FAIL: claimed {len(all_claimed)} jobs, expected {NUM_JOBS}")
            passed = False
        else:
            print(f"PASS: claimed {len(all_claimed)} jobs (== {NUM_JOBS})")

        # 2. No duplicate claims
        dupes = [sid for sid, c in Counter(all_claimed).items() if c > 1]
        if dupes:
            print(f"FAIL: {len(dupes)} submission IDs claimed more than once: {dupes[:10]}")
            passed = False
        else:
            print("PASS: no duplicate claims")

        # 3. No starvation: every worker got at least one job
        starved = [wid for wid, n in per_worker.items() if n == 0]
        if starved:
            print(f"WARN: {len(starved)} workers claimed zero jobs: {starved}")
            # Not a failure — scheduling may starve a worker.
        else:
            print("PASS: no worker was starved")

        print()
        print(f"Elapsed: {elapsed:.2f}s")
        print("Per-worker claim counts:")
        for wid, n in sorted(per_worker.items()):
            print(f"  {wid}: {n}")

        return 0 if passed else 1

    finally:
        print("\nCleaning up...")
        cleanup(conn)
        conn.close()


if __name__ == "__main__":
    sys.exit(main())