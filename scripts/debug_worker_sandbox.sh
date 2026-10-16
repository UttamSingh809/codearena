#!/usr/bin/env bash
# ============================================================
# CodeArena — worker/sandbox debug script
# ============================================================
#
# Reproduces the exact path a submission takes through the worker
# and sandbox, with verbose diagnostics at every step. Used to
# diagnose "worker returns FAILED" issues.
#
# Run: bash scripts/debug_worker_sandbox.sh
# ============================================================

set -u

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export DATABASE_URL="${DATABASE_URL:-postgresql://codearena:codearena@localhost:5432/codearena}"
QUEUE_NAME="/codearena_queue"
QUEUE_CAPACITY=1024
CGROUP_ROOT="/sys/fs/cgroup/codearena"
TEMP_ROOT="/tmp/codearena"

DEBUG_DIR="/tmp/codearena_debug_$$"
DEBUG_LOG="$DEBUG_DIR/debug.log"
API_LOG="$DEBUG_DIR/api.log"
WORKER_LOG="$DEBUG_DIR/worker.log"

mkdir -p "$DEBUG_DIR"

# ------------------------------------------------------------------
# Colours
# ------------------------------------------------------------------

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; BLUE='\033[0;34m'; NC='\033[0m'

log()      { echo "$@" | tee -a "$DEBUG_LOG"; }
log_info() { echo -e "${BLUE}[INFO]${NC} $*" | tee -a "$DEBUG_LOG"; }
log_ok()   { echo -e "${GREEN}[OK]${NC} $*"   | tee -a "$DEBUG_LOG"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $*" | tee -a "$DEBUG_LOG"; }
log_err()  { echo -e "${RED}[ERR]${NC} $*"    | tee -a "$DEBUG_LOG"; }
log_hdr()  { echo ""; echo "============================================================" | tee -a "$DEBUG_LOG"; echo "  $*" | tee -a "$DEBUG_LOG"; echo "============================================================" | tee -a "$DEBUG_LOG"; }

# ------------------------------------------------------------------
# Cleanup trap
# ------------------------------------------------------------------

WORKER_PID=""
API_PID=""
SUBMISSION_ID=""

cleanup() {
    local exit_code=$?
    log ""
    log_hdr "CLEANUP"

    if [[ -n "$WORKER_PID" ]]; then
        sudo kill -TERM "$WORKER_PID" 2>/dev/null || true
    fi
    if [[ -n "$API_PID" ]]; then
        kill -TERM "$API_PID" 2>/dev/null || true
    fi
    sleep 1
    sudo pkill -9 -f "codearena-worker" 2>/dev/null || true
    pkill -9 -f "uvicorn src.main:app" 2>/dev/null || true

    if [[ -n "$SUBMISSION_ID" ]]; then
        log "Cleaning up submission $SUBMISSION_ID"
        PGPASSWORD=codearena psql -h localhost -U codearena -d codearena -q -c "
            DELETE FROM results WHERE submission_id = $SUBMISSION_ID;
            DELETE FROM submissions WHERE id = $SUBMISSION_ID;" >/dev/null 2>&1 || true
    fi

    log "Debug directory: $DEBUG_DIR"
    log "  $DEBUG_LOG"
    log "  $API_LOG"
    log "  $WORKER_LOG"
    log ""
    log "Exit code: $exit_code"
    exit "$exit_code"
}

trap cleanup EXIT

# ------------------------------------------------------------------
# Preconditions
# ------------------------------------------------------------------

log_hdr "PRECONDITIONS"

# Kill stale processes
log_info "Killing stale processes..."
sudo pkill -9 -f "codearena-worker"  2>/dev/null || true
sudo pkill -9 -f "watchdog.watchdog" 2>/dev/null || true
pkill -9 -f "uvicorn src.main:app"   2>/dev/null || true
pkill -9 -f "test_producer"          2>/dev/null || true
pkill -9 -f "test_consumer"          2>/dev/null || true
sleep 1

if ps aux | grep -E 'codearena-worker|watchdog\.watchdog|uvicorn src' | grep -v grep >/dev/null; then
    log_err "Stale processes still running after kill:"
    ps aux | grep -E 'codearena-worker|watchdog|uvicorn' | grep -v grep | tee -a "$DEBUG_LOG"
    exit 1
fi
log_ok "No stale processes"

# Check DB
log_info "Checking PostgreSQL..."
if ! PGPASSWORD=codearena psql -h localhost -U codearena -d codearena -t -A -c "SELECT 1" >/dev/null 2>&1; then
    log_err "PostgreSQL not reachable at $DATABASE_URL"
    exit 1
fi
log_ok "PostgreSQL reachable"

# Check user and problem exist
UID_1=$(PGPASSWORD=codearena psql -h localhost -U codearena -d codearena -t -A -c "SELECT id FROM users WHERE id=1")
PID_1=$(PGPASSWORD=codearena psql -h localhost -U codearena -d codearena -t -A -c "SELECT id FROM problems WHERE id=1")
if [[ -z "$UID_1" || -z "$PID_1" ]]; then
    log_err "user id=1 or problem id=1 missing; run ./db/migrate.sh --seed"
    exit 1
fi
log_ok "user id=1 and problem id=1 exist"

# Check cgroup subtree control
log_info "Checking cgroup setup..."
if [[ -f "$CGROUP_ROOT/cgroup.subtree_control" ]]; then
    SUBCONTROL=$(cat "$CGROUP_ROOT/cgroup.subtree_control")
    log_info "  cgroup.subtree_control: '$SUBCONTROL'"
    if echo "$SUBCONTROL" | grep -q "cpu" && echo "$SUBCONTROL" | grep -q "memory"; then
        log_ok "cpu and memory controllers available in cgroup root"
    else
        log_warn "cpu or memory controllers missing from subtree_control"
    fi
else
    log_warn "$CGROUP_ROOT/cgroup.subtree_control not found"
fi

# Check binaries
log_info "Checking binaries..."
for b in os/sandbox/sandbox os/worker/codearena-worker os/ipc/tests/test_producer; do
    if [[ -x "$REPO_ROOT/$b" ]]; then
        log_ok "  $b"
    else
        log_err "  $b missing or not executable"
        exit 1
    fi
done

# Check API venv
log_info "Checking API venv..."
if [[ ! -f "$REPO_ROOT/api/venv/bin/activate" ]]; then
    log_err "API venv missing; run: cd api && python3 -m venv venv && pip install -r requirements.txt"
    exit 1
fi
log_ok "API venv present"

# ------------------------------------------------------------------
# Stage 1 — Create a fresh queue
# ------------------------------------------------------------------

log_hdr "STAGE 1: Fresh Queue"

log_info "Removing old queue and semaphores..."
rm -f /dev/shm/"${QUEUE_NAME#/}" /dev/shm/sem."${QUEUE_NAME#/}"_* 2>/dev/null || true

log_info "Creating queue $QUEUE_NAME (capacity=$QUEUE_CAPACITY)..."
cd "$REPO_ROOT/os/ipc"
timeout 1 ./tests/test_producer "$QUEUE_NAME" "$QUEUE_CAPACITY" 0 >/dev/null 2>&1 || true
cd "$REPO_ROOT"

if [[ -f /dev/shm/"${QUEUE_NAME#/}" ]]; then
    log_ok "Queue created"
    ls -la /dev/shm/"${QUEUE_NAME#/}" | tee -a "$DEBUG_LOG"
    ls -la /dev/shm/sem."${QUEUE_NAME#/}"_* 2>/dev/null | tee -a "$DEBUG_LOG" || true
else
    log_err "Queue creation failed"
    exit 1
fi

# ------------------------------------------------------------------
# Stage 2 — Insert submission directly
# ------------------------------------------------------------------

log_hdr "STAGE 2: Insert Submission Directly"

SOURCE_CODE='#include <stdio.h>
int main(){int a,b;if(scanf("%d %d",&a,&b)!=2)return 1;printf("%d\n",a+b);return 0;}'

log_info "Inserting submission into DB..."
SUBMISSION_ID=$(PGPASSWORD=codearena psql -h localhost -U codearena -d codearena -t -A -q -c "
    INSERT INTO submissions (user_id, problem_id, language, source_code)
    VALUES (1, 1, 'c', \$E2E_DEBUG\$${SOURCE_CODE}\$E2E_DEBUG\$)
    RETURNING id" | head -n 1)

if [[ -z "$SUBMISSION_ID" ]]; then
    log_err "Failed to insert submission"
    exit 1
fi
log_ok "Submission id=$SUBMISSION_ID inserted"

# ------------------------------------------------------------------
# Stage 3 — Enqueue descriptor
# ------------------------------------------------------------------

log_hdr "STAGE 3: Enqueue Job Descriptor"

# We need to enqueue a descriptor with submission_id = SUBMISSION_ID.
# test_producer uses sequential IDs starting at 1, so it's not usable
# here. Instead, use a small Python one-liner via the queue_writer module.

log_info "Enqueueing descriptor via queue_writer..."
PYTHONPATH="$REPO_ROOT/os" python3 <<PYEOF 2>&1 | tee -a "$DEBUG_LOG"
import sys
sys.path.insert(0, "$REPO_ROOT/os")
from watchdog.queue_writer import QueueWriter

w = QueueWriter()
ok = w.enqueue(
    submission_id=$SUBMISSION_ID,
    problem_id=1,
    language=0,     # C
    time_limit_ms=2000,
    memory_limit_mb=256,
    output_limit_kb=64,
    retry_count=0,
)
w.close()
print(f"enqueue result: {ok}")
sys.exit(0 if ok else 1)
PYEOF

if [[ $? -ne 0 ]]; then
    log_err "Enqueue failed"
    exit 1
fi
log_ok "Descriptor enqueued"

# Verify it's in the queue
log_info "Verifying queue state..."
cd "$REPO_ROOT/os/ipc"
timeout 2 ./tests/test_consumer "$QUEUE_NAME" 1 999 2>&1 | tee -a "$DEBUG_LOG" || true
cd "$REPO_ROOT"

# Re-enqueue (the consumer above consumed it)
log_info "Re-enqueueing descriptor (previous verify consumed it)..."
PYTHONPATH="$REPO_ROOT/os" python3 <<PYEOF 2>&1 | tee -a "$DEBUG_LOG"
import sys
sys.path.insert(0, "$REPO_ROOT/os")
from watchdog.queue_writer import QueueWriter
w = QueueWriter()
w.enqueue($SUBMISSION_ID, 1, 0, 2000, 256, 64, 0)
w.close()
print("re-enqueue OK")
PYEOF

# ------------------------------------------------------------------
# Stage 4 — Check DB state after enqueue
# ------------------------------------------------------------------

log_hdr "STAGE 4: DB State After Enqueue"

PGPASSWORD=codearena psql -h localhost -U codearena -d codearena <<SQL 2>&1 | tee -a "$DEBUG_LOG"
SELECT id, status, worker_id, retry_count, submitted_at
FROM submissions WHERE id = $SUBMISSION_ID;
SQL

# ------------------------------------------------------------------
# Stage 5 — Start the worker
# ------------------------------------------------------------------

log_hdr "STAGE 5: Start Worker"

log_info "Starting worker e2e-debug-worker..."
cd "$REPO_ROOT/os/worker"

sudo DATABASE_URL="$DATABASE_URL" \
    ./codearena-worker \
    --id e2e-debug-worker \
    --queue "$QUEUE_NAME" \
    --db "$DATABASE_URL" \
    --cgroup-root "$CGROUP_ROOT" \
    --tmp "$TEMP_ROOT" \
    --all-ns --seccomp \
    > "$WORKER_LOG" 2>&1 &
WORKER_PID=$!

cd "$REPO_ROOT"

# Wait for the worker to connect
for i in $(seq 1 20); do
    if sudo grep -q "waiting for jobs" "$WORKER_LOG" 2>/dev/null; then
        break
    fi
    sleep 0.5
done

if ! sudo kill -0 "$WORKER_PID" 2>/dev/null; then
    log_err "Worker died at startup"
    sudo cat "$WORKER_LOG" | tee -a "$DEBUG_LOG"
    exit 1
fi
log_ok "Worker running (pid=$WORKER_PID)"

# ------------------------------------------------------------------
# Stage 6 — Wait for the verdict
# ------------------------------------------------------------------

log_hdr "STAGE 6: Wait for Verdict"

log_info "Polling for up to 60 seconds..."
FINAL_STATUS=""
FINAL_VERDICT=""
for i in $(seq 1 60); do
    STATUS=$(PGPASSWORD=codearena psql -h localhost -U codearena -d codearena -t -A -q -c \
        "SELECT status FROM submissions WHERE id = $SUBMISSION_ID" | head -n 1)
    if [[ "$STATUS" == "COMPLETED" || "$STATUS" == "FAILED" ]]; then
        FINAL_STATUS="$STATUS"
        break
    fi
    if [[ $((i % 5)) -eq 0 ]]; then
        log_info "  [$i s] status=$STATUS"
    fi
    sleep 1
done

if [[ -z "$FINAL_STATUS" ]]; then
    log_err "Timed out after 60s"
    FINAL_STATUS=$(PGPASSWORD=codearena psql -h localhost -U codearena -d codearena -t -A -q -c \
        "SELECT status FROM submissions WHERE id = $SUBMISSION_ID" | head -n 1)
fi

log_info "Final status: $FINAL_STATUS"

# Fetch verdict if any
FINAL_VERDICT=$(PGPASSWORD=codearena psql -h localhost -U codearena -d codearena -t -A -q -c \
    "SELECT verdict FROM results WHERE submission_id = $SUBMISSION_ID" | head -n 1)

if [[ -n "$FINAL_VERDICT" ]]; then
    log_info "Verdict: $FINAL_VERDICT"
fi

# ------------------------------------------------------------------
# Stage 7 — Full diagnostic report
# ------------------------------------------------------------------

log_hdr "STAGE 7: Diagnostic Report"

log ""
log "--- DB: submission row ---"
PGPASSWORD=codearena psql -h localhost -U codearena -d codearena \
    -c "SELECT id, user_id, problem_id, language, status, worker_id, retry_count,
               processing_started_at, submitted_at
        FROM submissions WHERE id = $SUBMISSION_ID;" 2>&1 | tee -a "$DEBUG_LOG"

log ""
log "--- DB: result row ---"
PGPASSWORD=codearena psql -h localhost -U codearena -d codearena \
    -c "SELECT submission_id, verdict, score, runtime_ms, memory_kb,
               graded_by, graded_at, stderr, stdout
        FROM results WHERE submission_id = $SUBMISSION_ID;" 2>&1 | tee -a "$DEBUG_LOG"

log ""
log "--- Worker log (full) ---"
sudo cat "$WORKER_LOG" 2>&1 | tee -a "$DEBUG_LOG"

log ""
log "--- API log (if any) ---"
if [[ -f "$API_LOG" ]]; then
    cat "$API_LOG" | tee -a "$DEBUG_LOG"
else
    log "(API not used in this test)"
fi

log ""
log "--- Temp directory contents ---"
if [[ -d "$TEMP_ROOT" ]]; then
    sudo find "$TEMP_ROOT" -maxdepth 2 -type f -o -type d 2>&1 | tee -a "$DEBUG_LOG" || true
else
    log "(no $TEMP_ROOT directory)"
fi

log ""
log "--- Cgroup contents ---"
if [[ -d "$CGROUP_ROOT" ]]; then
    sudo ls -la "$CGROUP_ROOT" 2>&1 | tee -a "$DEBUG_LOG" || true
else
    log "(no $CGROUP_ROOT directory)"
fi

log ""
log "--- Queue state ---"
ls -la /dev/shm/"${QUEUE_NAME#/}" /dev/shm/sem."${QUEUE_NAME#/}"_* 2>&1 | tee -a "$DEBUG_LOG" || true

log ""
log "--- Running processes ---"
ps aux | grep -E 'codearena-worker|watchdog|uvicorn' | grep -v grep | tee -a "$DEBUG_LOG" || true

# ------------------------------------------------------------------
# Stage 8 — Manual sandbox reproduction
# ------------------------------------------------------------------

log_hdr "STAGE 8: Manual Sandbox Reproduction (if possible)"

# Try to find the worker's tmpdir to reuse the compiled binary
TMPDIR_CANDIDATES=$(sudo find "$TEMP_ROOT" -maxdepth 1 -type d -name "sub_${SUBMISSION_ID}_*" 2>/dev/null | head -n 1)

if [[ -n "$TMPDIR_CANDIDATES" && -x "$TMPDIR_CANDIDATES/sol" ]]; then
    log_info "Found worker tmpdir: $TMPDIR_CANDIDATES"
    log_info "Binary: $TMPDIR_CANDIDATES/sol"

    log_info "Running sandbox manually with the same config..."
    cd "$REPO_ROOT/os/sandbox"
    sudo ./sandbox \
        --binary "$TMPDIR_CANDIDATES/sol" \
        --input "3 5" \
        --cgroup-root "$CGROUP_ROOT" \
        --cgroup-name sandbox_debug_manual \
        --cgroup-cpu 100 \
        --cgroup-memory 256 \
        --cgroup-pids 64 \
        --cpu-seconds 4 \
        --max-file-size 16777216 \
        --max-pids 64 \
        --stack-size 8388608 \
        --max-open-files 64 \
        --all-ns \
        --seccomp \
        --time 2500 \
        --grace-ms 500 \
        2>&1 | tee -a "$DEBUG_LOG" || true
    cd "$REPO_ROOT"
else
    log_info "Worker tmpdir not found (may have been cleaned up)"
fi

# ------------------------------------------------------------------
# Stage 9 — Verdict
# ------------------------------------------------------------------

log_hdr "STAGE 9: SUMMARY"

log "Submission ID: $SUBMISSION_ID"
log "Final status:  $FINAL_STATUS"
log "Final verdict: ${FINAL_VERDICT:-<none>}"
log ""
log "Debug directory: $DEBUG_DIR"
log "Full log:        $DEBUG_LOG"
log ""

if [[ "$FINAL_VERDICT" == "AC" ]]; then
    log_ok "SUCCESS — the pipeline works"
    exit 0
elif [[ -n "$FINAL_VERDICT" ]]; then
    log_err "FAILURE — verdict is $FINAL_VERDICT (not AC)"
    exit 1
else
    log_err "FAILURE — no verdict produced"
    exit 1
fi