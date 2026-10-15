"""
CodeArena API — configuration.

Reads all settings from environment variables via pydantic-settings.
Fails at startup if required settings are missing.
"""

from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    model_config = SettingsConfigDict(
        env_file=".env",
        env_file_encoding="utf-8",
        case_sensitive=False,
        extra="ignore",
    )

    # Database
    database_url: str

    # JWT
    jwt_secret: str
    jwt_algorithm: str = "HS256"
    jwt_expiry_minutes: int = 60

    # Queue
    queue_name: str = "/codearena_queue"

    # Server
    api_host: str = "0.0.0.0"
    api_port: int = 8000

    # Rate limits
    rate_limit_register: str = "5/minute"
    rate_limit_login: str = "10/minute"
    rate_limit_submit: str = "10/minute"

    # Submission defaults
    max_source_code_bytes: int = 65536
    default_time_limit_ms: int = 2000
    default_memory_limit_mb: int = 256
    default_output_limit_kb: int = 64

    # Logging
    log_level: str = "INFO"


# Singleton settings. Imported everywhere.
settings = Settings()  # type: ignore[call-arg]