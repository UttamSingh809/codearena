"""
Orphaned cgroup cleanup.

Scans CGROUP_ROOT for subdirectories that have no processes
(empty cgroup.procs) and are older than CGROUP_MIN_AGE_SEC.
Removes them.

Why age matters: a cgroup that was created 100 ms ago might be
in the middle of having its PID attached by a worker. Removing
it would break the running sandbox. We wait 5 minutes to be safe.
"""

import logging
import os
import shutil
import time

from . import config

log = logging.getLogger("watchdog.cgroup_cleaner")


def _mtime_sec(path: str) -> float:
    try:
        return time.time() - os.path.getmtime(path)
    except OSError:
        return 0.0


def _has_processes(path: str) -> bool:
    """Return True if the cgroup's cgroup.procs has any PIDs."""
    procs = os.path.join(path, "cgroup.procs")
    try:
        with open(procs, "r") as f:
            data = f.read().strip()
        return bool(data)
    except OSError:
        # If we can't read cgroup.procs, assume the cgroup is broken
        # and safe to remove.
        return False


def run_once() -> int:
    """
    Scan CGROUP_ROOT, remove orphaned empty cgroups older than
    CGROUP_MIN_AGE_SEC. Returns the number of cgroups removed.
    """
    root = config.CGROUP_ROOT
    if not os.path.isdir(root):
        log.debug("cgroup root %s does not exist; skipping", root)
        return 0

    removed = 0
    for name in os.listdir(root):
        path = os.path.join(root, name)
        if not os.path.isdir(path):
            continue

        # Skip the parent's own control files
        if not os.path.exists(os.path.join(path, "cgroup.procs")):
            continue

        age = _mtime_sec(path)
        if age < config.CGROUP_MIN_AGE_SEC:
            log.debug("cgroup %s too young (%.0fs < %ds)",
                      name, age, config.CGROUP_MIN_AGE_SEC)
            continue

        if _has_processes(path):
            log.debug("cgroup %s has processes; skipping", name)
            continue

        try:
            os.rmdir(path)
            log.info("removed orphaned cgroup %s (age=%.0fs)", name, age)
            removed += 1
        except OSError as e:
            log.warning("cannot remove cgroup %s: %s", name, e)

    return removed