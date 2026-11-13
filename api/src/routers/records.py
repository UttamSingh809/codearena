from fastapi import APIRouter, Depends
from .. import db
from ..deps import CurrentUser, get_current_user

router = APIRouter(prefix="/api/stats", tags=["stats"])

@router.get("/records")
async def get_personal_records(user: CurrentUser = Depends(get_current_user)):
    """
    Fetch the user's personal bests and milestone counters.
    """
    # 1. Fastest AC (Runtime)
    fastest = await db.fetch_one(
        """
        SELECT r.runtime_ms as best_runtime, p.title
        FROM results r
        JOIN submissions s ON s.id = r.submission_id
        JOIN problems p ON p.id = s.problem_id
        WHERE s.user_id = $1 AND r.verdict = 'AC' AND r.runtime_ms IS NOT NULL
        ORDER BY r.runtime_ms ASC
        LIMIT 1
        """,
        user.id,
    )

    # 2. Hardest problem solved
    hardest = await db.fetch_one(
        """
        SELECT p.title, p.difficulty
        FROM results r
        JOIN submissions s ON s.id = r.submission_id
        JOIN problems p ON p.id = s.problem_id
        WHERE s.user_id = $1 AND r.verdict = 'AC'
        ORDER BY 
            CASE p.difficulty 
                WHEN 'hard' THEN 3 
                WHEN 'medium' THEN 2 
                WHEN 'easy' THEN 1 
                ELSE 0 
            END DESC
        LIMIT 1
        """,
        user.id,
    )

    # 3. Total unique ACs (for milestones)
    total_solved = await db.fetch_val(
        """
        SELECT COUNT(DISTINCT s.problem_id)
        FROM results r
        JOIN submissions s ON s.id = r.submission_id
        WHERE s.user_id = $1 AND r.verdict = 'AC'
        """,
        user.id,
    )

    return {
        "fastest_runtime_ms": fastest["best_runtime"] if fastest else None,
        "fastest_problem": fastest["title"] if fastest else None,
        "hardest_problem": hardest["title"] if hardest else None,
        "hardest_difficulty": hardest["difficulty"] if hardest else None,
        "total_solved": total_solved or 0,
    }