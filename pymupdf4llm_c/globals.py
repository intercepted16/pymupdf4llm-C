"""Global constants and paths for the pymupdf4llm_c project."""
# ruff: noqa: T201 -- allow print statements for debugging output

from pathlib import Path

PROJECT_ROOT = Path(__file__).parent.parent.resolve()


LIB_PATH = PROJECT_ROOT / "build" / "lib" / "libtomd.so"


if __name__ == "__main__":
    print(f"Project root: {PROJECT_ROOT}")
    print(f"Library path: {LIB_PATH}")
