#!/usr/bin/env bash
# ============================================================
# CodeArena - Environment Setup & Verification
# ============================================================
# Verifies that the host Linux environment supports all OS
# primitives required by the project. Run this BEFORE any
# development work. If any check fails, fix it first.
# ============================================================

set -euo pipefail

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

PASS=0
FAIL=0
WARN=0

pass() { echo -e "${GREEN}[PASS]${NC} $1"; PASS=$((PASS+1)); }
fail() { echo -e "${RED}[FAIL]${NC} $1"; FAIL=$((FAIL+1)); }
warn() { echo -e "${YELLOW}[WARN]${NC} $1"; WARN=$((WARN+1)); }
info() { echo -e "${BLUE}[INFO]${NC} $1"; }

section() {
    echo ""
    echo "============================================================"
    echo "  $1"
    echo "============================================================"
}

check_command() {
    if command -v "$1" &>/dev/null; then
        local version
        version=$("$1" --version 2>&1 | head -n1 || echo "unknown")
        pass "$1 found: $version"
        return 0
    else
        fail "$1 NOT found"
        return 1
    fi
}

# ============================================================
section "1. Operating System & Kernel"
# ============================================================

if [[ "$(uname -s)" == "Linux" ]]; then
    pass "Running on Linux"
else
    fail "Not running on Linux (found: $(uname -s))"
fi

KERNEL=$(uname -r)
info "Kernel version: $KERNEL"

# Kernel >= 5.4 for cgroups v2 maturity
KERNEL_MAJOR=$(echo "$KERNEL" | cut -d. -f1)
KERNEL_MINOR=$(echo "$KERNEL" | cut -d. -f2)
if [[ "$KERNEL_MAJOR" -gt 5 ]] || { [[ "$KERNEL_MAJOR" -eq 5 ]] && [[ "$KERNEL_MINOR" -ge 4 ]]; }; then
    pass "Kernel >= 5.4 (cgroups v2 supported)"
else
    fail "Kernel too old (need >= 5.4, found $KERNEL)"
fi

# ============================================================
section "2. cgroups v2"
# ============================================================

if [[ -f /sys/fs/cgroup/cgroup.controllers ]]; then
    pass "cgroups v2 is active (unified hierarchy)"
    CONTROLLERS=$(cat /sys/fs/cgroup/cgroup.controllers)
    info "Available controllers: $CONTROLLERS"
    for ctrl in cpu memory pids; do
        if echo "$CONTROLLERS" | grep -qw "$ctrl"; then
            pass "cgroup controller '$ctrl' available"
        else
            fail "cgroup controller '$ctrl' NOT available"
        fi
    done
else
    fail "cgroups v2 not detected (missing /sys/fs/cgroup/cgroup.controllers)"
    info "If on WSL2, ensure /etc/wsl.conf has [boot] systemd=true and restart WSL"
fi

# Check write access to cgroup tree (needed to create sub-cgroups)
CGROUP_TEST="/sys/fs/cgroup/codearena_setup_test_$$"
if mkdir -p "$CGROUP_TEST" 2>/dev/null; then
    pass "Can create cgroups (running as root or delegated)"
    rmdir "$CGROUP_TEST" 2>/dev/null || true
else
    warn "Cannot create cgroups without sudo — will need sudo for worker runtime"
    info "For development: run worker with sudo, or delegate cgroup subtree via systemd"
fi

# ============================================================
section "3. C Toolchain"
# ============================================================

check_command gcc
check_command make
check_command cmake || warn "cmake not found (optional, but recommended)"

# Check for Linux-specific headers
if [[ -f /usr/include/linux/seccomp.h ]]; then
    pass "linux/seccomp.h present (seccomp-BPF support)"
else
    fail "linux/seccomp.h missing — install linux-headers"
fi

if [[ -f /usr/include/sched.h ]]; then
    pass "sched.h present (namespace syscalls)"
else
    fail "sched.h missing"
fi

# libseccomp
if pkg-config --exists libseccomp 2>/dev/null; then
    LIBSECCOMP_VER=$(pkg-config --modversion libseccomp)
    pass "libseccomp found: $LIBSECCOMP_VER"
else
    warn "libseccomp not found — install libseccomp-dev"
fi

# ============================================================
section "4. Debugging & Observation Tools"
# ============================================================

check_command strace
check_command valgrind
check_command htop
check_command ps

# systemd-cgtop (part of systemd)
if command -v systemd-cgtop &>/dev/null; then
    pass "systemd-cgtop found"
else
    warn "systemd-cgtop not found (useful for demo)"
fi

