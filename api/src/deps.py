"""
CodeArena API — shared FastAPI dependencies.

These are reusable `Depends()` callables: get the current user,
require admin role, etc.
"""

import logging
from typing import Optional

from fastapi import Depends, HTTPException, status
from fastapi.security import HTTPAuthorizationCredentials, HTTPBearer

from . import db
from .auth import decode_token

log = logging.getLogger("api.deps")


# HTTPBearer extracts "Authorization: Bearer <token>" from the request.
_bearer = HTTPBearer(auto_error=False)


class CurrentUser:
    """Simple container for the authenticated user's identity."""

    __slots__ = ("id", "username", "role")

    def __init__(self, id: int, username: str, role: str):
        self.id = id
        self.username = username
        self.role = role


async def get_current_user(
    creds: Optional[HTTPAuthorizationCredentials] = Depends(_bearer),
) -> CurrentUser:
    """
    Extract and verify the JWT. Look up the user in the DB to ensure
    the account still exists (JWTs can outlive deleted accounts).
    Raises 401 on any failure.
    """
    if creds is None or not creds.credentials:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="missing authorization header",
            headers={"WWW-Authenticate": "Bearer"},
        )

    payload = decode_token(creds.credentials)
    if payload is None:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="invalid or expired token",
            headers={"WWW-Authenticate": "Bearer"},
        )

    sub = payload.get("sub")
    if not sub or not sub.isdigit():
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="malformed token subject",
        )

    user_id = int(sub)
    row = await db.fetch_one(
        "SELECT id, username, role FROM users WHERE id = $1",
        user_id,
    )
    if row is None:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="user no longer exists",
        )

    return CurrentUser(id=row["id"], username=row["username"], role=row["role"])


async def require_admin(
    user: CurrentUser = Depends(get_current_user),
) -> CurrentUser:
    """Require the authenticated user to have role='admin'."""
    if user.role != "admin":
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="admin privileges required",
        )
    return user