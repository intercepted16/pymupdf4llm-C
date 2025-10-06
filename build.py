"""Helper script to build source and wheel distributions with CMake output."""
from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).parent.resolve()
DIST_DIR = ROOT / "dist"


def main() -> None:
    if DIST_DIR.exists():
        shutil.rmtree(DIST_DIR)
    subprocess.check_call([sys.executable, "-m", "build", "--wheel", "--sdist"], cwd=ROOT)


if __name__ == "__main__":
    main()
