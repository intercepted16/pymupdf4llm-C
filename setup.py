from setuptools import setup, find_packages

setup(
    name="pymupdf4llm_c",
    version="1.0.0",
    packages=find_packages(),  # find all packages under pymupdf4llm_c
    include_package_data=True,  # makes sure package_data is respected
    package_data={
        "pymupdf4llm_c": ["lib/*.so"],  # include all .so in lib/
    },
)
