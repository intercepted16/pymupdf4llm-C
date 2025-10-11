"""Configuration for controlling JSON extraction."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Optional

from ._lib import get_default_library_path


@dataclass(slots=True)
class ConversionConfig:
    """Runtime configuration for the C-backed JSON extractor."""

    lib_path: Optional[Path] = None

    def resolve_lib_path(self) -> Optional[Path]:
        """Return the configured shared library path if supplied."""
        if self.lib_path is not None:
            return Path(self.lib_path)
        return get_default_library_path()


__all__ = ["ConversionConfig"]
