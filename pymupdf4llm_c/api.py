"""Public facing API helpers for the MuPDF JSON extractor."""

from __future__ import annotations

from pathlib import Path
from typing import List, Sequence

from .config import ConversionConfig
from .main import LibraryLoadError, convert_pdf_to_json


class ExtractionError(RuntimeError):
    """Raised when the extraction pipeline reports a failure."""


def to_json(
    pdf_path: str | Path,
    *,
    output_dir: str | Path | None = None,
    config: ConversionConfig | None = None,
    collect: bool = False,
) -> Sequence[Path] | List[List[dict]]:
    """Extract per-page JSON artefacts for ``pdf_path``."""
    config = config or ConversionConfig()

    pdf_path = Path(pdf_path)
    if not pdf_path.exists():
        raise FileNotFoundError(f"Input PDF not found: {pdf_path}")

    target_dir = (
        Path(output_dir) if output_dir else pdf_path.with_name(f"{pdf_path.stem}_json")
    )
    target_dir.mkdir(parents=True, exist_ok=True)

    try:
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


__all__ = ["ExtractionError", "to_json"]
