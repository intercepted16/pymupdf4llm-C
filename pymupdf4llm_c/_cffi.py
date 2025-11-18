from __future__ import annotations

from functools import lru_cache
from pathlib import Path
from typing import Callable, cast

from cffi import FFI


@lru_cache(maxsize=1)
def get_ffi() -> FFI:
    """Return a configured FFI instance for the libtomd library."""
    ffi = FFI()

    # Declare the C function signatures
    ffi.cdef("""
        int pdf_to_json(const char *pdf_path, const char *output_dir);
        char *page_to_json_string(const char *pdf_path, int page_number);
        void free(void *ptr);
    """)

    return ffi


# --- Typing for our library functions ---
class Lib:
    pdf_to_json: Callable[[bytes, bytes], int]
    page_to_json_string: Callable[[bytes, int], str]
    free: Callable[[object], None]


@lru_cache(maxsize=1)
def get_lib(ffi: FFI, path: Path | str) -> Lib:
    """Load the shared library and return it as a typed Lib object."""
    try:
        # Load the dynamic library (adjust path as needed)
        _lib = ffi.dlopen(str(path))
    except OSError as e:
        raise RuntimeError("Failed to load libtomd shared library") from e

    # Cast to our typed Lib so static checkers know the types
    return cast(Lib, _lib)
