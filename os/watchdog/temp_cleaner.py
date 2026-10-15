"""
Stale temp dir cleanup.

Scans TEMP_ROOT for subdirectories older than TEMP_MIN_AGE_SEC
and removes them recursively.

The worker creates one dir per submission under TEMP_ROOT named
sub_<id>_<pid>, compiles the source there, runs the sandbox, and
removes the dir on success. If the worker crashes, the dir is
left behind. This cleaner reclaims that space.
"""

import logging
import os
import shutil
import time

from . import config

log = logging.getLogger("watchdog.temp_cleaner")


def _age_sec(path: str) -> float:
    try:
        return time.time() - os.path.getmtime(path)
    except OSError:
        return 0.0


def run_once() -> int:
    """Scan TEMP_ROOT, remove stale dirs, return count removed."""
    root = config.TEMP_ROOT
    if not os.path.isdir(root):
        log.debug("temp root %s does not exist; skipping", root)
        return 0

    removed = 0
    for name in os.listdir(root):
        path = os.path.join(root, name)
        if not os.path.isdir(path):
            continue

        age = _age_sec(path)
        if age < config.TEMP_MIN_AGE_SEC:
            continue

        try:
            shutil.rmtree(path)
            log.info("removed stale temp dir %s (age=%.0fs)", name, age)
            removed += 1
        except OSError as e:
            log.warning("cannot remove temp dir %s: %s", name, e)

    return removed