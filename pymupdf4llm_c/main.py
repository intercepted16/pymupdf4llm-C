"""CLI entry point for producing per-page JSON artefacts."""

from __future__ import annotations

import sys
from pathlib import Path

from pymupdf4llm_c.logging_config import get_logger

from .api import ExtractionError, to_json

logger = get_logger(__name__)


def main(argv: list[str] | None = None) -> int:
    """CLI entry point for producing per-page JSON artefacts."""
    argv = list(sys.argv[1:] if argv is None else argv)

    if not argv or len(argv) > 2:
        logger.error(f"Usage: {Path(sys.argv[0]).name} <input.pdf> [output_dir]")
        return 1

    try:
        pdf_path = Path(argv[0])
        output_dir = Path(argv[1]) if len(argv) == 2 else None
        paths = to_json(pdf_path, output_dir=output_dir)

        if paths:
            out_path = paths[0].parent
        else:
            out_path = output_dir or pdf_path.with_name(f"{pdf_path.stem}_json")

        logger.info(f"Extracted {len(paths)} JSON files to {out_path}")
        for path in paths:
            logger.info(f"  • {path}")
        return 0
    except (FileNotFoundError, ExtractionError) as exc:
        logger.error(f"error: {exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
