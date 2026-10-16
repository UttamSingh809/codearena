#!/usr/bin/env python3
"""
CodeArena — sustained load stress test.

Submits N solutions with a bounded concurrency level, then waits
for all to complete. Measures:
  - Submission throughput (requests/sec accepted)
  - Grading throughput (jobs/sec completed)
  - End-to-end latency percentiles
  - Correctness (all AC, no duplicate results)

Run:
    python3 scripts/e2e_stress.py [N] [CONCURRENCY]

Defaults: N=200, CONCURRENCY=20
"""

import asyncio
import os
import statistics
import sys
import time

import asyncpg
import httpx


API_BASE = os.environ.get("API_BASE", "http://localhost:8000")
DATABASE_URL = os.environ.get(
    "DATABASE_URL",
    "postgresql://codearena:codearena@localhost:5432/codearena",
)

DEFAULT_N = 200
DEFAULT_CONC = 20


SOURCE = """#include <stdio.h>
int main(){long a,b;scanf("%ld %ld",&a,&b);printf("%ld\\n",a+b);return 0;}"""


RED = "\033[0;31m"
GREEN = "\033[0;32m"
BLUE = "\033[0;34m"
YELLOW = "\033[1;33m"
NC = "\033[0m"


def test_start(name: str):
    print(f"\n{BLUE}[TEST]{NC} {name}")


def test_pass(msg: str):
    print(f"  {GREEN}[PASS]{NC} {msg}")


def test_fail(msg: str):
    print(f"  {RED}[FAIL]{NC} {msg}")


def test_info(msg: str):
    print(f"  {YELLOW}[INFO]{NC} {msg}")


async def register(client: httpx.AsyncClient, suffix: str) -> str:
    r = await client.post(
        f"{API_BASE}/api/auth/register",
        json={
            "username": f"e2e_stress_{suffix}",
            "email": f"e2e_stress_{suffix}@example.com",
            "password": "passw0rd123",
        },
    )
    r.raise_for_status()
    return r.json()["access_token"]


async def submit(
    client: httpx.AsyncClient,
    token: str,
    sem: asyncio.Semaphore,
    results: list,
):
    async with sem:
        t0 = time.time()
        try:
            r = await client.post(
                f"{API_BASE}/api/submissions",
                headers={"Authorization": f"Bearer {token}"},
                json={
                    "problem_id": 1,
                    "language": "c",
                    "source_code": SOURCE,
                },
            )
            dt = time.time() - t0
            if r.status_code == 201:
                results.append((r.json()["id"], dt, None))
            else:
                results.append((None, dt, f"HTTP {r.status_code}: {r.text[:100]}"))
        except Exception as e:
            results.append((None, time.time() - t0, repr(e)))


async def poll_verdict(client, token, sub_id):
    r = await client.get(
        f"{API_BASE}/api/submissions/{sub_id}",
        headers={"Authorization": f"Bearer {token}"},
    )
    if r.status_code != 200:
        return None, None
    d = r.json()
    status = d.get("status", "")
    result = d.get("result")
    return status, result["verdict"] if result else None


async def wait_all(client, token, sub_ids, timeout=300):
    """Poll every 2s until all submissions are terminal."""
    pending = set(sub_ids)
    verdicts = {}
    start = time.time()
    while pending and (time.time() - start) < timeout:
        await asyncio.sleep(2)
        # Poll all in parallel
        tasks = {sid: asyncio.create_task(poll_verdict(client, token, sid))
                 for sid in pending}
        for sid, task in tasks.items():
            status, verdict = await task
            if status in ("COMPLETED", "FAILED"):
                verdicts[sid] = verdict or status
                pending.discard(sid)
    return verdicts, pending


def percentile(data, p):
    if not data:
        return 0
    s = sorted(data)
    k = int(len(s) * p / 100)
    k = min(k, len(s) - 1)
    return s[k]


async def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_N
    conc = int(sys.argv[2]) if len(sys.argv) > 2 else DEFAULT_CONC

    print("=" * 60)
    print(f"  CodeArena — Stress Test (N={n}, concurrency={conc})")
    print("=" * 60)

    suffix = f"{int(time.time())}_{os.getpid()}"

    async with httpx.AsyncClient(
        timeout=30.0,
        limits=httpx.Limits(max_connections=conc + 10),
    ) as client:
        test_start("Register user")
        token = await register(client, suffix)
        test_pass("registered")

        # ---- Submit phase ----
        test_start(f"Submit {n} solutions at concurrency={conc}")
        sem = asyncio.Semaphore(conc)
        results = []
        t0 = time.time()
        tasks = [submit(client, token, sem, results) for _ in range(n)]
        await asyncio.gather(*tasks)
        submit_elapsed = time.time() - t0

        submitted = [r[0] for r in results if r[0] is not None]
        failed = [r for r in results if r[0] is None]

        test_info(f"submit phase: {submit_elapsed:.2f}s "
                  f"({len(submitted)/submit_elapsed:.1f} req/s)")
        if failed:
            test_fail(f"{len(failed)} submissions failed")
            for f in failed[:3]:
                print(f"      {f[2]}")
        else:
            test_pass(f"all {n} submissions accepted")

        # Submission latency percentiles
        latencies = [r[1] * 1000 for r in results if r[0] is not None]  # ms
        if latencies:
            test_info(f"submit latency p50={percentile(latencies, 50):.0f}ms "
                      f"p95={percentile(latencies, 95):.0f}ms "
                      f"p99={percentile(latencies, 99):.0f}ms "
                      f"max={max(latencies):.0f}ms")

        # ---- Wait phase ----
        test_start("Wait for all verdicts")
        t0 = time.time()
        verdicts, pending = await wait_all(client, token, submitted, timeout=300)
        grade_elapsed = time.time() - t0
        test_info(f"grade phase: {grade_elapsed:.2f}s "
                  f"({len(verdicts)/grade_elapsed:.1f} jobs/s)")

        if pending:
            test_fail(f"{len(pending)} submissions never completed "
                      f"(timeout after 300s)")
        else:
            test_pass(f"all {len(verdicts)} submissions completed")

        # ---- Correctness ----
        test_start("Correctness")
        non_ac = {sid: v for sid, v in verdicts.items() if v != "AC"}
        if non_ac:
            test_fail(f"{len(non_ac)} verdicts not AC: "
                      f"{list(non_ac.items())[:5]}")
        else:
            test_pass(f"all {len(verdicts)} verdicts AC")

        # ---- DB consistency ----
        test_start("DB consistency")
        conn = await asyncpg.connect(DATABASE_URL)
        try:
            dup = await conn.fetchval(
                """
                SELECT COUNT(*) FROM (
                    SELECT submission_id
                    FROM results
                    WHERE submission_id = ANY($1::bigint[])
                    GROUP BY submission_id
                    HAVING COUNT(*) > 1
                ) t
                """,
                submitted,
            )
            if dup == 0:
                test_pass("no duplicate results")
            else:
                test_fail(f"{dup} submissions have duplicate results")
        finally:
            await conn.close()

        # ---- Summary ----
        print()
        print("=" * 60)
        print(f"  Total submissions: {n}")
        print(f"  Completed:         {len(verdicts)}")
        print(f"  Submit throughput: {n / submit_elapsed:.1f} req/s")
        print(f"  Grade throughput:  {len(verdicts) / grade_elapsed:.1f} jobs/s")
        print(f"  E2E wall time:     {submit_elapsed + grade_elapsed:.1f}s")
        print("=" * 60)

        return 0 if not pending and not non_ac else 1


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))