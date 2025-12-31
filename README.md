# PyMuPDF4LLM-C

fast PDF extractor in C using MuPDF. Outputs structured JSON with layout metadata. ~300 pages/second.

**primarily intended for use with python bindings. but for some reason i got bored and added Rust ones too if ya want.**

---

## what this is

a PDF extractor in C using MuPDF, inspired by pymupdf4llm. i took many of its heuristics and approach but rewrote it in C for speed, then bound it to Python and Rust so it's easy to use.

outputs JSON for every block: text, type, bounding box, font metrics, tables. you get the raw data to process however you need.

speed: ~300 pages/second on CPU. 1 million pages in ~55 minutes.

---

## the problem

most extractors give you raw text (fast but useless) or over-engineered solutions (slow, opinionated, not built for what you need). you want structured data. you want to know where things are, what they are, whether they're headers or body text. and you want this fast if you're processing large volumes.

---

## what you get

JSON with geometry, typography, and structure. use bounding boxes to find natural document boundaries. detect headers and footers by coordinates. reconstruct tables properly. you decide what to do with it.

```json
{
  "type": "heading",
  "text": "Step 1. Gather threat intelligence",
  "bbox": [64.00, 173.74, 491.11, 218.00],
  "font_size": 21.64,
  "font_weight": "bold"
}
```

instead of splitting on word count and getting mid-sentence breaks, you use layout to chunk semantically.

---

## comparison

| Tool | Speed (pps) | Quality | Tables | JSON | Use Case |
|------|-------------|---------|--------|------|----------|
| pymupdf4llm-C | ~300 | Good | Yes | Structured | High volume, full control |
| pymupdf4llm | ~10 | Good | Yes | Markdown | General |
| pymupdf | ~250 | Subpar | No | Text only | Basic extraction |
| marker | ~0.5-1 | Excellent | Yes | Markdown | Maximum accuracy |
| docling | ~2-5 | Excellent | Yes | JSON | Document intelligence |
| PaddleOCR | ~20-50 | Good (OCR) | Yes | Text | Scanned documents |

tradeoff: speed and control vs automatic extraction. marker and docling give higher fidelity if you have time.

---

## what it handles well

- millions of pages, fast
- custom parsing logic; you own the rules
- document archives, chunking strategies, any structured extraction
- CPU only; no expensive inference
- iterating on parsing logic without waiting hours

---

## what it doesn't handle

- scanned or image-heavy PDFs (no OCR)
- 99%+ accuracy on edge cases; trades precision for speed
- figures or image extraction

---

## installation

```bash
pip install pymupdf4llm-c
```

or Rust:

```bash
cargo add pymupdf4llm-c
```

wheels for Python 3.10–3.13 on macOS (ARM/x64) and Linux (glibc > 2.11). no Windows; see [BUILD.md](BUILD.md) to compile.

---

## usage

<details>
<summary><b>Python</b></summary>

### basic

```python
from pymupdf4llm_c import to_json

output_file = to_json("example.pdf")
print(f"Extracted to: {output_file}")
```

### collect in memory

```python
pages = to_json("report.pdf", collect=True)

for page_obj in pages:
    blocks = page_obj.get("data", [])
    for block in blocks:
        print(f"{block.get('type')}: {block.get('text', '')}")
```

### large files (streaming)

```python
from pymupdf4llm_c import iterate_json_pages

for page_blocks in iterate_json_pages("large.pdf"):
    for block in page_blocks:
        print(f"Block type: {block['type']}")
```

### per-page files

```python
json_files = to_json(pdf_path, output_dir="output_json")
```

### command-line

```bash
python -m pymupdf4llm_c.main input.pdf [output_dir]
```

</details>

<details>
<summary><b>Rust</b></summary>

```rust
use pymupdf4llm_c::{to_json, to_json_collect, PdfError};

fn main() -> Result<(), PdfError> {
    let paths = to_json("example.pdf", None)?;
    println!("Generated {} files", paths.len());

    let pages = to_json_collect("example.pdf", None)?;
    println!("Parsed {} pages", pages.len());

    Ok(())
}
```

</details>

---

## output structure

each page is a JSON array of blocks:

```json
[
  {
    "type": "heading",
    "text": "Introduction",
    "bbox": [72.0, 100.5, 523.5, 130.2],
    "font_size": 21.64,
    "font_weight": "bold",
    "page_number": 0
  },
  {
    "type": "paragraph",
    "text": "This document describes...",
    "bbox": [72.0, 140.5, 523.5, 200.2],
    "font_size": 12.0,
    "page_number": 0
  },
  {
    "type": "table",
    "bbox": [72.0, 220.0, 523.5, 400.0],
    "row_count": 3,
    "col_count": 2,
    "rows": [
      {
        "cells": [
          { "text": "Header A", "bbox": [72.0, 220.0, 297.75, 250.0] },
          { "text": "Header B", "bbox": [297.75, 220.0, 523.5, 250.0] }
        ]
      }
    ]
  }
]
```

fields: `type` (text, heading, paragraph, table, list, code), `bbox` (x0, y0, x1, y1), `font_size`, `font_weight`, `spans` (when styled).

---

## faq

**why not marker/docling?**  
if you have time and need maximum accuracy, use those. this is for when you're processing millions of pages or iterating on extraction logic quickly.

**how do i use bounding boxes for semantic chunking?**  
large y-gaps indicate topic breaks. font size changes show sections. indentation shows hierarchy. you write the logic using the metadata.

**will this handle my complex PDF?**  
optimized for well-formed digital PDFs. scanned documents, complex table structures, and image-heavy layouts won't extract as well as ML tools.

**commercial use?**  
only under AGPL v3 or with a license from Artifex (MuPDF's creators). see LICENSE.

---

## building from source

see [BUILD.md](BUILD.md).

---

## development

```bash
python -m venv .venv
source .venv/bin/activate
pip install -e .[dev]
```

build native extractor, then:

```bash
./lint.sh
pytest
```

---

## license

AGPL v3. commercial use requires license from Artifex.

---

## links

- repo: [github.com/intercepted16/pymupdf4llm-C](https://github.com/intercepted16/pymupdf4llm-C)
- pypi: [pymupdf4llm-C](https://pypi.org/project/pymupdf4llm-C)

feedback welcome.