# ipcmk (util-linux)
if command -v ipcmk &>/dev/null; then
    pass "ipcmk found (util-linux)"
else
    warn "ipcmk not found — install util-linux"
fi

# ============================================================
section "5. PostgreSQL"
# ============================================================

if command -v psql &>/dev/null; then
    PSQL_VER=$(psql --version | grep -oP '\d+\.\d+' | head -n1)
    pass "psql found: $PSQL_VER"
    PSQL_MAJOR=$(echo "$PSQL_VER" | cut -d. -f1)
    if [[ "$PSQL_MAJOR" -ge 15 ]]; then
        pass "PostgreSQL >= 15"
    else
        warn "PostgreSQL < 15 (recommend 15+)"
    fi
else
    fail "psql NOT found — install postgresql"
fi

# Check server running
if pg_isready &>/dev/null; then
    pass "PostgreSQL server is running"
else
    warn "PostgreSQL server not running — start with: sudo systemctl start postgresql"
fi

# ============================================================
section "6. Python"
# ============================================================

if command -v python3 &>/dev/null; then
    PY_VER=$(python3 --version | grep -oP '\d+\.\d+')
    PY_MAJOR=$(echo "$PY_VER" | cut -d. -f1)
    PY_MINOR=$(echo "$PY_VER" | cut -d. -f2)
    if [[ "$PY_MAJOR" -ge 3 ]] && [[ "$PY_MINOR" -ge 11 ]]; then
        pass "Python $PY_VER (>= 3.11)"
    else
        warn "Python $PY_VER (recommend 3.11+)"
    fi
else
    fail "python3 NOT found"
fi

if python3 -m venv --help &>/dev/null; then
    pass "python3-venv available"
else
    fail "python3-venv missing — install python3-venv"
fi

if command -v pip3 &>/dev/null; then
    pass "pip3 found"
else
    warn "pip3 not found"
fi

# ============================================================
section "7. POSIX IPC"
# ============================================================

if [[ -d /dev/shm ]]; then
    SHM_SIZE=$(df -h /dev/shm | tail -n1 | awk '{print $2}')
    pass "/dev/shm exists (size: $SHM_SIZE)"
else
    fail "/dev/shm missing — POSIX shared memory unavailable"
fi

if [[ -f /usr/include/semaphore.h ]]; then
    pass "semaphore.h present (POSIX semaphores)"
else
    fail "semaphore.h missing"
fi

# ============================================================
section "8. Namespace Support"
# ============================================================

# Check kernel supports namespaces
if [[ -f /proc/self/ns/pid ]]; then
    pass "PID namespaces available"
else
    fail "PID namespaces not available"
fi

if [[ -f /proc/self/ns/mnt ]]; then
    pass "Mount namespaces available"
else
    fail "Mount namespaces not available"
fi

if [[ -f /proc/self/ns/net ]]; then
    pass "Network namespaces available"
else
    fail "Network namespaces not available"
fi

if [[ -f /proc/self/ns/user ]]; then
    pass "User namespaces available"
else
    warn "User namespaces not available — may need sysctl: kernel.unprivileged_userns_clone=1"
fi

# ============================================================
section "9. Disk & Resources"
# ============================================================

# At least 10 GB free for tooling + test data
FREE_GB=$(df -BG / | tail -n1 | awk '{print $4}' | tr -d 'G')
if [[ "$FREE_GB" -ge 10 ]]; then
    pass "Disk space: ${FREE_GB}GB free"
else
    warn "Low disk space: ${FREE_GB}GB free (recommend 10GB+)"
fi

# CPU cores (affects worker count)
CORES=$(nproc)
pass "CPU cores: $CORES (recommend running $CORES workers)"

# RAM
RAM_GB=$(free -g | awk '/^Mem:/{print $2}')
pass "RAM: ${RAM_GB}GB (recommend 8GB+)"

# ============================================================
section "SUMMARY"
# ============================================================

echo ""
echo -e "  ${GREEN}Passed:  $PASS${NC}"
echo -e "  ${YELLOW}Warnings: $WARN${NC}"
echo -e "  ${RED}Failed:  $FAIL${NC}"
echo ""

if [[ "$FAIL" -gt 0 ]]; then
    echo -e "${RED}✗ Environment is NOT ready. Fix the failures above.${NC}"
    exit 1
elif [[ "$WARN" -gt 0 ]]; then
    echo -e "${YELLOW}⚠ Environment is functional but has warnings.${NC}"
    echo -e "${YELLOW}  Review warnings above before proceeding.${NC}"
    exit 0
else
    echo -e "${GREEN}✓ Environment is fully ready for CodeArena development.${NC}"
    exit 0
fi