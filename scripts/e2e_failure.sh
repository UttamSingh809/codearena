#!/usr/bin/env bash
# ============================================================
# CodeArena — failure recovery test
# ============================================================
#
# Submits a slow solution, kills the worker mid-job, and verifies
# the watchdog requeues it and a fresh worker completes it.
#
# Requires a second worker to be startable (start_worker can be
# called twice with different --id values).
#
# Run: bash scripts/e2e_failure.sh
# ============================================================

set -u
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
source "$SCRIPT_DIR/lib.sh"

kill_stale_processes

trap cleanup EXIT

# Speed up the watchdog for this test
export WATCHDOG_INTERVAL_SECONDS=2
export STALE_JOB_THRESHOLD_SECONDS=5

echo "============================================================"
echo "  CodeArena — Failure Recovery Test"
echo "============================================================"

if ! db_reachable; then
    echo -e "${RED}ERROR:${NC} PostgreSQL not reachable"
    exit 1
fi

echo ""
echo "=== Starting services ==="
start_api || exit 1
start_worker e2e-worker-A || exit 1
start_watchdog || exit 1

# Register
SUFFIX="$(date +%s)_$$"
USERNAME="e2e_fail_$SUFFIX"
EMAIL="e2e_fail_${SUFFIX}@example.com"
PASSWORD="passw0rd123"

REG=$(register_user "$USERNAME" "$EMAIL" "$PASSWORD")
TOKEN=$(echo "$REG" | python3 -c "import sys,json; print(json.load(sys.stdin).get('access_token',''))" 2>/dev/null)

if [[ -z "$TOKEN" ]]; then
    test_fail "registration failed"
    exit 1
fi
test_pass "user registered"

# ------------------------------------------------------------------
# Test: kill worker mid-job
# ------------------------------------------------------------------

test_start "Kill worker mid-job, watchdog requeues"

# Submit a solution that takes a while (sleeps 30 seconds before output).
# The watchdog lease is 5 seconds in this test, so the row will be
# considered stale after 5 seconds.
SRC='#include <stdio.h>
#include <unistd.h>
int main(){sleep(30);printf("done\n");return 0;}'

SUBMIT=$(submit_solution "$TOKEN" 1 "c" "$SRC")
SUB_ID=$(echo "$SUBMIT" | python3 -c "import sys,json; print(json.load(sys.stdin).get('id',''))" 2>/dev/null)

if [[ -z "$SUB_ID" ]]; then
    test_fail "submission failed"
    exit 1
fi
test_info "submitted id=$SUB_ID"

# Wait for the worker to claim it
WAITED=0
for _ in $(seq 1 30); do
    STATUS=$(psql_q "SELECT status FROM submissions WHERE id = $SUB_ID")
    if [[ "$STATUS" == "PROCESSING" ]]; then
        WAITED=1
        break
    fi
    sleep 0.5
done

if [[ "$WAITED" == "1" ]]; then
    test_pass "worker claimed submission (status=PROCESSING)"
else
    test_fail "worker never claimed submission"
    exit 1
fi

# Kill worker A
test_info "killing worker A (pid $WORKER_PID)"
sudo kill -9 "$WORKER_PID" 2>/dev/null || true
WORKER_PID=""
sleep 0.5

# Verify the row is still PROCESSING (worker died without committing)
STATUS=$(psql_q "SELECT status FROM submissions WHERE id = $SUB_ID")
if [[ "$STATUS" == "PROCESSING" ]]; then
    test_pass "submission still PROCESSING after worker death"
else
    test_fail "expected PROCESSING, got '$STATUS'"
fi

# Wait for the watchdog to detect and requeue
test_info "waiting for watchdog to requeue..."
REQUEUED=0
for _ in $(seq 1 30); do
    STATUS=$(psql_q "SELECT status FROM submissions WHERE id = $SUB_ID")
    RETRY=$(psql_q "SELECT retry_count FROM submissions WHERE id = $SUB_ID")
    if [[ "$STATUS" == "PENDING" ]] && [[ "$RETRY" -ge 1 ]]; then
        REQUEUED=1
        break
    fi
    sleep 0.5
done

if [[ "$REQUEUED" == "1" ]]; then
    test_pass "watchdog requeued submission (status=PENDING, retry=$RETRY)"
else
    test_fail "watchdog did not requeue (status=$STATUS retry=$RETRY)"
    sudo tail -20 "$WATCHDOG_LOG"
fi

# ------------------------------------------------------------------
# Test: second worker completes the requeued job
# ------------------------------------------------------------------

test_start "Second worker completes the requeued job"

start_worker e2e-worker-B || exit 1

# The submitted source sleeps 30s, so we need a generous timeout.
# But the sandbox timeout will kill it at ~2.5s (default problem
# time_limit_ms=2000 plus grace). So the verdict will be TLE, not AC.
# That's fine — the point is that the second worker processed it.
test_info "waiting for second worker to process..."
FINAL=""
for _ in $(seq 1 60); do
    STATUS=$(psql_q "SELECT status FROM submissions WHERE id = $SUB_ID")
    if [[ "$STATUS" == "COMPLETED" || "$STATUS" == "FAILED" ]]; then
        FINAL="$STATUS"
        break
    fi
    sleep 0.5
done

if [[ "$FINAL" == "COMPLETED" ]]; then
    test_pass "submission completed by second worker"
    VERDICT=$(psql_q "SELECT verdict FROM results WHERE submission_id = $SUB_ID")
    test_info "final verdict: $VERDICT"
elif [[ "$FINAL" == "FAILED" ]]; then
    test_fail "submission marked FAILED (expected COMPLETED)"
else
    test_fail "submission never reached terminal state (status=$STATUS)"
fi

# Verify only one result row exists
RESULT_COUNT=$(psql_q "SELECT COUNT(*) FROM results WHERE submission_id = $SUB_ID")
check_eq "exactly one result row" "1" "$RESULT_COUNT"

# Verify retry_count is at least 1
RETRY=$(psql_q "SELECT retry_count FROM submissions WHERE id = $SUB_ID")
if [[ "$RETRY" -ge 1 ]]; then
    test_pass "retry_count=$RETRY (was requeued)"
else
    test_fail "retry_count=$RETRY (expected >=1)"
fi

# Summary
echo ""
echo "============================================================"
echo -e "  Passed: ${GREEN}$PASS${NC}"
echo -e "  Failed: ${RED}$FAIL${NC}"
echo "============================================================"

[[ "$FAIL" -eq 0 ]] && echo -e "${GREEN}FAILURE TEST PASSED${NC}" && exit 0
echo -e "${RED}FAILURE TEST FAILED${NC}"
exit 1