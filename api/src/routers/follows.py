from fastapi import APIRouter, Depends, HTTPException

from .. import db
from ..deps import CurrentUser, get_current_user

router = APIRouter(prefix="/api/follows", tags=["follows"])


@router.post("/{username}")
async def follow_user(
    username: str,
    user: CurrentUser = Depends(get_current_user),
):
    """Follow a user."""
    target = await db.fetch_one("SELECT id FROM users WHERE username = $1", username)
    if not target:
        raise HTTPException(status_code=404, detail="user not found")
    if target["id"] == user.id:
        raise HTTPException(status_code=400, detail="cannot follow yourself")

    try:
        await db.execute(
            "INSERT INTO follows (follower_id, following_id) VALUES ($1, $2)",
            user.id,
            target["id"],
        )
    except Exception as e:
        # Ignore unique violation (already following)
        if "unique_violation" not in str(e):
            raise
    return {"status": "ok"}


@router.delete("/{username}")
async def unfollow_user(
    username: str,
    user: CurrentUser = Depends(get_current_user),
):
    """Unfollow a user."""
    target = await db.fetch_one("SELECT id FROM users WHERE username = $1", username)
    if not target:
        raise HTTPException(status_code=404, detail="user not found")

    await db.execute(
        "DELETE FROM follows WHERE follower_id = $1 AND following_id = $2",
        user.id,
        target["id"],
    )
    return {"status": "ok"}


@router.get("/status/{username}")
async def get_follow_status(
    username: str,
    user: CurrentUser = Depends(get_current_user),
):
    """Check if the current user follows the target user."""
    target = await db.fetch_one("SELECT id FROM users WHERE username = $1", username)
    if not target:
        raise HTTPException(status_code=404, detail="user not found")

    is_following = await db.fetch_val(
        "SELECT EXISTS(SELECT 1 FROM follows WHERE follower_id = $1 AND following_id = $2)",
        user.id,
        target["id"],
    )
    return {"is_following": is_following}
