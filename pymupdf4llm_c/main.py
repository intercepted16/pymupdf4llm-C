#!/usr/bin/env python3
"""Thin CFFI-based helpers for interacting with the MuPDF JSON extractor."""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Any, List, Sequence

from pymupdf4llm_c.logging_config import get_logger

from ._cffi import get_ffi, get_lib
from ._lib import get_default_library_path

logger = get_logger(__name__)


class LibraryLoadError(RuntimeError):
    """Raised when the shared library cannot be located or loaded."""


def _resolve_library_path(provided: str | Path | None) -> Path:
    """Resolve a concrete filesystem path to the shared library."""
    if provided is not None:
        candidate = Path(provided)
    else:
        default = get_default_library_path()
        candidate = Path(default) if default is not None else None

    if candidate is None:
        raise LibraryLoadError(
            "C library not found. Build it with 'make tomd' or set "
            "PYMUPDF4LLM_C_LIB to the compiled shared object."
        )

    candidate = candidate.resolve()
    if not candidate.exists():
        raise LibraryLoadError(f"C library not found at {candidate}")

    return candidate


def _load_library(lib_path: str | Path | None):
    """Load the shared library using CFFI."""
    path = _resolve_library_path(lib_path)
    ffi = get_ffi()
    lib = get_lib(ffi, path)
    return ffi, lib


def convert_pdf_to_json(
    pdf_path: str | Path,
    output_dir: str | Path,
    lib_path: str | Path | None = None,
) -> Sequence[Path]:
    """Invoke the C extractor to materialise page-level JSON files."""
    pdf_path = Path(pdf_path)
    if not pdf_path.exists():
        raise FileNotFoundError(f"Input PDF not found: {pdf_path}")

    output_dir = Path(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    _, lib = _load_library(lib_path)
    pdf_path_bytes = str(pdf_path).encode("utf-8")
    output_dir_bytes = str(output_dir).encode("utf-8")

    rc = lib.pdf_to_json(pdf_path_bytes, output_dir_bytes)
    if rc != 0:
        raise RuntimeError(f"C extractor reported failure (exit code {rc})")

    json_files: List[Path] = sorted(output_dir.glob("page_*.json"))
    return tuple(json_files)


def extract_page_json(
    pdf_path: str | Path,
    page_number: int,
    lib_path: str | Path | None = None,
) -> str:
    """Return raw JSON for a single page using the in-memory C helper."""
    if page_number < 0:
        raise ValueError("page_number must be >= 0")

    pdf_path = Path(pdf_path)
    if not pdf_path.exists():
        raise FileNotFoundError(f"Input PDF not found: {pdf_path}")

    ffi, lib = _load_library(lib_path)
    pdf_path_bytes = str(pdf_path).encode("utf-8")

    result_ptr = lib.page_to_json_string(pdf_path_bytes, page_number)
    if result_ptr == ffi.NULL:
        raise RuntimeError("C extractor returned NULL for page JSON")

    # Convert the C string to Python string
    json_string: str | Any = ffi.string(result_ptr).decode("utf-8")  # type: ignore[arg-type]
    if not isinstance(json_string, str):
        raise RuntimeError("Failed to decode JSON string from C extractor")

    # Free the allocated memory
    lib.free(result_ptr)

    return json_string


def main(argv: list[str] | None = None) -> int:
    """CLI entry point for producing per-page JSON artefacts."""
    argv = list(sys.argv[1:] if argv is None else argv)

    if not argv or len(argv) > 2:
        script = Path(sys.argv[0]).name
        logger.error(f"Usage: {script} <input.pdf> [output_dir]")
        return 1

    pdf_path = Path(argv[0])
    output_dir = Path(argv[1]) if len(argv) == 2 else pdf_path.with_suffix("")
    if len(argv) == 1:
        output_dir = output_dir.parent / f"{output_dir.name}_json"

    try:
        json_files = convert_pdf_to_json(pdf_path, output_dir)
    except (FileNotFoundError, LibraryLoadError, RuntimeError) as exc:
        logger.error(f"error: {exc}")
        return 1

    logger.info(f"Extracted {len(json_files)} JSON files to {output_dir}")
    for path in json_files:
        logger.info(f"  • {path}")
    return 0


if __name__ == "__main__":  # pragma: no cover - manual usage
    raise SystemExit(main())
