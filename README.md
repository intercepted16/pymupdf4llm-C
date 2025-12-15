# PyMuPDF4LLM-C
PyMuPDF4LLM-C provides a **high-throughput C extractor** for MuPDF that emits page-level JSON describing text, layout metadata, figures, and detected tables. It exposes both **Python** and **Rust** bindings for safe and ergonomic access.

---

## Highlights
* **Native extractor** – `libtomd` walks each PDF page with MuPDF and writes `page_XXX.json` artifacts containing block type, geometry, font metrics, and basic heuristics used by retrieval pipelines.
* **Safe, idiomatic bindings** – Python (`pymupdf4llm_c`) and Rust (`pymupdf4llm-c`) APIs provide easy, memory-safe access without exposing raw C pointers.
* **Single source of truth** – All heuristics, normalization, and JSON serialization live in dedicated C modules under `src/`, with public headers exposed via `include/` for downstream extensions.

---

## Installation

Install the Python package from PyPI:

```bash
pip install pymupdf4llm-c
````

For Rust, install with Cargo:

```bash
cargo add pymupdf4llm-c
```

---

## Building the Native Extractor

For instructions on building the C extractor, see the dedicated [BUILD.md](BUILD.md) file. This covers building MuPDF from the submodule, compiling the shared library, and setting up `libmupdf.so`.

---

## Usage

<details>
<summary>Python Usage</summary>

### Basic usage

```python
from pathlib import Path
from pymupdf4llm_c import ConversionConfig, ExtractionError, to_json

pdf_path = Path("example.pdf")

try:
    # Extract to a merged JSON file (default)
    output_file = to_json(pdf_path)
    print(f"Extracted to: {output_file}")
except ExtractionError as exc:
    print(f"Extraction failed: {exc}")
```

### Collecting parsed blocks in memory

Use `collect=True` to get parsed JSON in memory instead of writing to a file:

```python
from pymupdf4llm_c import to_json

# Returns list of page data (merged JSON structure)
pages = to_json("report.pdf", collect=True)

for page_obj in pages:
    page_num = page_obj.get("page", 0)
    blocks = page_obj.get("data", [])
    print(f"Page {page_num}: {len(blocks)} blocks")
    
    for block in blocks:
        print(f"  Type: {block.get('type')}, Text: {block.get('text', '')}")
```

**Memory and Validation:**
- `collect=True` validates the JSON structure and raises `ValueError` if invalid
- For PDFs larger than ~100MB, a warning is logged recommending `iterate_json_pages()` instead
- Disable the warning with `warn_large_collect=False`:

```python
# Suppress memory warning for large PDFs
pages = to_json("large_document.pdf", collect=True, warn_large_collect=False)
```
```

### Iterating pages with validation

For validation and type-safe iteration over JSON page files, use the helper:

```python
from pymupdf4llm_c import iterate_json_pages

# Yields each page as a typed Block list
for page_blocks in iterate_json_pages("path/to/page_001.json"):
    for block in page_blocks:
        print(f"Block: {block['type']}")
        if block['type'] == 'table':
            print(f"  Table: {block.get('row_count')}x{block.get('col_count')}")
```

**Memory-Efficient Iteration:**
This generator is recommended for large PDFs that would consume significant memory with `collect=True`. It validates JSON structure on-the-fly and yields pages one at a time:

```python
from pathlib import Path
from pymupdf4llm_c import to_json, iterate_json_pages

# Extract PDF (writes to disk, low memory)
output_file = to_json("large_document.pdf")

# Iterate pages without loading all into memory
for page_blocks in iterate_json_pages(output_file):
    # Process each page individually
    process_page(page_blocks)
```

### Legacy per-page output

Extract to individual per-page JSON files:

```python
output_dir = Path("output_json")
json_files = to_json(pdf_path, output_dir=output_dir)
print(f"Generated {len(json_files)} files")
```

### Override the shared library location

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

* **Error handling** – all functions return `Result<_, PdfError>`
* **Memory-safe** – FFI confined internally, no `unsafe` needed at the call site
* **Output** – file paths or in-memory JSON (`serde_json::Value`)

</details>

---

## Output Structure

<details>
<summary>JSON Output Structure</summary>

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
  },
  {
    "type": "text",
    "text": "Bold example text",
    "bbox": [72.0, 140.5, 523.5, 155.2],
    "font_size": 12.0,
    "font_weight": "bold",
    "page_number": 0,
    "length": 17,
    "spans": [
      {
        "text": "Bold example text",
        "bold": true,
        "font_size": 12.0
      }
    ]
  }
]
```

**Key Fields:**
* **type** – `text`, `heading`, `paragraph`, `table`, `figure`, `list`, `code`
* **bbox** – Bounding box `[x0, y0, x1, y1]`
* **font_size** – Average font size in points
* **font_weight** – `normal`, `bold`, or other weights
* **spans** – (Optional) Array of styled text segments. Only present when:
  - There are multiple text segments with different styling, OR
  - The text has applied styling (bold, italic, monospace, etc.)
  
  Plain unstyled text blocks will not include the `spans` array to avoid duplication.

**Tables** include `row_count`, `col_count`, and `confidence` scores.

</details>

---

## Command-line Usage (Python)

```bash
python -m pymupdf4llm_c.main input.pdf [output_dir]
```

If `output_dir` is omitted, a sibling directory suffixed with `_json` is created. The command prints the destination and each JSON file that was written.

---

## Development Workflow

1. Create and activate a virtual environment, then install dev extras:

```bash
python -m venv .venv
source .venv/bin/activate
pip install -e .[dev]
```

2. Build the native extractor (see [BUILD.md](BUILD.md))

3. Run linting and tests:

```bash
./lint.sh
pytest
```

---

## Troubleshooting

* **Library not found** – Build `libtomd` and ensure it is discoverable.
* **Build failures** – Check MuPDF headers/libraries.
* **Different JSON output** – Heuristics live in C code under `src/`; rebuild after changes.

---

## License

AGPL v3. Needed because MuPDF is AGPL.

If your project is free and OSS you can use it as long as it’s also AGPL licensed. For commercial projects, you need a license from Artifex, the creators of MuPDF.

See [LICENSE](LICENSE.md) for full details.
