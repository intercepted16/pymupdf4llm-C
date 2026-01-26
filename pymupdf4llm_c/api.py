"""Public facing API helpers for the MuPDF JSON extractor."""

from __future__ import annotations

from functools import lru_cache
from contextlib import contextmanager
import json
import os
import sys
from pathlib import Path

from ._cffi import get_ffi, get_lib
from ._lib import get_default_library_path
from .config import ConversionConfig
from .models import Block, Page, Pages
from typing import Any


class ExtractionError(RuntimeError):
    """Raised when the extraction pipeline reports a failure."""


class LibraryLoadError(RuntimeError):
    """Raised when the shared library cannot be located or loaded."""


@lru_cache(maxsize=1)
def _load_library(lib_path: str | Path | None):
    """Load and cache the shared library."""
    candidate = Path(lib_path).resolve() if lib_path else None
    if not candidate:
        if default := get_default_library_path():
            candidate = Path(default).resolve()

    if not candidate or not candidate.exists():
        raise LibraryLoadError(
            "C library not found. Build it with 'make tomd' or set "
            "PYMUPDF4LLM_C_LIB to the compiled shared object."
        )

    ffi = get_ffi()
    return ffi, get_lib(ffi, candidate)


@contextmanager
def _suppress_c_stdout():
    """Suppress stdout/stderr from C code."""
    saved_stdout = os.dup(1)
    saved_stderr = os.dup(2)

    try:
        devnull = os.open(os.devnull, os.O_WRONLY)
        os.dup2(devnull, 1)
        os.dup2(devnull, 2)
        os.close(devnull)
        yield
    finally:
        sys.stdout.flush()
        sys.stderr.flush()
        os.dup2(saved_stdout, 1)
        os.dup2(saved_stderr, 2)
        os.close(saved_stdout)
        os.close(saved_stderr)


class ConversionResult:
    """Result of PDF to JSON conversion. Use `.collect()` or iterate."""

    def __init__(self, path: Path):
        self._path = path

    @property
    def path(self) -> Path:
        """Path to the output JSON file."""
        return self._path

    def collect(self) -> Pages:
        """Load all pages into memory as a Pages object."""
        try:
            with open(self._path, "r", encoding="utf-8") as f:
                data: list[dict[str, Any]] = json.load(f)
        except json.JSONDecodeError as exc:
            raise ValueError(f"Invalid JSON in {self._path}: {exc}") from exc

        pages = Pages([])
        for page in data:
            pages.append(Page(page["data"]))
        return pages

    def __iter__(self):
        """Iterate over pages one at a time (memory-efficient)."""
        try:
            with open(self._path, "r", encoding="utf-8") as f:
                pages: list[dict[str, Any]] = json.load(f)
        except json.JSONDecodeError as exc:
            raise ValueError(f"Invalid JSON in {self._path}: {exc}") from exc

        for page in pages:
            yield Page(page["data"])

    def __repr__(self) -> str:
        return f"ConversionResult(path={self._path})"


def to_json(
    pdf_path: str | Path,
    *,
    output: str | Path | None = None,
    config: ConversionConfig | None = None,
) -> ConversionResult:
    """Extract PDF to JSON.

    Args:
        pdf_path: Path to input PDF file.
        output: Path for output JSON file. Defaults to pdf_path with .json extension.
        config: Conversion configuration.

    Returns:
        ConversionResult: Use `.collect()` for all pages or iterate for streaming.

    Example:
        >>> result = to_json("document.pdf")
        >>> # Load everything into memory
        >>> pages = result.collect()
        >>> pages.markdown  # Full document as markdown
        >>> pages[0].markdown  # First page as markdown
        >>> pages[0][0].markdown  # First block as markdown
        >>> # Stream pages (memory-efficient)
        >>> for page in result:
        ...     print(page.markdown)
    """
    pdf_path = Path(pdf_path).absolute().resolve()
    if not pdf_path.exists():
        raise FileNotFoundError(f"Input PDF not found: {pdf_path}")

    output_path = (
        Path(output).absolute().resolve()
        if output
        else pdf_path.with_suffix(".json").absolute().resolve()
    )
    output_path.parent.mkdir(parents=True, exist_ok=True)

    try:
        _, lib = _load_library((config or ConversionConfig()).resolve_lib_path())
        with _suppress_c_stdout():
            rc = lib.pdf_to_json(
                str(pdf_path).encode("utf-8"),
                str(output_path).encode("utf-8"),
            )
        if rc != 0:
            raise RuntimeError(f"C extractor failed (exit code {rc})")
    except (LibraryLoadError, RuntimeError) as exc:
        raise ExtractionError(str(exc)) from exc

    return ConversionResult(output_path)


__all__ = [
    "ExtractionError",
    "LibraryLoadError",
    "to_json",
    "ConversionResult",
    "Block",
    "Page",
    "Pages",
]
