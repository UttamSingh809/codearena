"""
/api/health — readiness and liveness probes.
"""

from fastapi import APIRouter

from .. import db

router = APIRouter(prefix="/api", tags=["health"])


@router.get("/health")
async def health():
    """
    Return OK if the API can reach the DB.
    Used by load balancers and monitoring.
    """
    db_ok = await db.healthcheck()
    return {
        "status": "ok" if db_ok else "degraded",
        "database": "up" if db_ok else "down",
    }