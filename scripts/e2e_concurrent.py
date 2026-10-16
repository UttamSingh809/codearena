#!/usr/bin/env python3
"""
CodeArena — concurrent submission end-to-end test.

Submits N identical correct solutions concurrently through the API.
Verifies:
  1. All submissions are accepted (HTTP 201).
  2. All complete with verdict=AC.
  3. The DB has exactly N results, one per submission.
  4. No submission has duplicate results.

Run:
    python3 scripts/e2e_concurrent.py [N]
"""

import asyncio
import os
import sys
import time
import uuid

import asyncpg
import httpx


API_BASE = os.environ.get("API_BASE", "http://localhost:8000")
DATABASE_URL = os.environ.get(
    "DATABASE_URL",
    "postgresql://codearena:codearena@localhost:5432/codearena",
)
DEFAULT_N = 50


# ------------------------------------------------------------------
# Colours
# ------------------------------------------------------------------

RED = "\033[0;31m"
GREEN = "\033[0;32m"
BLUE = "\033[0;34m"
YELLOW = "\033[1;33m"
NC = "\033[0m"


PASS = 0
FAIL = 0


def test_start(name: str):
    print(f"\n{BLUE}[TEST]{NC} {name}")


def test_pass(msg: str):
    global PASS
    PASS += 1
    print(f"  {GREEN}[PASS]{NC} {msg}")


def test_fail(msg: str):
    global FAIL
    FAIL += 1
    print(f"  {RED}[FAIL]{NC} {msg}")


def test_info(msg: str):
    print(f"  {YELLOW}[INFO]{NC} {msg}")


# ------------------------------------------------------------------
# Main
# ------------------------------------------------------------------

SOURCE = """#include <stdio.h>
int main(){long a,b;scanf("%ld %ld",&a,&b);printf("%ld\\n",a+b);return 0;}"""


async def register_user(client: httpx.AsyncClient, suffix: str):
    username = f"e2e_conc_{suffix}"
    email = f"e2e_conc_{suffix}@example.com"
    password = "passw0rd123"

    resp = await client.post(
        f"{API_BASE}/api/auth/register",
        json={"username": username, "email": email, "password": password},
    )
    resp.raise_for_status()
    return resp.json()["access_token"]


async def submit_one(client: httpx.AsyncClient, token: str, problem_id: int):
    resp = await client.post(
        f"{API_BASE}/api/submissions",
        headers={"Authorization": f"Bearer {token}"},
        json={
            "problem_id": problem_id,
            "language": "c",
            "source_code": SOURCE,
        },
    )
    if resp.status_code != 201:
        return None, resp.text
    return resp.json()["id"], None


async def get_verdict(client: httpx.AsyncClient, token: str, sub_id: int):
    resp = await client.get(
        f"{API_BASE}/api/submissions/{sub_id}",
        headers={"Authorization": f"Bearer {token}"},
    )
    if resp.status_code != 200:
        return None, None
    data = resp.json()
    status = data.get("status", "")
    result = data.get("result")
    verdict = result["verdict"] if result else None
    return status, verdict


async def wait_all(client, token, sub_ids, timeout=120):
    """Wait for all submissions to reach a terminal state."""
    start = time.time()
    results = {sid: None for sid in sub_ids}
    pending = set(sub_ids)

    while pending and (time.time() - start) < timeout:
        await asyncio.sleep(1)
        for sid in list(pending):
            status, verdict = await get_verdict(client, token, sid)
            if status in ("COMPLETED", "FAILED"):
                results[sid] = verdict or status
                pending.discard(sid)

    return results, pending


async def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_N

    print("=" * 60)
    print(f"  CodeArena — Concurrent Submission Test (N={n})")
    print("=" * 60)
    print(f"API: {API_BASE}")

    suffix = f"{int(time.time())}_{os.getpid()}"

    async with httpx.AsyncClient(timeout=30.0, limits=httpx.Limits(max_connections=n + 10)) as client:
        # Register
        test_start("Register user")
        try:
            token = await register_user(client, suffix)
            test_pass(f"user registered, token received")
        except Exception as e:
            test_fail(f"registration failed: {e}")
            return 1

        # Concurrent submissions
        test_start(f"Submit {n} solutions concurrently")
        t0 = time.time()
        tasks = [submit_one(client, token, 1) for _ in range(n)]
        results = await asyncio.gather(*tasks)
        elapsed = time.time() - t0

        submitted_ids = []
        failures = 0
        for sid, err in results:
            if sid is None:
                failures += 1
            else:
                submitted_ids.append(sid)

        if failures == 0:
            test_pass(f"{n} submissions accepted in {elapsed:.2f}s "
                      f"({n / elapsed:.1f}/s)")
        else:
            test_fail(f"{failures}/{n} submissions rejected")

        if not submitted_ids:
            return 1

        # Wait for verdicts
        test_start("Wait for all verdicts")
        verdicts, still_pending = await wait_all(client, token, submitted_ids, timeout=180)

        if still_pending:
            test_fail(f"{len(still_pending)} submissions never completed")
        else:
            test_pass(f"all {len(verdicts)} submissions reached terminal state")

        # Verify all AC
        non_ac = {sid: v for sid, v in verdicts.items() if v != "AC"}
        if non_ac:
            test_fail(f"{len(non_ac)} submissions not AC: "
                      f"{list(non_ac.items())[:5]}")
        else:
            test_pass(f"all {len(verdicts)} verdicts are AC")

        # Verify DB state
        test_start("Verify database consistency")
        conn = await asyncpg.connect(DATABASE_URL)
        try:
            # Every submitted id has exactly one result
            rows = await conn.fetch(
                """
                SELECT submission_id, COUNT(*) AS c
                FROM results
                WHERE submission_id = ANY($1::bigint[])
                GROUP BY submission_id
                """,
                submitted_ids,
            )
            counts = {r["submission_id"]: r["c"] for r in rows}

            duplicate_results = [sid for sid, c in counts.items() if c > 1]
            missing_results = [sid for sid in submitted_ids if sid not in counts]

            if duplicate_results:
                test_fail(f"{len(duplicate_results)} submissions have >1 result")
            else:
                test_pass("no submission has more than one result")

            if missing_results:
                test_fail(f"{len(missing_results)} submissions have no result")
            else:
                test_pass(f"exactly {len(submitted_ids)} results in DB")

            # All submissions are COMPLETED
            completed = await conn.fetchval(
                """
                SELECT COUNT(*) FROM submissions
                WHERE id = ANY($1::bigint[]) AND status = 'COMPLETED'
                """,
                submitted_ids,
            )
            if completed == len(submitted_ids):
                test_pass(f"all {completed} submissions marked COMPLETED")
            else:
                test_fail(f"only {completed}/{len(submitted_ids)} marked COMPLETED")
        finally:
            await conn.close()

        # Summary
        print()
        print("=" * 60)
        print(f"  Passed: {GREEN}{PASS}{NC}")
        print(f"  Failed: {RED}{FAIL}{NC}")
        print("=" * 60)

        return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))