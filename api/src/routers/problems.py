"""
/api/problems — list and view problems.
"""

from fastapi import APIRouter, Depends, HTTPException, Query

from .. import db
from ..deps import CurrentUser, get_current_user
from ..models import ProblemDetail, ProblemSummary

router = APIRouter(prefix="/api/problems", tags=["problems"])


@router.get("", response_model=list[ProblemSummary])
async def list_problems(
    limit: int = Query(50, ge=1, le=200),
    offset: int = Query(0, ge=0),
    user: CurrentUser = Depends(get_current_user),
) -> list[ProblemSummary]:
    """List active problems, newest first."""
    rows = await db.fetch_all(
        """
        SELECT id, title, difficulty, time_limit_ms, memory_limit_mb,
               created_at
        FROM problems
        WHERE is_active = TRUE
        ORDER BY id ASC
        LIMIT $1 OFFSET $2
        """,
        limit, offset,
    )
    return [ProblemSummary(**dict(r)) for r in rows]


@router.get("/{problem_id}", response_model=ProblemDetail)
async def get_problem(
    problem_id: int,
    user: CurrentUser = Depends(get_current_user),
) -> ProblemDetail:
    """Fetch a problem by ID."""
    row = await db.fetch_one(
        """
        SELECT id, title, description, input_format, output_format,
               constraints, difficulty, time_limit_ms, memory_limit_mb,
               created_at
        FROM problems
        WHERE id = $1 AND is_active = TRUE
        """,
        problem_id,
    )
    if row is None:
        raise HTTPException(status_code=404, detail="problem not found")
    return ProblemDetail(**dict(row))