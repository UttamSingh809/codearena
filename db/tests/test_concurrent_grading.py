#!/usr/bin/env python3
"""
CodeArena — Concurrent grading test.

Demonstrates that the atomic grading transaction is race-free:

  1. Create N PROCESSING submissions.
  2. Spawn N grader threads, each trying to grade a specific submission.
  3. Each grader runs the full transaction:
       BEGIN
       SELECT ... FOR UPDATE
       INSERT INTO results ... ON CONFLICT DO NOTHING
       UPDATE submissions SET status='COMPLETED'
       COMMIT
  4. Verify:
       - Exactly N results exist (one per submission).
       - No submission has more than one result.
       - Every submission is COMPLETED.
       - No deadlocks.

Also runs a "double-grade race" variant: two threads try to grade
the SAME submission. Verify exactly one succeeds.

Run:
    python3 db/tests/test_concurrent_grading.py
"""

import os
import random
import sys
import threading
import time

try:
    import psycopg2
    import psycopg2.extras
except ImportError:
    print("ERROR: psycopg2 not installed. Run: pip install psycopg2-binary", file=sys.stderr)
    sys.exit(1)


# ----- Configuration -----

NUM_SUBMISSIONS = 100
NUM_GRADERS = 20
MAX_RETRIES = 5


# ----- Helpers -----

def load_database_url() -> str:
    url = os.environ.get("DATABASE_URL")
    if url:
        return url
    env_path = os.path.join(os.path.dirname(__file__), "..", "..", ".env")
    if os.path.exists(env_path):
        with open(env_path) as f:
            for line in f:
                if line.startswith("DATABASE_URL="):
                    return line.strip().split("=", 1)[1]
    raise RuntimeError("DATABASE_URL not set and not found in .env")


def setup(conn) -> list:
    """Create N PROCESSING submissions, return their IDs."""
    with conn.cursor() as cur:
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
            cur.execute("INSERT INTO problems (title) VALUES ('Sum Two Numbers') RETURNING id")
            problem_id = cur.fetchone()[0]
        else:
            problem_id = row[0]

        # Clean slate
        cur.execute("DELETE FROM results WHERE graded_by LIKE 'test-grade-%'")
        cur.execute("DELETE FROM submissions WHERE worker_id LIKE 'test-grade-%'")

        # Insert N submissions in PROCESSING state
        ids = []
        for i in range(NUM_SUBMISSIONS):
            cur.execute(
                """
                INSERT INTO submissions
                    (user_id, problem_id, language, source_code, status,
                     processing_started_at, worker_id)
                VALUES (%s, %s, 'c', 'int main(){return 0;}', 'PROCESSING',
                        NOW(), %s)
                RETURNING id
                """,
                (user_id, problem_id, f"test-grade-setup-{i}"),
            )
            ids.append(cur.fetchone()[0])
    conn.commit()
    return ids


def grade_one(conn, submission_id: int, worker_id: str) -> str:
    """
    Run the full atomic grading transaction on one submission.
    Returns: 'graded' | 'skipped' | 'error:<reason>'
    """
    for attempt in range(MAX_RETRIES):
        try:
            with conn.cursor() as cur:
                conn.autocommit = False

                # Step 1: lock + verify
                cur.execute(
                    "SELECT id, status, worker_id FROM submissions "
                    "WHERE id = %s FOR UPDATE",
                    (submission_id,),
                )
                row = cur.fetchone()
                if not row:
                    conn.rollback()
                    return "skipped"

                _id, status, current_worker = row
                if status != "PROCESSING":
                    conn.rollback()
                    return "skipped"

                # Step 2: insert result (UNIQUE + ON CONFLICT)
                cur.execute(
                    """
                    INSERT INTO results
                        (submission_id, verdict, score, runtime_ms, memory_kb, graded_by)
                    VALUES (%s, 'AC', 100, 42, 2048, %s)
                    ON CONFLICT (submission_id) DO NOTHING
                    """,
                    (submission_id, worker_id),
                )
                if cur.rowcount == 0:
                    conn.rollback()
                    return "skipped"

                # Step 3: mark completed
                cur.execute(
                    """
                    UPDATE submissions
                    SET status = 'COMPLETED',
                        processing_started_at = NULL,
                        worker_id = NULL
                    WHERE id = %s
                    """,
                    (submission_id,),
                )

                conn.commit()
                return "graded"

        except psycopg2.errors.DeadlockDetected:
            conn.rollback()
            # Exponential backoff with jitter
            delay = (0.01 * (2 ** attempt)) + random.uniform(0, 0.01)
            time.sleep(delay)
            continue

        except Exception as e:
            conn.rollback()
            return f"error:{e!r}"

    return "error:max_retries"


