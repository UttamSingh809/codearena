#!/usr/bin/env bash
# ============================================================
# CodeArena — Step 13 integration test
# ============================================================
#
# Tests the watchdog end-to-end. Assumes:
#   - PostgreSQL is running, schema migrated, seeds loaded
#   - os/ipc has been built (test_producer, test_consumer exist)
#   - Python deps installed: psycopg2-binary, posix-ipc
#   - The user can sudo (for cgroup operations and running the
#     watchdog as root)
#
# Usage:
#   DATABASE_URL=postgresql://... bash scripts/test_step13.sh
#
# On success, prints "ALL TESTS PASSED" and exits 0.
# On any failure, prints the failing test and exits 1.
# ============================================================

set -u

# ------------------------------------------------------------------
# Configuration
# ------------------------------------------------------------------

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WATCHDOG_DIR="$REPO_ROOT/os/watchdog"
IPC_DIR="$REPO_ROOT/os/ipc"
QUEUE_NAME="/codearena_queue"
QUEUE_CAPACITY=1024
CGROUP_ROOT="/sys/fs/cgroup/codearena"
TEMP_ROOT="/tmp/codearena"

# Fast test config
export WATCHDOG_INTERVAL_SECONDS=1
export STALE_JOB_THRESHOLD_SECONDS=10
export CGROUP_CLEANUP_INTERVAL_SECONDS=1
export CGROUP_CLEANUP_AGE_SECONDS=10
export TEMP_CLEANUP_INTERVAL_SECONDS=1
export TEMP_CLEANUP_AGE_SECONDS=10
export MAX_RETRY_COUNT=3
export QUEUE_NAME

# Log files (owned by root after the watchdog starts)
LOG1=/tmp/watchdog_step13.log
LOG2=/tmp/watchdog_step13_2.log
ENV_FILE=/tmp/watchdog_step13.env

# Colours
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; BLUE='\033[0;34m'; NC='\033[0m'

# ------------------------------------------------------------------
# Counters
# ------------------------------------------------------------------

PASS=0
FAIL=0
TEST_NAME=""

# ------------------------------------------------------------------
# Helpers
# ------------------------------------------------------------------

test_start() {
    TEST_NAME="$1"
    echo ""
    echo -e "${BLUE}[TEST]${NC} $TEST_NAME"
}

test_pass() {
    PASS=$((PASS + 1))
    echo -e "  ${GREEN}[PASS]${NC} $1"
}

test_fail() {
    FAIL=$((FAIL + 1))
    echo -e "  ${RED}[FAIL]${NC} $1"
}

test_info() {
    echo -e "  ${YELLOW}[INFO]${NC} $1"
}

psql_q() {
    PGPASSWORD=codearena psql -h localhost -U codearena -d codearena -t -A -q -c "$1" \
        | head -n 1
}

psql_f() {
    PGPASSWORD=codearena psql -h localhost -U codearena -d codearena -q -f "$1"
}

cleanup() {
    echo ""
    echo "=== Cleanup ==="

    # Stop the watchdog (may be running as root; kill by pattern)
    sudo pkill -f "watchdog.watchdog" 2>/dev/null || true
    sleep 0.3

    # Stop any leftover test_producer / test_consumer
    pkill -f "test_producer $QUEUE_NAME" 2>/dev/null || true
    pkill -f "test_consumer $QUEUE_NAME" 2>/dev/null || true

    # Remove test rows
    psql_q "DELETE FROM results WHERE graded_by IN ('watchdog', 'test-step13')" >/dev/null 2>&1 || true
    psql_q "DELETE FROM submissions WHERE worker_id LIKE 'dead-worker-%' OR worker_id LIKE 'test-step13-%'" >/dev/null 2>&1 || true

    # Remove test cgroups
    sudo find "$CGROUP_ROOT" -maxdepth 1 -name 'test_*' -type d -exec rmdir {} \; 2>/dev/null || true

    # Remove test temp dirs
    rm -rf "$TEMP_ROOT"/test_* 2>/dev/null || true

    # Remove log and env files (may be root-owned)
    sudo rm -f "$LOG1" "$LOG2" "$ENV_FILE" 2>/dev/null || true

    echo "Cleanup done."
}

trap cleanup EXIT

# ------------------------------------------------------------------
# Prerequisites
# ------------------------------------------------------------------

echo "============================================================"
echo "  CodeArena Step 13 — Watchdog Integration Test"
echo "============================================================"
echo ""

echo "=== Prerequisites ==="

