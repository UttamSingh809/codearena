"""
CodeArena API — asyncpg database layer.

Provides a connection pool and thin helpers for executing queries
with parameters. No ORM — every query is explicit SQL text.
"""

import logging
from typing import Any, List, Optional

import asyncpg

from .config import settings

log = logging.getLogger("api.db")


# ------------------------------------------------------------------
# Pool lifecycle
# ------------------------------------------------------------------

_pool: Optional[asyncpg.Pool] = None


async def open_pool() -> asyncpg.Pool:
    """Create the global connection pool. Called once at startup."""
    global _pool
    if _pool is not None:
        return _pool

    _pool = await asyncpg.create_pool(
        dsn=settings.database_url,
        min_size=2,
        max_size=10,
        command_timeout=30,
        server_settings={
            "application_name": "codearena-api",
        },
    )
    log.info("asyncpg pool opened (min=2, max=10)")
    return _pool


async def close_pool() -> None:
    """Close the global connection pool. Called once at shutdown."""
    global _pool
    if _pool is None:
        return
    await _pool.close()
    _pool = None
    log.info("asyncpg pool closed")


def pool() -> asyncpg.Pool:
    """Return the global pool. Raises if not initialized."""
    if _pool is None:
        raise RuntimeError("database pool not initialized")
    return _pool


# ------------------------------------------------------------------
# Query helpers
# ------------------------------------------------------------------

async def fetch_one(query: str, *args: Any) -> Optional[asyncpg.Record]:
    """Execute a query and return the first row, or None."""
    async with pool().acquire() as conn:
        return await conn.fetchrow(query, *args)


async def fetch_all(query: str, *args: Any) -> List[asyncpg.Record]:
    """Execute a query and return all rows."""
    async with pool().acquire() as conn:
        return await conn.fetch(query, *args)


async def fetch_val(query: str, *args: Any) -> Any:
    """Execute a query and return the first column of the first row."""
    async with pool().acquire() as conn:
        return await conn.fetchval(query, *args)


async def execute(query: str, *args: Any) -> str:
    """Execute a statement (INSERT/UPDATE/DELETE) and return the status."""
    async with pool().acquire() as conn:
        return await conn.execute(query, *args)


async def execute_in_transaction(queries_and_args) -> List[Any]:
    """
    Execute multiple statements inside one transaction.
    `queries_and_args` is a list of (query, args) tuples.
    Returns a list of results (fetchval for each query).
    """
    async with pool().acquire() as conn:
        async with conn.transaction():
            results = []
            for query, args in queries_and_args:
                results.append(await conn.fetchval(query, *args))
            return results


async def healthcheck() -> bool:
    """Return True if the DB responds to a trivial query."""
    try:
        result = await fetch_val("SELECT 1")
        return result == 1
    except Exception as e:
        log.error("healthcheck failed: %s", e)
        return False