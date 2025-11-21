from __future__ import annotations

import os
import sys
from functools import lru_cache
from pathlib import Path
from typing import Callable, cast

from cffi import FFI


@lru_cache(maxsize=1)
def get_ffi() -> FFI:
    """Return a configured FFI instance for the libtomd library."""
    ffi = FFI()

    # Declare the C function signatures and structs
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

    # Table functions
    find_tables_on_page: Callable[[object, object, int, object], object]
    find_tables_with_mupdf_native: Callable[[bytes, int], object]
    free_table_array: Callable[[object], None]


@lru_cache(maxsize=1)
def get_lib(ffi: FFI, path: Path | str) -> Lib:
    """Load the shared library and return it as a typed Lib object."""
    try:
        # Add the library directory to LD_LIBRARY_PATH so dependencies can be found
        lib_dir = Path(path).parent
        
        # For Linux/macOS, we need to use ctypes to preload the dependency
        if sys.platform.startswith('linux') or sys.platform == 'darwin':
            import ctypes
            mupdf_lib = lib_dir / "libmupdf.so.27.0"
            if mupdf_lib.exists():
                # Load libmupdf.so.27.0 with RTLD_GLOBAL so it's available to libtomd.so
                ctypes.CDLL(str(mupdf_lib), mode=ctypes.RTLD_GLOBAL)
        
        # Load the dynamic library (adjust path as needed)
        _lib = ffi.dlopen(str(path))
    except OSError as e:
        raise RuntimeError("Failed to load libtomd shared library") from e

    # Cast to our typed Lib so static checkers know the types
    return cast(Lib, _lib)
