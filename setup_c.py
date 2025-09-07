import numpy
from setuptools import Extension, setup

# Define the C extension
to_md_c_extension = Extension(
    "to_md_c",
    sources=["to_md.c"],
    include_dirs=[
        "/usr/include/python3.11",
        numpy.get_include(),
    ],
    extra_compile_args=[
        "-O3",  # Maximum optimization
        "-g",
        "-fno-omit-frame-pointer",
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
        "-std=c99",
    ],
    extra_link_args=[
        "-g",
        "-Wl,-O1",
    ],
    language="c",
)

setup(
    name="to_md_c",
    version="1.0.0",
    description="Ultra-optimized PDF to Markdown converter in pure C",
    author="AI Assistant",
    ext_modules=[to_md_c_extension],
    zip_safe=False,
    python_requires=">=3.7",
)

