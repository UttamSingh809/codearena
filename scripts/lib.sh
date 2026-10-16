#!/usr/bin/env bash
# ============================================================
# CodeArena — shared test helpers
# ============================================================
#
# Sourced by e2e_*.sh scripts. Provides:
#   - Service lifecycle (start/stop postgres deps, worker, API)
#   - HTTP helpers (curl wrappers with retries)
#   - Assertions (check_eq, check_contains, check_status)
#   - Cleanup trap
# ============================================================

set -u

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
API_BASE="${API_BASE:-http://localhost:8000}"
QUEUE_NAME="${QUEUE_NAME:-/codearena_queue}"
DATABASE_URL="${DATABASE_URL:-postgresql://codearena:codearena@localhost:5432/codearena}"
SANDBOX_TMP_ROOT="${SANDBOX_TMP_ROOT:-/tmp/codearena}"
CGROUP_ROOT="${CGROUP_ROOT:-/sys/fs/cgroup/codearena}"

WORKER_LOG="/tmp/codearena_e2e_worker.log"
API_LOG="/tmp/codearena_e2e_api.log"
WATCHDOG_LOG="/tmp/codearena_e2e_watchdog.log"

WORKER_PID=""
API_PID=""
WATCHDOG_PID=""

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; BLUE='\033[0;34m'; NC='\033[0m'

# ------------------------------------------------------------------
# Assertions
# ------------------------------------------------------------------

PASS=0
FAIL=0

test_start() { echo ""; echo -e "${BLUE}[TEST]${NC} $1"; }
test_pass() { PASS=$((PASS+1)); echo -e "  ${GREEN}[PASS]${NC} $1"; }
test_fail() { FAIL=$((FAIL+1)); echo -e "  ${RED}[FAIL]${NC} $1"; }
test_info() { echo -e "  ${YELLOW}[INFO]${NC} $1"; }

check_eq() {
    local name="$1" expected="$2" actual="$3"
    if [[ "$expected" == "$actual" ]]; then
        test_pass "$name"
    else
        test_fail "$name (expected '$expected', got '$actual')"
    fi
}

check_contains() {
    local name="$1" needle="$2" haystack="$3"
    if echo "$haystack" | grep -q "$needle"; then
        test_pass "$name"
    else
        test_fail "$name (needle '$needle' not found)"
    fi
}

check_nonempty() {
    local name="$1" value="$2"
    if [[ -n "$value" ]]; then
        test_pass "$name"
    else
        test_fail "$name (empty)"
    fi
}

# ------------------------------------------------------------------
# Database
# ------------------------------------------------------------------

psql_q() {
    PGPASSWORD=codearena psql -h localhost -U codearena -d codearena -t -A -q -c "$1" \
        | head -n 1
}

db_reachable() {
    psql_q "SELECT 1" >/dev/null 2>&1
}

# ------------------------------------------------------------------
# Service lifecycle
# ------------------------------------------------------------------

kill_stale_processes() {
    echo "=== Killing stale processes ==="
    sudo pkill -9 -f "codearena-worker" 2>/dev/null || true
    sudo pkill -9 -f "watchdog.watchdog" 2>/dev/null || true
    pkill -9 -f "uvicorn src.main:app" 2>/dev/null || true
    pkill -9 -f "test_producer /codearena_queue" 2>/dev/null || true
    pkill -9 -f "test_consumer /codearena_queue" 2>/dev/null || true
    sleep 1
    local remaining
    remaining=$(ps aux | grep -E 'codearena-worker|watchdog.watchdog|uvicorn src.main:app' | grep -v grep | wc -l)
    if [[ "$remaining" -gt 0 ]]; then
        echo "  WARNING: $remaining stale processes remain"
        ps aux | grep -E 'codearena-worker|watchdog.watchdog|uvicorn' | grep -v grep || true
    else
        echo "  no stale processes"
    fi
}
ensure_temp_root() {
    local tmp_root="${SANDBOX_TMP_ROOT:-/tmp/codearena}"
    if [[ ! -d "$tmp_root" ]]; then
        echo "  [INFO] creating temp root $tmp_root"
        mkdir -p "$tmp_root"
        chmod 1777 "$tmp_root"   # world-writable, sticky bit (like /tmp)
    fi
}
ensure_cgroup_root() {
    local cg_root="${CGROUP_ROOT:-/sys/fs/cgroup/codearena}"
    if [[ ! -d "$cg_root" ]]; then
        echo "  [INFO] creating cgroup root $cg_root"
        sudo mkdir -p "$cg_root"
        # Enable controllers on the subtree
        echo "+cpu +memory +pids" | sudo tee "$cg_root/cgroup.subtree_control" >/dev/null 2>&1 || true
    fi
}

