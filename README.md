# PyMuPDF4LLM-C

PyMuPDF4LLM-C pairs a high-throughput C pipeline with the reference
`pymupdf4llm` Python package to generate clean Markdown from PDFs.
It keeps the Python API you already know while preferring the native
implementation for a 10-100× speed-up on table heavy documents.

- C backend handles text extraction and table detection in parallel.
- Python fallback (via `pymupdf4llm`) keeps things working when the
  native library is unavailable.
- Drop-in replacement for `pymupdf4llm.to_markdown` with extra tuning
  hooks via `ConversionConfig`.

## Installation

The project is published as `pymupdf4llm-c`. Install it from PyPI or from
source:

```bash
pip install pymupdf4llm-c
# or
pip install .
```

The Python fallback requires `pymupdf4llm`; it will be pulled in as a
runtime dependency when you install this package.

### Optional native build requirements

Using the accelerated C extension assumes you have built the `libtomd`
shared library. You will need:

- CMake 3.20+
- A C compiler toolchain (GCC/Clang on Linux, MSVC on Windows, Xcode on macOS)

If the shared library is missing, the package will log a warning and
continue with the pure Python path.

## Quick start

```python
from pathlib import Path
from pymupdf4llm_c import to_markdown

pdf_path = Path("example.pdf")
md_path = to_markdown(pdf_path)
print(md_path.read_text()[:500])
```

`to_markdown` returns the path to the generated Markdown file. By default
it writes the output next to the PDF (`example.md`).

Pass `output_path` to control where the Markdown is written:

```python
md_path = to_markdown("reports/input.pdf", output_path="artifacts/output.md")
```

## Configuring the conversion

Fine-tune the runtime behaviour with `ConversionConfig`:

```python
from pymupdf4llm_c import ConversionConfig, to_markdown

config = ConversionConfig(
    dpi=200,
    use_batch_callback=True,
    pymupdf_kwargs={"page_chunks": True, "write_images": True, "image_path": "images"},
)

md_path = to_markdown("report.pdf", config=config)
```

Key options:

- `dpi`: Rasterisation DPI used by the fallback path for embedded images.
- `lib_path`: Override the detected shared library (`Path` or string).
- `use_batch_callback`: Toggle the high-throughput batch workflow. Disable
  this if you built only the single-callback variant of the C library.
- `pymupdf_kwargs`: Extra keyword arguments forwarded to
  `pymupdf4llm.to_markdown` when the fallback runs.

You can also guide discovery by setting the `PYMUPDF4LLM_C_LIB`
environment variable to point at a specific `libtomd` binary.

## Building the native library

The repository ships with a convenience script that configures and builds
the native component:

```bash
./build.sh                      # Release build to build/native/
BUILD_DIR=build/debug ./build.sh # Custom build directory
CMAKE_BUILD_TYPE=Debug ./build.sh
```

Once built, the shared library is placed where `ConversionConfig` can find
it automatically. If you prefer manual control, point `ConversionConfig.lib_path`
or `PYMUPDF4LLM_C_LIB` to the produced binary.

## Development workflow

1. Create a virtual environment and install the project in editable mode:
   ```bash
   python -m venv .venv
   source .venv/bin/activate
   pip install -e .[dev]
   ```
2. Run formatting and static checks:
   ```bash
   ./lint.sh
   ```
3. Execute the test suite:
   ```bash
   pytest
   ```

`requirements-test.txt` captures the testing dependencies if you prefer
installing them manually.

## Troubleshooting

- **Cannot load libtomd**: Ensure you ran `./build.sh` and that the produced
  library is on disk. As a fallback, set `PYMUPDF4LLM_C_LIB` to point at the
  exact `libtomd.*` file.
- **Python fallback errors**: Confirm `pymupdf4llm` is installed and up to date
  and that you are passing valid paths. The fallback writes directly to the
  requested `output_path` when provided.
- **Table extraction differences**: Toggle `use_batch_callback` or adjust
  `pymupdf_kwargs` to mirror the exact behaviour of the pure Python pipeline.

## License

See `LICENSE` for details.
