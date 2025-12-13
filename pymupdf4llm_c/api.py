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
# 1. Define overload for output_file (new behavior: single merged JSON)
# ---------------------------------------------------------
@overload
def to_json(
    pdf_path: str | Path,
    *,
    output_file: str | Path,
    output_dir: None = None,
    config: ConversionConfig | None = None,
    collect: Literal[False] = False,
) -> Path: ...


# ---------------------------------------------------------
# 2. Define overload for output_dir (legacy behavior: per-page JSONs)
# ---------------------------------------------------------
@overload
def to_json(
    pdf_path: str | Path,
    *,
    output_file: None = None,
    output_dir: str | Path,
    config: ConversionConfig | None = None,
    collect: Literal[False] = False,
) -> List[Path]: ...


# ---------------------------------------------------------
# 3. Define overload for no output params (default: single merged JSON)
# ---------------------------------------------------------
@overload
def to_json(
    pdf_path: str | Path,
    *,
    output_file: None = None,
    output_dir: None = None,
    config: ConversionConfig | None = None,
    collect: Literal[False] = False,
) -> Path: ...


# ---------------------------------------------------------
# 4. Define overload for when collect is True
# ---------------------------------------------------------
@overload
def to_json(
    pdf_path: str | Path,
    *,
    output_file: str | Path | None = None,
    output_dir: str | Path | None = None,
    config: ConversionConfig | None = None,
    collect: Literal[True],
) -> List[Block]: ...


def to_json(
    pdf_path: str | Path,
    *,
    output_file: str | Path | None = None,
    output_dir: str | Path | None = None,
    config: ConversionConfig | None = None,
    collect: bool = False,
) -> Path | List[Path] | List[Block]:
    """Extract PDF to JSON.

    When `output_dir` is provided (legacy API), extracts per-page JSON files.
    When `output_file` is provided or neither is provided, merges to a single JSON file.

    Args:
        pdf_path: Path to input PDF file.
        output_file: Path to output merged JSON file.
        output_dir: Directory to write per-page JSON files (legacy behavior).
        config: Conversion configuration.
        collect: If True, return parsed JSON blocks; if False, return file path(s).

    Returns:
        - List[Path] if output_dir is provided: list of per-page JSON file paths
        - Path if output_file or neither param provided: single merged JSON file path
        - List[Block] if collect=True: parsed JSON block structures
    """
    import json
    import tempfile

    pdf_path = Path(pdf_path).resolve()
    if not pdf_path.exists():
        raise FileNotFoundError(f"Input PDF not found: {pdf_path}")

    # Legacy behavior: output_dir extracts per-page JSON files
    if output_dir:
        output_dir_path = Path(output_dir).resolve()
        output_dir_path.mkdir(parents=True, exist_ok=True)

        try:
            # Extract to temp file first
            with tempfile.NamedTemporaryFile(
                mode="w", suffix=".json", delete=False
            ) as tmp:
                temp_path = tmp.name

            _, lib = _load_library((config or ConversionConfig()).resolve_lib_path())
            rc = lib.pdf_to_json(
                str(pdf_path).encode("utf-8"), temp_path.encode("utf-8")
            )
            if rc != 0:
                raise RuntimeError(f"C extractor reported failure (exit code {rc})")

            # Parse merged JSON and reconstruct as per-page files
            merged_data = json.loads(Path(temp_path).read_text(encoding="utf-8"))
            extracted_files = []

            for page_obj in merged_data:
                page_num = page_obj.get("page", 0)
                page_data = page_obj.get("data", [])
                page_file = output_dir_path / f"page_{page_num:03d}.json"
                page_file.write_text(json.dumps(page_data), encoding="utf-8")
                extracted_files.append(page_file)

            # Clean up temp file
            Path(temp_path).unlink()

            return extracted_files

        except (LibraryLoadError, RuntimeError) as exc:
            raise ExtractionError(str(exc)) from exc

    # New behavior: output_file or default - merge to single JSON
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
