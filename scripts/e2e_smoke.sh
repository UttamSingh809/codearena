#!/usr/bin/env bash
# ============================================================
# CodeArena — e2e smoke test
# ============================================================
#
# Minimal end-to-end check: start services, submit one correct
# solution, verify the verdict is AC.
#
# Run: bash scripts/e2e_smoke.sh
# ============================================================

set -u
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
source "$SCRIPT_DIR/lib.sh"
source "$SCRIPT_DIR/lib.sh"

kill_stale_processes

trap cleanup EXIT

echo "============================================================"
echo "  CodeArena — End-to-End Smoke Test"
echo "============================================================"

# Prerequisites
if ! db_reachable; then
    echo -e "${RED}ERROR:${NC} PostgreSQL not reachable"
    exit 1
fi

# Start services
echo ""
echo "=== Starting services ==="
start_api  || exit 1
start_worker e2e-worker-smoke || exit 1

# Register
echo ""
echo "=== Submitting solution ==="
SUFFIX="$(date +%s)_$$"
USERNAME="e2e_smoke_$SUFFIX"
EMAIL="e2e_smoke_${SUFFIX}@example.com"
PASSWORD="passw0rd123"

REG=$(register_user "$USERNAME" "$EMAIL" "$PASSWORD")
TOKEN=$(echo "$REG" | python3 -c "import sys,json; print(json.load(sys.stdin).get('access_token',''))" 2>/dev/null)

if [[ -z "$TOKEN" ]]; then
    test_fail "registration failed"
    echo "$REG"
    exit 1
fi

test_pass "user registered: $USERNAME"

# Submit correct solution to Problem 1
SRC='#include <stdio.h>
int main(){int a,b;scanf("%d %d",&a,&b);printf("%d\n",a+b);return 0;}'

SUBMIT=$(submit_solution "$TOKEN" 1 "c" "$SRC")
SUB_ID=$(echo "$SUBMIT" | python3 -c "import sys,json; print(json.load(sys.stdin).get('id',''))" 2>/dev/null)

if [[ -z "$SUB_ID" ]]; then
    test_fail "submission failed"
    echo "$SUBMIT"
    exit 1
fi

test_info "submitted id=$SUB_ID"
test_pass "submission created"

# Wait for verdict
echo "  waiting for verdict..."
VERDICT=$(wait_for_verdict "$TOKEN" "$SUB_ID" 30)

check_eq "verdict is AC" "AC" "$VERDICT"

# Summary
echo ""
echo "============================================================"
echo -e "  Passed: ${GREEN}$PASS${NC}"
echo -e "  Failed: ${RED}$FAIL${NC}"
echo "============================================================"

[[ "$FAIL" -eq 0 ]] && echo -e "${GREEN}SMOKE TEST PASSED${NC}" && exit 0
echo -e "${RED}SMOKE TEST FAILED${NC}"
exit 1