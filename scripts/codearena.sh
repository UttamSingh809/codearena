#!/usr/bin/env bash
# ============================================================
# CodeArena — single-command dev environment
# ============================================================
#
# Starts every service CodeArena needs:
#   1. Queue      (POSIX shared memory)
#   2. API        (FastAPI on port 8000)
#   3. Worker     (sandbox grader)
#   4. Watchdog   (stale job recovery + cleanup)
#   5. UI         (static file server on port 3000)
#
# Logs are written to $LOG_DIR and tailed to the terminal with
# a [service-name] prefix. Press Ctrl-C to stop everything.
#
# Usage:
#   bash scripts/codearena.sh            # start all
#   bash scripts/codearena.sh start      # same
#   bash scripts/codearena.sh stop       # stop all
#   bash scripts/codearena.sh status     # show what's running
#   bash scripts/codearena.sh logs <svc> # tail one service log
#
# Services can be selectively disabled with env vars:
#   NO_UI=1   NO_WATCHDOG=1   NO_WORKER=1   NO_API=1
# ============================================================

set -u

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

# ------------------------------------------------------------------
# Configuration
# ------------------------------------------------------------------

QUEUE_NAME="${QUEUE_NAME:-/codearena_queue}"
QUEUE_CAPACITY="${QUEUE_CAPACITY:-1024}"
DATABASE_URL="${DATABASE_URL:-postgresql://codearena:codearena@localhost:5432/codearena}"
JWT_SECRET="${JWT_SECRET:-$(cat "$REPO_ROOT/api/.env" 2>/dev/null | grep -E '^JWT_SECRET=' | cut -d= -f2- || true)}"
API_PORT="${API_PORT:-8000}"
UI_PORT="${UI_PORT:-3000}"
CGROUP_ROOT="${CGROUP_ROOT:-/sys/fs/cgroup/codearena}"
SANDBOX_TMP="${SANDBOX_TMP:-/tmp/codearena}"

LOG_DIR="$REPO_ROOT/.dev-logs"
PID_DIR="$REPO_ROOT/.dev-pids"
mkdir -p "$LOG_DIR" "$PID_DIR"

# ------------------------------------------------------------------
# Colours
# ------------------------------------------------------------------

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
BLUE='\033[0;34m'; DIM='\033[2m'; NC='\033[0m'

log_info()  { echo -e "${BLUE}[info]${NC}  $*"; }
log_ok()    { echo -e "${GREEN}[ok]${NC}    $*"; }
log_warn()  { echo -e "${YELLOW}[warn]${NC}  $*"; }
log_err()   { echo -e "${RED}[err]${NC}   $*"; }

# ------------------------------------------------------------------
# Per-service PID tracking
# ------------------------------------------------------------------

SERVICES=(queue api worker watchdog ui)

save_pid() {
    echo "$1" > "$PID_DIR/$2.pid"
}

get_pid() {
    if [[ -f "$PID_DIR/$1.pid" ]]; then
        cat "$PID_DIR/$1.pid"
    fi
}

