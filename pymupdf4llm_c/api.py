"""Public facing API helpers for the MuPDF JSON extractor."""

from __future__ import annotations

from functools import lru_cache
from pathlib import Path
from typing import Any, List, Literal, TypedDict, cast, overload

from ._cffi import get_ffi, get_lib
from ._lib import get_default_library_path
from .config import ConversionConfig


class ExtractionError(RuntimeError):
    """Raised when the extraction pipeline reports a failure."""


class LibraryLoadError(RuntimeError):
    """Raised when the shared library cannot be located or loaded."""


class Block(TypedDict, total=False):
    """Type definition for the extracted JSON block structure."""

    type: str
    text: str
    bbox: list[float]
    font_size: float
    confidence: float | None
    row_count: int | None
    col_count: int | None


@lru_cache(maxsize=1)
def _load_library(lib_path: str | Path | None):
    """Load and cache the shared library. Validates once, trusts afterward."""
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


# ---------------------------------------------------------
# 1. Define overload for when collect is False (default)
# ---------------------------------------------------------
@overload
def to_json(
    pdf_path: str | Path,
    *,
    output_file: str | Path | None = None,
    config: ConversionConfig | None = None,
    collect: Literal[False] = False,
) -> Path: ...


# ---------------------------------------------------------
# 2. Define overload for when collect is True
# ---------------------------------------------------------
@overload
def to_json(
    pdf_path: str | Path,
    *,
    output_file: str | Path | None = None,
    config: ConversionConfig | None = None,
    collect: Literal[True],
) -> List[Block]: ...


def to_json(
    pdf_path: str | Path,
    *,
    output_file: str | Path | None = None,
    config: ConversionConfig | None = None,
    collect: bool = False,
) -> Path | List[Block]:
    """Extract PDF and merge all pages into a single JSON file or return blocks."""
    pdf_path = Path(pdf_path).resolve()
    if not pdf_path.exists():
        raise FileNotFoundError(f"Input PDF not found: {pdf_path}")

    output_path = Path(output_file) if output_file else pdf_path.with_suffix(".json")
    output_path.parent.mkdir(parents=True, exist_ok=True)

    try:
        _, lib = _load_library((config or ConversionConfig()).resolve_lib_path())
        rc = lib.pdf_to_json(
            str(pdf_path).encode("utf-8"), str(output_path).encode("utf-8")
        )
        if rc != 0:
            raise RuntimeError(f"C extractor reported failure (exit code {rc})")
    except (LibraryLoadError, RuntimeError) as exc:
        raise ExtractionError(str(exc)) from exc

    if collect:
        import json

        data = json.loads(output_path.read_text(encoding="utf-8"))
        return data

    return output_path


def extract_page_json(
    pdf_path: str | Path,
    page_number: int,
    lib_path: str | Path | None = None,
) -> str:
    """Return raw JSON for a single page using the in-memory C helper."""
    if page_number < 0:
        raise ValueError("page_number must be >= 0")

    ffi, lib = _load_library(lib_path)
    result_ptr = lib.page_to_json_string(str(pdf_path).encode("utf-8"), page_number)

    if result_ptr == ffi.NULL:
        raise RuntimeError("C extractor returned NULL for page JSON")

    try:
        return cast(bytes, ffi.string(cast(Any, result_ptr))).decode("utf-8")
    finally:
        lib.free(result_ptr)


__all__ = ["ExtractionError", "to_json"]
