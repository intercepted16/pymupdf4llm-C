"""Public facing API helpers for the MuPDF JSON extractor."""

from __future__ import annotations

from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple

from .config import ConversionConfig
from .main import (
    LibraryLoadError,
    convert_pdf_to_json,
    extract_page_json,
    get_pdf_metadata,
)


class ExtractionError(RuntimeError):
    """Raised when the extraction pipeline reports a failure."""


def to_json(
    pdf_path: str | Path,
    *,
    output_dir: str | Path | None = None,
    config: ConversionConfig | None = None,
    collect: bool = False,
    pages: Optional[Tuple[int, int]] = None,
) -> Sequence[Path] | List[List[dict]]:
    """Extract per-page JSON artefacts for ``pdf_path``.

    Args:
        pdf_path: Path to the PDF file to process.
        output_dir: Directory to write JSON files. Defaults to
            <pdf_stem>_json.
        config: Optional conversion configuration.
        collect: If True, parse and return JSON data instead of paths.
        pages: Optional tuple (start, end) for page range (0-indexed,
            inclusive). If None, extract all pages.

    Returns:
        Sequence of Path objects or list of parsed JSON data depending
        on collect parameter.

    Raises:
        FileNotFoundError: If the input PDF does not exist.
        ExtractionError: If extraction fails.
    """
    config = config or ConversionConfig()

    pdf_path = Path(pdf_path)
    if not pdf_path.exists():
        raise FileNotFoundError(f"Input PDF not found: {pdf_path}")

    target_dir = (
        Path(output_dir)
        if output_dir
        else pdf_path.with_name(f"{pdf_path.stem}_json")
    )
    target_dir.mkdir(parents=True, exist_ok=True)

    try:
        if pages is not None:
            # Extract specific page range
            start_page, end_page = pages
            json_paths = _extract_page_range(
                pdf_path, target_dir, start_page, end_page, config
            )
        else:
            # Extract all pages
            json_paths = convert_pdf_to_json(
                pdf_path, target_dir, config.resolve_lib_path()
            )
    except (LibraryLoadError, RuntimeError) as exc:
        raise ExtractionError(str(exc)) from exc

    if not collect:
        return json_paths

    import json

    results: List[List[dict]] = []
    for path in json_paths:
        with path.open("r", encoding="utf-8") as fh:
            results.append(json.load(fh))
    return results


def _extract_page_range(
    pdf_path: Path,
    output_dir: Path,
    start_page: int,
    end_page: int,
    config: ConversionConfig,
) -> Sequence[Path]:
    """Extract a specific range of pages from a PDF."""
    import json

    from .logging_config import get_logger

    if start_page < 0:
        raise ValueError("start_page must be >= 0")
    if end_page < start_page:
        raise ValueError("end_page must be >= start_page")

    logger = get_logger() if config.verbose else None
    json_paths: List[Path] = []
    lib_path = config.resolve_lib_path()
    total_pages = end_page - start_page + 1

    for idx, page_num in enumerate(range(start_page, end_page + 1)):
        if config.progress_callback:
            config.progress_callback(idx + 1, total_pages)

        if logger:
            logger.info(
                "Extracting page %d (%d/%d)", page_num, idx + 1, total_pages
            )

        try:
            json_str = extract_page_json(pdf_path, page_num, lib_path)
            output_file = output_dir / f"page_{page_num:03d}.json"
            with output_file.open("w", encoding="utf-8") as fh:
                # Parse and re-serialize to ensure valid JSON
                data = json.loads(json_str)
                json.dump(data, fh, ensure_ascii=False, indent=2)
            json_paths.append(output_file)
        except RuntimeError as exc:
            if logger:
                logger.error("Failed to extract page %d: %s", page_num, exc)
            # Page doesn't exist or extraction failed
            break

    return tuple(json_paths)


def get_metadata(
    pdf_path: str | Path,
    config: ConversionConfig | None = None,
) -> Dict[str, str | int]:
    """Extract metadata from a PDF document.

    Args:
        pdf_path: Path to the PDF file.
        config: Optional conversion configuration.

    Returns:
        Dictionary containing metadata fields such as title, author,
        subject, creator, producer, creation_date, modification_date,
        and page_count.

    Raises:
        FileNotFoundError: If the input PDF does not exist.
        ExtractionError: If metadata extraction fails.
    """
    config = config or ConversionConfig()

    pdf_path = Path(pdf_path)
    if not pdf_path.exists():
        raise FileNotFoundError(f"Input PDF not found: {pdf_path}")

    try:
        metadata = get_pdf_metadata(pdf_path, config.resolve_lib_path())
        return metadata
    except (LibraryLoadError, RuntimeError) as exc:
        raise ExtractionError(str(exc)) from exc


__all__ = ["ExtractionError", "to_json", "get_metadata"]
