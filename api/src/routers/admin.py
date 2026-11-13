from fastapi import APIRouter, Depends, HTTPException
from .. import db
from ..deps import CurrentUser, require_admin

router = APIRouter(prefix="/api/admin", tags=["admin"])


@router.get("/stats")
async def get_platform_stats(
    admin: CurrentUser = Depends(require_admin)
):
    """
    Fetch high-level platform statistics.
    Only accessible to admins.
    """
    stats = await db.fetch_one(
        """
        SELECT
            (SELECT COUNT(*) FROM users) AS total_users,
            (SELECT COUNT(*) FROM problems) AS total_problems,
            (SELECT COUNT(*) FROM submissions) AS total_submissions,
            (SELECT COUNT(*) FROM submissions WHERE status = 'PENDING') AS pending_jobs,
            (SELECT COUNT(*) FROM submissions WHERE status = 'PROCESSING') AS processing_jobs
        """
    )
    return dict(stats) if stats else {}


@router.get("/users")
async def list_users(
    limit: int = 50,
    offset: int = 0,
    admin: CurrentUser = Depends(require_admin)
):
    """
    List all users, paginated. Only accessible to admins.
    """
    users = await db.fetch_all(
        """
        SELECT id, username, email, role, created_at, last_login_at, xp
        FROM users
        ORDER BY id ASC
        LIMIT $1 OFFSET $2
        """,
        limit, offset
    )
    return [dict(u) for u in users]


@router.put("/users/{user_id}/role")
async def update_user_role(
    user_id: int,
    role: str,  # "student", "instructor", "admin"
    admin: CurrentUser = Depends(require_admin)
):
    """
    Change a user's role. Only accessible to admins.
    """
    if role not in ("student", "instructor", "admin"):
        raise HTTPException(status_code=400, detail="Invalid role")

    if user_id == admin.id:
        raise HTTPException(status_code=400, detail="You cannot change your own role")

    await db.execute(
        "UPDATE users SET role = $1 WHERE id = $2",
        role, user_id
    )
    return {"status": "ok"}
