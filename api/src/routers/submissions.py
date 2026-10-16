"""
/api/submissions — create, view, list.
"""

import logging

from fastapi import APIRouter, Depends, HTTPException, Query, status

from .. import db, queue
from ..config import settings
from ..deps import CurrentUser, get_current_user
from ..models import (
    ResultDetail, SubmissionCreated, SubmissionDetail,
    SubmissionListResponse, SubmissionSummary, SubmitRequest,
)

log = logging.getLogger("api.routers.submissions")
router = APIRouter(prefix="/api/submissions", tags=["submissions"])


@router.post("", response_model=SubmissionCreated,
             status_code=status.HTTP_201_CREATED)
async def create_submission(
    req: SubmitRequest,
    user: CurrentUser = Depends(get_current_user),
) -> SubmissionCreated:
    """
    Create a PENDING submission and enqueue it for grading.

    Steps:
      1. Verify the problem exists and is active.
      2. Fetch its limits (time, memory).
      3. INSERT into submissions (status='PENDING'), return id.
      4. Enqueue a job descriptor.
      5. Return {id, status, submitted_at}.
    """
    # 1. Look up the problem.
    problem = await db.fetch_one(
        """
        SELECT id, time_limit_ms, memory_limit_mb
        FROM problems
        WHERE id = $1 AND is_active = TRUE
        """,
        req.problem_id,
    )
    if problem is None:
        raise HTTPException(status_code=404, detail="problem not found")

    # 2. Create the PENDING row.
    try:
        row = await db.fetch_one(
            """
            INSERT INTO submissions
                (user_id, problem_id, language, source_code)
            VALUES ($1, $2, $3, $4)
            RETURNING id, status, submitted_at
            """,
            user.id, req.problem_id, req.language, req.source_code,
        )
    except Exception as e:
        log.exception("create_submission failed")
        raise HTTPException(status_code=500, detail="failed to create submission")

    if row is None:
        raise HTTPException(status_code=500, detail="failed to create submission")

    # 3. Enqueue for grading.
    enqueued = queue.enqueue_submission(
        submission_id=row["id"],
        problem_id=problem["id"],
        language=req.language,
        time_limit_ms=problem["time_limit_ms"],
        memory_limit_mb=problem["memory_limit_mb"],
        output_limit_kb=settings.default_output_limit_kb,
    )
    if not enqueued:
        # The row exists but no worker will see it. Log, but still
        # return 201 — the watchdog will requeue after the lease.
        log.error("failed to enqueue submission %d", row["id"])

    return SubmissionCreated(**dict(row))


@router.get("/{submission_id}", response_model=SubmissionDetail)
async def get_submission(
    submission_id: int,
    user: CurrentUser = Depends(get_current_user),
) -> SubmissionDetail:
    """
    Fetch one submission with its result (if graded).

    Visibility: users can view their own submissions; admins can view
    any. (For a fuller rule, add a public-visibility flag on problems.)
    """
    row = await db.fetch_one(
        """
        SELECT
            s.id, s.user_id, s.problem_id, s.language, s.source_code,
            s.status, s.submitted_at, s.processing_started_at,
            s.retry_count,
            s.test_total, s.test_passed, s.test_current,
            u.username,
            p.title AS problem_title,
            r.verdict, r.score, r.runtime_ms, r.memory_kb,
            r.stdout, r.stderr, r.graded_at, r.graded_by,
            r.test_total    AS r_test_total,
            r.test_passed   AS r_test_passed,
            r.first_failed  AS r_first_failed
        FROM submissions s
        LEFT JOIN results r ON r.submission_id = s.id
        LEFT JOIN users u ON u.id = s.user_id
        LEFT JOIN problems p ON p.id = s.problem_id
        WHERE s.id = $1
        """,
        submission_id,
    )
    if row is None:
        raise HTTPException(status_code=404, detail="submission not found")

    # Access control.
    if user.role != "admin" and row["user_id"] != user.id:
        raise HTTPException(status_code=403, detail="not your submission")

    result = None
    if row["verdict"] is not None:
        result = ResultDetail(
            verdict=row["verdict"],
            score=row["score"],
            runtime_ms=row["runtime_ms"],
            memory_kb=row["memory_kb"],
            stdout=row["stdout"],
            stderr=row["stderr"],
            graded_at=row["graded_at"],
            graded_by=row["graded_by"],
            test_total=row["r_test_total"],
            test_passed=row["r_test_passed"],
            first_failed=row["r_first_failed"],
        )

    return SubmissionDetail(
        id=row["id"],
        user_id=row["user_id"],
        problem_id=row["problem_id"],
        language=row["language"],
        source_code=row["source_code"],
        status=row["status"],
        submitted_at=row["submitted_at"],
        processing_started_at=row["processing_started_at"],
        retry_count=row["retry_count"],
        username=row["username"],
        problem_title=row["problem_title"],
        test_total=row["test_total"],
        test_passed=row["test_passed"],
        test_current=row["test_current"],
        result=result,
    )


@router.get("", response_model=SubmissionListResponse)
async def list_my_submissions(
    limit: int = Query(20, ge=1, le=100),
    offset: int = Query(0, ge=0),
    user: CurrentUser = Depends(get_current_user),
) -> SubmissionListResponse:
    """Paginated submission history for the authenticated user."""
    total = await db.fetch_val(
        "SELECT COUNT(*) FROM submissions WHERE user_id = $1",
        user.id,
    )

    rows = await db.fetch_all(
        """
        SELECT
            s.id, s.problem_id, p.title AS problem_title, s.language,
            s.status, s.submitted_at,
            r.verdict, r.score, r.runtime_ms, r.memory_kb
        FROM submissions s
        JOIN problems p ON p.id = s.problem_id
        LEFT JOIN results r ON r.submission_id = s.id
        WHERE s.user_id = $1
        ORDER BY s.submitted_at DESC, s.id DESC
        LIMIT $2 OFFSET $3
        """,
        user.id, limit, offset,
    )

    items = [SubmissionSummary(**dict(r)) for r in rows]
    return SubmissionListResponse(
        items=items, total=total, limit=limit, offset=offset,
    )