is_running() {
    local pid
    pid="$(get_pid "$1")"
    [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null
}

# ------------------------------------------------------------------
# Cleanup
# ------------------------------------------------------------------

stop_all() {
    log_info "stopping services..."
    for svc in "${SERVICES[@]}"; do
        local pid
        pid="$(get_pid "$svc")"
        if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
            # SIGTERM first
            kill -TERM "$pid" 2>/dev/null || true
        fi
    done

    # Wait up to 3 seconds for graceful shutdown
    for _ in $(seq 1 30); do
        local any_alive=0
        for svc in "${SERVICES[@]}"; do
            local pid
            pid="$(get_pid "$svc")"
            if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
                any_alive=1
                break
            fi
        done
        [[ "$any_alive" -eq 0 ]] && break
        sleep 0.1
    done

    # Escalate to SIGKILL
    for svc in "${SERVICES[@]}"; do
        local pid
        pid="$(get_pid "$svc")"
        if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
            kill -KILL "$pid" 2>/dev/null || true
        fi
    done

    # Kill by pattern, to catch any stragglers not tracked by PID
    sudo pkill -9 -f "codearena-worker" 2>/dev/null || true
    sudo pkill -9 -f "watchdog.watchdog" 2>/dev/null || true
    pkill -9 -f "uvicorn src.main:app" 2>/dev/null || true
    pkill -9 -f "http.server $UI_PORT" 2>/dev/null || true

    rm -f "$PID_DIR"/*.pid
    log_ok "all services stopped"
}

# ------------------------------------------------------------------
# Prerequisites
# ------------------------------------------------------------------

check_prereqs() {
    local ok=1

    if ! pg_isready -h localhost -p 5432 >/dev/null 2>&1; then
        log_err "PostgreSQL is not running on localhost:5432"
        ok=0
    fi

    if [[ ! -d "$REPO_ROOT/api/venv" ]]; then
        log_err "api/venv not found — run: cd api && python3 -m venv venv && pip install -r requirements.txt"
        ok=0
    fi

    if [[ ! -x "$REPO_ROOT/os/worker/codearena-worker" ]]; then
        log_err "worker not built — run: make -C os/worker"
        ok=0
    fi

    if [[ ! -x "$REPO_ROOT/os/ipc/tests/test_producer" ]]; then
        log_err "IPC not built — run: make -C os/ipc"
        ok=0
    fi

    if [[ "$ok" -eq 0 ]]; then
        exit 1
    fi
}

# ------------------------------------------------------------------
# Service starters
# ------------------------------------------------------------------

start_queue() {
    if [[ -f /dev/shm/"${QUEUE_NAME#/}" ]] && is_running queue; then
        log_info "queue already exists"
        return 0
    fi

    log_info "creating queue $QUEUE_NAME (capacity=$QUEUE_CAPACITY)"
    rm -f /dev/shm/"${QUEUE_NAME#/}" /dev/shm/sem."${QUEUE_NAME#/}"_* 2>/dev/null || true
    (cd "$REPO_ROOT/os/ipc" && timeout 1 ./tests/test_producer "$QUEUE_NAME" "$QUEUE_CAPACITY" 0 >/dev/null 2>&1) || true

    if [[ ! -f /dev/shm/"${QUEUE_NAME#/}" ]]; then
        log_err "queue creation failed"
        return 1
    fi

    # The queue is a one-shot setup; no long-running process to track.
    log_ok "queue created"
}

start_api() {
    if is_running api; then
        log_info "API already running (pid $(get_pid api))"
        return 0
    fi

    if [[ -z "$JWT_SECRET" ]]; then
        log_warn "JWT_SECRET not set; generating one (tokens won't survive restart)"
        JWT_SECRET="$(python3 -c 'import secrets; print(secrets.token_urlsafe(64))')"
    fi

    log_info "starting API on :$API_PORT"
    (
        cd "$REPO_ROOT/api"
        source venv/bin/activate
        DATABASE_URL="$DATABASE_URL" \
        JWT_SECRET="$JWT_SECRET" \
        exec uvicorn src.main:app --host 0.0.0.0 --port "$API_PORT" --log-level info
    ) > "$LOG_DIR/api.log" 2>&1 &
    save_pid $! api

    # Wait for readiness
    for _ in $(seq 1 40); do
        if curl -s -o /dev/null -w "%{http_code}" "http://localhost:$API_PORT/api/health" 2>/dev/null | grep -q 200; then
            log_ok "API ready (pid $(get_pid api))"
            return 0
        fi
        if ! is_running api; then
            log_err "API died during startup — see $LOG_DIR/api.log"
            tail -20 "$LOG_DIR/api.log"
            return 1
        fi
        sleep 0.25
    done

    log_err "API did not become ready within 10s — see $LOG_DIR/api.log"
    return 1
}

start_worker() {
    if is_running worker; then
        log_info "worker already running (pid $(get_pid worker))"
        return 0
    fi

    log_info "starting worker"
    (
        cd "$REPO_ROOT/os/worker"
        exec sudo DATABASE_URL="$DATABASE_URL" \
            ./codearena-worker \
            --id dev-worker \
            --queue "$QUEUE_NAME" \
            --db "$DATABASE_URL" \
            --cgroup-root "$CGROUP_ROOT" \
            --tmp "$SANDBOX_TMP" \
            --all-ns --seccomp
    ) > "$LOG_DIR/worker.log" 2>&1 &
    save_pid $! worker

    # Wait for "waiting for jobs" in the log
    for _ in $(seq 1 40); do
        if sudo grep -q "waiting for jobs" "$LOG_DIR/worker.log" 2>/dev/null; then
            log_ok "worker ready (pid $(get_pid worker))"
            return 0
        fi
        if ! is_running worker; then
            log_err "worker died during startup — see $LOG_DIR/worker.log"
            sudo tail -20 "$LOG_DIR/worker.log"
            return 1
        fi
        sleep 0.25
    done

    log_err "worker did not become ready within 10s"
    return 1
}

start_watchdog() {
    if is_running watchdog; then
        log_info "watchdog already running (pid $(get_pid watchdog))"
        return 0
    fi

    log_info "starting watchdog"
    (
        cd "$REPO_ROOT/os"
        exec sudo env \
            DATABASE_URL="$DATABASE_URL" \
            QUEUE_NAME="$QUEUE_NAME" \
            WATCHDOG_INTERVAL_SECONDS=2 \
            STALE_JOB_THRESHOLD_SECONDS=10 \
            CGROUP_CLEANUP_INTERVAL_SECONDS=30 \
            TEMP_CLEANUP_INTERVAL_SECONDS=30 \
            python3 -m watchdog.watchdog
    ) > "$LOG_DIR/watchdog.log" 2>&1 &
    save_pid $! watchdog
    sleep 1

    if is_running watchdog; then
        log_ok "watchdog ready (pid $(get_pid watchdog))"
        return 0
    fi
    log_err "watchdog died during startup — see $LOG_DIR/watchdog.log"
    return 1
}

start_ui() {
    if is_running ui; then
        log_info "UI already running (pid $(get_pid ui))"
        return 0
    fi

    log_info "starting UI on :$UI_PORT"
    (
        cd "$REPO_ROOT"
        exec python3 -m http.server "$UI_PORT" --directory ui
    ) > "$LOG_DIR/ui.log" 2>&1 &
    save_pid $! ui

    for _ in $(seq 1 20); do
        if curl -s -o /dev/null -w "%{http_code}" "http://localhost:$UI_PORT/" 2>/dev/null | grep -qE '200|30[0-9]'; then
            log_ok "UI ready (pid $(get_pid ui))"
            return 0
        fi
        if ! is_running ui; then
            log_err "UI died during startup — see $LOG_DIR/ui.log"
            return 1
        fi
        sleep 0.25
    done
    log_err "UI did not become ready within 5s"
    return 1
}

# ------------------------------------------------------------------
# Log tailing
# ------------------------------------------------------------------

tail_logs() {
    echo ""
    log_info "tailing logs (Ctrl-C to stop all services)"
    echo ""

    # Wait until every log file exists
    for _ in $(seq 1 20); do
        local all_present=1
        for svc in api worker watchdog ui; do
            [[ -f "$LOG_DIR/$svc.log" ]] || all_present=0
        done
        [[ "$all_present" -eq 1 ]] && break
        sleep 0.25
    done

    # Use tail -F on all logs, prefix each line with its service name.
    # The 'sed' prefixes each line; tail follows files even after rotation.
    (
        tail -n 0 -F \
            "$LOG_DIR/api.log" \
            "$LOG_DIR/worker.log" \
            "$LOG_DIR/watchdog.log" \
            "$LOG_DIR/ui.log" \
        2>/dev/null \
        | awk '
            /^==> .* <==$/ {
                gsub(/^==> | <==$/, "")
                sub(/.*\//, "")
                sub(/\.log$/, "")
                svc = $0
                next
            }
            { printf "\033[2m[%s]\033[0m %s\n", svc, $0; fflush() }
        '
    ) &
    TAIL_PID=$!

    # Wait for Ctrl-C
    trap 'kill "$TAIL_PID" 2>/dev/null || true; exit 0' INT TERM
    wait "$TAIL_PID" 2>/dev/null || true
}

# ------------------------------------------------------------------
# Status
# ------------------------------------------------------------------

status_all() {
    echo "CodeArena service status"
    echo "------------------------"
    for svc in "${SERVICES[@]}"; do
        local pid
        pid="$(get_pid "$svc")"
        if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
            printf "  %-10s ${GREEN}running${NC}  (pid %s)\n" "$svc" "$pid"
        else
            printf "  %-10s ${DIM}stopped${NC}\n" "$svc"
        fi
    done

    echo ""
    echo "URLs:"
    echo "  API:  http://localhost:$API_PORT"
    echo "  Docs: http://localhost:$API_PORT/docs"
    echo "  UI:   http://localhost:$UI_PORT"

    echo ""
    echo "Logs:  $LOG_DIR/"
    echo ""
}

# ------------------------------------------------------------------
# Main
# ------------------------------------------------------------------

cmd="${1:-start}"

case "$cmd" in
    start)
        # Trap Ctrl-C so stop_all runs
        trap 'stop_all; exit 0' INT TERM

        check_prereqs

        echo ""
        echo "============================================================"
        echo "  CodeArena — starting all services"
        echo "============================================================"
        echo ""

        start_queue    || exit 1
        start_api      || { stop_all; exit 1; }
        start_worker   || { stop_all; exit 1; }
        start_watchdog || { stop_all; exit 1; }
        start_ui       || { stop_all; exit 1; }

        echo ""
        echo "============================================================"
        echo -e "  ${GREEN}Everything is running.${NC}  Open: ${BLUE}http://localhost:$UI_PORT${NC}"
        echo "============================================================"
        echo ""

        status_all

        echo "Press Ctrl-C to stop all services."
        echo ""

        tail_logs
        ;;

    stop)
        stop_all
        ;;

    status)
        status_all
        ;;

    logs)
        svc="${2:-}"
        if [[ -z "$svc" ]]; then
            log_err "usage: $0 logs <api|worker|watchdog|ui>"
            exit 1
        fi
        tail -F "$LOG_DIR/$svc.log"
        ;;

    restart)
        stop_all
        sleep 1
        exec "$0" start
        ;;

    *)
        echo "usage: $0 [start|stop|status|logs <svc>|restart]"
        exit 1
        ;;
esac