"""
Pydantic models for submissions and results.
"""

from datetime import datetime
from typing import Optional

from pydantic import BaseModel, Field

from .problem import ProblemSummary


SUPPORTED_LANGUAGES = {"c", "cpp", "python", "java", "go", "rust"}


class SubmitRequest(BaseModel):
    problem_id: int = Field(..., gt=0)
    language: str = Field(..., min_length=1, max_length=16)
    source_code: str = Field(..., min_length=1, max_length=65536)

    def model_post_init(self, __context) -> None:
        if self.language not in SUPPORTED_LANGUAGES:
            raise ValueError(
                f"unsupported language: {self.language}. "
                f"Supported: {sorted(SUPPORTED_LANGUAGES)}"
            )


class SubmissionCreated(BaseModel):
    id: int
    status: str
    submitted_at: datetime


class ResultDetail(BaseModel):
    verdict: str
    score: int
    runtime_ms: Optional[int] = None
    memory_kb: Optional[int] = None
    stdout: Optional[str] = None
    stderr: Optional[str] = None
    graded_at: Optional[datetime] = None


class SubmissionDetail(BaseModel):
    id: int
    user_id: int
    problem_id: int
    language: str
    source_code: str
    status: str
    submitted_at: datetime
    processing_started_at: Optional[datetime] = None
    retry_count: int
    result: Optional[ResultDetail] = None


class SubmissionSummary(BaseModel):
    id: int
    problem_id: int
    problem_title: str
    language: str
    status: str
    submitted_at: datetime
    verdict: Optional[str] = None
    score: Optional[int] = None
    runtime_ms: Optional[int] = None
    memory_kb: Optional[int] = None


class SubmissionListResponse(BaseModel):
    items: list[SubmissionSummary]
    total: int
    limit: int
    offset: int