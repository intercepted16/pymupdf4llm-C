#!/usr/bin/env python3
"""Setup script for compiling the Cython PDF-to-Markdown converter with full performance + debug symbols."""

import os

from Cython.Build import cythonize
from Cython.Compiler import Options
from setuptools import Extension, setup

# Cython compiler directives
compiler_directives = {
    "language_level": 3,
    "boundscheck": False,
    "wraparound": False,
    "cdivision": True,
    "nonecheck": False,
    "overflowcheck": False,
    "embedsignature": True,
    "infer_types": True,
    "optimize.use_switch": True,
    "optimize.unpack_method_calls": True,
}

# Disable docstrings in C
Options.docstrings = False
Options.embed_pos_in_docstring = False

# Compiler and linker flags for optimized builds with debug info
if os.name != "nt":
    extra_compile_args = [
        "-O3",
        "-g",  # debug info for GDB
        "-fno-omit-frame-pointer",  # stack traces work
        "-march=native",
        "-mtune=native",
        "-ffast-math",
        "-funroll-loops",
        "-finline-functions",
        "-ftree-vectorize",
        "-fno-signed-zeros",
        "-fno-trapping-math",
        "-fassociative-math",
        "-freciprocal-math",
        "-Wno-unused-function",
        "-Wno-unused-variable",
        "-Wno-sign-compare",
    ]
    extra_link_args = ["-g", "-Wl,-O1"]
else:
    extra_compile_args = ["/O2", "/Zi", "/Oi", "/Ot", "/Oy", "/GL", "/DNDEBUG"]
    extra_link_args = ["/DEBUG"]

extensions = [
    Extension(
        name="to_md",
        sources=["to_md.pyx"],
        extra_compile_args=extra_compile_args,
        extra_link_args=extra_link_args,
        define_macros=[
            ("NPY_NO_DEPRECATED_API", "NPY_1_7_API_VERSION"),
            ("CYTHON_TRACE", "0"),
            ("CYTHON_TRACE_NOGIL", "0"),
        ],
    )
]

setup(
    name="to_md_cython",
    version="1.0.0",
    ext_modules=cythonize(
        extensions,
        compiler_directives=compiler_directives,
        annotate=True,  # generate HTML annotation to inspect optimizations
        nthreads=os.cpu_count() or 1,
        build_dir="build",
    ),
)
