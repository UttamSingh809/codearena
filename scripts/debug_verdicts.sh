#!/usr/bin/env bash
set -u
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT/os/sandbox"

test_case() {
    local label="$1"
    local expected="$2"
    local src="$3"

    local dir="/tmp/codearena_${label}"
    mkdir -p "$dir"
    printf '%s\n' "$src" > "$dir/sol.c"
    gcc -O2 -std=c11 -o "$dir/sol" "$dir/sol.c" 2>/dev/null

    echo ""
    echo "============================================================"
    echo "  Case: $label (expected: $expected)"
    echo "============================================================"

    echo "--- no namespaces, no cgroup, no seccomp ---"
    sudo ./sandbox --binary "$dir/sol" --no-cgroup \
        --cpu-seconds 4 --max-pids 64 --time 2500 --grace-ms 500 2>&1 | \
        grep -E "verdict:|exit_code:|signal:|timed_out:|oom_killed:|ns_setup" || true

    echo "--- cgroup only ---"
    sudo ./sandbox --binary "$dir/sol" \
        --cgroup-root /sys/fs/cgroup/codearena \
        --cgroup-name "debug_${label}_cg" \
        --cgroup-cpu 100 --cgroup-memory 256 --cgroup-pids 64 \
        --cpu-seconds 4 --max-pids 64 --time 2500 --grace-ms 500 2>&1 | \
        grep -E "verdict:|exit_code:|signal:|timed_out:|oom_killed:|ns_setup" || true

    echo "--- all namespaces + seccomp ---"
    sudo ./sandbox --binary "$dir/sol" \
        --cgroup-root /sys/fs/cgroup/codearena \
        --cgroup-name "debug_${label}_ns" \
        --cgroup-cpu 100 --cgroup-memory 256 --cgroup-pids 64 \
        --cpu-seconds 4 --max-pids 64 \
        --all-ns --seccomp \
        --time 2500 --grace-ms 500 2>&1 | \
        grep -E "verdict:|exit_code:|signal:|timed_out:|oom_killed:|ns_setup" || true
}

test_case "re"  "RE"  'int main(){return 1;}'
test_case "tle" "TLE" 'int main(){volatile long x=0;while(1)x++;return 0;}'
test_case "mle" "MLE" '#include <stdlib.h>
#include <string.h>
int main(){size_t sz=1024UL*1024*1024;char*p=malloc(sz);if(!p)return 1;memset(p,0,sz);return 0;}'
