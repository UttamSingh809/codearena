"""
CodeArena API — FastAPI application.

Sets up logging, the DB pool, the queue writer, and mounts all
routers. Uses the modern lifespan context for startup/shutdown.
"""

import logging
from contextlib import asynccontextmanager

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

from . import db, queue
from .config import settings
from .routers import auth, health, leaderboard, problems, submissions, stats


# ------------------------------------------------------------------
# Logging
# ------------------------------------------------------------------

logging.basicConfig(
    level=settings.log_level,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
    datefmt="%Y-%m-%d %H:%M:%S",
)
log = logging.getLogger("api")


# ------------------------------------------------------------------
# Lifespan
# ------------------------------------------------------------------

@asynccontextmanager
async def lifespan(app: FastAPI):
    log.info("starting API")
    await db.open_pool()
    queue.open_queue()
    log.info("API ready")
    try:
        yield
    finally:
        log.info("shutting down API")
        queue.close_queue()
        await db.close_pool()
        log.info("API stopped")


# ------------------------------------------------------------------
# App
# ------------------------------------------------------------------

app = FastAPI(
    title="CodeArena API",
    description="Sandboxed code evaluation platform",
    version="1.0.0",
    lifespan=lifespan,
)

# CORS: allow the UI origin during development. In production,
# restrict to the real frontend domain.
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# Mount routers
app.include_router(health.router)
app.include_router(auth.router)
app.include_router(problems.router)
app.include_router(submissions.router)
app.include_router(leaderboard.router)
app.include_router(stats.router)


@app.get("/")
async def root():
    return {
        "name": "CodeArena API",
        "version": "1.0.0",
        "docs": "/docs",
    }