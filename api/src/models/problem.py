"""
Pydantic models for problems.
"""

from datetime import datetime
from typing import List, Optional

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


class TestCaseInput(BaseModel):
    input_data: str = ""
    expected_output: str
    is_sample: bool = False
    points: int = 10


class CreateProblemRequest(BaseModel):
    title: str = Field(..., max_length=255)
    description: str
    input_format: Optional[str] = None
    output_format: Optional[str] = None
    constraints: Optional[str] = None
    time_limit_ms: int = Field(2000, gt=0, le=60000)
    memory_limit_mb: int = Field(256, gt=0, le=4096)
    difficulty: str = Field("easy", pattern="^(easy|medium|hard)$")
    test_cases: List[TestCaseInput] = Field(..., min_items=1)