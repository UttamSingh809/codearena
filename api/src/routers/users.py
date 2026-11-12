"""
/api/users — public user profiles.
"""

from fastapi import APIRouter, Depends, HTTPException

from .. import db
from ..deps import CurrentUser, get_current_user
from ..models import UpdateProfileRequest

import math

router = APIRouter(prefix="/api/users", tags=["users"])


@router.get("/{username}")
async def get_public_profile(
    username: str,
    user: CurrentUser = Depends(get_current_user),
):
    """
    Return a user's public profile: username, join date, global rank,
    number of solved problems, and total submissions.

    This endpoint is available to any authenticated user (so profiles
    can be viewed by anyone logged in), but returns nothing sensitive
    (no email, no password hash, no source code).
    """
    target = await db.fetch_one(
        """
        SELECT id, username, role, created_at, bio, avatar_url, xp
        FROM users
        WHERE username = $1
        """,
        username,
    )
    if target is None:
        raise HTTPException(status_code=404, detail="user not found")

    # Aggregate stats from the leaderboard materialized view
    stats = await db.fetch_one(
        """
        SELECT
            COALESCE(SUM(best_score), 0)                       AS total_score,
            COUNT(*) FILTER (WHERE accepted_count > 0)         AS total_solved,
            COALESCE(SUM(total_submissions), 0)                AS total_submissions
        FROM leaderboard
        WHERE user_id = $1
        """,
        target["id"],
    )

    # Compute global rank by counting users with higher scores
    rank_row = await db.fetch_one(
        """
        WITH scores AS (
            SELECT user_id, SUM(best_score) AS total
            FROM leaderboard
            GROUP BY user_id
        )
        SELECT
            COUNT(*) + 1 AS rank
        FROM scores
        WHERE total > COALESCE(
            (SELECT SUM(best_score) FROM leaderboard WHERE user_id = $1),
            0
        )
        """,
        target["id"],
    )

    # Calculate level from XP
    xp = target["xp"]
    level = int(math.isqrt(xp // 100)) + 1

    return {
        "id": target["id"],
        "username": target["username"],
        "role": target["role"],
        "joined_at": target["created_at"],
        "bio": target["bio"],
        "avatar_url": target["avatar_url"],
        "xp": xp,
        "level": level,
        "rank": rank_row["rank"] if rank_row else None,
        "total_score": stats["total_score"],
        "total_solved": stats["total_solved"],
        "total_submissions": stats["total_submissions"],
    }


@router.put("/me/profile")
async def update_profile(
    req: UpdateProfileRequest,
    user: CurrentUser = Depends(get_current_user),
):
    """Update the authenticated user's public bio and avatar URL."""
    provided = req.model_fields_set
    if not provided:
        return {"status": "ok"}

    await db.execute(
        """
        UPDATE users
        SET bio = CASE WHEN $1 THEN $2 ELSE bio END,
            avatar_url = CASE WHEN $3 THEN $4 ELSE avatar_url END
        WHERE id = $5
        """,
        "bio" in provided,
        req.bio,
        "avatar_url" in provided,
        req.avatar_url,
        user.id,
    )
    return {"status": "ok"}