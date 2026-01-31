# PyMuPDF4LLM-C

> This projects C extension has now been rewritten in Go. Performance, quality, and code quality have all improved. However, the Python-API remains the same.

A "blazingly-fast" (oh wait, this isn't in Rust..) PDF extractor **for Python** written in Go using MuPDF in the backend, inspired by `pymupdf4llm`. I took many of its heuristics and approaches. Initially, it was supposed to be a 1:1 port (just generating the same Markdown output), but I later pivoted.

Most extractors give you raw text (fast but useless) or *full-on* OCR/ML. This is a middle ground.

Outputs JSON for every block: text, type, bounding box, font metrics, tables. You get the raw data to process however you need.

**Speed (averaged):** ~520 pages/second on CPU. 1 million pages in ~32 minutes.

**Full performance breakdown** [here](#Performance-Breakdown)

**Capabilities/comparisons to others tools** [here](#Capabilities).

**Primarily intended for use with Python bindings.**

---

# Installation

```bash
pip install pymupdf4llm-c
```

*You can prefix this with whatever tools you use, like `uv`, `poetry`, etc.*

> There are wheels for Python 3.9–3.14 (inclusive of minor versions) on macOS (ARM/x64) and all modern Linux distributions.

**To build from source**, see [BUILD.md](BUILD.md). 

---
# Capabilities

| Tool            | Speed (pps) | Tables | Images (Figures)                                  | OCR (Y/N)     | JSON Output      | Best For              |
| --------------- | ----------- | ------ | ------------------------------------------------- | ------------- | ---------------- | --------------------- |
| pymupdf4llm-C   | ~300        | Yes    | No (WIP)                                          | N             | Yes (structured) | RAG, high volume      |
| pymupdf4llm     | ~10         | Yes    | Yes (but not ML to get contents)                  | N             | Markdown         | General extraction    |
| pymupdf (alone) | ~250        | No     | No, not by itself, requires more effort I believe | N             | No (text only)   | basic text extraction |
| marker          | ~0.5-1      | Yes    | Yes (contents with ML?)                           | Y (optional?) | Markdown         | Maximum fidelity      |
| docling         | ~2-5        | Yes    | Yes                                               | Y             | JSON             | Document intelligence |
| PaddleOCR       | ~20-50      | Yes    | Yes                                               | Y             | Text             | Scanned documents     |


**Trade-off:** speed and control vs automatic extraction. Marker and Docling give higher fidelity if you have time.

## what it handles well

- millions of pages, fast
- custom parsing logic; you own the rules
- document archives, chunking strategies, any structured extraction
- CPU only; no expensive inference
- iterating on parsing logic without waiting hours

## what it doesn't handle

- scanned or image-heavy PDFs (no OCR)
- 99%+ accuracy on edge cases; trades precision for speed
- figures or image extraction

---
# Usage

### basic

```python
from pymupdf4llm_c import to_json

result = to_json("example.pdf", output="example.json")
print(f"Extracted to: {result.path}")
```

> You can omit the `output` field; it defaults to `<file>.json`

### collect all pages in memory

```python
result = to_json("report.pdf", output="report.json")
pages = result.collect()

# Access pages as objects with markdown conversion
for page in pages:
    print(page.markdown)
    
# Access individual blocks
for block in pages[0]:
    print(f"{block.type}: {block.text if hasattr(block, 'text') else ''}")
```

> This still saves it to `result.path`; it just allows you to load it into memory. If you don't want to write to disk at all, consider providing a special path.

> This is only for smaller PDFs. For larger ones, this may result in crashes due to loading everything into RAM. See below for a solution.

### stream pages (memory-efficient)

```python
result = to_json("large.pdf", output="large.json")

# Iterate one page at a time without loading everything
for page in result:
    for block in page:
        print(f"Block type: {block.type}")
```

### convert to markdown

```python
result = to_json("document.pdf", output="document.json")
pages = result.collect()

# Full document as markdown
full_markdown = pages.markdown

# Single page as markdown
page_markdown = pages[0].markdown

# Single block as markdown
block_markdown = pages[0][0].markdown
```

> `.markdown` is a property, not a function

### command-line

```bash
python -m pymupdf4llm_c.main input.pdf [output_dir]
```

---

## Output structure

Each page is a JSON array of blocks. Every block has:

- `type`: block type (text, heading, paragraph, list, table, code)
- `bbox`: [x0, y0, x1, y1] bounding box coordinates
- `font_size`: font size in points (average for multi-span blocks)
- `length`: character count
- `spans`: array of styled text spans with style flags (bold, italic, mono-space, etc.)

> Note that a span represents a logical group of styling. in *most* blocks, it is likely that there is only one span.

### Block types 

> *Not real JSON; just to demonstrate output. (pseudo).*

**text/paragraph/code blocks:**
```json
{
  "type": "text",
  "bbox": [72.03, 132.66, 542.7, 352.22],
  "font_size": 12.0,
  "length": 1145,
  "lines": 14,
  "spans": [
    {
      "text": "Block content here...",
      "font_size": 12.0,
      "bold": false,
      "italic": false,
      "monospace": false,
      "strikeout": false,
      "superscript": false,
      "subscript": false,
      "link": false,
      "uri": false
    }
  ]
}
```

**headings:**
```json
{
  "type": "heading",
  "bbox": [111.80, 187.53, 509.10, 217.56],
  "font_size": 32.0,
  "length": 25,
  "level": 1,
  "spans": [
    {
      "text": "Heading Text",
      // all styling flags (as seen in the above)
    }
  ]
}
```

**lists:**
```json
{
  "type": "list",
  "bbox": [40.44, 199.44, 107.01, 345.78],
  "font_size": 11.04,
  "length": 89,
  "spans": [],
  "items": [
    {
      "spans": [
        {
          "text": "First item",
		  // all styling flags.
        }
      ],
      "list_type": "bulleted",
      "indent": 0,
      "prefix": false
    },
    {
      "spans": [
        {
          "text": "Second item",
		  // all styling flags.
        }
      ],
      "list_type": "numbered",
      "indent": 0,
      "prefix": "1."
    }
  ]
}
```

**tables:**
```json
{
  "type": "table",
  "bbox": [72.0, 220.0, 523.5, 400.0],
  "font_size": 12.0,
  "length": 256,
  "row_count": 3,
  "col_count": 2,
  "cell_count": 2,
  "spans": [],
  "rows": [
    {
      "bbox": [72.0, 220.0, 523.5, 250.0],
      "cells": [
        {
          "bbox": [72.0, 220.0, 297.75, 250.0],
          "spans": [
            {
              "text": "Header A",
              // all styling flags.
            }
          ]
        },
        {
          "bbox": [297.75, 220.0, 523.5, 250.0],
          "spans": [
            {
              "text": "Header B",
              // all styling flags.
            }
          ]
        }
      ]
    }
  ]
}
```

### Span fields

all text spans contain:
- `text`: span content
- `font_size`: size in points
- `bold`, `italic`, `monospace`, `strikeout`, `superscript`, `subscript`: boolean style flags
- `link`: boolean indicating if span contains a hyperlink
- `uri`: URI string if linked, otherwise false

---

# FAQ

**why not marker/docling?**  
if you have time and need maximum accuracy, use those. this is for when you're processing millions of pages or iterating on extraction logic quickly.

**how do i use bounding boxes for semantic chunking?**  
large y-gaps indicate topic breaks. font size changes show sections. indentation shows hierarchy. you write the logic using the metadata.

**will this handle my complex PDF?**  
optimized for well-formed digital PDFs. scanned documents, complex table structures, and image-heavy layouts won't extract as well as ML tools.

**commercial use?**  
only under AGPL-v3 or with a license from Artifex (MuPDF's creators). see [LICENSE](LICENSE)

**Any trade-offs due to the speed gains; you must have lost some fidelity from `pymupdf4llm`?**
If we're talking trade-offs in comparison to PyMuPDF4LLM:

Not as much as you'd think.

The reason for PyMuPDF4LLM being so slow wasn't due to its quality. It was an inefficient code-base. O(n^2) algorithms, raw numbers in Python, pretty much just unoptimized code and a bad language for lots of maths.

This isn't a trade-off of the project itself, but there may still be minor cases where I haven't 100% copied the heuristics.

If we're talking about trade-offs in comparison to tools like Paddle, Marker & Docling:

It does not do any fancy ML. It's just some basic geometric maths. Therefore it won't handle:

- scanned pages; no OCR  
- & complex tables or tables without some form of edges 

**why did you build this?**
Dumb reason. I was building a RAG project with my dad (I'm 15). He did not care about speed at all. But I just got bored of waiting for chunking the PDFs every time I made a minor change. I couldn't find anything with even 50% of the quality that would be faster. And anyway, my chunks were trash. So it was either: raw text, or ML, and I didn't want either of them.

---
# Performance Breakdown

Using `go/cmd/tomd/main.go` with `input_pdf [output_dir]`, I measured performance on:

- ~1600 page document (path not available)
- ~150 page document (`test_data/pdfs/nist.pdf`)

> Performance depends on document size and available cores. With more pages to saturate your cores, you may see better throughput. Wall-clock time should scale approximately linearly with core count.

**Test system:** AMD Ryzen 7 4800H (8 cores, 6 used)

**Runtime breakdown:**
- Go code: ~25% of runtime
- MuPDF: ~75% of runtime

On the NIST document (150 pages): Go spent 78ms out of 363ms total (21%), MuPDF spent 285ms (79%).

**Calculated average:**
- 1600 pages in 3000ms + 150 pages in 350ms = 1750 pages in 3350ms
- **~520 pages/second**

---
# Licensing and Links

## licensing

TL;DR: use it all you want in OSS software. if you buy license for MUPDF from Artifex, you are excluded from all AGPL requirements.

- derived work of `mupdf`.
- inspired by `pymupdf4llm`; i have used it as a reference

AGPL v3. commercial use requires license from Artifex.


modifications and enhancements specific to this library are 2026 Adit Bajaj.

see [LICENSE](LICENSE) for the legal stuff.

## links

- repo: [github.com/intercepted16/pymupdf4llm-C](https://github.com/intercepted16/pymupdf4llm-C)
- pypi: [pymupdf4llm-C](https://pypi.org/project/pymupdf4llm-C)

feedback welcome.
