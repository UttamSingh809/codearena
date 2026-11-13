from datetime import datetime
from typing import List, Optional

from fastapi import APIRouter, Depends, HTTPException
from pydantic import BaseModel

from .. import db
from ..deps import CurrentUser, get_current_user

router = APIRouter(prefix="/api/discussions", tags=["discussions"])


class CreateCommentRequest(BaseModel):
    problem_id: int
    parent_id: Optional[int] = None
    content: str


class CommentResponse(BaseModel):
    id: int
    problem_id: int
    user_id: int
    username: str
    avatar_url: Optional[str] = None
    parent_id: Optional[int] = None
    content: str
    upvotes: int
    created_at: datetime
    has_upvoted: bool = False


@router.get("/problem/{problem_id}", response_model=List[CommentResponse])
async def get_problem_comments(
    problem_id: int,
    user: CurrentUser = Depends(get_current_user),
):
    """Fetch all top-level comments and replies for a problem."""
    rows = await db.fetch_all(
        """
        SELECT 
            d.id, d.problem_id, d.user_id, d.parent_id, d.content, 
            d.upvotes, d.created_at,
            u.username, u.avatar_url,
            EXISTS(SELECT 1 FROM discussion_upvotes du WHERE du.discussion_id = d.id AND du.user_id = $2) AS has_upvoted
        FROM discussions d
        JOIN users u ON u.id = d.user_id
        WHERE d.problem_id = $1
        ORDER BY d.created_at ASC
        """,
        problem_id,
        user.id,
    )
    return [dict(r) for r in rows]


@router.post("", response_model=CommentResponse)
async def create_comment(
    req: CreateCommentRequest,
    user: CurrentUser = Depends(get_current_user),
):
    """Post a new comment or reply."""
    if not req.content.strip():
        raise HTTPException(status_code=400, detail="Comment cannot be empty")
    if len(req.content) > 2000:
        raise HTTPException(status_code=400, detail="Comment is too long")

    row = await db.fetch_one(
        """
        INSERT INTO discussions (problem_id, user_id, parent_id, content)
        VALUES ($1, $2, $3, $4)
        RETURNING id, problem_id, user_id, parent_id, content, upvotes, created_at
        """,
        req.problem_id,
        user.id,
        req.parent_id,
        req.content,
    )

    # Fetch the username to return in the response
    u = await db.fetch_one("SELECT username, avatar_url FROM users WHERE id = $1", user.id)

    return CommentResponse(
        id=row["id"],
        problem_id=row["problem_id"],
        user_id=row["user_id"],
        username=u["username"],
        avatar_url=u["avatar_url"],
        parent_id=row["parent_id"],
        content=row["content"],
        upvotes=row["upvotes"],
        created_at=row["created_at"],
        has_upvoted=False,
    )


@router.post("/{comment_id}/upvote")
async def toggle_upvote(
    comment_id: int,
    user: CurrentUser = Depends(get_current_user),
):
    """Toggle upvote on a comment."""
    # Check if already upvoted
    exists = await db.fetch_val(
        "SELECT EXISTS(SELECT 1 FROM discussion_upvotes WHERE discussion_id = $1 AND user_id = $2)",
        comment_id,
        user.id,
    )

    if exists:
        # Remove upvote
        await db.execute(
            "DELETE FROM discussion_upvotes WHERE discussion_id = $1 AND user_id = $2",
            comment_id,
            user.id,
        )
        await db.execute(
            "UPDATE discussions SET upvotes = upvotes - 1 WHERE id = $1",
            comment_id,
        )
        return {"status": "removed"}
    else:
        # Add upvote
        await db.execute(
            "INSERT INTO discussion_upvotes (discussion_id, user_id) VALUES ($1, $2)",
            comment_id,
            user.id,
        )
        await db.execute(
            "UPDATE discussions SET upvotes = upvotes + 1 WHERE id = $1",
            comment_id,
        )
        return {"status": "added"}