# DATABASE_URL
if [[ -z "${DATABASE_URL:-}" ]]; then
    echo -e "${RED}ERROR:${NC} DATABASE_URL not set"
    exit 1
fi
echo "DATABASE_URL: $DATABASE_URL"

# psql reachable
if ! psql_q "SELECT 1" >/dev/null 2>&1; then
    echo -e "${RED}ERROR:${NC} cannot reach PostgreSQL"
    exit 1
fi
echo -e "${GREEN}OK${NC}: PostgreSQL reachable"

# Python deps
if ! python3 -c "import psycopg2, posix_ipc" 2>/dev/null; then
    echo -e "${RED}ERROR:${NC} missing Python deps (psycopg2-binary, posix-ipc)"
    exit 1
fi
echo -e "${GREEN}OK${NC}: Python deps present"

# Watchdog package
if [[ ! -f "$WATCHDOG_DIR/__init__.py" ]]; then
    touch "$WATCHDOG_DIR/__init__.py"
fi
if [[ ! -f "$WATCHDOG_DIR/watchdog.py" ]]; then
    echo -e "${RED}ERROR:${NC} watchdog.py not found"
    exit 1
fi
echo -e "${GREEN}OK${NC}: watchdog package present"

# IPC binaries
if [[ ! -x "$IPC_DIR/tests/test_producer" ]] || [[ ! -x "$IPC_DIR/tests/test_consumer" ]]; then
    echo -e "${RED}ERROR:${NC} IPC test binaries not built; run 'make -C os/ipc'"
    exit 1
fi
echo -e "${GREEN}OK${NC}: IPC test binaries present"

# Ensure user_id=1 and problem_id=1 exist
if ! psql_q "SELECT 1 FROM users WHERE id = 1" | grep -q 1; then
    echo -e "${YELLOW}WARN:${NC} user id=1 missing; inserting test user"
    psql_q "INSERT INTO users (username, email, password_hash) VALUES ('step13user', 'step13@test.com', 'h')" >/dev/null
fi
if ! psql_q "SELECT 1 FROM problems WHERE id = 1" | grep -q 1; then
    echo -e "${YELLOW}WARN:${NC} problem id=1 missing; inserting test problem"
    psql_q "INSERT INTO problems (title) VALUES ('Step13 Problem')" >/dev/null
fi

# Clean slate
psql_q "DELETE FROM results WHERE graded_by IN ('watchdog', 'test-step13')" >/dev/null
psql_q "DELETE FROM submissions WHERE worker_id LIKE 'dead-worker-%'" >/dev/null

echo ""

# ------------------------------------------------------------------
# Setup: create the queue
# ------------------------------------------------------------------

echo "=== Queue setup ==="

# Clean any existing queue
rm -f /dev/shm/"${QUEUE_NAME#/}" /dev/shm/sem."${QUEUE_NAME#/}"_* 2>/dev/null || true

# Create a fresh queue via test_producer with 0 jobs
cd "$IPC_DIR"
timeout 1 ./tests/test_producer "$QUEUE_NAME" "$QUEUE_CAPACITY" 0 2>/dev/null || true

if [[ ! -f /dev/shm/"${QUEUE_NAME#/}" ]]; then
    echo -e "${RED}ERROR:${NC} queue was not created"
    exit 1
fi
echo -e "${GREEN}OK${NC}: queue $QUEUE_NAME created"

# ------------------------------------------------------------------
# Prepare environment file for the root-run watchdog
# ------------------------------------------------------------------

cat > "$ENV_FILE" <<EOF
DATABASE_URL=$DATABASE_URL
QUEUE_NAME=$QUEUE_NAME
WATCHDOG_INTERVAL_SECONDS=$WATCHDOG_INTERVAL_SECONDS
STALE_JOB_THRESHOLD_SECONDS=$STALE_JOB_THRESHOLD_SECONDS
CGROUP_CLEANUP_INTERVAL_SECONDS=$CGROUP_CLEANUP_INTERVAL_SECONDS
CGROUP_CLEANUP_AGE_SECONDS=$CGROUP_CLEANUP_AGE_SECONDS
TEMP_CLEANUP_INTERVAL_SECONDS=$TEMP_CLEANUP_INTERVAL_SECONDS
TEMP_CLEANUP_AGE_SECONDS=$TEMP_CLEANUP_AGE_SECONDS
MAX_RETRY_COUNT=$MAX_RETRY_COUNT
LOG_LEVEL=INFO
EOF

# ------------------------------------------------------------------
# Start the watchdog (as root, so cgroup removal works)
# ------------------------------------------------------------------

echo ""
echo "=== Starting watchdog (as root) ==="

