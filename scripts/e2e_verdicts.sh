#!/usr/bin/env bash
# ============================================================
# CodeArena — verdict coverage test
# ============================================================
#
# Submits one solution per verdict and verifies the worker
# returns the expected verdict for each.
#
# Verdicts covered: AC, WA, TLE, MLE, RE, CE
#
# Run: bash scripts/e2e_verdicts.sh
# ============================================================

set -u
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
source "$SCRIPT_DIR/lib.sh"

kill_stale_processes

trap cleanup EXIT

echo "============================================================"
echo "  CodeArena — Verdict Coverage Test"
echo "============================================================"

if ! db_reachable; then
    echo -e "${RED}ERROR:${NC} PostgreSQL not reachable"
    exit 1
fi

echo ""
echo "=== Starting services ==="
start_api || exit 1
start_worker e2e-worker-verdicts || exit 1

# Register
SUFFIX="$(date +%s)_$$"
USERNAME="e2e_verdicts_$SUFFIX"
EMAIL="e2e_verdicts_${SUFFIX}@example.com"
PASSWORD="passw0rd123"

REG=$(register_user "$USERNAME" "$EMAIL" "$PASSWORD")
TOKEN=$(echo "$REG" | python3 -c "import sys,json; print(json.load(sys.stdin).get('access_token',''))" 2>/dev/null)

if [[ -z "$TOKEN" ]]; then
    test_fail "registration failed"
    exit 1
fi
test_pass "user registered"

# ------------------------------------------------------------------
# Source code snippets
# ------------------------------------------------------------------

# AC: correct sum
SRC_AC='#include <stdio.h>
int main(){long a,b;scanf("%ld %ld",&a,&b);printf("%ld\n",a+b);return 0;}'

# WA: prints wrong answer
SRC_WA='#include <stdio.h>
int main(){long a,b;scanf("%ld %ld",&a,&b);printf("%ld\n",a-b);return 0;}'

# TLE: infinite loop
SRC_TLE='int main(){volatile long x=0;while(1)x++;return 0;}'

# MLE: allocate 1 GB and touch it. The printf forces GCC to keep the
SRC_MLE='#include <stdio.h>
#include <stdlib.h>
int main(){
    size_t sz = 1024UL * 1024 * 1024;
    volatile char *p = malloc(sz);
    if (!p) return 1;
    for (size_t i = 0; i < sz; i++) {
        p[i] = 1;
    }
    printf("last = %d\n", (int)p[sz - 1]);
    return 0;
}'

# RE: exit code 1
SRC_RE='int main(){return 1;}'

# CE: syntax error
SRC_CE='int main(){ this is not valid c code ;;; }'

# ------------------------------------------------------------------
# Helper: submit and check verdict
# ------------------------------------------------------------------

run_verdict_case() {
    local label="$1" expected="$2" source="$3"
    test_start "$label → expected $expected"

    local resp sub_id verdict
    resp=$(submit_solution "$TOKEN" 1 "c" "$source")
    sub_id=$(echo "$resp" | python3 -c "import sys,json; print(json.load(sys.stdin).get('id',''))" 2>/dev/null)

    if [[ -z "$sub_id" ]]; then
        test_fail "submission rejected: $resp"
        return 1
    fi

    test_info "submitted id=$sub_id"

    verdict=$(wait_for_verdict "$TOKEN" "$sub_id" 60)
    check_eq "verdict" "$expected" "$verdict"
}

# ------------------------------------------------------------------
# Run all cases
# ------------------------------------------------------------------

run_verdict_case "AC case"  "AC" "$SRC_AC"
run_verdict_case "WA case"  "WA" "$SRC_WA"
run_verdict_case "TLE case" "TLE" "$SRC_TLE"
run_verdict_case "MLE case" "MLE" "$SRC_MLE"
run_verdict_case "RE case"  "RE" "$SRC_RE"
run_verdict_case "CE case"  "CE" "$SRC_CE"

# Summary
echo ""
echo "============================================================"
echo -e "  Passed: ${GREEN}$PASS${NC}"
echo -e "  Failed: ${RED}$FAIL${NC}"
echo "============================================================"

[[ "$FAIL" -eq 0 ]] && echo -e "${GREEN}VERDICT TEST PASSED${NC}" && exit 0
echo -e "${RED}VERDICT TEST FAILED${NC}"
exit 1