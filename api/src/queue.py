"""
CodeArena API — IPC queue writer.

Reuses the same queue protocol as the watchdog: the writer module
lives under os/watchdog and is imported here. This guarantees the
binary layout matches byte-for-byte across both producers.

The queue must already exist (created by the deployment script or
by the first producer). If it does not, the API fails at startup.
"""

import logging
import sys
from pathlib import Path
from typing import Optional

# Add os/watchdog to sys.path so we can import queue_writer.
# The watchdog package lives at <repo>/os/watchdog.
_HERE = Path(__file__).resolve().parent
_REPO = _HERE.parent.parent
_WATCHDOG_DIR = _REPO / "os" / "watchdog"

if str(_WATCHDOG_DIR.parent) not in sys.path:
    sys.path.insert(0, str(_WATCHDOG_DIR.parent))

from watchdog.queue_writer import QueueWriter  # type: ignore

log = logging.getLogger("api.queue")


_writer: Optional[QueueWriter] = None


def open_queue() -> QueueWriter:
    """Attach to the queue. Called once at startup."""
    global _writer
    if _writer is not None:
        return _writer
    _writer = QueueWriter()
    log.info("queue writer attached")
    return _writer


def close_queue() -> None:
    """Detach from the queue. Called once at shutdown."""
    global _writer
    if _writer is None:
        return
    _writer.close()
    _writer = None
    log.info("queue writer closed")


def enqueue_submission(
    submission_id: int,
    problem_id: int,
    language: str,
    time_limit_ms: int,
    memory_limit_mb: int,
    output_limit_kb: int,
) -> bool:
    """
    Enqueue a submission for grading.

    `language` is a string; convert to the numeric enum used in the
    queue protocol. Returns True on success, False on any failure.
    """
    if _writer is None:
        raise RuntimeError("queue not initialized")

    lang_map = {
        "c": 0,
        "cpp": 1,
        "python": 2,
        "java": 3,
        "go": 4,
        "rust": 5,
    }
    lang_id = lang_map.get(language, 255)

    try:
        return _writer.enqueue(
            submission_id=submission_id,
            problem_id=problem_id,
            language=lang_id,
            time_limit_ms=time_limit_ms,
            memory_limit_mb=memory_limit_mb,
            output_limit_kb=output_limit_kb,
            retry_count=0,
        )
    except Exception as e:
        log.error("enqueue failed: %s", e)
        return False