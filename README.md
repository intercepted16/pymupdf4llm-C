# PyMuPDF4LLM-C

fast PDF extractor in C using MuPDF. Outputs structured JSON with layout metadata. ~300 pages/second.

**primarily intended for use with python bindings.**

---

## what this is

a PDF extractor in C using MuPDF, inspired by pymupdf4llm. i took many of its heuristics and approach but rewrote it in C for speed, then bound it to Python so it's easy to use.

outputs JSON for every block: text, type, bounding box, font metrics, tables. you get the raw data to process however you need.

speed: ~300 pages/second on CPU. 1 million pages in ~55 minutes.

---

## the problem

most extractors give you raw text (fast but useless) or over-engineered solutions (slow, opinionated, not built for what you need). you want structured data. you want to know where things are, what they are, whether they're headers or body text. and you want this fast if you're processing large volumes.

---

## what you get

JSON with geometry, typography, and structure. use bounding boxes to find natural document boundaries. detect headers and footers by coordinates. reconstruct tables properly. you decide what to do with it.

<details>
<summary><b>heading with accurate coordinates and styling</b></summary>

```json
{
  "type": "heading",
  "bbox": [111.80, 187.53, 509.10, 217.56],
  "font_size": 32.0,
  "length": 25,
  "level": 1,
  "spans": [
    {
      "text": "Introduction",
      "font_size": 32.0,
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
</details>

<details>
<summary><b>paragraph with mixed styling (bold, italic, links)</b></summary>

```json
{
  "type": "text",
  "bbox": [72.03, 140.5, 542.7, 200.2],
  "font_size": 12.0,
  "length": 189,
  "lines": 4,
  "spans": [
    {
      "text": "Cloud technology enables ",
      "font_size": 12.0,
      "bold": false,
      "italic": false,
      "monospace": false,
      "strikeout": false,
      "superscript": false,
      "subscript": false,
      "link": false,
      "uri": false
    },
    {
      "text": "multi-tenant",
      "font_size": 12.0,
      "bold": true,
      "italic": true,
      "monospace": false,
      "strikeout": false,
      "superscript": false,
      "subscript": false,
      "link": true,
      "uri": "https://aws.amazon.com/multi-tenant"
    },
    {
      "text": " services that scale efficiently across infrastructure.",
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
</details>

<details>
<summary><b>bulleted and nested list with indentation</b></summary>

```json
{
  "type": "list",
  "bbox": [40.44, 199.44, 240.01, 345.78],
  "font_size": 11.04,
  "length": 89,
  "spans": [],
  "items": [
    {
      "spans": [{"text": "Logical isolation boundaries", "font_size": 11.04, "bold": false, "italic": false, "monospace": false, "strikeout": false, "superscript": false, "subscript": false, "link": false, "uri": false}],
      "list_type": "bulleted",
      "indent": 0,
      "prefix": false
    },
    {
      "spans": [{"text": "Data center architecture", "font_size": 11.04, "bold": false, "italic": false, "monospace": false, "strikeout": false, "superscript": false, "subscript": false, "link": false, "uri": false}],
      "list_type": "bulleted",
      "indent": 1,
      "prefix": false
    },
    {
      "spans": [{"text": "Nested list item", "font_size": 11.04, "bold": false, "italic": false, "monospace": false, "strikeout": false, "superscript": false, "subscript": false, "link": false, "uri": false}],
      "list_type": "bulleted",
      "indent": 2,
      "prefix": false
    }
  ]
}
```
</details>

<details>
<summary><b>table with detected structure</b></summary>

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
          "spans": [{"text": "Feature", "font_size": 12.0, "bold": true, "italic": false, "monospace": false, "strikeout": false, "superscript": false, "subscript": false, "link": false, "uri": false}]
        },
        {
          "bbox": [297.75, 220.0, 523.5, 250.0],
          "spans": [{"text": "Speed (pages/sec)", "font_size": 12.0, "bold": true, "italic": false, "monospace": false, "strikeout": false, "superscript": false, "subscript": false, "link": false, "uri": false}]
        }
      ]
    },
    {
      "bbox": [72.0, 250.0, 523.5, 280.0],
      "cells": [
        {
          "bbox": [72.0, 250.0, 297.75, 280.0],
          "spans": [{"text": "pymupdf4llm-C", "font_size": 12.0, "bold": false, "italic": false, "monospace": false, "strikeout": false, "superscript": false, "subscript": false, "link": false, "uri": false}]
        },
        {
          "bbox": [297.75, 250.0, 523.5, 280.0],
          "spans": [{"text": "~300", "font_size": 12.0, "bold": false, "italic": false, "monospace": false, "strikeout": false, "superscript": false, "subscript": false, "link": false, "uri": false}]
        }
      ]
    }
  ]
}
```
</details>

instead of splitting on word count and getting mid-sentence breaks, you use layout to chunk semantically.

> note that a span represents a logical group of styling. in most blocks, it is likely that there is only one span.
> some information in the top level JSON may be inaccurate and/or redundant. 

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

**i use uv**, you can prefix with `uv` or whatever you want.

wheels for Python 3.9–3.14 (inclusive of minor versions) on macOS (ARM/x64) and all modern Linux distros > 2007. no Windows, its a pain; see [BUILD.md](BUILD.md) to compile.

---

## usage

<details>
<summary><b>Python</b></summary>

---

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

---

## output structure

each page is a JSON array of blocks. every block has:

- `type`: block type (text, heading, paragraph, list, table, code)
- `bbox`: [x0, y0, x1, y1] bounding box coordinates
- `font_size`: font size in points (average for multi-span blocks)
- `length`: character count
- `spans`: array of styled text spans with style flags (bold, italic, monospace, etc.)

### block types

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
      "font_size": 32.0,
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
          "font_size": 11.04,
          "bold": false,
          "italic": false,
          "monospace": false,
          "strikeout": false,
          "superscript": false,
          "subscript": false,
          "link": false,
          "uri": false
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
          "font_size": 11.04,
          "bold": false,
          "italic": false,
          "monospace": false,
          "strikeout": false,
          "superscript": false,
          "subscript": false,
          "link": false,
          "uri": false
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
        },
        {
          "bbox": [297.75, 220.0, 523.5, 250.0],
          "spans": [
            {
              "text": "Header B",
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
      ]
    }
  ]
}
```