ensure_queue() {
    # Always recreate the queue so no stale descriptors or semaphore
    # state leaks between test runs.
    rm -f /dev/shm/"${QUEUE_NAME#/}" \
          /dev/shm/sem."${QUEUE_NAME#/}"_* 2>/dev/null || true

    test_info "creating fresh queue $QUEUE_NAME"
    cd "$REPO_ROOT/os/ipc"
    timeout 1 ./tests/test_producer "$QUEUE_NAME" 1024 0 2>/dev/null || true
    cd "$REPO_ROOT"

    if [[ ! -f /dev/shm/"${QUEUE_NAME#/}" ]]; then
        test_fail "failed to create queue"
        return 1
    fi
    return 0
}

start_worker() {
    local wid="${1:-e2e-worker-1}"
    ensure_temp_root
    ensure_cgroup_root
    test_info "starting worker $wid"
    cd "$REPO_ROOT/os/worker"
    sudo DATABASE_URL="$DATABASE_URL" \
        ./codearena-worker \
        --id "$wid" \
        --queue "$QUEUE_NAME" \
        --db "$DATABASE_URL" \
        --cgroup-root /sys/fs/cgroup/codearena \
        --tmp /tmp/codearena \
        --all-ns --seccomp \
        > "$WORKER_LOG" 2>&1 &
    WORKER_PID=$!
    cd "$REPO_ROOT"

    for _ in $(seq 1 20); do
        if sudo grep -q "waiting for jobs" "$WORKER_LOG" 2>/dev/null; then
            break
        fi
        sleep 0.2
    done

    if ! sudo kill -0 "$WORKER_PID" 2>/dev/null; then
        test_fail "worker failed to start"
        sudo tail -20 "$WORKER_LOG"
        return 1
    fi
    test_pass "worker running (pid $WORKER_PID)"
}

start_api() {
    ensure_queue || return 1
    test_info "starting API"
    cd "$REPO_ROOT/api"
    # shellcheck disable=SC1091
    source venv/bin/activate
    export DATABASE_URL
    export JWT_SECRET="${JWT_SECRET:-$(python3 -c 'import secrets; print(secrets.token_urlsafe(64))')}"
    uvicorn src.main:app --host 0.0.0.0 --port 8000 --log-level warning \
        > "$API_LOG" 2>&1 &
    API_PID=$!
    cd "$REPO_ROOT"

    for _ in $(seq 1 30); do
        if curl -s -o /dev/null -w "%{http_code}" "$API_BASE/api/health" 2>/dev/null | grep -q 200; then
            break
        fi
        sleep 0.5
    done

    if ! kill -0 "$API_PID" 2>/dev/null; then
        test_fail "API failed to start"
        tail -20 "$API_LOG"
        return 1
    fi
    test_pass "API running (pid $API_PID)"
}

start_watchdog() {
    test_info "starting watchdog"
    cd "$REPO_ROOT/os"
    sudo env \
        DATABASE_URL="$DATABASE_URL" \
        QUEUE_NAME="$QUEUE_NAME" \
        WATCHDOG_INTERVAL_SECONDS="${WATCHDOG_INTERVAL_SECONDS:-2}" \
        STALE_JOB_THRESHOLD_SECONDS="${STALE_JOB_THRESHOLD_SECONDS:-10}" \
        CGROUP_CLEANUP_INTERVAL_SECONDS=30 \
        TEMP_CLEANUP_INTERVAL_SECONDS=30 \
        python3 -m watchdog.watchdog > "$WATCHDOG_LOG" 2>&1 &
    WATCHDOG_PID=$!
    cd "$REPO_ROOT"
    sleep 1

    if ! sudo kill -0 "$WATCHDOG_PID" 2>/dev/null; then
        test_fail "watchdog failed to start"
        sudo tail -20 "$WATCHDOG_LOG"
        return 1
    fi
    test_pass "watchdog running (pid $WATCHDOG_PID)"
}

