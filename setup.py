"""setup.py: Build script for pymupdf4llm_c package."""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

from setuptools import setup
from setuptools.command.build_py import build_py as build_py_base
from wheel.bdist_wheel import bdist_wheel as bdist_wheel_base

ROOT = Path(__file__).parent.resolve()
PACKAGENAME = "pymupdf4llm_c"
TARGET_NAME = "tomd"
LIB_BASENAME = "tomd"


class build_py(build_py_base):
    """Custom build that compiles the Go shared library."""

    def run(self) -> None:
        self._build_libtomd()
        super().run()

    def _build_libtomd(self) -> None:
        go_dir = ROOT / "go"
        build_dir = ROOT / "build"

        build_dir.mkdir(parents=True, exist_ok=True)

        
        print("Downloading Go dependencies...")
        try:
            subprocess.check_call(["go", "mod", "download"], cwd=go_dir)
        except subprocess.CalledProcessError as e:
            print(f"Warning: Failed to download Go dependencies: {e}")
            

        
        if sys.platform == "linux":
            lib_ext = ".so"
        elif sys.platform == "darwin":
            lib_ext = ".dylib"
        elif sys.platform == "win32":
            lib_ext = ".dll"
        else:
            lib_ext = ".so"

        lib_name = f"lib{LIB_BASENAME}{lib_ext}"
        output_path = build_dir / lib_name

        
        print(f"Building Go shared library: {lib_name}")
        build_cmd = [
            "go",
            "build",
            "-buildmode=c-shared",
            "-o",
            str(output_path),
            "./cmd/tomd",
        ]

        env = os.environ.copy()

        try:
            subprocess.check_call(build_cmd, cwd=go_dir, env=env)
        except subprocess.CalledProcessError as e:
            print(f"Error building Go library: {e}")
            raise

        if not output_path.exists():
            raise FileNotFoundError(
                f"Go build succeeded but library not found at {output_path}"
            )

        
        target_dir = Path(self.build_lib) / PACKAGENAME / "lib"
        target_dir.mkdir(parents=True, exist_ok=True)

        print(f"Copying {output_path} to {target_dir / lib_name}")
        shutil.copy2(output_path, target_dir / lib_name)


class bdist_wheel(bdist_wheel_base):
    """Custom bdist_wheel to mark the wheel as platform-specific."""

    def finalize_options(self) -> None:
        super().finalize_options()
        
        self.root_is_pure = False


if __name__ == "__main__":
    setup(
        name=PACKAGENAME,
        packages=[PACKAGENAME],
        package_data={PACKAGENAME: ["lib/*.so", "lib/*.dylib", "lib/*.dll"]},
        cmdclass={"build_py": build_py, "bdist_wheel": bdist_wheel},
    )
