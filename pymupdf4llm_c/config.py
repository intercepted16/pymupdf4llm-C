"""Configuration objects for controlling PDF to Markdown conversion."""
from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, Optional

from ._lib import get_default_library_path


@dataclass(slots=True)
class ConversionConfig:
    """Runtime configuration for the conversion pipeline.

    Attributes:
        dpi: Resolution used for image rendering when falling back to Python.
        table_strategy: Strategy name used by the downstream renderer.
        batch_size: Number of tables processed per batch in the Python layer.
        lib_path: Optional override for the compiled ``libtomd`` shared object.
        use_batch_callback: Whether to use the high-throughput batch callback.
        pymupdf_kwargs: Arbitrary keyword arguments forwarded to ``pymupdf4llm``.
    """

    dpi: int = 150
    table_strategy: str = "lines_strict"
    batch_size: int = 16
    lib_path: Optional[Path] = None
    use_batch_callback: bool = True
    pymupdf_kwargs: Dict[str, object] = field(default_factory=dict)

    def resolve_lib_path(self) -> Optional[Path]:
        """Return the configured shared library path if supplied."""
        if self.lib_path is not None:
            return Path(self.lib_path)
        return get_default_library_path()
