"""
Write job descriptors to the CodeArena POSIX shared-memory queue
from Python.

Uses posix_ipc for shm_open and sem_open, and the standard library's
mmap module for the memory mapping. The queue layout must match
os/ipc/include/queue_protocol.h exactly — if that struct changes,
this file must be updated to match.
"""

import logging
import mmap
import struct
import time

import posix_ipc

from . import config

log = logging.getLogger("watchdog.queue_writer")


# ------------------------------------------------------------------
# Protocol constants (must match os/ipc/include/queue_protocol.h)
# ------------------------------------------------------------------

QUEUE_MAGIC   = 0xC0DEA2EA
QUEUE_VERSION = 1

# job_descriptor_t layout (x86-64, natural alignment):
#   uint64_t submission_id;    offset 0
#   uint32_t problem_id;       offset 8
#   uint32_t language;         offset 12
#   uint32_t time_limit_ms;    offset 16
#   uint32_t memory_limit_mb;  offset 20
#   uint32_t output_limit_kb;  offset 24
#   uint32_t retry_count;      offset 28
#   uint64_t enqueue_time_ns;  offset 32
#   uint32_t worker_id;        offset 40
#   uint32_t magic;            offset 44
# Total: 48 bytes
JOB_DESC_FMT  = "<QIIIIIIQII"
JOB_DESC_SIZE = struct.calcsize(JOB_DESC_FMT)

# queue_header_t layout:
#   uint32_t magic;            offset 0
#   uint32_t version;          offset 4
#   uint32_t capacity;         offset 8
#   uint32_t head;             offset 12
#   uint32_t tail;             offset 16
#   uint32_t count;            offset 20
#   uint64_t total_enqueued;   offset 24
#   uint64_t total_dequeued;   offset 32
#   uint64_t total_dropped;    offset 40
#   uint8_t  reserved[32];     offset 48
# Total: 80 bytes
#
# The trailing 32s field is a reserved area; we must include it in
# every pack and unpack or the field count won't match.
HDR_FMT  = "<IIIIIIQQQ32s"
HDR_SIZE = struct.calcsize(HDR_FMT)

# Language enum (must match codearena_language_t)
LANG_C      = 0
LANG_CPP    = 1
LANG_PYTHON = 2


# ------------------------------------------------------------------
# Writer
# ------------------------------------------------------------------

class QueueWriter:
    """
    Attach to an existing CodeArena queue and enqueue jobs.

    Not thread-safe — the caller should serialize access or create
    one writer per thread.
    """

    def __init__(self):
        name = config.QUEUE_NAME
        self._shm_name = name
        self._sem_mtx_name   = name + "_mtx"
        self._sem_empty_name = name + "_empty"
        self._sem_full_name  = name + "_full"

        try:
            self._shm = posix_ipc.SharedMemory(name, flags=0)
        except posix_ipc.ExistentialError:
            raise RuntimeError(f"queue {name!r} does not exist")

        # Use the standard library's mmap module. posix_ipc only
        # handles the shm_open call; the memory mapping itself is
        # done by mmap.mmap().
        self._map = mmap.mmap(
            self._shm.fd,
            self._shm.size,
            flags=mmap.MAP_SHARED,
            prot=mmap.PROT_READ | mmap.PROT_WRITE,
        )
        self._shm.close_fd()

        # Read header to sanity check.
        hdr = self._read_header()
        magic, version, capacity = hdr[0], hdr[1], hdr[2]
        if magic != QUEUE_MAGIC or version != QUEUE_VERSION:
            raise RuntimeError(
                f"queue {name!r} has bad magic/version: "
                f"magic=0x{magic:08x} version={version}"
            )
        self._capacity = capacity

        # Open the three semaphores
        self._mtx   = posix_ipc.Semaphore(self._sem_mtx_name,   flags=0)
        self._empty = posix_ipc.Semaphore(self._sem_empty_name, flags=0)
        self._full  = posix_ipc.Semaphore(self._sem_full_name,  flags=0)

        log.info("attached to queue %s (capacity=%d)", name, capacity)

    def close(self):
        for s in (self._mtx, self._empty, self._full):
            try:
                s.close()
            except Exception:
                pass
        try:
            self._map.close()
        except Exception:
            pass

    # -- low-level operations --

    def _read_header(self):
        """Return a 10-tuple matching HDR_FMT."""
        return struct.unpack_from(HDR_FMT, self._map, 0)

    def _write_header(self, magic, version, capacity, head, tail, count,
                      total_enq, total_deq, total_drop):
        struct.pack_into(
            HDR_FMT, self._map, 0,
            magic, version, capacity, head, tail, count,
            total_enq, total_deq, total_drop, b"\x00" * 32,
        )

    def enqueue(self, submission_id: int, problem_id: int,
                language: int, time_limit_ms: int,
                memory_limit_mb: int, output_limit_kb: int,
                retry_count: int) -> bool:
        """
        Enqueue one job. Blocks if the queue is full.
        Returns True on success, False on any error.
        """
        job = struct.pack(
            JOB_DESC_FMT,
            submission_id & 0xFFFFFFFFFFFFFFFF,
            problem_id    & 0xFFFFFFFF,
            language      & 0xFFFFFFFF,
            time_limit_ms & 0xFFFFFFFF,
            memory_limit_mb & 0xFFFFFFFF,
            output_limit_kb & 0xFFFFFFFF,
            retry_count   & 0xFFFFFFFF,
            time.monotonic_ns(),
            0,               # worker_id = 0 (unclaimed)
            QUEUE_MAGIC,
        )
        assert len(job) == JOB_DESC_SIZE

        # Wait for an empty slot
        try:
            self._empty.acquire()
        except Exception as e:
            log.error("sem acquire empty: %s", e)
            return False

        # Acquire the mutex
        try:
            self._mtx.acquire()
        except Exception as e:
            self._empty.release()
            log.error("sem acquire mtx: %s", e)
            return False

        try:
            # Unpack 10 fields; the 10th is the reserved block.
            (magic, version, capacity, head, tail, count,
             total_enq, total_deq, total_drop, _reserved) = self._read_header()

            slot_offset = HDR_SIZE + tail * JOB_DESC_SIZE
            self._map.seek(slot_offset)
            self._map.write(job)

            tail = (tail + 1) % capacity
            count += 1
            total_enq += 1

            self._write_header(
                magic, version, capacity, head, tail, count,
                total_enq, total_deq, total_drop,
            )
        finally:
            self._mtx.release()
            self._full.release()

        return True


# ------------------------------------------------------------------
# Module-level convenience
# ------------------------------------------------------------------

_shared_writer = None


def _get_writer() -> QueueWriter:
    global _shared_writer
    if _shared_writer is None:
        _shared_writer = QueueWriter()
    return _shared_writer


def enqueue_job(submission_id: int,
                problem_id: int = 1,
                language: int = LANG_C,
                time_limit_ms: int = 2000,
                memory_limit_mb: int = 256,
                output_limit_kb: int = 64,
                retry_count: int = 1) -> bool:
    """
    Enqueue a job descriptor. Called by stale_jobs when requeueing.
    Returns True on success.
    """
    try:
        w = _get_writer()
    except Exception as e:
        log.error("cannot attach to queue: %s", e)
        return False
    return w.enqueue(submission_id, problem_id, language,
                     time_limit_ms, memory_limit_mb, output_limit_kb,
                     retry_count)