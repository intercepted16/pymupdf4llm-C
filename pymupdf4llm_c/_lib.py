"""Utilities for locating the compiled ``libtomd`` shared object."""

from __future__ import annotations

import os
import sys
from functools import lru_cache
from pathlib import Path
from typing import Iterator, Sequence

_ENV_VAR = "PYMUPDF4LLM_C_LIB"
_PACKAGE_NAME = "pymupdf4llm_c"


def _shared_library_names() -> Sequence[str]:
    """Return possible filenames for the shared library on this platform."""
    if sys.platform == "win32":
        return ("tomd.dll",)
    if sys.platform == "darwin":
        return ("libtomd.dylib", "tomd.dylib")
    # Default to POSIX shared object naming.
    return ("libtomd.so", "tomd.so")


def _iter_search_directories(package_root: Path) -> Iterator[Path]:
    """Yield directories that may contain the bundled shared library."""
    project_root = package_root.parent
    build_root = project_root / "build"

    # Primary locations first: packaged library within the module itself.
    yield package_root / "lib"

    # Editable installs often place build artefacts directly under the project root.
    yield project_root / "lib"

    # Default CMake output inside the top-level ``build`` directory.
    yield build_root
    yield build_root / "lib"

    # Known setuptools build directory pattern.
    yield build_root / "lib" / _PACKAGE_NAME / "lib"

    if build_root.exists():
        for child in build_root.iterdir():
            if child.is_dir() and child.name.startswith("lib"):
                yield child
                yield child / _PACKAGE_NAME / "lib"


def _iter_candidate_paths(package_root: Path) -> Iterator[Path]:
    """Generate candidate paths for the shared library."""
    names = _shared_library_names()
    seen: set[Path] = set()

    # Environment override takes absolute precedence.
    env_path = os.environ.get(_ENV_VAR)
    if env_path:
        candidate = Path(env_path)
        if candidate.exists():
            resolved = candidate.resolve()
            if resolved not in seen:
                seen.add(resolved)
                yield resolved

    for directory in _iter_search_directories(package_root):
        if not directory.exists():
            continue
        for name in names:
            for candidate in directory.rglob(name):
                if not candidate.is_file():
                    continue
                resolved = candidate.resolve()
                if resolved not in seen:
                    seen.add(resolved)
                    yield resolved


@lru_cache(maxsize=1)
def get_default_library_path() -> Path | None:
    """Attempt to locate the packaged ``libtomd`` shared library.

    Returns ``None`` when the library cannot be found.
    """
    package_root = Path(__file__).resolve().parent

    for candidate in _iter_candidate_paths(package_root):
        return candidate

    return None


def clear_cached_library_path() -> None:
    """Reset the cached resolution result (useful for tests)."""
    get_default_library_path.cache_clear()
