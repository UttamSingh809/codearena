"""
CodeArena watchdog — shared configuration.

Reads environment variables with sane defaults. Every value that
affects behavior is configurable here.
"""

import os


def _int_env(name: str, default: int) -> int:
    v = os.environ.get(name)
    if v is None or v == "":
        return default
    try:
        return int(v)
    except ValueError:
        return default


def _str_env(name: str, default: str) -> str:
    v = os.environ.get(name)
    return v if v not in (None, "") else default


# ------------------------------------------------------------------
# Database
# ------------------------------------------------------------------

def database_url() -> str:
    """Return DATABASE_URL, or raise if not set."""
    url = os.environ.get("DATABASE_URL")
    if url:
        return url
    # Try .env in the repo root
    here = os.path.dirname(os.path.abspath(__file__))
    env_path = os.path.join(here, "..", "..", ".env")
    if os.path.exists(env_path):
        with open(env_path) as f:
            for line in f:
                line = line.strip()
                if line.startswith("DATABASE_URL="):
                    return line.split("=", 1)[1]
    raise RuntimeError("DATABASE_URL not set and not found in .env")


# ------------------------------------------------------------------
# Queue (for requeueing stale jobs)
# ------------------------------------------------------------------

QUEUE_NAME = _str_env("QUEUE_NAME", "/codearena_queue")
QUEUE_CAPACITY = _int_env("QUEUE_CAPACITY", 1024)


# ------------------------------------------------------------------
# Stale job detection
# ------------------------------------------------------------------

# How often the stale-job detector runs.
STALE_CHECK_INTERVAL_SEC = _int_env("WATCHDOG_INTERVAL_SECONDS", 5)

# A submission is stale if its processing_started_at is older than
# this many seconds.
STALE_LEASE_SEC = _int_env("STALE_JOB_THRESHOLD_SECONDS", 60)

# After this many retries, mark the submission FAILED instead of
# requeueing it.
MAX_RETRY_COUNT = _int_env("MAX_RETRY_COUNT", 3)


# ------------------------------------------------------------------
# Cgroup cleanup
# ------------------------------------------------------------------

CGROUP_ROOT = _str_env("CGROUP_ROOT", "/sys/fs/cgroup/codearena")

# How often to scan for orphaned cgroups.
CGROUP_CHECK_INTERVAL_SEC = _int_env("CGROUP_CLEANUP_INTERVAL_SECONDS", 30)

# Only remove a cgroup if it has been empty for at least this long.
# Guards against racing a worker that's mid-attach.
CGROUP_MIN_AGE_SEC = _int_env("CGROUP_CLEANUP_AGE_SECONDS", 300)


# ------------------------------------------------------------------
# Temp dir cleanup
# ------------------------------------------------------------------

TEMP_ROOT = _str_env("SANDBOX_TMP_ROOT", "/tmp/codearena")

# How often to scan for stale temp dirs.
TEMP_CHECK_INTERVAL_SEC = _int_env("TEMP_CLEANUP_INTERVAL_SECONDS", 60)

# Only remove a temp dir older than this many seconds.
TEMP_MIN_AGE_SEC = _int_env("TEMP_CLEANUP_AGE_SECONDS", 300)


# ------------------------------------------------------------------
# Logging
# ------------------------------------------------------------------

LOG_LEVEL = _str_env("LOG_LEVEL", "INFO").upper()