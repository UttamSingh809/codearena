<div align="center">

# CodeArena

> A secure, concurrent sandboxed code evaluation platform built from scratch. Every OS primitive is a raw syscall, every database write is hand-written SQL. No Docker, no ORM, no framework that hides the kernel.

[![Typing SVG](https://readme-typing-svg.demolab.com?font=Fira+Code&weight=600&size=22&pause=1000&color=36BCF7&center=true&vCenter=true&width=850&lines=Linux+Namespaces+%2B+cgroups+v2+%2B+seccomp-BPF;POSIX+Shared+Memory+%2B+Semaphores;PostgreSQL+ACID+Transactions;C+%2B+Python+%2B+FastAPI+%2B+Vanilla+JS)](https://git.io/typing-svg)

[![My Skills](https://skillicons.dev/icons?i=c,python,fastapi,postgres,linux,bash,git,github,vim,vscode)](https://skillicons.dev)

[![Linux](https://img.shields.io/badge/Linux-5.4%2B-000000?style=flat-square&logo=linux&logoColor=white)](https://www.kernel.org/)
[![PostgreSQL](https://img.shields.io/badge/PostgreSQL-15%2B-336791?style=flat-square&logo=postgresql&logoColor=white)](https://www.postgresql.org/)
[![Python](https://img.shields.io/badge/Python-3.11%2B-3776AB?style=flat-square&logo=python&logoColor=white)](https://www.python.org/)
[![C](https://img.shields.io/badge/C-C11-A8B9CC?style=flat-square&logo=c&logoColor=white)](https://en.wikipedia.org/wiki/C11_(C_standard_revision))
[![FastAPI](https://img.shields.io/badge/FastAPI-0.115-009688?style=flat-square&logo=fastapi&logoColor=white)](https://fastapi.tiangolo.com/)
[![License](https://img.shields.io/badge/License-MIT-yellow?style=flat-square)](LICENSE)

[![Build](https://img.shields.io/badge/build-passing-brightgreen?style=flat-square)]()
[![Tests](https://img.shields.io/badge/tests-38%20passing-brightgreen?style=flat-square)]()
[![Verdicts](https://img.shields.io/badge/verdicts-AC%20WA%20TLE%20MLE%20RE%20CE-blue?style=flat-square)]()

</div>

## What It Is

CodeArena accepts code submissions over HTTP, executes each one in a Linux sandbox that combines **namespaces**, **cgroups v2**, **seccomp-BPF**, **rlimits**, and a **wall-clock watchdog**, compares the program's output to expected outputs, and stores the verdict in PostgreSQL under an ACID transaction.

The design is deliberately pedagogical:

- **The sandbox is C.** Every isolation layer is a raw syscall, not a library that wraps it.
- **The database is PostgreSQL.** Every query is hand-written SQL with `$N` placeholders. No ORM.
- **The IPC is a POSIX shared-memory ring buffer.** Producers and consumers synchronize with three named semaphores.
- **The grading transaction is explicit.** `BEGIN`, `SELECT ... FOR UPDATE`, `INSERT`, `UPDATE`, `COMMIT`.

The result is a working end-to-end judge that also doubles as a case study in process management, inter-process communication, resource isolation, transactional consistency, and analytical SQL.

---

## Why Build It

Production online judges hide their internals behind container runtimes and ORMs. That's the right engineering choice at scale, but it hides the very concepts that systems courses aim to teach.

CodeArena inverts that. Docker is explicitly out of scope. So is SQLAlchemy, Django ORM, Prisma, and anything else that would obscure what the kernel and the database are actually doing.

The trade-off: a bit more code to write, a lot more to learn.

---

## Architecture

### High-Level Design

```mermaid
flowchart LR
    Client["Client<br/>(Browser / CLI)"] -->|HTTPS / REST / JWT| API

    subgraph API_LAYER["API Layer — FastAPI"]
        API --> Auth["/auth"]
        API --> Problems["/problems"]
        API --> Submit["/submit"]
        API --> Leaderboard["/leaderboard"]
        Auth --> Pool["asyncpg pool<br/>Raw SQL · no ORM"]
        Problems --> Pool
        Submit --> Pool
        Leaderboard --> Pool
    end

    Pool <--> DB[("PostgreSQL<br/>System of Record")]
    Submit -->|enqueue job| Queue["POSIX shared-memory queue<br/>/dev/shm/codearena_queue<br/>Ring buffer + mtx / empty / full semaphores"]
    Queue -->|dequeue job| Workers

    subgraph WORKERS["Worker Pool — C"]
        Workers["Worker 1 · Worker 2 · … · Worker N"] --> Sandbox
        subgraph SANDBOX["Per-job Linux Sandbox"]
            Sandbox["Namespaces<br/>cgroups v2<br/>seccomp-BPF<br/>rlimits<br/>pipe capture<br/>wall-clock watchdog"]
        end
        Sandbox --> Commit["Atomic result transaction<br/>BEGIN → SELECT FOR UPDATE<br/>INSERT results + per-case results<br/>UPDATE submission → COMMIT"]
    end
    Commit --> DB
    UI["Static UI assets<br/>HTML / CSS / JS"] <--> Client
    Watchdog["Python Watchdog<br/>every 2 s: stale jobs<br/>requeue / fail; clean cgroups + temp dirs"] -. monitors .-> DB
    Watchdog -. cleanup .-> Sandbox
```

### Component Diagram

| Component | Language | Responsibility |
|-----------|----------|----------------|
| **API** | Python 3.11 + FastAPI | HTTP endpoints, JWT auth, request validation, DB queries, queue enqueue |
| **UI** | HTML + vanilla JS | Login, problem list, editor, submission view, leaderboard |
| **Worker** | C (links against sandbox + queue + libpq) | Claim jobs, run sandbox, commit results |
| **Sandbox** | C | Isolated execution: namespaces, cgroups, seccomp, rlimits, watchdog |
| **IPC Queue** | C | POSIX shm ring buffer with semaphore synchronization |
| **Watchdog** | Python | Stale-job detection, cgroup/temp cleanup |
| **Database** | PostgreSQL 15+ | Persistent storage with ACID transactions |

### Data Flow

```mermaid
flowchart LR
    A["User submits code"] --> B["API validates with Pydantic<br/>Insert PENDING submission in PostgreSQL"]
    B --> C["Enqueue job descriptor<br/>POSIX shared-memory ring buffer"]
    C --> D["Worker dequeues job<br/>sem_wait(full)"]
    D --> E["Claim submission in PostgreSQL<br/>status = PROCESSING"]
    E --> F["Fetch source code and test cases"]
    F --> G["Create per-job sandbox<br/>cgroup + pipes + namespaces + seccomp"]
    G --> H["Compile if required<br/>execute with resource limits"]
    H --> I["Capture output and compare<br/>compute verdict"]
    I --> J["Commit atomically<br/>results + per-case results + submission status"]
    J --> K["API returns verdict<br/>GET /api/submissions/{id}"]
```

---

## Sequence Diagrams

### Submission Lifecycle

```mermaid
sequenceDiagram
    autonumber
    actor User
    participant API
    participant Queue as POSIX SHM Queue
    participant Worker
    participant Sandbox
    participant DB as PostgreSQL

    User->>API: Submit source code
    API->>DB: INSERT submission (PENDING)
    DB-->>API: Submission ID
    API->>Queue: Enqueue job descriptor
    API-->>User: 201 Created
    Queue-->>Worker: Dequeue job
    Worker->>DB: Claim submission
    DB-->>Worker: PROCESSING
    Worker->>Sandbox: Fork and execute
    Sandbox-->>Worker: Exit status and captured output
    Worker->>DB: Commit grading transaction
    DB-->>Worker: Commit OK
    User->>API: Poll submission status
    API->>DB: SELECT submission and verdict
    DB-->>API: COMPLETED + verdict
    API-->>User: Submission result
```

### Grading Transaction

The commit is a single transaction. Every step either succeeds or the entire transaction rolls back, leaving the submission in `PROCESSING` for the watchdog to requeue.

```
BEGIN;

  -- Lock the submission row. Any concurrent grader will block here.
  SELECT id, status, worker_id
  FROM submissions
  WHERE id = $1
  FOR UPDATE;

  -- If another worker already committed, this row would be COMPLETED
  -- and the application would ROLLBACK. Otherwise:

  INSERT INTO results
    (submission_id, verdict, score, runtime_ms, memory_kb, graded_by,
     test_total, test_passed, first_failed)
  VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9)
  ON CONFLICT (submission_id) DO NOTHING;

  INSERT INTO test_case_results
    (submission_id, test_case_id, ordinal, verdict, actual_output,
     runtime_ms, memory_kb)
  VALUES ($1, $2, $3, $4, $5, $6, $7)
  ON CONFLICT (submission_id, test_case_id) DO NOTHING;
  -- (one insert per test case)

  UPDATE submissions
  SET status = 'COMPLETED',
      processing_started_at = NULL,
      worker_id = NULL,
      test_current = NULL
  WHERE id = $1;

COMMIT;
```

**Correctness guarantees:**

- The `FOR UPDATE` lock plus the `UNIQUE (submission_id)` constraint on `results` makes double-grading impossible.
- If the worker crashes between `BEGIN` and `COMMIT`, PostgreSQL rolls back automatically. The submission stays `PROCESSING` and the watchdog requeues it.
- All per-test-case inserts are in the same transaction. There's no partial state where some test cases are recorded and others aren't.

### Failure Recovery

```mermaid
flowchart LR
    A["Worker claims job<br/>status = PROCESSING"] --> B["Worker crashes<br/>SIGKILL / segfault / OOM"]
    B --> C["Submission row remains PROCESSING<br/>processing_started_at is set"]
    C --> D["Watchdog scans stale jobs<br/>every 2 seconds<br/>older than lease threshold"]
    D --> E{"retry_count < MAX_RETRY?"}
    E -- Yes --> F["Set status = PENDING<br/>increment retry_count"]
    F --> G["Re-enqueue job descriptor"]
    G --> H["Another worker claims and grades job"]
    E -- No --> I["Set status = FAILED<br/>insert FAILED result"]
```

---

## System Design Deep Dive

### OS Sandbox Layer

Every submission runs inside a Linux sandbox that applies six isolation layers. Each layer is independently enforced by the kernel and composes with the others.

**1. rlimits (`setrlimit`)** — per-process hard limits set in the child before `execve`:

| Limit | Purpose |
|-------|---------|
| `RLIMIT_CPU` | CPU seconds; SIGXCPU on exceed |
| `RLIMIT_FSIZE` | Max bytes writable to any file; SIGXFSZ |
| `RLIMIT_NPROC` | Max processes/threads (per UID); fork returns EAGAIN |
| `RLIMIT_STACK` | Stack size cap |
| `RLIMIT_NOFILE` | Max open file descriptors |
| `RLIMIT_CORE` | Core dump size — always 0 |

**2. cgroups v2** — per-submission cgroup under `/sys/fs/cgroup/codearena/`:

| File | Purpose |
|------|---------|
| `cpu.max` | CPU bandwidth as a rate (`<quota> <period>`) |
| `memory.max` | RSS hard limit; kernel OOM-kills on exceed |
| `memory.swap.max` | Set to 0 to prevent swap from hiding memory overrun |
| `pids.max` | Process count cap that works even for root |

**3. Namespaces (`unshare(CLONE_NEW*)`)** — six namespaces isolate the child from the host:

| Namespace | Isolates |
|-----------|----------|
| PID | Process IDs; child sees itself as PID 1 |
| Mount | Filesystem tree; `pivot_root` swaps the root |
| Network | Network stack; no interfaces unless requested |
| UTS | hostname and domain name |
| IPC | SysV IPC and POSIX message queues |
| User | UID/GID mapping; unprivileged root inside the namespace |

**4. seccomp-BPF** — a whitelist filter that kills the process on any non-whitelisted syscall. Two profiles ship:

- **C profile** — the minimal set for statically or dynamically linked C/C++ programs.
- **Python profile** — superset for CPython.

Dangerous syscalls (`ptrace`, `mount`, `bpf`, `userfaultfd`, `kexec_load`, `init_module`, `reboot`, `swapon`, `perf_event_open`, `open_by_handle_at`) are never allowed. The action is `SCMP_ACT_KILL_PROCESS` — a SIGSYS terminates the process before the kernel even executes the syscall.

**5. Output capture** — `poll()` on two pipes reads stdout and stderr in parallel, avoiding the classic deadlock where a child blocked writing to stderr is never read because the parent is blocked reading stdout. Buffers are capped at `output_limit` bytes and the pipe is drained after the cap so the child never blocks.

**6. Wall-clock watchdog** — a `pthread` runs `watchdog_wait` concurrently with output capture. When the wall-clock deadline fires, it sends SIGTERM, waits a grace period, then SIGKILL. This catches programs that `sleep`, `pause`, or block on I/O — cases where `RLIMIT_CPU` never fires.

### IPC Queue Layer

The work queue is a **POSIX shared-memory ring buffer** with three named semaphores.

**Layout:**

```mermaid
flowchart LR
    A["/dev/shm/codearena_queue"] --> B["queue_header_t<br/>fixed 80 bytes"]
    B --> C["magic · version · capacity<br/>head · tail · count<br/>total_enqueued · total_dequeued"]
    A --> D["job_descriptor_t × capacity"]
    D --> E["submission_id · problem_id · language<br/>time_limit_ms · memory_limit_mb<br/>output_limit_kb · retry_count<br/>enqueue_time_ns · worker_id · magic"]
```

**Synchronization** — the classic three-semaphore bounded-buffer:

```mermaid
sequenceDiagram
    participant P as Producer
    participant E as empty semaphore
    participant M as mtx semaphore
    participant R as Ring buffer
    participant F as full semaphore
    participant C as Consumer

    P->>E: sem_wait(empty)
    P->>M: sem_wait(mtx)
    P->>R: Write ring[tail], advance tail, increment count
    P->>M: sem_post(mtx)
    P->>F: sem_post(full)

    C->>F: sem_wait(full)
    C->>M: sem_wait(mtx)
    C->>R: Read ring[head], advance head, decrement count
    C->>M: sem_post(mtx)
    C->>E: sem_post(empty)
```

**Why POSIX shm:** zero-copy descriptor transfer, explicit synchronization, kernel-managed semaphores. It's the fastest bounded-buffer primitive that doesn't require a server process.

**Why not a message broker (Redis, RabbitMQ):** they hide the producer-consumer synchronization problem that the project is meant to teach.

**Why `magic` and `version`:** detect stale or corrupted segments from previous runs. If they don't match, the queue refuses to open.

### Worker Pool

Each worker is a C process that loops:

1. **`queue_dequeue`** — block on the queue's `full` semaphore.
2. **Claim the submission** in PostgreSQL:
   ```sql
   UPDATE submissions
   SET status='PROCESSING', processing_started_at=NOW(), worker_id=$1
   WHERE id = (
       SELECT id FROM submissions
       WHERE status='PENDING'
       ORDER BY submitted_at, id
       LIMIT 1
       FOR UPDATE SKIP LOCKED
   )
   RETURNING ...
   ```
   `SKIP LOCKED` means two workers never block on the same row; the second immediately gets the next candidate.

3. **Fetch test cases** for the problem.
4. **Write the source code** to a per-job temp directory.
5. **Compile** (C/C++ only) with `setrlimit(RLIMIT_CPU=10, RLIMIT_AS=512MB, RLIMIT_NPROC=32)` on the compiler process.
6. **For each test case:**
   - Update `submissions.test_current` and `test_passed` for the UI's live progress bar.
   - Run the sandbox: fork, isolate, limit, execute, capture, compare.
   - Record the per-case verdict.
   - **Short-circuit on terminal verdicts** (TLE, MLE, RE, CE) — those cannot pass a later case.
7. **Commit the result atomically** (see the Grading Transaction sequence above).

**Heartbeat.** A background thread bumps `processing_started_at` every 30 seconds so the watchdog doesn't requeue a submission that's genuinely running but slow.

### Database Layer

Schema is normalized to **third normal form**. There are six tables:

| Table | Purpose |
|-------|---------|
| `users` | Registered users with bcrypt password hashes and a role enum |
| `problems` | Problem statements, limits, difficulty, active flag |
| `test_cases` | Input/expected output pairs, ordered by `ordinal`, sample flag, points |
| `submissions` | One row per submission; status lifecycle PENDING → PROCESSING → COMPLETED/FAILED/CANCELLED |
| `results` | One row per graded submission (1:1 via UNIQUE), verdict, score, runtime, memory, stdout, stderr |
| `test_case_results` | One row per (submission, test case): verdict, actual output, runtime, memory |

Plus one materialized view:

- **`leaderboard`** — one row per (user, problem) with best score, best runtime, best memory, accepted count, total submissions. `REFRESH MATERIALIZED VIEW CONCURRENTLY` supports non-blocking refresh, backed by a `UNIQUE (user_id, problem_id)` index.

**Indexes** are chosen for specific queries:

| Index | Query |
|-------|-------|
| `idx_submissions_pending_fifo` (partial, WHERE status='PENDING') | Worker claim |
| `idx_submissions_stale` (partial, WHERE status='PROCESSING') | Watchdog stale detection |
| `idx_submissions_user_time` | User submission history |
| `idx_submissions_problem_time` | Per-problem submissions |
| `idx_results_verdict` | Verdict distribution |
| `idx_results_graded_at` | Recent activity |
| `idx_leaderboard_ranking` (partial, WHERE best_score > 0) | Ranking order |
| `idx_tcr_submission_ordinal` | Per-case results fetch |

**Concurrency guarantees** — verified in `db/tests/`:

- **No double-grades.** The grading transaction locks the submission with `FOR UPDATE`. The `UNIQUE (submission_id)` constraint is the belt to the lock's suspenders.
- **No lost updates.** Requeue uses `UPDATE ... SET retry_count = retry_count + 1` — an atomic increment, not a read-modify-write.
- **No phantom reads in critical paths.** REPEATABLE READ where consistency across statements matters.
- **Deadlock impossible in the grading path.** Every `FOR UPDATE` locks a single row; nothing locks two rows in inconsistent order.

### API Layer

FastAPI + asyncpg. Every endpoint is documented at `/docs` (OpenAPI).

**Endpoints:**

| Method | Path | Auth | Purpose |
|--------|------|------|---------|
| POST | `/api/auth/register` | No | Create user, return JWT |
| POST | `/api/auth/login` | No | Authenticate, return JWT |
| GET | `/api/auth/me` | Yes | Current user profile |
| GET | `/api/problems` | Yes | List problems |
| GET | `/api/problems/{id}` | Yes | Problem detail |
| POST | `/api/submissions` | Yes | Create PENDING, enqueue |
| GET | `/api/submissions/{id}` | Yes | Submission + result |
| GET | `/api/submissions/{id}/testcases` | Yes | Per-test-case breakdown |
| GET | `/api/submissions` | Yes | User's submission history |
| GET | `/api/leaderboard` | Yes | Global ranking |
| GET | `/api/leaderboard/problem/{id}` | Yes | Per-problem ranking |
| GET | `/api/health` | No | DB readiness probe |

**Auth** — JWT with a 60-minute expiry. Passwords are hashed with bcrypt (12 rounds). Login is constant-time: the API always runs `bcrypt.checkpw`, even when the user doesn't exist, to avoid timing leaks.

**Validation** — Pydantic v2 enforces field constraints at the boundary. A submission with an unsupported language or a source code over 64 KB returns 422 before touching the DB.

**Rate limiting** — `slowapi` provides per-IP limits on `/register` and `/login`, per-user limits on `/submit`.

### UI Layer

No build step, no framework. Seven HTML pages, one CSS file, one JS file.

| Page | Purpose |
|------|---------|
| `index.html` | Split-screen login / register |
| `dashboard.html` | Landing page with stats and recent submissions |
| `problems.html` | Filterable problem list |
| `problem.html` | Split-view: statement left, editor right, live verdict |
| `submission.html` | Rich verdict display with test case breakdown |
| `history.html` | Filterable submission history |
| `leaderboard.html` | Global ranking with medals |

**Interaction features:**

- JWT stored in `localStorage`.
- Live polling on the submission page (2 s interval) until the verdict is terminal.
- Per-problem code drafts saved in `localStorage`, restored on reload.
- Toast notifications for success/error.
- Verdict badges with human-readable labels ("Accepted", "Wrong Answer").
- Live progress bar while grading: "Running test 12 of 63…".
- Per-test-case breakdown with sample inputs, expected outputs, and actual outputs.

### Watchdog

A Python process that runs three independent loops:

1. **Stale job recovery** — every 2 s, find `PROCESSING` rows older than the lease. Requeue or mark `FAILED`.
2. **Cgroup cleanup** — every 30 s, remove orphaned empty cgroups older than 5 minutes.
3. **Temp dir cleanup** — every 60 s, remove stale `/tmp/codearena/sub_*` dirs.

The age threshold for cgroup and temp cleanup prevents racing a worker that just created the resource.

**Lease-based, not heartbeat-based.** A pure heartbeat design trusts the worker to send signals. If the worker is alive but hung (deadlock, blocked syscall), it might keep sending heartbeats while making no progress. A lease is "processing_started_at older than X" — no trust required. If the timestamp stops advancing, the job is stale.

---

## Data Model

```mermaid
erDiagram
    USERS {
        bigint id PK
        text username
        text email
        text password_hash
        text role
        timestamp created_at
        timestamp last_login
    }
    PROBLEMS {
        bigint id PK
        text title
        text description
        text input_fmt
        text output_fmt
        text constraints
        int time_limit
        int memory_limit
        text difficulty
        boolean is_active
        bigint created_by FK
    }
    TEST_CASES {
        bigint id PK
        bigint problem_id FK
        int ordinal
        text input_data
        text expected_output
        boolean is_sample
        int points
    }
    SUBMISSIONS {
        bigint id PK
        bigint user_id FK
        bigint problem_id FK
        text language
        text source_code
        text status
        timestamp submitted_at
        timestamp processing_started_at
        text worker_id
        int retry_count
        int test_total
        int test_passed
        int test_current
    }
    RESULTS {
        bigint id PK
        bigint submission_id UK
        text verdict
        int score
        int runtime_ms
        int memory_kb
        text stdout
        text stderr
        timestamp graded_at
        text graded_by
        int test_total
        int test_passed
        int first_failed
    }
    TEST_CASE_RESULTS {
        bigint id PK
        bigint submission_id FK
        bigint test_case_id FK
        int ordinal
        text verdict
        text actual_output
        int runtime_ms
        int memory_kb
        timestamp created_at
    }

    USERS ||--o{ SUBMISSIONS : creates
    PROBLEMS ||--o{ SUBMISSIONS : receives
    PROBLEMS ||--o{ TEST_CASES : contains
    SUBMISSIONS ||--o| RESULTS : produces
    SUBMISSIONS ||--o{ TEST_CASE_RESULTS : records
    TEST_CASES ||--o{ TEST_CASE_RESULTS : evaluates
```

---

## Threat Model

The sandbox assumes every submission is **hostile**. Defense is layered: even if one layer fails, the others contain the damage.

| Threat | Mitigation |
|--------|------------|
| Fork bomb | `pids.max` in cgroup + `RLIMIT_NPROC` |
| Memory bomb | `memory.max` + `memory.swap.max=0` + OOM killer |
| CPU hog | `cpu.max` rate + `RLIMIT_CPU` + wall-clock watchdog |
| Infinite loop | Wall-clock watchdog (SIGTERM → SIGKILL) |
| Output flood | Bounded pipe reader + pipe draining after cap |
| File read (`/etc/passwd`) | Mount namespace + `pivot_root` |
| Network access | Network namespace (no interfaces) |
| `ptrace` another process | seccomp-BPF whitelist blocks `ptrace` |
| `mount` filesystem | seccomp-BPF blocks `mount` |
| `bpf` / `userfaultfd` | seccomp-BPF blocks them |
| Privilege escalation | User namespace + dropped capabilities + `no_new_privs` |
| Core dump leaks memory | `RLIMIT_CORE = 0` |
| Zombie processes | `wait4` + SIGCHLD handling |
| Orphaned grandchildren | `PR_SET_PDEATHSIG(SIGKILL)` in grandchild |
| Shared memory corruption | Semaphore protection + magic/version checks |
| SQL injection | Parameterized queries everywhere (`$1`, `$2`, ...) |
| JWT forgery | HMAC-SHA256 with a 64-byte secret |
| Username enumeration | Constant-time login (always runs bcrypt) |
| Double grading | `SELECT FOR UPDATE` + `UNIQUE (submission_id)` |

---

## Technology Choices

Every choice trades off simplicity for visibility. The project's educational goal dictates the stack.

| Layer | Choice | Alternative | Why Chosen |
|-------|--------|-------------|------------|
| OS | Ubuntu 22.04+ / WSL2 | Any Linux | cgroups v2 unified hierarchy is default; namespace support mature |
| Sandbox | Raw C syscalls | Docker / gVisor | Project mandate: make OS primitives visible |
| IPC | POSIX shared memory + semaphores | Redis / RabbitMQ | Classic bounded-buffer synchronization is a teaching goal |
| Worker | C (links sandbox + queue + libpq) | Python subprocess | Direct syscall access; one process, one event loop |
| API | Python 3.11 + FastAPI | Express.js, Flask, Django | Async-first, Pydantic validation, no ORM required |
| Database | PostgreSQL 15+ | MySQL, MongoDB | ACID, isolation levels, analytical SQL (window functions, materialized views) |
| DB driver | `asyncpg` | SQLAlchemy, psycopg2 | Raw SQL with `$N` placeholders; explicit transactions |
| JWT | `python-jose` | PyJWT, node-jsonwebtoken | Works with FastAPI's security dependencies |
| Password hashing | `bcrypt` | `argon2`, `scrypt` | Well-understood, ships with Python; 72-byte limit enforced at API boundary |
| Validation | Pydantic v2 | Marshmallow, Joi | FastAPI native; fast and type-safe |
| Watchdog | Python | C, Go | Pure orchestration (DB + filesystem); no performance need |
| UI | Vanilla HTML/JS/CSS | React, Vue | Deliberately minimal; demonstrates the API, not the frontend |
| Static server (dev) | `python3 -m http.server` | nginx, node serve | No build step; one command |

**Deliberately avoided:**

- **Docker** — hides exactly the OS primitives the project is meant to demonstrate.
- **SQLAlchemy ORM** — hides the SQL and transaction control the DBMS section is meant to demonstrate.
- **Redis / RabbitMQ** — replaces the IPC synchronization the OS section is meant to demonstrate.
- **Kubernetes / cloud orchestration** — out of scope for a single-machine educational platform.

---

## Project Structure

```
codearena/
├── os/                          OS-layer components (C)
│   ├── sandbox/                 Per-submission isolation
│   │   ├── src/
│   │   │   ├── main.c           CLI entry point
│   │   │   ├── sandbox.c        fork/exec, capture, watchdog thread
│   │   │   ├── pipe_reader.c    bounded dual-pipe reader
│   │   │   ├── cgroup.c         cgroups v2 management
│   │   │   ├── namespace.c      unshare + pivot_root
│   │   │   ├── seccomp_filter.c BPF whitelist
│   │   │   ├── watchdog.c       wall-clock timeout
│   │   │   └── verdict.c        verdict determination
│   │   ├── include/
│   │   ├── tests/               15 hostile test programs
│   │   └── Makefile
│   ├── ipc/                     POSIX shared-memory queue
│   │   ├── src/
│   │   │   ├── queue.c          lifecycle, stats
│   │   │   ├── queue_producer.c enqueue
│   │   │   ├── queue_consumer.c dequeue
│   │   │   └── queue_internal.h private layout
│   │   ├── include/
│   │   ├── tests/               producer, consumer, stress
│   │   └── Makefile
│   ├── worker/                  Job processor
│   │   ├── src/
│   │   │   ├── main.c           CLI
│   │   │   ├── worker.c         main loop, atomic commit
│   │   │   ├── db_client.c      libpq wrapper
│   │   │   ├── job_processor.c  compile + run + aggregate
│   │   │   └── worker_internal.h
│   │   ├── include/
│   │   └── Makefile
│   └── watchdog/                Python watchdog
│       ├── watchdog.py          three-loop scheduler
│       ├── stale_jobs.py        stale detection
│       ├── queue_writer.py      shm client
│       ├── cgroup_cleaner.py    orphaned cgroups
│       ├── temp_cleaner.py      stale temp dirs
│       └── config.py
│
├── db/                          Database layer
│   ├── migrations/              Versioned SQL migrations
│   │   ├── 001_initial_schema.sql
│   │   ├── 002_indexes.sql
│   │   ├── 003_materialized_views.sql
│   │   ├── 004_test_progress.sql
│   │   └── 005_test_case_results.sql
│   ├── seeds/                   Sample data
│   │   ├── users.sql
│   │   ├── problems.sql
│   │   ├── test_cases.sql
│   │   ├── problems_extra.sql
│   │   ├── test_cases_extra.sql
│   │   ├── demo_users.sql
│   │   └── demo_submissions.sql
│   ├── queries/                 Named queries
│   ├── tests/                   Concurrency tests
│   └── migrate.sh
│
├── api/                         FastAPI backend
│   ├── src/
│   │   ├── main.py              app + lifespan
│   │   ├── config.py            pydantic-settings
│   │   ├── db.py                asyncpg pool
│   │   ├── queue.py             shm client (imports from watchdog)
│   │   ├── auth.py              bcrypt + JWT
│   │   ├── deps.py              reusable dependencies
│   │   ├── models/              Pydantic schemas
│   │   └── routers/             HTTP endpoints
│   ├── tests/                   API integration tests
│   └── requirements.txt
│
├── ui/                          Frontend
│   ├── index.html               login / register
│   ├── dashboard.html           stats + recent
│   ├── problems.html            problem list
│   ├── problem.html             split-view editor
│   ├── submission.html          rich verdict + test case breakdown
│   ├── history.html             submission history
│   ├── leaderboard.html         rankings
│   └── static/
│       ├── style.css
│       └── app.js
│
├── scripts/                     Development scripts
│   ├── codearena.sh             single-command dev environment
│   ├── dev_up.sh                legacy multi-terminal setup
│   ├── e2e_smoke.sh             end-to-end happy path
│   ├── e2e_verdicts.sh          verdict coverage
│   ├── e2e_failure.sh           failure recovery
│   ├── e2e_concurrent.py        concurrent submissions
│   └── e2e_stress.py            sustained load
│
├── docs/                        Documentation
│   ├── ARCHITECTURE.md
│   ├── SYSCALLS.md
│   ├── SCHEMA.md
│   ├── QUERIES.md
│   ├── SECURITY.md
│   ├── FAILURES.md
│   ├── TESTING.md
│   └── E2E.md
│
├── systemd/                     Production service units
├── .env.example
├── LICENSE
└── README.md
```

---

## Getting Started

### Prerequisites

- **Linux** with kernel ≥ 5.4 (cgroups v2 unified hierarchy). Ubuntu 22.04 or WSL2 recommended.
- **PostgreSQL 15+**
- **gcc**, **make**, **libseccomp-dev**, **libpq-dev**
- **Python 3.11+** with `python3-venv`
- **sudo** access (workers need cgroup write permission)
- **strace**, **valgrind** (for debugging and verification)

### One-Time Setup

```bash
# Clone
git clone https://github.com/<your-username>/codearena.git
cd codearena

# Verify environment
bash scripts/setup.sh

# Create DB and user
sudo -u postgres psql <<'SQL'
CREATE USER codearena WITH PASSWORD 'codearena';
CREATE DATABASE codearena OWNER codearena;
GRANT ALL PRIVILEGES ON DATABASE codearena TO codearena;
SQL

# Configure
cp .env.example .env
# Edit .env: set DATABASE_URL and JWT_SECRET
python3 -c "import secrets; print(secrets.token_urlsafe(64))"  # for JWT_SECRET

# Migrate + seed
./db/migrate.sh --seed

# Build OS components
make -C os/ipc
make -C os/sandbox
make -C os/worker

# Install API deps
cd api
python3 -m venv venv
source venv/bin/activate
pip install -r requirements.txt
cd ..
```

### Run Everything With One Command

```bash
bash scripts/codearena.sh
```

This starts:

- The POSIX shared-memory queue
- The API on `http://localhost:8000`
- The worker
- The watchdog
- The UI on `http://localhost:3000`

Logs are written to `.dev-logs/` and tailed with a service prefix. Press **Ctrl-C** to stop everything.

### Manual Start (Multiple Terminals)

<details>
<summary>Click to expand</summary>

**Terminal A — API:**

```bash
cd api
source venv/bin/activate
export DATABASE_URL=postgresql://codearena:codearena@localhost:5432/codearena
export JWT_SECRET="<your-secret>"
uvicorn src.main:app --host 0.0.0.0 --port 8000
```

**Terminal B — Worker:**

```bash
cd os/worker
sudo DATABASE_URL=postgresql://codearena:codearena@localhost:5432/codearena \
    ./codearena-worker \
    --id worker-1 \
    --queue /codearena_queue \
    --db postgresql://codearena:codearena@localhost:5432/codearena \
    --cgroup-root /sys/fs/cgroup/codearena \
    --tmp /tmp/codearena \
    --all-ns --seccomp
```

**Terminal C — Watchdog:**

```bash
cd os
sudo env \
    DATABASE_URL=postgresql://codearena:codearena@localhost:5432/codearena \
    QUEUE_NAME=/codearena_queue \
    python3 -m watchdog.watchdog
```

**Terminal D — UI:**

```bash
python3 -m http.server 3000 --directory ui
```

</details>

### Open the UI

Navigate to `http://localhost:3000`. Register an account, pick a problem, submit a solution, and watch the verdict arrive in real time.

---

## Testing

### Unit and Integration Tests

```bash
# OS sandbox tests
cd os/sandbox && make test

# IPC queue tests
cd os/ipc && make test

# Database concurrency tests
cd db && ./tests/run_all.sh

# API integration tests
cd api && source venv/bin/activate && pytest tests/ -v
```

### End-to-End Tests

Run the whole pipeline with a single command each:

```bash
# Happy path — one submission, one AC verdict
bash scripts/e2e_smoke.sh

# All six verdicts (AC, WA, TLE, MLE, RE, CE)
bash scripts/e2e_verdicts.sh

# Worker crash mid-job → watchdog requeues → second worker completes
bash scripts/e2e_failure.sh

# Concurrent submissions — no loss, no duplicates
python3 scripts/e2e_concurrent.py 50

# Sustained load — throughput and correctness
python3 scripts/e2e_stress.py 200 20
```

Each script starts its own services, runs its scenario, verifies against the database, and cleans up. They're self-contained and safe to run repeatedly.

### Security Tests

The `os/sandbox/tests/` directory contains fifteen hostile programs, each targeting a specific isolation layer:

| Test | What it tries | Expected verdict |
|------|---------------|------------------|
| `fork_bomb` | Spawn processes recursively | Contained by `pids.max` |
| `memory_bomb` | Allocate beyond `memory.max` | MLE (OOM kill) |
| `cpu_hog` | Loop forever consuming CPU | TLE (watchdog) |
| `file_filler` | Write beyond `RLIMIT_FSIZE` | RE |
| `file_reader` | Read `/etc/passwd` | RE (file not visible) |
| `proc_scanner` | Read `/proc/1/cmdline` | RE (PID ns isolation) |
| `network_probe` | Open a TCP socket | RE (net ns) |
| `sleep_forever` | Block forever | TLE (watchdog) |
| `syscall_fuzzer` | Call every blocked syscall | RE (SIGSYS) |
| `show_ns` | Print namespace inodes | Diagnostic |

---

## Deployment

### Single-Machine (systemd)

Three unit files are provided:

```bash
sudo cp systemd/codearena-*.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now codearena-api codearena-worker@1 codearena-watchdog
```

The worker uses a template unit — `codearena-worker@.service` — so you can run N workers by starting `codearena-worker@1`, `@2`, `@3`, etc.

### Hardening

The systemd units include:

- `NoNewPrivileges=yes`
- `ProtectSystem=strict`
- `ProtectHome=yes`
- `ReadWritePaths=/sys/fs/cgroup/codearena /tmp/codearena`

Adjust paths to match your install location.

### Behind a Reverse Proxy

The API is a standard WSGI/ASGI app. Put nginx in front for TLS termination:

```nginx
location /api/ {
    proxy_pass http://localhost:8000;
    proxy_set_header Host $host;
    proxy_set_header X-Real-IP $remote_addr;
}

location / {
    root /opt/codearena/ui;
    try_files $uri $uri/ /index.html;
}
```

---

## Performance

Baseline throughput on a 4-core machine (2024-era laptop, WSL2):

| Metric | Value |
|--------|-------|
| Submission acceptance | ~150 submissions/second (HTTP + DB) |
| Grading throughput | 3–8 jobs/second (bound by sandbox startup) |
| Sandbox cold start | 20–30 ms (cgroup + namespaces + seccomp) |
| Simple program runtime | 10–50 ms |
| Concurrent submissions (50) | All AC, no duplicates, < 3 seconds total wall time |
| Database transactions | > 5,000 TPS on the grading workload |

Bottlenecks, in order:

1. **Sandbox setup** — cgroup creation, namespace unshare, seccomp load.
2. **Process fork** — the sandbox forks twice (once for I/O isolation, once for PID namespace).
3. **Compilation** — for C/C++ submissions, `gcc -O2` dominates.

For higher throughput, run more workers. Each worker is single-threaded but independent.

---

## Limitations & Future Work

**Known limitations:**

- **No Docker, by design.** The project deliberately stays on raw kernel primitives.
- **Single-machine.** No distributed orchestration; the IPC queue is local.
- **Local filesystem.** No object storage for large artifacts.
- **No rate limiting on `/submit`** in the base build (a config option is stubbed).
- **Java, Go, Rust not supported.** The sandbox works; the compiler integration is missing.
- **No multi-tenant isolation.** Every submission shares the host's UID.
- **No persistent queue.** If the host reboots, in-flight jobs are lost from the queue — the watchdog requeues them after restart.

**Future work:**

- Add `test_case_results` aggregation for "which test case fails most often?" analytics.
- Add a per-case diff view for WA verdicts.
- Add language profiles for Java, Go, Rust.
- Add problem tags (schema change).
- Add support for custom checkers (special judges).
- Add Prometheus metrics endpoint.
- Add a formal verification pass on the semaphore protocol.

---

## Contributing

This is a course project, but external contributions are welcome. Before submitting a PR:

1. Read `docs/ARCHITECTURE.md` to understand the design constraints.
2. Keep the "no Docker, no ORM" rules.
3. Add a test for every behavior change.
4. Run `scripts/e2e_smoke.sh` and `scripts/e2e_verdicts.sh` before opening the PR.
5. Ensure `valgrind` is clean for any C code changes.

See `docs/CONTRIBUTING.md` for details.

---

## License

MIT. See `LICENSE`.

---

## Acknowledgments

- The Linux kernel team for cgroups v2, namespaces, and seccomp.
- The PostgreSQL team for materialized views and row-level locking.
- The FastAPI team for a framework that doesn't get in the way of the SQL.
- The maintainers of `libseccomp` and `posix_ipc` for making syscall filtering and IPC accessible.
- DOMJudge and CMS for showing what a real online judge looks like.
