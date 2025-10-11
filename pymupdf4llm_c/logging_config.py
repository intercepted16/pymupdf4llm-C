"""Centralised logging configuration utilities for the package."""

from __future__ import annotations

import json
import logging
from typing import Any, Mapping, MutableMapping, Optional

_DEFAULT_FORMAT = "%(asctime)s - %(name)s - %(levelname)s - %(message)s"
_LOGGER_CACHE: MutableMapping[str, logging.Logger] = {}


def get_logger(
    name: str = "pymupdf4llm_c", level: int = logging.INFO
) -> logging.Logger:
    """Return a logger configured with a sensible default handler."""
    if name in _LOGGER_CACHE:
        return _LOGGER_CACHE[name]

    logger = logging.getLogger(name)
    if not logger.handlers:
        handler = logging.StreamHandler()
        formatter = logging.Formatter(_DEFAULT_FORMAT)
        handler.setFormatter(formatter)
        logger.addHandler(handler)
    logger.setLevel(level)
    logger.propagate = False

    _LOGGER_CACHE[name] = logger
    return logger


def _serialise_payload(payload: Mapping[str, Any]) -> str:
    """Convert arbitrary payloads to a JSON string for logging."""
    try:
        return json.dumps(payload, default=str, ensure_ascii=False)
    except TypeError:
        serialisable = {key: str(value) for key, value in payload.items()}
        return json.dumps(serialisable, ensure_ascii=False)


def log_response(
    title: str,
    payload: Mapping[str, Any],
    level: str = "INFO",
    logger: Optional[logging.Logger] = None,
) -> None:
    """Log a structured response payload.

    Args:
        title: Human-readable title of the log entry.
        payload: Mapping of additional structured data to log.
        level: Logging level name (e.g. ``"INFO"``).
        logger: Optional pre-configured logger.
    """
    logger = logger or get_logger("pymupdf4llm_c.response")
    log_method = getattr(logger, level.lower(), logger.info)
    log_method("%s | %s", title, _serialise_payload(payload))
