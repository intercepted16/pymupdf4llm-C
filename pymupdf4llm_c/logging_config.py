"""Centralised logging configuration utilities for the package."""

from __future__ import annotations

import logging
from functools import lru_cache

_DEFAULT_FORMAT = "%(asctime)s - %(name)s - %(levelname)s - %(message)s"


@lru_cache(maxsize=None)
def get_logger(
    name: str = "pymupdf4llm_c", level: int = logging.INFO
) -> logging.Logger:
    """Return a logger configured with a sensible default handler."""
    logger = logging.getLogger(name)
    if not logger.handlers:
        handler = logging.StreamHandler()
        handler.setFormatter(logging.Formatter(_DEFAULT_FORMAT))
        logger.addHandler(handler)
    logger.setLevel(level)
    logger.propagate = False
    return logger
