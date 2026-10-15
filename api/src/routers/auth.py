"""
/api/auth — register, login, current user profile.
"""

import logging

from fastapi import APIRouter, Depends, HTTPException, status

from .. import db
from ..auth import (
    DUMMY_BCRYPT_HASH, create_access_token, hash_password,
    verify_password,
)
from ..config import settings
from ..deps import CurrentUser, get_current_user
from ..models import (
    LoginRequest, RegisterRequest, TokenResponse, UserPublic,
)

log = logging.getLogger("api.routers.auth")
router = APIRouter(prefix="/api/auth", tags=["auth"])


@router.post(
    "/register",
    response_model=TokenResponse,
    status_code=status.HTTP_201_CREATED,
)
async def register(req: RegisterRequest) -> TokenResponse:
    """
    Register a new user, hash the password, and return a JWT.

    Duplicate username or email produces 409 Conflict.
    """
    try:
        pw_hash = hash_password(req.password)
    except ValueError as e:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail=str(e),
        )

    try:
        row = await db.fetch_one(
            """
            INSERT INTO users (username, email, password_hash)
            VALUES ($1, $2, $3)
            RETURNING id, username, email, role, created_at, last_login_at
            """,
            req.username, req.email, pw_hash,
        )
    except Exception as e:
        msg = str(e)
        if "unique" in msg.lower() or "duplicate" in msg.lower():
            raise HTTPException(
                status_code=status.HTTP_409_CONFLICT,
                detail="username or email already registered",
            )
        log.exception("register failed")
        raise HTTPException(status_code=500, detail="registration failed")

    if row is None:
        raise HTTPException(status_code=500, detail="registration failed")

    token = create_access_token(
        user_id=row["id"],
        username=row["username"],
        role=row["role"],
    )

    return TokenResponse(
        access_token=token,
        expires_in=settings.jwt_expiry_minutes * 60,
        user=UserPublic(**dict(row)),
    )


@router.post("/login", response_model=TokenResponse)
async def login(req: LoginRequest) -> TokenResponse:
    """Authenticate an existing user and return a fresh JWT."""
    row = await db.fetch_one(
        """
        SELECT id, username, email, password_hash, role,
               created_at, last_login_at
        FROM users WHERE email = $1
        """,
        req.email,
    )

    # Use a constant-time-safe failure: always run verify_password,
    # even if the user doesn't exist, to avoid leaking timing info
    # about which emails are registered.
    stored_hash = row["password_hash"] if row else DUMMY_BCRYPT_HASH
    ok = verify_password(req.password, stored_hash)

    if row is None or not ok:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="invalid email or password",
        )

    # Update last_login_at (best effort; not fatal if it fails).
    try:
        await db.execute(
            "UPDATE users SET last_login_at = NOW() WHERE id = $1",
            row["id"],
        )
    except Exception:
        log.warning("failed to update last_login_at for user %d", row["id"])

    token = create_access_token(
        user_id=row["id"],
        username=row["username"],
        role=row["role"],
    )

    return TokenResponse(
        access_token=token,
        expires_in=settings.jwt_expiry_minutes * 60,
        user=UserPublic(
            id=row["id"],
            username=row["username"],
            email=row["email"],
            role=row["role"],
            created_at=row["created_at"],
            last_login_at=row["last_login_at"],
        ),
    )


@router.get("/me", response_model=UserPublic)
async def me(user: CurrentUser = Depends(get_current_user)) -> UserPublic:
    """Return the authenticated user's profile."""
    row = await db.fetch_one(
        """
        SELECT id, username, email, role, created_at, last_login_at
        FROM users WHERE id = $1
        """,
        user.id,
    )
    if row is None:
        raise HTTPException(status_code=404, detail="user not found")
    return UserPublic(**dict(row))