### span fields

all text spans contain:
- `text`: span content
- `font_size`: size in points
- `bold`, `italic`, `monospace`, `strikeout`, `superscript`, `subscript`: boolean style flags
- `link`: boolean indicating if span contains a hyperlink
- `uri`: URI string if linked, otherwise false

---

## faq

**why not marker/docling?**  
if you have time and need maximum accuracy, use those. this is for when you're processing millions of pages or iterating on extraction logic quickly.

**how do i use bounding boxes for semantic chunking?**  
large y-gaps indicate topic breaks. font size changes show sections. indentation shows hierarchy. you write the logic using the metadata.

**will this handle my complex PDF?**  
optimized for well-formed digital PDFs. scanned documents, complex table structures, and image-heavy layouts won't extract as well as ML tools.

**commercial use?**  
only under AGPL v3 or with a license from Artifex (MuPDF's creators). see [LICENSE](LICENSE)

---

## building from source

see [BUILD.md](BUILD.md).

---

## license

- derived work of `mupdf`.
- inspired by `pymupdf4llm`; i have used it as a reference

AGPL v3. commercial use requires license from Artifex.

modifications and enhancements specific to this library are 2026 Adit Bajaj.

see [LICENSE](LICENSE) for the legal stuff.

---

## links

- repo: [github.com/intercepted16/pymupdf4llm-C](https://github.com/intercepted16/pymupdf4llm-C)
- pypi: [pymupdf4llm-C](https://pypi.org/project/pymupdf4llm-C)

feedback welcome.
