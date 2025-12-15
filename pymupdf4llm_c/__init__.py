"""Top-level package for the MuPDF JSON extraction bindings."""

from __future__ import annotations

from importlib import metadata

from .api import Block, ExtractionError, iterate_json_pages, to_json
from .config import ConversionConfig

__all__ = [
    "Block",
    "ConversionConfig",
    "ExtractionError",
    "iterate_json_pages",
    "to_json",
    "__version__",
]

try:  # pragma: no cover - metadata only available when installed
    __version__ = metadata.version("pymupdf4llm-c")
except metadata.PackageNotFoundError:  # pragma: no cover - fallback for local dev
    __version__ = "1.0.0"
