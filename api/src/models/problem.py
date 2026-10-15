"""
Pydantic models for problems.
"""

from datetime import datetime

from pydantic import BaseModel, Field


class ProblemSummary(BaseModel):
    id: int
    title: str
    difficulty: str
    time_limit_ms: int
    memory_limit_mb: int
    created_at: datetime


class ProblemDetail(BaseModel):
    id: int
    title: str
    description: str | None
    input_format: str | None
    output_format: str | None
    constraints: str | None
    difficulty: str
    time_limit_ms: int
    memory_limit_mb: int
    created_at: datetime