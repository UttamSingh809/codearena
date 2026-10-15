"""
CodeArena API — authentication.

JWT creation/verification and password hashing.

Password hashing uses the `bcrypt` library directly (no passlib).
bcrypt has a 72-byte limit on the plaintext; we reject longer
passwords at the API boundary with a clear 400 error rather than
silently truncating.
"""

import logging
from datetime import datetime, timedelta, timezone
from typing import Optional

import bcrypt
from jose import JWTError, jwt

from .config import settings

log = logging.getLogger("api.auth")


# bcrypt's hard limit on the plaintext, in bytes.
BCRYPT_MAX_BYTES = 72

# Precomputed bcrypt hash of the string "dummy" with cost factor 12.
# Used only to keep login timing uniform when the user doesn't exist.
# Verifying against this hash always returns False for any real
# password, so it's never a security hole. See verify_password() and
# the login handler for the timing rationale.
DUMMY_BCRYPT_HASH = (
    "$2b$12$LQv3c1yqBWVHxkd0LHAkCOYz6TtxMQJqhN8/LewdBPj2FpQ1c6P8m"
)

# ------------------------------------------------------------------
# Password hashing
# ------------------------------------------------------------------

def hash_password(plain: str) -> str:
    """
    Hash a plaintext password with bcrypt.

    Raises ValueError if the password exceeds bcrypt's 72-byte limit.
    The API layer maps this to a 400 response.
    """
    pw_bytes = plain.encode("utf-8")
    if len(pw_bytes) > BCRYPT_MAX_BYTES:
        raise ValueError(
            f"password too long ({len(pw_bytes)} bytes, max {BCRYPT_MAX_BYTES})"
        )
    hashed = bcrypt.hashpw(pw_bytes, bcrypt.gensalt(rounds=12))
    return hashed.decode("ascii")


def verify_password(plain: str, hashed: str) -> bool:
    """
    Check a plaintext password against a bcrypt hash.
    Returns False on any error (malformed hash, wrong password,
    password longer than 72 bytes).
    """
    try:
        pw_bytes = plain.encode("utf-8")
        if len(pw_bytes) > BCRYPT_MAX_BYTES:
            # Still run a comparison against a dummy hash so the
            # timing doesn't leak. We just return False after.
            bcrypt.checkpw(b"x" * BCRYPT_MAX_BYTES,
                           b"$2b$12$" + b"x" * 53)
            return False
        return bcrypt.checkpw(pw_bytes, hashed.encode("ascii"))
    except (ValueError, TypeError):
        return False


# ------------------------------------------------------------------
# JWT
# ------------------------------------------------------------------

def create_access_token(user_id: int, username: str, role: str) -> str:
    """
    Create a signed JWT for the given user.
    Subject (`sub`) is the user ID as a string, per RFC 7519.
    """
    now = datetime.now(timezone.utc)
    expires = now + timedelta(minutes=settings.jwt_expiry_minutes)

    payload = {
        "sub": str(user_id),
        "username": username,
        "role": role,
        "iat": int(now.timestamp()),
        "exp": int(expires.timestamp()),
    }
    return jwt.encode(payload, settings.jwt_secret,
                      algorithm=settings.jwt_algorithm)


def decode_token(token: str) -> Optional[dict]:
    """
    Decode and verify a JWT. Returns the payload dict on success,
    or None on any error (expired, wrong signature, malformed).
    """
    try:
        return jwt.decode(token, settings.jwt_secret,
                          algorithms=[settings.jwt_algorithm])
    except JWTError as e:
        log.debug("token decode failed: %s", e)
        return None