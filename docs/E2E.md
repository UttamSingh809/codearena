# CodeArena — End-to-End Tests

The scripts under `scripts/e2e_*.sh` and `scripts/e2e_*.py` test the complete CodeArena pipeline:

**API → Queue → Worker → Sandbox → Database → API**

Each script starts the services it needs, runs its test scenario, and cleans up afterward.

## Prerequisites

Before running the tests, ensure the following requirements are met:

- **PostgreSQL:** Running with the database schema migrated and seed data loaded.
  ```bash
  ./db/migrate.sh --seed
  ```
- **API environment:** Python virtual environment created and dependencies installed.
  ```bash
  cd api
  python3 -m venv venv
  source venv/bin/activate
  pip install -r requirements.txt
  cd ..
  ```
- **Worker:** Built successfully.
  ```bash
  make -C os/worker
  ```
- **IPC binaries:** Built successfully.
  ```bash
  make -C os/ipc
  ```
- **Privileges:** `sudo` must be available because the worker and watchdog require root privileges for cgroup operations.
- **Configuration:** Ensure the database connection string and other required environment variables are configured.

## Test Scripts

| Script | Approximate Duration | Purpose |
|---|---:|---|
| `e2e_smoke.sh` | 5 seconds | Verifies that a single submission receives an Accepted (AC) verdict. |
| `e2e_verdicts.sh` | 30 seconds | Verifies that AC, WA, TLE, MLE, RE, and CE verdicts are reachable. |
| `e2e_concurrent.py [N]` | 30 seconds for N = 50 | Tests concurrent submissions and checks for lost or duplicate results. |
| `e2e_failure.sh` | 30 seconds | Tests worker crash recovery through the watchdog. |
| `e2e_stress.py [N] [C]` | 60 seconds for N = 200, C = 20 | Measures throughput under sustained submission load. |

*Durations are approximate and depend on hardware, database performance, and sandbox execution time.*

## Running the Tests

Run all commands from the repository root.

### 1. Smoke Test

Verifies the basic end-to-end submission and grading workflow.

```bash
bash scripts/e2e_smoke.sh
```

### 2. Verdict Coverage Test

Checks whether all six expected verdicts can be produced.

```bash
bash scripts/e2e_verdicts.sh
```

### 3. Concurrent Submission Test

Submits 50 jobs concurrently and checks that each submission is processed exactly once.

```bash
python3 scripts/e2e_concurrent.py 50
```

### 4. Failure Recovery Test

Simulates a worker crash and verifies that the watchdog recovers and requeues the affected submission.

```bash
bash scripts/e2e_failure.sh
```

### 5. Stress Test

Submits 200 jobs with up to 20 jobs in flight, measuring throughput and checking correctness.

```bash
python3 scripts/e2e_stress.py 200 20
```

## Interpreting Results

Each script exits with status code `0` on success and `1` on failure. Failed tests should print relevant service logs to help diagnose the problem.

### Smoke Test

Expected output:

```text
[TEST] verdict is AC
[PASS] verdict is AC
SMOKE TEST PASSED
```

Common failure causes:

- **Worker did not start:** Check `/tmp/codearena_e2e_worker.log`.
- **Unexpected WA or RE verdict:** Check source-code escaping and the submission payload.
- **Timeout:** Check whether the worker is stuck or the queue is empty.

### Verdict Coverage Test

All six verdicts must appear:

- **AC — Accepted:** The submission produces the expected output.
- **WA — Wrong Answer:** The output differs from the expected result.
- **TLE — Time Limit Exceeded:** The submission exceeds its time limit.
- **MLE — Memory Limit Exceeded:** The submission exceeds its memory limit.
- **RE — Runtime Error:** The submission terminates with a runtime error.
- **CE — Compilation Error:** The submission fails to compile.

If a verdict test fails, investigate the corresponding component:

| Verdict | Potential Cause |
|---|---|
| AC | Sandbox execution or database persistence failure |
| WA | Output comparison logic in `job_processor.c` |
| TLE | Wall-clock timeout or watchdog failure |
| MLE | Cgroup memory limit not applied correctly |
| RE | Runtime exit status not propagated correctly |
| CE | Compiler unavailable or output path misconfigured |

