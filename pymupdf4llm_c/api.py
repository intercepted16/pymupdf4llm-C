"""Public facing API helpers for the MuPDF JSON extractor."""

from __future__ import annotations

from functools import lru_cache
from pathlib import Path
from typing import (
    Generator,
    List,
    Literal,
    TypedDict,
    overload,
)

from ._cffi import get_ffi, get_lib
from ._lib import get_default_library_path
from .config import ConversionConfig


class ExtractionError(RuntimeError):
    """Raised when the extraction pipeline reports a failure."""


class LibraryLoadError(RuntimeError):
    """Raised when the shared library cannot be located or loaded."""


class Span(TypedDict, total=False):
    """A text span with styling information.

    Spans are only included in the JSON output when:
    - There are multiple text segments with different styling, OR
    - A single segment has applied styling (bold, italic, monospace, etc.)

    Plain text blocks without any styling will not include the spans array.
    """

    text: str
    bold: bool
    italic: bool
    monospace: bool
    strikeout: bool
    superscript: bool
    subscript: bool
    font_size: float


class Block(TypedDict, total=False):
    """Type definition for the extracted JSON block structure.

    Represents a single block (paragraph, heading, table, figure, etc.)
    extracted from a PDF page.

    The `spans` array is only included when the block contains styled text
    (bold, italic, monospace, etc.) or multiple segments with different styles.
    Plain unstyled text blocks will not include the spans array to avoid duplication.
    """

    # Core fields (present in all blocks)
    type: Literal["text", "heading", "paragraph", "table", "figure", "list"]
    text: str
    bbox: list[float]  # [x0, y0, x1, y1]
    font_size: float
    page_number: int
    length: int

    # Optional styling fields
    bold_ratio: float
    lines: int
    spans: list[Span]  # Only present when there's actual text styling

    # Table-specific fields
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
    warn_large_collect: bool = True,
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
    warn_large_collect: bool = True,
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
    warn_large_collect: bool = True,
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
    warn_large_collect: bool = True,
) -> List[Block]: ...


def to_json(
    pdf_path: str | Path,
    *,
    output_file: str | Path | None = None,
    output_dir: str | Path | None = None,
    config: ConversionConfig | None = None,
    collect: bool = False,
    warn_large_collect: bool = True,
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
        warn_large_collect: If True, log a warning when collect=True results in >100MB.
                          For large PDFs, use iterate_json_pages() instead.

    Returns:
        - List[Path] if output_dir is provided: list of per-page JSON file paths
        - Path if output_file or neither param provided: single merged JSON file path
        - List[Block] if collect=True: parsed JSON block structures (validated)

    Raises:
        ValueError: If collect=True and the JSON structure is invalid.
    """
    import json
    import logging
    import sys
    import tempfile

    logger = logging.getLogger(__name__)
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
        try:
            data = json.loads(output_path.read_text(encoding="utf-8"))
        except json.JSONDecodeError as exc:
            raise ValueError(
                f"Invalid JSON in extracted file {output_path}: {exc}"
            ) from exc

        # Validate structure
        if not isinstance(data, list):
            raise ValueError(
                f"Expected JSON array (list), got {type(data).__name__} at top level. "
                f"Valid formats: list of page objects with 'page' and 'data' keys."
            )

        if data:
            # Check first item structure
            first = data[0]
            if not isinstance(first, dict):
                raise ValueError(
                    f"Expected list of dicts, but first item is {type(first).__name__}"
                )
            if "page" not in first or "data" not in first:
                raise ValueError(
                    f"Expected dicts with 'page' and 'data' keys. "
                    f"Got keys: {list(first.keys())}"
                )

        # Estimate memory usage
        if warn_large_collect:
            try:
                import sys

                estimated_bytes = sys.getsizeof(data)
                estimated_mb = estimated_bytes / (1024 * 1024)

                if estimated_mb > 100:
                    logger.warning(
                        f"collect=True loaded {estimated_mb:.1f}MB into memory. "
                        f"For large PDFs, consider using iterate_json_pages() instead, "
                        f"which is more memory-efficient. "
                        f"To suppress this warning, pass warn_large_collect=False."
                    )
            except Exception:
                # Silently ignore size estimation errors
                pass

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
        return ffi.string(result_ptr).decode("utf-8")  # type: ignore
    finally:
        lib.free(result_ptr)


def iterate_json_pages(
    json_path: str | Path,
) -> Generator[list[Block], None, None]:
    """Iterate over pages from a JSON file, yielding typed Block lists.

    This generator reads a JSON file and yields each page as a list of typed
    Block dictionaries. It validates the structure and ensures type safety.

    Args:
        json_path: Path to a JSON file (single page or merged multi-page).

    Yields:
        list[Block]: A list of Block dictionaries for each page.

    Raises:
        FileNotFoundError: If the JSON file does not exist.
        ValueError: If the JSON structure is invalid.

    Example:
        >>> for page_blocks in iterate_json_pages("output.json"):
        ...     for block in page_blocks:
        ...         print(f"Block type: {block['type']}")
    """
    import json

    json_path = Path(json_path).resolve()
    if not json_path.exists():
        raise FileNotFoundError(f"JSON file not found: {json_path}")

    if not json_path.is_file():
        raise ValueError(f"Path is not a file: {json_path}")

    try:
        with open(json_path, "r", encoding="utf-8") as f:
            data = json.load(f)
    except json.JSONDecodeError as exc:
        raise ValueError(f"Invalid JSON in {json_path}: {exc}") from exc

    # Handle both single-page and multi-page formats
    if isinstance(data, list):
        # Check if this is a list of blocks (single page) or list of pages
        if data and isinstance(data[0], dict):
            if "type" in data[0]:
                # This looks like a single page of blocks
                yield data
            else:
                # Could be a list of pages with page/data structure
                # Try to yield as-is if it has the expected structure
                yield data
    elif isinstance(data, dict):
        # Single object - wrap in list and yield
        yield [data]
    else:
        raise ValueError(
            f"Unexpected JSON structure in {json_path}: "
            f"expected list or dict, got {type(data).__name__}"
        )


__all__ = ["ExtractionError", "to_json", "iterate_json_pages", "Block"]
