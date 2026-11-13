from datetime import date, timedelta

from fastapi import APIRouter, Depends, HTTPException

from .. import db
from ..deps import CurrentUser, require_admin
from ..models.problem import CreateProblemRequest

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
    search: str | None = None,
    admin: CurrentUser = Depends(require_admin)
):
    """
    List all users, paginated. Only accessible to admins.
    Supports optional search by username or email.
    """
    if search:
        users = await db.fetch_all(
            """
            SELECT id, username, email, role, created_at, xp, is_banned
            FROM users
            WHERE username ILIKE $1 OR email ILIKE $1
            ORDER BY id ASC LIMIT $2 OFFSET $3
            """, f"%{search}%", limit, offset
        )
    else:
        users = await db.fetch_all(
            """
            SELECT id, username, email, role, created_at, xp, is_banned
            FROM users ORDER BY id ASC LIMIT $1 OFFSET $2
            """, limit, offset
        )
    return [dict(u) for u in users]


@router.put("/users/{user_id}/ban")
async def toggle_user_ban(
    user_id: int,
    ban: bool,
    admin: CurrentUser = Depends(require_admin)
):
    if user_id == admin.id:
        raise HTTPException(status_code=400, detail="You cannot ban yourself")
        
    await db.execute(
        "UPDATE users SET is_banned = $1 WHERE id = $2",
        ban, user_id
    )
    return {"status": "ok"}


@router.get("/jobs")
async def get_job_queue(
    admin: CurrentUser = Depends(require_admin)
):
    """
    Fetch details about currently processing and pending jobs,
    plus worker health.
    """
    # 1. Pending & Processing Counts
    counts = await db.fetch_one(
        """
        SELECT
            COUNT(*) FILTER (WHERE status = 'PENDING') AS pending,
            COUNT(*) FILTER (WHERE status = 'PROCESSING') AS processing,
            COUNT(*) FILTER (WHERE status = 'FAILED') AS failed
        FROM submissions
        """
    )

    # 2. Active Workers (from the submissions table)
    workers = await db.fetch_all(
        """
        SELECT DISTINCT worker_id, COUNT(*) AS active_jobs
        FROM submissions
        WHERE status = 'PROCESSING' AND worker_id IS NOT NULL
        GROUP BY worker_id
        """
    )

    # 3. Recent Failures
    failures = await db.fetch_all(
        """
        SELECT id, problem_id, worker_id, retry_count, processing_started_at
        FROM submissions
        WHERE status = 'FAILED'
        ORDER BY id DESC
        LIMIT 10
        """
    )

    return {
        "counts": dict(counts) if counts else {"pending": 0, "processing": 0, "failed": 0},
        "active_workers": [dict(w) for w in workers],
        "recent_failures": [dict(f) for f in failures]
    }


@router.get("/analytics")
async def get_platform_analytics(
    days: int = 14,
    admin: CurrentUser = Depends(require_admin)
):
    """
    Fetch submission trends for the last N days.
    """
    end_date = date.today()
    start_date = end_date - timedelta(days=days - 1)

    # Group submissions by day and verdict
    rows = await db.fetch_all(
        """
        SELECT
            date_trunc('day', s.submitted_at)::date AS day,
            COALESCE(r.verdict, 'PENDING') AS verdict,
            COUNT(*) AS count
        FROM submissions s
        LEFT JOIN results r ON r.submission_id = s.id
        WHERE s.submitted_at >= $1::date
        GROUP BY day, verdict
        ORDER BY day ASC
        """,
        start_date
    )

    return [dict(r) for r in rows]


@router.post("/problems")
async def create_problem(
    req: CreateProblemRequest,
    admin: CurrentUser = Depends(require_admin),
):
    """
    Create a new problem and its test cases atomically.
    """
    async with db.pool().acquire() as conn:
        async with conn.transaction():
            # 1. Insert the problem
            problem_row = await conn.fetchrow(
                """
                INSERT INTO problems
                    (title, description, input_format, output_format,
                     constraints, time_limit_ms, memory_limit_mb,
                     difficulty, is_active, created_by)
                VALUES ($1, $2, $3, $4, $5, $6, $7, $8, TRUE, $9)
                RETURNING id
                """,
                req.title, req.description, req.input_format,
                req.output_format, req.constraints, req.time_limit_ms,
                req.memory_limit_mb, req.difficulty, admin.id,
            )
            problem_id = problem_row["id"]

            # 2. Insert all test cases
            for idx, tc in enumerate(req.test_cases, start=1):
                await conn.execute(
                    """
                    INSERT INTO test_cases
                        (problem_id, ordinal, input_data, expected_output,
                         is_sample, points)
                    VALUES ($1, $2, $3, $4, $5, $6)
                    """,
                    problem_id, idx, tc.input_data, tc.expected_output,
                    tc.is_sample, tc.points,
                )

    return {"id": problem_id, "status": "created"}