sudo bash -c "set -a; source $ENV_FILE; set +a; cd $REPO_ROOT/os; python3 -m watchdog.watchdog" \
    > "$LOG1" 2>&1 &
WD_PID=$!
echo "Watchdog PID: $WD_PID"
echo "Log: $LOG1"

# Wait for it to attach to the queue
for _ in $(seq 1 20); do
    if sudo grep -q "attached to queue" "$LOG1" 2>/dev/null; then
        break
    fi
    sleep 0.2
done

if ! sudo kill -0 "$WD_PID" 2>/dev/null; then
    echo -e "${RED}ERROR:${NC} watchdog died at startup"
    sudo tail -20 "$LOG1"
    exit 1
fi
echo -e "${GREEN}OK${NC}: watchdog running and attached to queue"

# ------------------------------------------------------------------
# TEST 1: Stale job detection and requeue
# ------------------------------------------------------------------

test_start "1: Stale job detection and requeue"

STALE_ID=$(psql_q "INSERT INTO submissions
    (user_id, problem_id, language, source_code, status,
     processing_started_at, worker_id, retry_count)
VALUES (1, 1, 'c', 'int main(){return 0;}', 'PROCESSING',
        NOW() - INTERVAL '30 seconds', 'dead-worker-1', 0)
RETURNING id")

test_info "Inserted stale submission id=$STALE_ID (backdated 30s)"

# Wait up to 5s for the watchdog to requeue
STATUS=""; RETRY=""
for _ in $(seq 1 25); do
    STATUS=$(psql_q "SELECT status FROM submissions WHERE id = $STALE_ID")
    RETRY=$(psql_q "SELECT retry_count FROM submissions WHERE id = $STALE_ID")
    if [[ "$STATUS" == "PENDING" ]] && [[ "$RETRY" == "1" ]]; then
        break
    fi
    sleep 0.2
done

if [[ "$STATUS" == "PENDING" ]]; then
    test_pass "submission requeued to PENDING"
else
    test_fail "expected PENDING, got '$STATUS'"
fi

if [[ "$RETRY" == "1" ]]; then
    test_pass "retry_count incremented to 1"
else
    test_fail "expected retry_count=1, got '$RETRY'"
fi

WORKER_ID=$(psql_q "SELECT COALESCE(worker_id, 'NULL') FROM submissions WHERE id = $STALE_ID")
if [[ "$WORKER_ID" == "NULL" ]]; then
    test_pass "worker_id cleared"
else
    test_fail "expected worker_id NULL, got '$WORKER_ID'"
fi

# Verify the queue has a descriptor
cd "$IPC_DIR"
CONSUMER_OUT=$(timeout 3 ./tests/test_consumer "$QUEUE_NAME" 1 99 2>&1 || true)
if echo "$CONSUMER_OUT" | grep -q "got 1 jobs"; then
    test_pass "job descriptor enqueued (consumer got 1)"
else
    test_fail "no job descriptor in queue: $CONSUMER_OUT"
fi

# ------------------------------------------------------------------
# TEST 2: FAILED path after max retries
# ------------------------------------------------------------------

test_start "2: FAILED after MAX_RETRY_COUNT"

FAIL_ID=$(psql_q "INSERT INTO submissions
    (user_id, problem_id, language, source_code, status,
     processing_started_at, worker_id, retry_count)
VALUES (1, 1, 'c', 'int main(){return 0;}', 'PROCESSING',
        NOW() - INTERVAL '30 seconds', 'dead-worker-2', 3)
RETURNING id")

test_info "Inserted submission id=$FAIL_ID with retry_count=3"

STATUS=""
for _ in $(seq 1 25); do
    STATUS=$(psql_q "SELECT status FROM submissions WHERE id = $FAIL_ID")
    if [[ "$STATUS" == "FAILED" ]]; then
        break
    fi
    sleep 0.2
done

if [[ "$STATUS" == "FAILED" ]]; then
    test_pass "submission marked FAILED"
else
    test_fail "expected FAILED, got '$STATUS'"
fi

VERDICT=$(psql_q "SELECT verdict FROM results WHERE submission_id = $FAIL_ID")
if [[ "$VERDICT" == "FAILED" ]]; then
    test_pass "results row inserted with verdict=FAILED"
else
    test_fail "expected results verdict=FAILED, got '$VERDICT'"
fi

# ------------------------------------------------------------------
# TEST 3: Idempotency
# ------------------------------------------------------------------

test_start "3: Idempotency — requeued row not touched again"

IDEM_ID=$(psql_q "INSERT INTO submissions
    (user_id, problem_id, language, source_code, status,
     processing_started_at, worker_id, retry_count)
VALUES (1, 1, 'c', 'int main(){return 0;}', 'PROCESSING',
        NOW() - INTERVAL '30 seconds', 'dead-worker-3', 0)
RETURNING id")

# Wait for first requeue
STATUS=""
for _ in $(seq 1 25); do
    STATUS=$(psql_q "SELECT status FROM submissions WHERE id = $IDEM_ID")
    [[ "$STATUS" == "PENDING" ]] && break
    sleep 0.2
done

RETRY_BEFORE=$(psql_q "SELECT retry_count FROM submissions WHERE id = $IDEM_ID")

# Wait two more cycles (no requeue should happen)
sleep 3
RETRY_AFTER=$(psql_q "SELECT retry_count FROM submissions WHERE id = $IDEM_ID")

if [[ "$RETRY_BEFORE" == "$RETRY_AFTER" ]]; then
    test_pass "retry_count stable at $RETRY_AFTER across cycles"
else
    test_fail "retry_count changed from $RETRY_BEFORE to $RETRY_AFTER"
fi

# ------------------------------------------------------------------
# TEST 4: Cgroup cleanup (empty, old)
# ------------------------------------------------------------------

test_start "4: Cgroup cleanup — empty old cgroup removed"

CGROUP_NAME="test_orphan_$$"
sudo mkdir -p "$CGROUP_ROOT/$CGROUP_NAME" 2>/dev/null || true
sudo touch -d "10 minutes ago" "$CGROUP_ROOT/$CGROUP_NAME" 2>/dev/null || true

test_info "Created $CGROUP_ROOT/$CGROUP_NAME with old mtime"

# Wait for cleanup
REMOVED=0
for _ in $(seq 1 25); do
    if [[ ! -d "$CGROUP_ROOT/$CGROUP_NAME" ]]; then
        REMOVED=1
        break
    fi
    sleep 0.2
done

if [[ "$REMOVED" == "1" ]]; then
    test_pass "orphaned cgroup removed"
else
    test_fail "cgroup still present after 5s"
    sudo rmdir "$CGROUP_ROOT/$CGROUP_NAME" 2>/dev/null || true
fi

# ------------------------------------------------------------------
# TEST 5: Cgroup with live process NOT removed
# ------------------------------------------------------------------

test_start "5: Cgroup with live process is preserved"

CGROUP_LIVE="test_live_$$"
sudo mkdir -p "$CGROUP_ROOT/$CGROUP_LIVE" 2>/dev/null || true

# Start a sleep inside the cgroup
sudo bash -c "echo \$\$ > $CGROUP_ROOT/$CGROUP_LIVE/cgroup.procs; sleep 30" &
LIVE_PID=$!
sleep 0.5

# Backdate its mtime
sudo touch -d "10 minutes ago" "$CGROUP_ROOT/$CGROUP_LIVE" 2>/dev/null || true

# Wait 3 cycles
sleep 3

if [[ -d "$CGROUP_ROOT/$CGROUP_LIVE" ]]; then
    test_pass "cgroup with live process preserved"
else
    test_fail "cgroup was incorrectly removed"
fi

# Clean up the live cgroup
sudo kill "$LIVE_PID" 2>/dev/null || true
sleep 0.3
sudo rmdir "$CGROUP_ROOT/$CGROUP_LIVE" 2>/dev/null || true

# ------------------------------------------------------------------
# TEST 6: Temp dir cleanup (old)
# ------------------------------------------------------------------

test_start "6: Temp cleanup — old temp dir removed"

TEMP_DIR="$TEMP_ROOT/test_stale_$$"
mkdir -p "$TEMP_DIR/subdir"
touch "$TEMP_DIR/sol.c"
touch -d "10 minutes ago" "$TEMP_DIR"

test_info "Created $TEMP_DIR with old mtime"

REMOVED=0
for _ in $(seq 1 25); do
    if [[ ! -d "$TEMP_DIR" ]]; then
        REMOVED=1
        break
    fi
    sleep 0.2
done

if [[ "$REMOVED" == "1" ]]; then
    test_pass "stale temp dir removed"
else
    test_fail "temp dir still present after 5s"
    rm -rf "$TEMP_DIR"
fi

# ------------------------------------------------------------------
# TEST 7: Temp dir cleanup (recent, preserved)
# ------------------------------------------------------------------

test_start "7: Temp cleanup — recent temp dir preserved"

TEMP_FRESH="$TEMP_ROOT/test_fresh_$$"
mkdir -p "$TEMP_FRESH"

test_info "Created $TEMP_FRESH (fresh, age<10s)"

# Wait 3 cycles; age will be ~3s, well below MIN_AGE=10
sleep 3

if [[ -d "$TEMP_FRESH" ]]; then
    test_pass "recent temp dir preserved"
else
    test_fail "recent temp dir was incorrectly removed"
fi

rm -rf "$TEMP_FRESH"

# ------------------------------------------------------------------
# TEST 8: Signal handling (SIGTERM)
# ------------------------------------------------------------------

test_start "8: Signal handling — SIGTERM causes clean shutdown"

# Kill the running watchdog with SIGTERM (it's root, so sudo)
sudo kill -TERM "$WD_PID"
WAITED=0
for _ in $(seq 1 50); do
    if ! sudo kill -0 "$WD_PID" 2>/dev/null; then
        WAITED=1
        break
    fi
    sleep 0.1
done

if [[ "$WAITED" == "1" ]]; then
    test_pass "watchdog exited on SIGTERM"
else
    test_fail "watchdog still running after SIGTERM"
    sudo kill -9 "$WD_PID" 2>/dev/null || true
fi

if sudo grep -q "received signal 15" "$LOG1"; then
    test_pass "SIGTERM was logged"
else
    test_fail "no SIGTERM log entry"
fi

if sudo grep -q "watchdog stopped" "$LOG1"; then
    test_pass "clean shutdown logged"
else
    test_fail "no 'watchdog stopped' log entry"
fi

WD_PID=""

# ------------------------------------------------------------------
# TEST 9: Config changes respected (short lease)
# ------------------------------------------------------------------

test_start "9: Config changes respected (lease=5s)"

# Update the env file for the second run
cat > "$ENV_FILE" <<EOF
DATABASE_URL=$DATABASE_URL
QUEUE_NAME=$QUEUE_NAME
WATCHDOG_INTERVAL_SECONDS=1
STALE_JOB_THRESHOLD_SECONDS=5
CGROUP_CLEANUP_INTERVAL_SECONDS=1
CGROUP_CLEANUP_AGE_SECONDS=10
TEMP_CLEANUP_INTERVAL_SECONDS=1
TEMP_CLEANUP_AGE_SECONDS=10
MAX_RETRY_COUNT=3
LOG_LEVEL=INFO
EOF

sudo bash -c "set -a; source $ENV_FILE; set +a; cd $REPO_ROOT/os; python3 -m watchdog.watchdog" \
    > "$LOG2" 2>&1 &
WD_PID=$!
sleep 1.5

if sudo grep -q "lease=5s" "$LOG2"; then
    test_pass "watchdog picked up lease=5s from env"
else
    test_fail "lease=5s not in startup log"
fi

# Insert a row that's only 8s old — stale under lease=5s but not 60s
SHORT_ID=$(psql_q "INSERT INTO submissions
    (user_id, problem_id, language, source_code, status,
     processing_started_at, worker_id, retry_count)
VALUES (1, 1, 'c', 'int main(){return 0;}', 'PROCESSING',
        NOW() - INTERVAL '8 seconds', 'dead-worker-4', 0)
RETURNING id")

test_info "Inserted submission id=$SHORT_ID (8s old)"

STATUS=""
for _ in $(seq 1 25); do
    STATUS=$(psql_q "SELECT status FROM submissions WHERE id = $SHORT_ID")
    [[ "$STATUS" == "PENDING" ]] && break
    sleep 0.2
done

if [[ "$STATUS" == "PENDING" ]]; then
    test_pass "8s-old row requeued under lease=5s"
else
    test_fail "row not requeued (status=$STATUS)"
fi

# Stop the second watchdog
sudo kill -TERM "$WD_PID" 2>/dev/null || true
sleep 0.5
WD_PID=""

# ------------------------------------------------------------------
# Summary
# ------------------------------------------------------------------

echo ""
echo "============================================================"
echo "  RESULTS"
echo "============================================================"
echo ""
echo -e "  ${GREEN}Passed: $PASS${NC}"
echo -e "  ${RED}Failed: $FAIL${NC}"
echo ""

if [[ "$FAIL" -eq 0 ]]; then
    echo -e "${GREEN}ALL TESTS PASSED${NC}"
    exit 0
else
    echo -e "${RED}SOME TESTS FAILED${NC}"
    echo ""
    echo "Watchdog log 1:"
    sudo tail -30 "$LOG1" 2>/dev/null || true
    echo ""
    echo "Watchdog log 2:"
    sudo tail -30 "$LOG2" 2>/dev/null || true
    exit 1
fi