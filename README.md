# PyMuPDF4LLM-C

PyMuPDF4LLM-C provides a high-throughput C extractor for MuPDF that emits
page-level JSON describing text, layout metadata, figures, and detected
tables. It exposes both **Python and Rust bindings** for safe and ergonomic access.

## Highlights

- **Native extractor** – `libtomd` walks each PDF page with MuPDF and writes
  `page_XXX.json` artefacts containing block type, geometry, font metrics, and
  basic heuristics used by retrieval pipelines.
- **Safe, idiomatic bindings** – Python (`pymupdf4llm_c`) and Rust (`pymupdf4llm-c`) 
  APIs provide easy, memory-safe access without exposing raw C pointers.
- **Single source of truth** – All heuristics, normalisation, and JSON
  serialisation now live in dedicated C modules under `src/`, with public
  headers exposed via `include/` for downstream extensions.

## Installation

Install the Python package from PyPI:

```bash
pip install pymupdf4llm-c
````

For Rust, you can install with Cargo:
```bash
cargo add pymupdf4llm-c
````

## Building the native extractor

```bash
./build.sh                      # Release build in build/native
BUILD_DIR=build/debug ./build.sh # Custom build directory
CMAKE_BUILD_TYPE=Debug ./build.sh
```

---

<details>
<summary>Python Usage</summary>

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

Collect parsed JSON structures:

```python
results = to_json("report.pdf", collect=True)
for page_blocks in results:
    for block in page_blocks:
        print(f"Block type: {block['type']}, Text: {block.get('text', '')}")
```

Override the shared library location:

```python
config = ConversionConfig(lib_path=Path("/opt/lib/libtomd.so"))
results = to_json("report.pdf", config=config, collect=True)
```

</details>

<details>
<summary>Rust Usage</summary>

### Basic usage

```rust
use std::path::Path;
use pymupdf4llm_c::{to_json, to_json_collect, extract_page_json, PdfError};

fn main() -> Result<(), PdfError> {
    let pdf_path = Path::new("example.pdf");

    // Extract to files
    let paths = to_json(pdf_path, None)?;
    println!("Generated {} JSON files:", paths.len());
    for path in &paths {
        println!("  - {:?}", path);
    }

    // Collect JSON in memory
    let pages = to_json_collect(pdf_path, None)?;
    println!("Parsed {} pages in memory", pages.len());

    // Extract single page
    let page_json = extract_page_json(pdf_path, 0)?;
    println!("First page JSON: {}", page_json);

    Ok(())
}
```

* **Error handling:** all functions return `Result<_, PdfError>`
* **Memory-safe:** FFI confined internally, no unsafe needed at the call site
* **Output:** file paths or in-memory JSON (`serde_json::Value`)

</details>

## JSON output structure

Each PDF page is extracted to a separate JSON file (e.g., `page_001.json`) containing an array of block objects:

```json
[
  {
    "type": "paragraph",
    "text": "Extracted text content",
    "bbox": [72.0, 100.5, 523.5, 130.2],
    "font_size": 11.0,
    "font_weight": "normal",
    "page_number": 0,
    "length": 22
  }
]
```

**Block types:** `paragraph`, `heading`, `table`, `list`, `figure`

**Key fields:**

* `bbox` – Bounding box `[x0, y0, x1, y1]` in PDF points
* `type` – Block classification for semantic processing
* `font_size`, `font_weight` – Styling metadata
* Tables include `row_count`, `col_count`, `confidence`

## Command-line usage (Python)

```bash
python -m pymupdf4llm_c.main input.pdf [output_dir]
```

If `output_dir` is omitted a sibling directory suffixed with `_json` is
created. The command prints the destination and each JSON file that was written.

## Development workflow

1. Create and activate a virtual environment and install dev extras:

```bash
python -m venv .venv
source .venv/bin/activate
pip install -e .[dev]
```

2. Build the native extractor (`./build.sh`)
3. Run linting and tests:

```bash
./lint.sh
pytest
```

## Troubleshooting

* **Library not found** – Build `libtomd` and ensure it is discoverable.
* **Build failures** – Check MuPDF headers/libraries.
* **Different JSON output** – Heuristics live in C code under `src/`; rebuild after changes.

## License

See `LICENSE` for details.

```
