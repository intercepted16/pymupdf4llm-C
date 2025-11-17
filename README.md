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

Pass `collect=True` to `to_json` if you want the parsed JSON structures
returned instead of file paths. The optional `ConversionConfig` lets you
override the shared library location:

```python
config = ConversionConfig(lib_path=Path("/opt/lib/libtomd.so"))
results = to_json("report.pdf", config=config, collect=True)
```

## JSON output structure

Each PDF page is extracted to a separate JSON file (e.g., `page_001.json`,
`page_002.json`, etc.) containing an array of block objects. Each block
represents a semantic unit of content identified by the extractor with its
position, type, and metadata.

### Block schema

Every block object in the JSON output contains the following fields:

```json
{
  "type": "paragraph",
  "text": "This is the extracted text content",
  "bbox": [72.0, 100.5, 523.5, 130.2],
  "font_size": 11.0,
  "font_weight": "normal",
  "page_number": 0,
  "length": 35
}
```

**Common fields (all block types):**

- **`type`** – Block classification: `"paragraph"`, `"heading"`, `"table"`,
  `"list"`, `"figure"`, or `"other"`
- **`text`** – UTF-8 normalized text content (empty string for figures)
- **`bbox`** – Bounding box as `[x0, y0, x1, y1]` in PDF coordinate space
  (bottom-left origin). Units are in points (1/72 inch)
- **`font_size`** – Average font size in points
- **`font_weight`** – Either `"normal"` or `"bold"` based on bold character ratio
- **`page_number`** – Zero-based page index
- **`length`** – Number of Unicode characters in the text field

**Type-specific fields:**

- **Paragraphs and lists** include `"lines"` – Number of text lines in the block
- **Tables** include:
  - `"row_count"` – Estimated number of rows
  - `"col_count"` – Estimated number of columns
  - `"cell_count"` – Total cells (row_count × col_count)
  - `"confidence"` – Table detection confidence score (0.0 to 1.0)

### Example JSON outputs

**Simple paragraph block:**

```json
[
  {
    "type": "paragraph",
    "text": "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Sed do eiusmod tempor incididunt ut labore et dolore magna aliqua.",
    "bbox": [72.0, 720.5, 540.0, 755.3],
    "font_size": 12.0,
    "font_weight": "normal",
    "page_number": 0,
    "length": 123,
    "lines": 3
  }
]
```

**Heading block:**

```json
[
  {
    "type": "heading",
    "text": "Introduction",
    "bbox": [72.0, 680.0, 200.5, 698.0],
    "font_size": 18.0,
    "font_weight": "bold",
    "page_number": 0,
    "length": 12
  }
]
```

**Table block:**

```json
[
  {
    "type": "table",
    "text": "",
    "bbox": [72.0, 400.0, 540.0, 650.0],
    "font_size": 10.0,
    "font_weight": "normal",
    "page_number": 1,
    "length": 0,
    "row_count": 8,
    "col_count": 4,
    "cell_count": 32,
    "confidence": 0.85
  }
]
```

**List block:**

```json
[
  {
    "type": "list",
    "text": "• First item in the list\n• Second item with more detail\n• Third and final item",
    "bbox": [72.0, 500.0, 400.0, 560.0],
    "font_size": 11.0,
    "font_weight": "normal",
    "page_number": 2,
    "length": 78,
    "lines": 3
  }
]
```

**Figure/image block:**

```json
[
  {
    "type": "figure",
    "text": "",
    "bbox": [150.0, 300.0, 450.0, 500.0],
    "font_size": 0.0,
    "font_weight": "normal",
    "page_number": 3,
    "length": 0
  }
]
```

**Multi-block page example:**

```json
[
  {
    "type": "heading",
    "text": "Chapter 1: Getting Started",
    "bbox": [72.0, 750.0, 300.0, 770.0],
    "font_size": 16.0,
    "font_weight": "bold",
    "page_number": 0,
    "length": 26
  },
  {
    "type": "paragraph",
    "text": "This chapter introduces the fundamental concepts you need to understand before proceeding.",
    "bbox": [72.0, 700.0, 540.0, 730.0],
    "font_size": 11.0,
    "font_weight": "normal",
    "page_number": 0,
    "length": 92,
    "lines": 2
  },
  {
    "type": "list",
    "text": "1. First prerequisite\n2. Second prerequisite\n3. Third prerequisite",
    "bbox": [90.0, 640.0, 520.0, 685.0],
    "font_size": 11.0,
    "font_weight": "normal",
    "page_number": 0,
    "length": 67,
    "lines": 3
  }
]
```

## Usage examples

### Processing with collect=True

When you need to analyze block data in memory rather than reading from files:

```python
from pymupdf4llm_c import to_json

# Get parsed JSON structures directly
pages = to_json("document.pdf", collect=True)

# Iterate through pages and blocks
for page_idx, blocks in enumerate(pages):
    print(f"Page {page_idx + 1} has {len(blocks)} blocks")
    
    for block in blocks:
        print(f"  - {block['type']}: {block['length']} chars")
        if block['type'] == 'heading':
            print(f"    Heading: {block['text'][:50]}")
```

