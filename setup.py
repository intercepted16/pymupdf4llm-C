from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

from setuptools import setup
from setuptools.command.build_py import build_py as build_py_base


ROOT = Path(__file__).parent.resolve()
PACKAGENAME = "pymupdf4llm_c"
TARGET_NAME = "tomd"
LIB_BASENAME = "tomd"


class build_py(build_py_base):
    """Custom build that compiles the MuPDF shared library with CMake."""

    def run(self) -> None:
        self._build_libtomd()
        super().run()

    def _build_libtomd(self) -> None:
        build_dir = ROOT / "build"
        cmake_build_type = os.environ.get("CMAKE_BUILD_TYPE", "Release")

        build_dir.mkdir(parents=True, exist_ok=True)
        lib_output = build_dir / "lib"

        # Use the *real* source directory, not ".."
        source_dir = ROOT

        configure_cmd = [
            "cmake",
            str(source_dir),
            f"-DCMAKE_BUILD_TYPE={cmake_build_type}",
        ]
        build_cmd = ["cmake", "--build", ".", "--target", TARGET_NAME]

        env = os.environ.copy()
        env.setdefault("CMAKE_BUILD_PARALLEL_LEVEL", str(os.cpu_count() or 1))

        subprocess.check_call(["cmake", "--version"], env=env)
        subprocess.check_call(configure_cmd, env=env, cwd=build_dir)
        subprocess.check_call(build_cmd, env=env, cwd=build_dir)

        produced = self._find_library(lib_output)
        target_dir = Path(self.build_lib) / PACKAGENAME / "lib"
        target_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy2(produced, target_dir / produced.name)

    @staticmethod
    def _find_library(search_dir: Path) -> Path:
        suffixes = {
            "linux": ".so",
            "darwin": ".dylib",
            "win32": ".dll",
        }
        platform = sys.platform
        suffix = suffixes.get(platform, ".so")

        pattern = f"*{LIB_BASENAME}*{suffix}"
        candidates = sorted(p for p in search_dir.glob(pattern) if p.is_file())
        if not candidates:
            raise FileNotFoundError(
                f"Unable to locate built {LIB_BASENAME} library in {search_dir}"
            )
        return candidates[0]


if __name__ == "__main__":
    setup(cmdclass={"build_py": build_py})