These are diagnostic starting points, not definitive causes.

### Concurrent Submission Test

Expected output:

```text
[PASS] all 50 verdicts are AC
[PASS] no submission has more than one result
[PASS] exactly 50 results in DB
```

If duplicate results appear, inspect transaction handling and row locking, including `SELECT ... FOR UPDATE`.

If submissions are missing, investigate queue handling, `SKIP LOCKED` behavior, descriptor delivery, and worker processing.

### Failure Recovery Test

Expected output:

```text
[PASS] worker claimed submission
[PASS] submission still PROCESSING after worker death
[PASS] watchdog requeued submission
[PASS] submission completed by second worker
```

If recovery fails:

- Check watchdog logs for stale-job detection.
- Verify that the watchdog requeues the submission correctly.
- Confirm that the queue writer successfully enqueues a descriptor.
- Verify that a second worker is running and can claim the requeued submission.

### Stress Test

The stress test reports throughput and correctness failures.

Indicative baseline figures for a four-core machine are:

| Metric | Approximate Baseline |
|---|---:|
| Submission throughput | 100–500 requests/second |
| Grading throughput | 2–20 jobs/second |
| Expected verdicts | All AC for valid test submissions |
| Duplicate results | None |

These figures are environment-dependent reference estimates, not guaranteed performance targets. Actual results depend on the HTTP server, database, queue implementation, compiler startup, and sandbox overhead.

## Cleanup

The scripts use `trap cleanup EXIT` to attempt cleanup when they terminate normally or encounter an error.

If a script is forcibly terminated with `kill -9`, manual cleanup may be necessary.

**Warning:** The following commands terminate matching processes and delete database records. Review the process matches and ensure the database contains only disposable test data before running them.

### 1. Stop Test Processes

```bash
sudo pkill -f codearena-worker
sudo pkill -f watchdog.watchdog
pkill -f "uvicorn src.main:app"
```

These patterns may match processes unrelated to the test. Inspect running processes first if other instances of CodeArena are active.

### 2. Remove Test Database Records

Run these commands only after confirming that the matching records belong exclusively to end-to-end tests.

```bash
psql "$DATABASE_URL" -c \
  "DELETE FROM results WHERE graded_by LIKE 'e2e-%';"

psql "$DATABASE_URL" -c \
  "DELETE FROM submissions WHERE worker_id LIKE 'e2e-%';"
```

If foreign-key constraints or transaction dependencies prevent deletion, inspect the schema and remove dependent test records in the appropriate order. Do not disable database constraints or use broader deletion filters as a workaround.

## Why These Tests Matter

- **Smoke test:** Establishes that the basic submission-to-verdict pipeline works.
- **Verdict coverage:** Checks that the grading system can produce the expected verdicts for different execution outcomes.
- **Concurrency test:** Detects lost submissions, duplicate results, and race conditions under concurrent workloads.
- **Failure recovery test:** Verifies recovery from a common operational failure: worker crashes.
- **Stress test:** Measures throughput and helps expose race conditions and correctness problems under sustained load.

Together, these tests provide coverage of the API, queue, worker, sandbox, and database integration.

## Running the Full Suite

From the repository root:

```bash
cd ~/projects/codearena

# 1. Smoke test
bash scripts/e2e_smoke.sh

# 2. Verdict coverage
bash scripts/e2e_verdicts.sh

# 3. Concurrent submissions
python3 scripts/e2e_concurrent.py 50

# 4. Failure recovery
bash scripts/e2e_failure.sh

# 5. Stress test
python3 scripts/e2e_stress.py 200 20
```

**Expected total runtime:** Approximately 3 minutes, depending on the environment.

### Completion Criteria

The end-to-end suite passes when:

- Every script exits with status code `0`.
- All six expected verdicts are produced by the verdict coverage test.
- Concurrent submissions produce no missing or duplicate results.
- The watchdog successfully recovers from the simulated worker failure.
- The stress test completes without correctness failures.

Passing the suite provides evidence that the tested end-to-end workflows are functioning correctly. It does not, by itself, guarantee production readiness, security, or performance under every possible workload.