### Using bounding boxes for semantic boundaries

Bounding boxes (`bbox`) provide precise spatial information enabling
layout-aware document processing. Each bbox is `[x0, y0, x1, y1]` where (x0, y0)
is the bottom-left corner and (x1, y1) is the top-right corner in PDF
coordinates.

**Example: Extracting content from specific page regions**

```python
from pymupdf4llm_c import to_json

pages = to_json("report.pdf", collect=True)

def is_in_left_column(bbox, page_width=612):
    """Check if a block is in the left column of a two-column layout."""
    x0, y0, x1, y1 = bbox
    midpoint = page_width / 2
    return x1 < midpoint + 20  # Small tolerance for column boundaries

# Extract only left column content
for page_blocks in pages:
    left_column_blocks = [
        block for block in page_blocks 
        if is_in_left_column(block['bbox'])
    ]
    
    for block in left_column_blocks:
        print(f"{block['type']}: {block['text'][:60]}...")
```

**Example: Identifying content proximity for context**

```python
def blocks_are_adjacent(bbox1, bbox2, vertical_threshold=20):
    """Check if two blocks are vertically adjacent."""
    x0_1, y0_1, x1_1, y1_1 = bbox1
    x0_2, y0_2, x1_2, y1_2 = bbox2
    
    # Check vertical distance
    vertical_gap = abs(y0_2 - y1_1)
    
    # Check horizontal overlap
    horizontal_overlap = not (x1_1 < x0_2 or x1_2 < x0_1)
    
    return vertical_gap < vertical_threshold and horizontal_overlap

# Find paragraphs that follow headings
for page_blocks in pages:
    for i, block in enumerate(page_blocks):
        if block['type'] == 'heading' and i + 1 < len(page_blocks):
            next_block = page_blocks[i + 1]
            if blocks_are_adjacent(block['bbox'], next_block['bbox']):
                print(f"Section: {block['text']}")
                print(f"Content: {next_block['text'][:100]}...")
```

**Example: Spatial document sectioning**

```python
def split_by_vertical_position(blocks, split_y=400):
    """Split blocks into header and body based on vertical position."""
    header_blocks = []
    body_blocks = []
    
    for block in blocks:
        x0, y0, x1, y1 = block['bbox']
        # Higher y values are at the top of the page
        if y0 > split_y:
            header_blocks.append(block)
        else:
            body_blocks.append(block)
    
    return header_blocks, body_blocks

pages = to_json("letterhead.pdf", collect=True)
for page_blocks in pages:
    headers, body = split_by_vertical_position(page_blocks)
    print(f"Header elements: {len(headers)}")
    print(f"Body elements: {len(body)}")
```

### RAG (Retrieval-Augmented Generation) integration

PyMuPDF4LLM-C is specifically designed for RAG pipelines, providing structured,
semantically-aware document chunks with rich metadata for improved retrieval and
context quality.

**Why this matters for RAG:**

1. **Semantic chunking** – Blocks represent natural semantic boundaries
   (paragraphs, sections, tables) rather than arbitrary character splits
2. **Type-aware processing** – Different content types can be handled
   appropriately (e.g., tables for structured data, headings for hierarchy)
3. **Spatial context** – Bounding boxes enable layout-aware chunking and can
   preserve reading order in complex layouts
4. **Metadata for filtering** – Page numbers, font sizes, and block types enable
   smart chunk selection and ranking

**Example: Building semantic chunks for a vector database**

```python
from pymupdf4llm_c import to_json

def create_rag_chunks(pdf_path):
    """Extract semantically meaningful chunks with metadata for RAG."""
    pages = to_json(pdf_path, collect=True)
    chunks = []
    
    for page_idx, blocks in enumerate(pages):
        current_section = None
        
        for block in blocks:
            # Track section headings for context
            if block['type'] == 'heading':
                current_section = block['text']
                # Optionally index headings separately
                chunks.append({
                    'text': block['text'],
                    'type': 'heading',
                    'section': current_section,
                    'page': page_idx + 1,
                    'metadata': {
                        'font_size': block['font_size'],
                        'bbox': block['bbox']
                    }
                })
            
            # Create chunks for content blocks
            elif block['type'] in ['paragraph', 'list']:
                chunks.append({
                    'text': block['text'],
                    'type': block['type'],
                    'section': current_section or 'Unknown',
                    'page': page_idx + 1,
                    'metadata': {
                        'font_size': block['font_size'],
                        'lines': block.get('lines', 0),
                        'bbox': block['bbox']
                    }
                })
            
            # Handle tables specially
            elif block['type'] == 'table':
                chunks.append({
                    'text': f"[Table: {block['row_count']}x{block['col_count']}]",
                    'type': 'table',
                    'section': current_section or 'Unknown',
                    'page': page_idx + 1,
                    'metadata': {
                        'rows': block['row_count'],
                        'cols': block['col_count'],
                        'confidence': block['confidence'],
                        'bbox': block['bbox']
                    }
                })
    
    return chunks

# Create embeddings for each chunk
chunks = create_rag_chunks("technical_manual.pdf")

# Example: Store in a vector database (pseudo-code)
for chunk in chunks:
    # embedding = embed_model.encode(chunk['text'])
    # vector_db.store(
    #     embedding=embedding,
    #     text=chunk['text'],
    #     metadata=chunk['metadata'],
    #     page=chunk['page'],
    #     section=chunk['section']
    # )
    pass
```

