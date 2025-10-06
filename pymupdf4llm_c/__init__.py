"""Top-level package for the PyMuPDF4LLM C bindings library."""
from __future__ import annotations

from importlib import metadata

from .config import ConversionConfig
from .models import PageParameters
from .api import to_markdown

__all__ = [
    "ConversionConfig",
    "PageParameters",
    "to_markdown",
    "__version__",
]

try:  # pragma: no cover - metadata only available when installed
    __version__ = metadata.version("pymupdf4llm-c")
except metadata.PackageNotFoundError:  # pragma: no cover - fallback for local dev
    # Local editable installs or source checkouts where the package metadata has
    # not been generated yet. Keep the version consistent with pyproject.
    __version__ = "1.0.0"
