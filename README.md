# PyMuPDF4LLM-C

PyMuPDF4LLM-C provides a high-throughput C extractor for MuPDF that emits
page-level JSON describing text, layout metadata, figures, and detected
Tables. The Python package layers a small ctypes shim and convenience API on
top.

## Highlights

- **Native extractor** – `libtomd` walks each PDF page with MuPDF and writes
  `page_XXX.json` artefacts containing block type, geometry, font metrics, and
  basic heuristics used by retrieval pipelines.
- **Python-friendly API** – `pymupdf4llm_c.to_json()` returns the generated
  JSON paths or (optionally) the parsed payloads so it slots into existing
  tooling.
- **Single source of truth** – All heuristics, normalisation, and JSON
  serialisation now live in dedicated C modules under `src/`, with public
  headers exposed via `include/` for downstream extensions.

## Installation

Install the published wheel or sdist directly from PyPI:

```bash
pip install pymupdf4llm-c
```

The wheel bundles a prebuilt `libtomd` for common platforms. If the shared
library cannot be located at runtime you will receive a `LibraryLoadError`.
Provide the path manually via `ConversionConfig(lib_path=...)` or the
`PYMUPDF4LLM_C_LIB` environment variable.

## Building the native extractor

When working from source (or on an unsupported platform) build the C library
before invoking the Python API:

```bash
./build.sh                      # Release build in build/native
BUILD_DIR=build/debug ./build.sh # Custom build directory
CMAKE_BUILD_TYPE=Debug ./build.sh
```

The script configures CMake, compiles `libtomd`, and leaves the artefact under
`build/` so the Python package can find it. The headers are under `include/`
if you need to consume the C API directly.

## Python quick start

### Basic usage

```python
from pathlib import Path

from pymupdf4llm_c import ConversionConfig, ExtractionError, to_json

pdf_path = Path("example.pdf")
output_dir = pdf_path.with_name(f"{pdf_path.stem}_json")

try:
    json_files = to_json(pdf_path, output_dir=output_dir)
    print(f"Generated {len(json_files)} files:")
    for path in json_files:
        print(f"  - {path}")
except ExtractionError as exc:
    print(f"Extraction failed: {exc}")
```

### Advanced features

Collect parsed JSON structures instead of file paths:

```python
results = to_json("report.pdf", collect=True)
for page_blocks in results:
    for block in page_blocks:
        print(f"Block type: {block['type']}, Text: {block.get('text', '')}")
```

Process multiple PDFs in batch:

```python
from pymupdf4llm_c import batch_convert

pdfs = ["doc1.pdf", "doc2.pdf", "doc3.pdf"]
results = batch_convert(pdfs, continue_on_error=True)

for pdf, output in results.items():
    if isinstance(output, Exception):
        print(f"Failed: {pdf}: {output}")
    else:
        print(f"Success: {pdf} -> {len(output)} pages")
```

Extract specific page ranges with the `pages` parameter:

```python
# Extract only pages 5-10 (0-indexed)
json_files = to_json("large.pdf", pages=(5, 10))
```

Extract PDF metadata:

```python
from pymupdf4llm_c import get_metadata

metadata = get_metadata("example.pdf")
print(f"Page count: {metadata['page_count']}")
```

Use progress callbacks and verbose logging for long-running operations:

```python
def progress(current, total):
    print(f"Processing page {current}/{total}")

config = ConversionConfig(
    progress_callback=progress,
    verbose=True
)
json_files = to_json("large.pdf", config=config, pages=(0, 50))
```

Override the shared library location:

```python
config = ConversionConfig(lib_path=Path("/opt/lib/libtomd.so"))
results = to_json("report.pdf", config=config, collect=True)
```

## Performance tips

- **Page range filtering**: Extract only the pages you need using the `pages`
  parameter to reduce processing time and memory usage.
- **Library caching**: The shared library is automatically cached after first
  load, improving performance for repeated calls.
- **Batch processing**: Process multiple PDFs in sequence to benefit from
  library caching.
- **Progress callbacks**: Use progress callbacks to monitor long-running
  operations without blocking.

## Command-line usage

The package includes a minimal CLI that mirrors the Python API:

```bash
python -m pymupdf4llm_c.main input.pdf [output_dir]
```

If `output_dir` is omitted a sibling directory suffixed with `_json` is
created. The command prints the destination and each JSON file that was
written.

## Development workflow

1. Create and activate a virtual environment, then install the project in
   editable mode with the dev extras:
   ```bash
   python -m venv .venv
   source .venv/bin/activate
   pip install -e .[dev]
   ```
2. Build the native extractor (`./build.sh`) so tests can load `libtomd`.
3. Run linting and the test suite:
   ```bash
   ./lint.sh
   pytest
   ```

`requirements-test.txt` lists the testing dependencies if you prefer manual
installation.

## Troubleshooting

- **Library not found** – Build the extractor and ensure the resulting
  `libtomd.*` is on disk. Set `PYMUPDF4LLM_C_LIB` or
  `ConversionConfig(lib_path=...)` if the default search paths do not apply to
  your environment.
- **Build failures** – Verify MuPDF development headers and libraries are
  installed and on the compiler's search path. Consult `CMakeLists.txt` for the
  expected dependencies.
- **Different JSON output** – The heuristics live entirely inside the C code
  under `src/`. Adjust them there and rebuild to change behaviour.

## License

See `LICENSE` for details.