def cleanup(conn):
    with conn.cursor() as cur:
        cur.execute("DELETE FROM results WHERE graded_by LIKE 'test-grade-%'")
        cur.execute("DELETE FROM submissions WHERE worker_id LIKE 'test-grade-%' "
                    "OR source_code = 'int main(){return 0;}'")
    conn.commit()


# ----- Worker thread -----

def worker(worker_id: str, submission_ids: list, db_url: str, results: list, errors: list):
    try:
        conn = psycopg2.connect(db_url)
        outcomes = {}
        for sid in submission_ids:
            outcome = grade_one(conn, sid, worker_id)
            outcomes[sid] = outcome
        results.append((worker_id, outcomes))
        conn.close()
    except Exception as e:
        errors.append((worker_id, repr(e)))


# ----- Test 1: N graders, N submissions, each graded exactly once -----

def test_no_double_grade(db_url: str) -> bool:
    print("=" * 60)
    print(f"Test 1: {NUM_GRADERS} graders on {NUM_SUBMISSIONS} submissions")
    print("=" * 60)

    conn = psycopg2.connect(db_url)
    try:
        submission_ids = setup(conn)
        print(f"Created {len(submission_ids)} PROCESSING submissions")

        # Distribute submissions across graders — some overlap on purpose
        buckets = [[] for _ in range(NUM_GRADERS)]
        for i, sid in enumerate(submission_ids):
            buckets[i % NUM_GRADERS].append(sid)
        # Add some overlap: every submission also goes to the next grader
        for i, sid in enumerate(submission_ids):
            buckets[(i + 1) % NUM_GRADERS].append(sid)

        results = []
        errors = []
        threads = [
            threading.Thread(
                target=worker,
                args=(f"test-grade-{i}", buckets[i], db_url, results, errors),
            )
            for i in range(NUM_GRADERS)
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
                print(f"  {wid}: {err}")
            return False

        # Aggregate outcomes
        graded_count = 0
        skipped_count = 0
        error_count = 0
        for wid, outcomes in results:
            for sid, outcome in outcomes.items():
                if outcome == "graded":
                    graded_count += 1
                elif outcome == "skipped":
                    skipped_count += 1
                else:
                    error_count += 1
                    print(f"  {wid} on {sid}: {outcome}")

        # Verify against the database
        with conn.cursor() as cur:
            cur.execute(
                "SELECT COUNT(*) FROM results WHERE submission_id = ANY(%s)",
                (submission_ids,),
            )
            db_result_count = cur.fetchone()[0]

            cur.execute(
                "SELECT COUNT(*) FROM submissions WHERE id = ANY(%s) AND status = 'COMPLETED'",
                (submission_ids,),
            )
            db_completed_count = cur.fetchone()[0]

            cur.execute(
                "SELECT submission_id, COUNT(*) FROM results "
                "WHERE submission_id = ANY(%s) GROUP BY submission_id HAVING COUNT(*) > 1",
                (submission_ids,),
            )
            dupes = cur.fetchall()

        print(f"\nElapsed: {elapsed:.2f}s")
        print(f"Graded (thread-reported):  {graded_count}")
        print(f"Skipped (thread-reported): {skipped_count}")
        print(f"Errors:                    {error_count}")
        print(f"DB result count:           {db_result_count} (expected {NUM_SUBMISSIONS})")
        print(f"DB completed count:        {db_completed_count} (expected {NUM_SUBMISSIONS})")

        passed = True
        if db_result_count != NUM_SUBMISSIONS:
            print(f"FAIL: DB result count != {NUM_SUBMISSIONS}")
            passed = False
        else:
            print(f"PASS: exactly {NUM_SUBMISSIONS} results")
        if db_completed_count != NUM_SUBMISSIONS:
            print(f"FAIL: DB completed count != {NUM_SUBMISSIONS}")
            passed = False
        else:
            print(f"PASS: exactly {NUM_SUBMISSIONS} COMPLETED submissions")
        if dupes:
            print(f"FAIL: {len(dupes)} submissions with multiple results: {dupes[:5]}")
            passed = False
        else:
            print("PASS: no submission has more than one result")
        if error_count > 0:
            print(f"FAIL: {error_count} unexpected errors")
            passed = False

        return passed
    finally:
        print("\nCleaning up Test 1...")
        cleanup(conn)
        conn.close()


# ----- Test 2: two graders, same submission, one wins -----

def test_same_submission_race(db_url: str) -> bool:
    print()
    print("=" * 60)
    print("Test 2: two graders race on the SAME submission")
    print("=" * 60)

    conn = psycopg2.connect(db_url)
    try:
        with conn.cursor() as cur:
            cur.execute("SELECT id FROM users WHERE username = 'alice' LIMIT 1")
            user_id = cur.fetchone()[0]
            cur.execute("SELECT id FROM problems WHERE title = 'Sum Two Numbers' LIMIT 1")
            problem_id = cur.fetchone()[0]
            cur.execute("DELETE FROM results WHERE graded_by LIKE 'test-grade-%'")
            cur.execute("DELETE FROM submissions WHERE worker_id LIKE 'test-grade-%'")
            cur.execute(
                """
                INSERT INTO submissions
                    (user_id, problem_id, language, source_code, status,
                     processing_started_at, worker_id)
                VALUES (%s, %s, 'c', 'int main(){return 0;}', 'PROCESSING',
                        NOW(), 'test-grade-setup-race')
                RETURNING id
                """,
                (user_id, problem_id),
            )
            sid = cur.fetchone()[0]
        conn.commit()

        print(f"Submission id: {sid}")
        print("Firing 2 grader threads simultaneously...")

        results = []
        errors = []
        threads = [
            threading.Thread(
                target=worker,
                args=(f"test-grade-race-{i}", [sid], db_url, results, errors),
            )
            for i in range(2)
        ]
        for t in threads:
            t.start()
        for t in threads:
            t.join()

        if errors:
            print("ERRORS:", errors)
            return False

        graded = sum(1 for _, outcomes in results for o in outcomes.values() if o == "graded")
        skipped = sum(1 for _, outcomes in results for o in outcomes.values() if o == "skipped")

        print(f"Graded: {graded} (expected 1)")
        print(f"Skipped: {skipped} (expected 1)")

        with conn.cursor() as cur:
            cur.execute("SELECT COUNT(*) FROM results WHERE submission_id = %s", (sid,))
            db_count = cur.fetchone()[0]

        print(f"DB result count: {db_count} (expected 1)")

        return graded == 1 and skipped == 1 and db_count == 1

    finally:
        print("\nCleaning up Test 2...")
        cleanup(conn)
        conn.close()


# ----- Main -----

def main():
    db_url = load_database_url()
    print(f"Connecting to: {db_url}")

    t1 = test_no_double_grade(db_url)
    t2 = test_same_submission_race(db_url)

    print()
    print("=" * 60)
    print(f"Test 1 (no double grade):  {'PASS' if t1 else 'FAIL'}")
    print(f"Test 2 (same-sub race):    {'PASS' if t2 else 'FAIL'}")
    print("=" * 60)

    return 0 if (t1 and t2) else 1


if __name__ == "__main__":
    sys.exit(main())