#!/usr/bin/env python3
"""
CodeArena watchdog — main entry point.

Runs three periodic tasks in separate threads:

  1. Stale job detection (every STALE_CHECK_INTERVAL_SEC)
     Finds submissions stuck in PROCESSING beyond the lease and
     either requeues them (via DB + queue) or marks them FAILED.

  2. Cgroup cleanup (every CGROUP_CHECK_INTERVAL_SEC)
     Removes orphaned empty cgroups older than CGROUP_MIN_AGE_SEC.

  3. Temp dir cleanup (every TEMP_CHECK_INTERVAL_SEC)
     Removes stale temp dirs older than TEMP_MIN_AGE_SEC.

The process exits cleanly on SIGTERM or SIGINT.

Usage:
    DATABASE_URL=postgresql://... python3 watchdog.py
"""

import logging
import os
import signal
import sys
import threading
import time

import psycopg2

from . import config
from . import stale_jobs
from . import cgroup_cleaner
from . import temp_cleaner


# ------------------------------------------------------------------
# Logging
# ------------------------------------------------------------------

logging.basicConfig(
    level=config.LOG_LEVEL,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
    datefmt="%Y-%m-%d %H:%M:%S",
)
log = logging.getLogger("watchdog")


# ------------------------------------------------------------------
# Signal handling
# ------------------------------------------------------------------

_stop = threading.Event()


def _handle_signal(sig, frame):
    log.info("received signal %d; shutting down", sig)
    _stop.set()


signal.signal(signal.SIGTERM, _handle_signal)
signal.signal(signal.SIGINT,  _handle_signal)


# ------------------------------------------------------------------
# Periodic tasks
# ------------------------------------------------------------------

def _stale_jobs_loop():
    """Runs every STALE_CHECK_INTERVAL_SEC."""
    db_url = config.database_url()
    interval = config.STALE_CHECK_INTERVAL_SEC
    log.info("stale-jobs loop starting (interval=%ds, lease=%ds, max_retry=%d)",
             interval, config.STALE_LEASE_SEC, config.MAX_RETRY_COUNT)

    while not _stop.is_set():
        try:
            conn = psycopg2.connect(db_url)
            try:
                requeued, failed = stale_jobs.run_once(conn)
                if requeued or failed:
                    log.info("stale-jobs cycle: requeued=%d failed=%d",
                             requeued, failed)
            finally:
                conn.close()
        except Exception as e:
            log.error("stale-jobs loop error: %s", e)

        _stop.wait(interval)


def _cgroup_loop():
    """Runs every CGROUP_CHECK_INTERVAL_SEC."""
    interval = config.CGROUP_CHECK_INTERVAL_SEC
    log.info("cgroup-cleanup loop starting (interval=%ds, min_age=%ds)",
             interval, config.CGROUP_MIN_AGE_SEC)

    while not _stop.is_set():
        try:
            removed = cgroup_cleaner.run_once()
            if removed:
                log.info("cgroup-cleanup cycle: removed=%d", removed)
        except Exception as e:
            log.error("cgroup-cleanup loop error: %s", e)

        _stop.wait(interval)


def _temp_loop():
    """Runs every TEMP_CHECK_INTERVAL_SEC."""
    interval = config.TEMP_CHECK_INTERVAL_SEC
    log.info("temp-cleanup loop starting (interval=%ds, min_age=%ds)",
             interval, config.TEMP_MIN_AGE_SEC)

    while not _stop.is_set():
        try:
            removed = temp_cleaner.run_once()
            if removed:
                log.info("temp-cleanup cycle: removed=%d", removed)
        except Exception as e:
            log.error("temp-cleanup loop error: %s", e)

        _stop.wait(interval)


# ------------------------------------------------------------------
# Main
# ------------------------------------------------------------------

def main():
    log.info("watchdog starting (pid=%d)", os.getpid())

    threads = [
        threading.Thread(target=_stale_jobs_loop, name="stale-jobs", daemon=True),
        threading.Thread(target=_cgroup_loop,     name="cgroups",    daemon=True),
        threading.Thread(target=_temp_loop,       name="temp",       daemon=True),
    ]
    for t in threads:
        t.start()

    # Wait for the stop flag; threads will exit on their own.
    while not _stop.is_set():
        _stop.wait(1.0)

    log.info("waiting for threads to finish...")
    for t in threads:
        t.join(timeout=5.0)

    log.info("watchdog stopped")
    return 0


if __name__ == "__main__":
    sys.exit(main())