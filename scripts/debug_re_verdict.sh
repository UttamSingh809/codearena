#!/usr/bin/env bash
# Debug why `int main(){return 1;}` produces FAILED instead of RE.

set -u
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export DATABASE_URL="${DATABASE_URL:-postgresql://codearena:codearena@localhost:5432/codearena}"

echo "=== Compile the RE test program ==="
mkdir -p /tmp/codearena_re_test
cat > /tmp/codearena_re_test/sol.c <<'EOF'
int main(){return 1;}
EOF
gcc -O2 -std=c11 -o /tmp/codearena_re_test/sol /tmp/codearena_re_test/sol.c
echo "Compiled: $(ls -la /tmp/codearena_re_test/sol)"

echo ""
echo "=== Run sandbox directly, no namespaces, no seccomp ==="
cd "$REPO_ROOT/os/sandbox"
sudo ./sandbox \
    --binary /tmp/codearena_re_test/sol \
    --no-cgroup \
    --cpu-seconds 4 \
    --max-pids 64 \
    --time 2500 \
    --grace-ms 500
echo "exit=$?"

echo ""
echo "=== Run sandbox with cgroup ==="
sudo ./sandbox \
    --binary /tmp/codearena_re_test/sol \
    --cgroup-root /sys/fs/cgroup/codearena \
    --cgroup-name debug_re \
    --cgroup-cpu 100 \
    --cgroup-memory 256 \
    --cgroup-pids 64 \
    --cpu-seconds 4 \
    --max-pids 64 \
    --time 2500 \
    --grace-ms 500
echo "exit=$?"

echo ""
echo "=== Run sandbox with all namespaces + seccomp ==="
sudo ./sandbox \
    --binary /tmp/codearena_re_test/sol \
    --cgroup-root /sys/fs/cgroup/codearena \
    --cgroup-name debug_re_ns \
    --cgroup-cpu 100 \
    --cgroup-memory 256 \
    --cgroup-pids 64 \
    --cpu-seconds 4 \
    --max-pids 64 \
    --all-ns \
    --seccomp \
    --time 2500 \
    --grace-ms 500
echo "exit=$?"