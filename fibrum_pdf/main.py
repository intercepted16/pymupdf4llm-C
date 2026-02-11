"""cli entry point."""

from __future__ import annotations
import logging
import sys
from pathlib import Path
from .api import ExtractionError, to_json


def main(argv: list[str] | None = None) -> int:
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")

    args = argv if argv is not None else sys.argv[1:]
    if not args or len(args) > 2:
        print(
            f"usage: {Path(sys.argv[0]).name} <input.pdf> [output.json]",
            file=sys.stderr,
        )
        return 1

    try:
        result = to_json(args[0], args[1] if len(args) > 1 else None)
        logging.getLogger(__name__).info("wrote %s", result.path)
        return 0
    except (FileNotFoundError, ExtractionError) as e:
        logging.getLogger(__name__).error("%s", e)
        return 1


if __name__ == "__main__":
    sys.exit(main())
