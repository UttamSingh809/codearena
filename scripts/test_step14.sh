#!/usr/bin/env bash
# ============================================================
# CodeArena — Step 14 API integration test
# ============================================================
#
# Assumes:
#   - PostgreSQL is running, schema migrated, seeds loaded
#   - The IPC queue exists
#   - The API is running at http://localhost:8000
#
# Verifies: health, register, login, me, problems, submit,
# get submission, history, leaderboard.
# ============================================================

set -u

BASE="${API_BASE:-http://localhost:8000}"
PASS=0
FAIL=0

RED='\033[0;31m'; GREEN='\033[0;32m'; BLUE='\033[0;34m'; NC='\033[0m'

test_start() { echo ""; echo -e "${BLUE}[TEST]${NC} $1"; }
test_pass() { PASS=$((PASS+1)); echo -e "  ${GREEN}[PASS]${NC} $1"; }
test_fail() { FAIL=$((FAIL+1)); echo -e "  ${RED}[FAIL]${NC} $1"; }

check_eq() {
    local name="$1" expected="$2" actual="$3"
    if [[ "$expected" == "$actual" ]]; then
        test_pass "$name"
    else
        test_fail "$name (expected '$expected', got '$actual')"
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

# Unique email per run
SUFFIX="$(date +%s)_$$"
USERNAME="step14user_$SUFFIX"
EMAIL="step14_${SUFFIX}@example.com"
PASSWORD="passw0rd123"

echo "============================================================"
echo "  CodeArena Step 14 — API Integration Test"
echo "============================================================"
echo "Base URL: $BASE"
echo "Test user: $EMAIL"

# ------------------------------------------------------------------
# Test 1: Health
# ------------------------------------------------------------------

test_start "1: Health check"
HEALTH=$(curl -s "$BASE/api/health")
DB_STATUS=$(echo "$HEALTH" | python3 -c "import sys, json; print(json.load(sys.stdin).get('database', ''))" 2>/dev/null)
check_eq "database=up" "up" "$DB_STATUS"

# ------------------------------------------------------------------
# Test 2: Register
# ------------------------------------------------------------------

test_start "2: Register user"
REG_RESP=$(curl -s -X POST "$BASE/api/auth/register" \
    -H "Content-Type: application/json" \
    -d "{\"username\":\"$USERNAME\",\"email\":\"$EMAIL\",\"password\":\"$PASSWORD\"}")

TOKEN=$(echo "$REG_RESP" | python3 -c "import sys, json; print(json.load(sys.stdin).get('access_token', ''))" 2>/dev/null)
check_nonempty "token returned" "$TOKEN"

USER_ID=$(echo "$REG_RESP" | python3 -c "import sys, json; print(json.load(sys.stdin).get('user', {}).get('id', ''))" 2>/dev/null)
check_nonempty "user id returned" "$USER_ID"

# ------------------------------------------------------------------
# Test 3: Duplicate registration rejected
# ------------------------------------------------------------------

test_start "3: Duplicate registration rejected"
DUP_CODE=$(curl -s -o /dev/null -w "%{http_code}" \
    -X POST "$BASE/api/auth/register" \
    -H "Content-Type: application/json" \
    -d "{\"username\":\"$USERNAME\",\"email\":\"$EMAIL\",\"password\":\"$PASSWORD\"}")
check_eq "409 conflict" "409" "$DUP_CODE"

# ------------------------------------------------------------------
# Test 4: Login
# ------------------------------------------------------------------

test_start "4: Login"
LOGIN_RESP=$(curl -s -X POST "$BASE/api/auth/login" \
    -H "Content-Type: application/json" \
    -d "{\"email\":\"$EMAIL\",\"password\":\"$PASSWORD\"}")
LOGIN_TOKEN=$(echo "$LOGIN_RESP" | python3 -c "import sys, json; print(json.load(sys.stdin).get('access_token', ''))" 2>/dev/null)
check_nonempty "login token returned" "$LOGIN_TOKEN"

# ------------------------------------------------------------------
# Test 5: Wrong password rejected
# ------------------------------------------------------------------

test_start "5: Wrong password rejected"
BAD_CODE=$(curl -s -o /dev/null -w "%{http_code}" \
    -X POST "$BASE/api/auth/login" \
    -H "Content-Type: application/json" \
    -d "{\"email\":\"$EMAIL\",\"password\":\"wrongpass99\"}")
check_eq "401 unauthorized" "401" "$BAD_CODE"

# ------------------------------------------------------------------
# Test 6: /me
# ------------------------------------------------------------------

test_start "6: Authenticated /me"
ME_RESP=$(curl -s "$BASE/api/auth/me" -H "Authorization: Bearer $TOKEN")
ME_USERNAME=$(echo "$ME_RESP" | python3 -c "import sys, json; print(json.load(sys.stdin).get('username', ''))" 2>/dev/null)
check_eq "username matches" "$USERNAME" "$ME_USERNAME"

# ------------------------------------------------------------------
# Test 7: Unauthenticated /me rejected
# ------------------------------------------------------------------

test_start "7: Unauthenticated /me rejected"
NOAUTH_CODE=$(curl -s -o /dev/null -w "%{http_code}" "$BASE/api/auth/me")
check_eq "401 without token" "401" "$NOAUTH_CODE"

# ------------------------------------------------------------------
# Test 8: List problems
# ------------------------------------------------------------------

test_start "8: List problems"
PROB_LIST=$(curl -s "$BASE/api/problems" -H "Authorization: Bearer $TOKEN")
PROB_COUNT=$(echo "$PROB_LIST" | python3 -c "import sys, json; print(len(json.load(sys.stdin)))" 2>/dev/null)
if [[ "$PROB_COUNT" -ge 1 ]]; then
    test_pass "at least one problem returned ($PROB_COUNT)"
else
    test_fail "no problems returned"
fi

FIRST_PROBLEM_ID=$(echo "$PROB_LIST" | python3 -c "import sys, json; print(json.load(sys.stdin)[0]['id'])" 2>/dev/null)
check_nonempty "first problem id" "$FIRST_PROBLEM_ID"

# ------------------------------------------------------------------
# Test 9: Get problem detail
# ------------------------------------------------------------------

test_start "9: Get problem detail"
PROB_DETAIL=$(curl -s "$BASE/api/problems/$FIRST_PROBLEM_ID" \
    -H "Authorization: Bearer $TOKEN")
PROB_TITLE=$(echo "$PROB_DETAIL" | python3 -c "import sys, json; print(json.load(sys.stdin).get('title', ''))" 2>/dev/null)
check_nonempty "problem title" "$PROB_TITLE"

# ------------------------------------------------------------------
# Test 10: Submit solution
# ------------------------------------------------------------------

test_start "10: Submit solution"

# Build the JSON with escaped newlines
SRC_CODE='#include <stdio.h>\nint main(){int a,b;scanf("%d %d",&a,&b);printf("%d\\n",a+b);return 0;}'

SUBMIT_RESP=$(curl -s -X POST "$BASE/api/submissions" \
    -H "Authorization: Bearer $TOKEN" \
    -H "Content-Type: application/json" \
    -d "{\"problem_id\":$FIRST_PROBLEM_ID,\"language\":\"c\",\"source_code\":\"$SRC_CODE\"}")

SUB_ID=$(echo "$SUBMIT_RESP" | python3 -c "import sys, json; print(json.load(sys.stdin).get('id', ''))" 2>/dev/null)
check_nonempty "submission id returned" "$SUB_ID"

SUB_STATUS=$(echo "$SUBMIT_RESP" | python3 -c "import sys, json; print(json.load(sys.stdin).get('status', ''))" 2>/dev/null)
check_eq "initial status=PENDING" "PENDING" "$SUB_STATUS"

# ------------------------------------------------------------------
# Test 11: Get submission
# ------------------------------------------------------------------

test_start "11: Get submission detail"
GET_SUB=$(curl -s "$BASE/api/submissions/$SUB_ID" \
    -H "Authorization: Bearer $TOKEN")
GOT_ID=$(echo "$GET_SUB" | python3 -c "import sys, json; print(json.load(sys.stdin).get('id', ''))" 2>/dev/null)
check_eq "submission id matches" "$SUB_ID" "$GOT_ID"

# ------------------------------------------------------------------
# Test 12: List my submissions
# ------------------------------------------------------------------

test_start "12: List my submissions"
LIST_RESP=$(curl -s "$BASE/api/submissions?limit=10" \
    -H "Authorization: Bearer $TOKEN")
LIST_TOTAL=$(echo "$LIST_RESP" | python3 -c "import sys, json; print(json.load(sys.stdin).get('total', -1))" 2>/dev/null)
if [[ "$LIST_TOTAL" -ge 1 ]]; then
    test_pass "at least one submission ($LIST_TOTAL total)"
else
    test_fail "no submissions in list"
fi

# ------------------------------------------------------------------
# Test 13: Leaderboard (empty is fine)
# ------------------------------------------------------------------

test_start "13: Global leaderboard"
LB_RESP=$(curl -s "$BASE/api/leaderboard" -H "Authorization: Bearer $TOKEN")
LB_TYPE=$(echo "$LB_RESP" | python3 -c "import sys, json; print(type(json.load(sys.stdin)).__name__)" 2>/dev/null)
check_eq "returns a list" "list" "$LB_TYPE"

# ------------------------------------------------------------------
# Test 14: Invalid submission rejected
# ------------------------------------------------------------------

test_start "14: Invalid language rejected"
BAD_SUB_CODE=$(curl -s -o /dev/null -w "%{http_code}" \
    -X POST "$BASE/api/submissions" \
    -H "Authorization: Bearer $TOKEN" \
    -H "Content-Type: application/json" \
    -d "{\"problem_id\":$FIRST_PROBLEM_ID,\"language\":\"brainfuck\",\"source_code\":\"+.\"}")
# FastAPI returns 422 for pydantic validation errors.
check_eq "422 validation error" "422" "$BAD_SUB_CODE"

# ------------------------------------------------------------------
# Test 15: Nonexistent submission returns 404
# ------------------------------------------------------------------

test_start "15: Nonexistent submission 404"
NOTFOUND_CODE=$(curl -s -o /dev/null -w "%{http_code}" \
    "$BASE/api/submissions/9999999" \
    -H "Authorization: Bearer $TOKEN")
check_eq "404 not found" "404" "$NOTFOUND_CODE"

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
    echo ""
    echo "Test user: $USERNAME / $EMAIL"
    echo "Test submission id: $SUB_ID"
    exit 0
else
    echo -e "${RED}SOME TESTS FAILED${NC}"
    exit 1
fi