stop_all() {
    for pid_var in WORKER_PID API_PID WATCHDOG_PID; do
        local pid="${!pid_var}"
        if [[ -n "$pid" ]]; then
            if sudo kill -0 "$pid" 2>/dev/null; then
                sudo kill -TERM "$pid" 2>/dev/null || true
            fi
            eval "$pid_var="
        fi
    done
    sleep 1

    # Escalate: kill anything matching the service patterns, by
    # process name, using SIGKILL. This catches stray sudo wrappers
    # and processes that ignored SIGTERM.
    sudo pkill -9 -f "codearena-worker" 2>/dev/null || true
    sudo pkill -9 -f "watchdog.watchdog" 2>/dev/null || true
    pkill -9 -f "uvicorn src.main:app" 2>/dev/null || true
}

cleanup() {
    local exit_code=$?
    echo ""
    echo "=== Cleanup ==="
    stop_all

    # Remove test data
    psql_q "DELETE FROM results WHERE graded_by LIKE 'e2e-%' OR graded_by = 'watchdog'" >/dev/null 2>&1 || true
    psql_q "DELETE FROM submissions WHERE worker_id LIKE 'e2e-%' OR source_code LIKE '%E2E_MARKER%'" >/dev/null 2>&1 || true
    psql_q "DELETE FROM users WHERE username LIKE 'e2e_%'" >/dev/null 2>&1 || true

    # Temp dirs
    sudo rm -rf /tmp/codearena/e2e_* 2>/dev/null || true

    if [[ "$exit_code" -eq 0 ]]; then
        sudo rm -f "$WORKER_LOG" "$API_LOG" "$WATCHDOG_LOG" 2>/dev/null || true
    else
        echo "  Logs preserved (exit=$exit_code):"
        echo "    API:      $API_LOG"
        echo "    Worker:   $WORKER_LOG"
        echo "    Watchdog: $WATCHDOG_LOG"
    fi

    echo "Cleanup done."
}

# ------------------------------------------------------------------
# HTTP helpers
# ------------------------------------------------------------------

register_user() {
    local username="$1" email="$2" password="$3"
    curl -s -X POST "$API_BASE/api/auth/register" \
        -H "Content-Type: application/json" \
        -d "{\"username\":\"$username\",\"email\":\"$email\",\"password\":\"$password\"}"
}

login_user() {
    local email="$1" password="$2"
    curl -s -X POST "$API_BASE/api/auth/login" \
        -H "Content-Type: application/json" \
        -d "{\"email\":\"$email\",\"password\":\"$password\"}"
}

submit_solution() {
    local token="$1" problem_id="$2" language="$3" source="$4"
    # Escape source for JSON
    local escaped
    escaped=$(python3 -c "import json,sys; print(json.dumps(sys.stdin.read()))" <<< "$source")
    curl -s -X POST "$API_BASE/api/submissions" \
        -H "Authorization: Bearer $token" \
        -H "Content-Type: application/json" \
        -d "{\"problem_id\":$problem_id,\"language\":\"$language\",\"source_code\":$escaped}"
}

wait_for_verdict() {
    local token="$1" submission_id="$2" timeout="${3:-30}"
    local elapsed=0
    while [[ "$elapsed" -lt "$timeout" ]]; do
        local resp
        resp=$(curl -s "$API_BASE/api/submissions/$submission_id" \
            -H "Authorization: Bearer $token")
        local status verdict
        status=$(echo "$resp" | python3 -c "import sys,json; print(json.load(sys.stdin).get('status',''))" 2>/dev/null)
        verdict=$(echo "$resp" | python3 -c "import sys,json; r=json.load(sys.stdin).get('result'); print(r['verdict'] if r else '')" 2>/dev/null)
        if [[ "$status" == "COMPLETED" || "$status" == "FAILED" ]]; then
            echo "$verdict"
            return 0
        fi
        sleep 1
        elapsed=$((elapsed + 1))
    done
    echo "TIMEOUT"
    return 1
}