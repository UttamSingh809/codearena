"""
/api/stats — analytics endpoints.

Currently: submission heat-map data for the current user.
"""

from datetime import date, timedelta

from fastapi import APIRouter, Depends, Query

from .. import db
from ..deps import CurrentUser, get_current_user
from ..models import UserStreak

router = APIRouter(prefix="/api/stats", tags=["stats"])


@router.get("/heatmap")
async def submission_heatmap(
    days: int = Query(365, ge=7, le=730),
    user: CurrentUser = Depends(get_current_user),
):
    """
    Return per-day submission counts for the current user over the
    last `days` days.

    Response:
        {
          "start": "2025-10-19",
          "end":   "2026-10-19",
          "days":  {"2025-10-19": 0, "2025-10-20": 3, ...},
          "total": 42,
          "max":   5
        }

    Days with zero submissions are included with value 0.
    """
    end = date.today()
    start = end - timedelta(days=days - 1)

    rows = await db.fetch_all(
        """
        SELECT
            (submitted_at AT TIME ZONE 'UTC')::date AS day,
            COUNT(*) AS count
        FROM submissions
        WHERE user_id = $1
          AND submitted_at >= $2::date
          AND submitted_at <  $3::date + INTERVAL '1 day'
        GROUP BY day
        ORDER BY day
        """,
        user.id, start, end,
    )

    # Build a dense map — every day from start to end has an entry.
    counts = {row["day"]: row["count"] for row in rows}
    day_map = {}
    d = start
    total = 0
    max_count = 0
    while d <= end:
        c = counts.get(d, 0)
        day_map[d.isoformat()] = c
        total += c
        if c > max_count:
            max_count = c
        d += timedelta(days=1)

    return {
        "start": start.isoformat(),
        "end":   end.isoformat(),
        "days":  day_map,
        "total": total,
        "max":   max_count,
    }


@router.get("/streak", response_model=UserStreak)
async def get_user_streak(user: CurrentUser = Depends(get_current_user)):
    """
    Calculate the current daily streak based on accepted submissions.
    A streak counts consecutive days with at least one AC.
    If the last AC was today or yesterday, the streak is active.
    """
    # Get distinct days with at least one AC, ordered descending
    rows = await db.fetch_all(
        """
        SELECT DISTINCT (r.graded_at AT TIME ZONE 'UTC')::date AS day
        FROM results r
        JOIN submissions s ON s.id = r.submission_id
        WHERE s.user_id = $1 AND r.verdict = 'AC'
        ORDER BY day DESC
        """,
        user.id,
    )

    if not rows:
        return UserStreak(current_streak=0, last_submission_date=None)

    today = date.today()
    yesterday = today - timedelta(days=1)
    
    days = [row["day"] for row in rows]
    last_date = days[0]

    # If the last submission is older than yesterday, the streak is broken
    if last_date < yesterday:
        return UserStreak(current_streak=0, last_submission_date=last_date.isoformat())

    # Count backwards
    streak = 1
    expected = last_date - timedelta(days=1)
    
    for day in days[1:]:
        if day == expected:
            streak += 1
            expected -= timedelta(days=1)
        else:
            break

    return UserStreak(current_streak=streak, last_submission_date=last_date.isoformat())