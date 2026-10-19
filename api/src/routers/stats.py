"""
/api/stats — analytics endpoints.

Currently: submission heat-map data for the current user.
"""

from datetime import date, timedelta

from fastapi import APIRouter, Depends, Query

from .. import db
from ..deps import CurrentUser, get_current_user

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