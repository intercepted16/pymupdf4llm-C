# PyMuPDF4LLM-C

High-performance PDF to Markdown conversion that pairs a MuPDF-based C backend
with Python fallbacks built on top of `pymupdf4llm`. The package exposes a
simple `to_markdown()` helper that mirrors the reference Python implementation
while defaulting to the optimised native bindings when available.

The repository also provides CMake tooling for building the `libtomd` shared
library that powers the native integration.