**Example: Smart chunking with context windows**

```python
def create_contextual_chunks(pdf_path, context_window=1):
    """Create chunks with surrounding context for better RAG retrieval."""
    pages = to_json(pdf_path, collect=True)
    chunks = []
    
    for page_idx, blocks in enumerate(pages):
        for i, block in enumerate(blocks):
            if block['type'] not in ['paragraph', 'list']:
                continue
            
            # Get context from surrounding blocks
            context_before = []
            context_after = []
            
            # Previous blocks for context
            for j in range(max(0, i - context_window), i):
                prev = blocks[j]
                if prev['type'] == 'heading':
                    context_before.append(f"Section: {prev['text']}")
                elif prev['type'] in ['paragraph', 'list']:
                    context_before.append(prev['text'][:100])
            
            # Next blocks for context
            for j in range(i + 1, min(len(blocks), i + context_window + 1)):
                next_block = blocks[j]
                if next_block['type'] in ['paragraph', 'list']:
                    context_after.append(next_block['text'][:100])
            
            # Create enriched chunk
            chunk = {
                'primary_text': block['text'],
                'context_before': ' | '.join(context_before),
                'context_after': ' | '.join(context_after),
                'full_text': ' '.join(
                    context_before + [block['text']] + context_after
                ),
                'page': page_idx + 1,
                'bbox': block['bbox'],
                'type': block['type']
            }
            chunks.append(chunk)
    
    return chunks

# Use for retrieval with richer context
chunks = create_contextual_chunks("research_paper.pdf")
```

**Example: Type-specific RAG processing**

```python
def process_for_rag(pdf_path):
    """Separate processing strategies for different content types."""
    pages = to_json(pdf_path, collect=True)
    
    text_chunks = []
    tables = []
    key_points = []
    
    for page_idx, blocks in enumerate(pages):
        for block in blocks:
            page_num = page_idx + 1
            
            # High-value chunks: headings and important text
            if block['type'] == 'heading':
                key_points.append({
                    'text': block['text'],
                    'page': page_num,
                    'importance': 'high',
                    'bbox': block['bbox']
                })
            
            # Standard text for general retrieval
            elif block['type'] in ['paragraph', 'list']:
                text_chunks.append({
                    'text': block['text'],
                    'page': page_num,
                    'weight': 'normal',
                    'bbox': block['bbox']
                })
            
            # Tables for structured data queries
            elif block['type'] == 'table':
                tables.append({
                    'location': f"Page {page_num}",
                    'dimensions': f"{block['row_count']}×{block['col_count']}",
                    'confidence': block['confidence'],
                    'bbox': block['bbox'],
                    # Could extract actual table content here
                })
    
    return {
        'text_chunks': text_chunks,
        'tables': tables,
        'key_points': key_points
    }

# Process document with type awareness
result = process_for_rag("financial_report.pdf")
print(f"Extracted {len(result['text_chunks'])} text chunks")
print(f"Found {len(result['tables'])} tables")
print(f"Identified {len(result['key_points'])} key points")
```

**Example: Metadata-enhanced retrieval**

```python
def enhanced_retrieval_chunks(pdf_path):
    """Create chunks with rich metadata for advanced filtering."""
    pages = to_json(pdf_path, collect=True)
    chunks = []
    
    for page_idx, blocks in enumerate(pages):
        for block in blocks:
            if not block['text'].strip():
                continue
            
            # Calculate additional metadata
            x0, y0, x1, y1 = block['bbox']
            width = x1 - x0
            height = y1 - y0
            
            chunk = {
                'text': block['text'],
                'page': page_idx + 1,
                'type': block['type'],
                'char_count': block['length'],
                'font_size': block['font_size'],
                'is_bold': block['font_weight'] == 'bold',
                'position': {
                    'x': x0,
                    'y': y0,
                    'width': width,
                    'height': height,
                    'area': width * height
                },
                'lines': block.get('lines', 1)
            }
            
            # Additional computed features
            chunk['avg_chars_per_line'] = (
                block['length'] / block.get('lines', 1)
            )
            
            # Table-specific metadata
            if block['type'] == 'table':
                chunk['table_info'] = {
                    'rows': block['row_count'],
                    'cols': block['col_count'],
                    'confidence': block['confidence']
                }
            
            chunks.append(chunk)
    
    return chunks

# Enable sophisticated retrieval filtering
chunks = enhanced_retrieval_chunks("dataset.pdf")

# Example queries:
# - Find chunks with large font (headings/titles)
headings = [c for c in chunks if c['font_size'] > 14]

# - Find dense text blocks (likely important content)
dense_blocks = [c for c in chunks if c['avg_chars_per_line'] > 60]

# - Find content from specific page regions
top_of_page = [c for c in chunks if c['position']['y'] > 700]
```

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
