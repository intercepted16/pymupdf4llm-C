"""Configuration for controlling JSON extraction."""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Optional

from ._lib import get_default_library_path


@dataclass(slots=True)
class ConversionConfig:
    """Runtime configuration for the C-backed JSON extractor.

    Attributes:
        lib_path: Optional path to the shared library. If None, the
            library is discovered automatically.
        progress_callback: Optional callback function that receives
            (current_page, total_pages) for progress reporting.
        verbose: Enable verbose logging for debugging.
    """

    lib_path: Optional[Path] = None
    progress_callback: Optional[Callable[[int, int], None]] = field(
        default=None, compare=False
    )
    verbose: bool = False

    def resolve_lib_path(self) -> Optional[Path]:
        """Return the configured shared library path if supplied."""
        if self.lib_path is not None:
            return Path(self.lib_path)
        return get_default_library_path()


__all__ = ["ConversionConfig"]
