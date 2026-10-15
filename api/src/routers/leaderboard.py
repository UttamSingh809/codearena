"""
/api/leaderboard — rankings.
"""

from fastapi import APIRouter, Depends, Query

from .. import db
from ..deps import CurrentUser, get_current_user

router = APIRouter(prefix="/api/leaderboard", tags=["leaderboard"])


@router.get("")
async def global_leaderboard(
    limit: int = Query(50, ge=1, le=200),
    offset: int = Query(0, ge=0),
    user: CurrentUser = Depends(get_current_user),
):
    """Overall ranking across all problems."""
    rows = await db.fetch_all(
        """
        SELECT
            RANK() OVER (
                ORDER BY SUM(best_score) DESC,
                         COUNT(*) FILTER (WHERE accepted_count > 0) DESC
            ) AS rank,
            username,
            SUM(best_score)                             AS total_score,
            COUNT(*) FILTER (WHERE accepted_count > 0)  AS total_solved,
            SUM(total_submissions)                      AS total_submissions
        FROM leaderboard
        GROUP BY user_id, username
        ORDER BY total_score DESC, total_solved DESC
        LIMIT $1 OFFSET $2
        """,
        limit, offset,
    )
    return [
        {
            "rank": r["rank"],
            "username": r["username"],
            "total_score": r["total_score"],
            "total_solved": r["total_solved"],
            "total_submissions": r["total_submissions"],
        }
        for r in rows
    ]


@router.get("/problem/{problem_id}")
async def problem_leaderboard(
    problem_id: int,
    limit: int = Query(50, ge=1, le=200),
    offset: int = Query(0, ge=0),
    user: CurrentUser = Depends(get_current_user),
):
    """Ranking for one problem."""
    rows = await db.fetch_all(
        """
        SELECT
            RANK() OVER (
                ORDER BY best_score DESC, best_runtime_ms ASC
            ) AS rank,
            username,
            best_score,
            best_runtime_ms,
            best_memory_kb,
            accepted_count,
            last_submission_at
        FROM leaderboard
        WHERE problem_id = $1 AND best_score > 0
        ORDER BY best_score DESC, best_runtime_ms ASC
        LIMIT $2 OFFSET $3
        """,
        problem_id, limit, offset,
    )
    return [
        {
            "rank": r["rank"],
            "username": r["username"],
            "best_score": r["best_score"],
            "best_runtime_ms": r["best_runtime_ms"],
            "best_memory_kb": r["best_memory_kb"],
            "accepted_count": r["accepted_count"],
            "last_submission_at": r["last_submission_at"],
        }
        for r in rows
    ]