"""Manual helper for invoking the JSON extractor from the command line."""

from __future__ import annotations

import sys
from pathlib import Path

from pymupdf4llm_c import to_json


def main(argv: list[str] | None = None) -> None:
    argv = sys.argv[1:] if argv is None else argv
    if len(argv) not in (1, 2):
        print("Usage: python -m tests.manual_test <input.pdf> [output_dir]")
        raise SystemExit(1)

    pdf_path = Path(argv[0])
    output_dir = (
        Path(argv[1])
        if len(argv) == 2
        else pdf_path.with_suffix("").with_name(f"{pdf_path.stem}_json")
    )

    json_paths = to_json(pdf_path, output_dir=output_dir)
    print(f"Extracted {len(json_paths)} page JSON files to {output_dir}")


if __name__ == "__main__":  # pragma: no cover - manual execution only
    main()
