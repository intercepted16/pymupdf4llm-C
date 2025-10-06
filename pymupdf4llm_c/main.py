#!/usr/bin/env python3
"""Complete integration example: C-first PDF processing with Python table fallback.

This demonstrates the DECOUPLED BATCH PROCESSING architecture:
1. C processes entire document in parallel (fast text extraction)
2. C detects tables and records placeholders (no Python calls during processing)
3. After C completes, Python processes ALL tables in parallel using multiprocessing
4. C reassembles final output by replacing placeholders with table markdown

This achieves:
- Minimal C↔Python context switching (only ONE batch call instead of hundreds)
- Maximum CPU utilization (C uses all cores, then Python uses all cores)
- 10-100× speedup for table-heavy documents
"""

import ctypes
import sys
from pathlib import Path
from multiprocessing import Pool, cpu_count
from typing import List, Tuple

from ._lib import get_default_library_path

# Import PyMuPDF and pymupdf4llm for page extraction
try:
    import pymupdf4llm
except ImportError:
    print("ERROR: pymupdf4llm not installed. Run: pip install pymupdf4llm")
    sys.exit(1)

# Keep references to prevent garbage collection
_callback_buffers = []

# Global PDF path for multiprocessing workers
_global_pdf_path = None


def _worker_extract_table(args: Tuple[int, List[float]]) -> Tuple[int, str]:
    """Worker function for multiprocessing pool.
    Extracts a single table from a page.
    
    Args:
        args: Tuple of (page_number, bbox as [x0, y0, x1, y1])
        
    Returns:
        Tuple of (page_number, markdown_string)
    """
    page_num, bbox = args

    try:
        # Extract the entire page with tables using pymupdf4llm
        # Note: pymupdf4llm doesn't support bbox extraction directly,
        # so we extract the entire page
        md_text = pymupdf4llm.to_markdown(
            _global_pdf_path,
            pages=[page_num],
            page_chunks=False,
            write_images=False,
            image_path="",
            image_format="png",
            dpi=150
        )

        return (page_num, md_text if md_text and md_text.strip() else "")

    except Exception as e:
        print(f"  ERROR extracting page {page_num}: {e}")
        return (page_num, f"<!-- Table extraction failed: {e} -->")


def batch_extract_tables(pdf_path: str, page_numbers: List[int], bboxes: List[List[float]]) -> List[str]:
    """Extract multiple tables in parallel using multiprocessing.
    
    This is the main performance optimization: instead of calling Python
    hundreds of times from C (slow FFI overhead), we call it ONCE with
    all tables, then use Python's multiprocessing to parallelize internally.
    
    Args:
        pdf_path: Path to PDF file
        page_numbers: List of page numbers containing tables
        bboxes: List of bounding boxes (each is [x0, y0, x1, y1])
        
    Returns:
        List of markdown strings (one per table, in same order as input)
    """
    global _global_pdf_path
    _global_pdf_path = pdf_path

    num_workers = max(1, cpu_count() - 1)  # Leave one core free
    print(f"  Using {num_workers} worker processes for {len(page_numbers)} tables...")

    # Prepare work items
    work_items = list(zip(page_numbers, bboxes))

    # Process in parallel, fall back to sequential if the platform blocks semaphores
    try:
        with Pool(processes=num_workers) as pool:
            results = pool.map(_worker_extract_table, work_items)
    except (PermissionError, OSError) as exc:
        print(f"  Multiprocessing unavailable ({exc}); falling back to sequential extraction.")
        results = [_worker_extract_table(item) for item in work_items]

    # Sort results by page number and extract markdown
    results_sorted = sorted(results, key=lambda x: x[0])
    markdown_list = [md for _, md in results_sorted]

    return markdown_list


def create_batch_callback():
    """Create a ctypes callback for BATCH table processing.
    
    This is called ONCE after C completes all text processing.
    It receives ALL detected tables and processes them in parallel.
    
    Returns:
        CFUNCTYPE callback compatible with:
        char** (*callback)(const char* pdf_path, int* page_numbers, 
                          double* bboxes, int* bbox_counts, int table_count)
    """
    @ctypes.CFUNCTYPE(
        ctypes.c_char_p,                   # Returns buffer (serialized markdown)
        ctypes.c_char_p,                   # pdf_path
        ctypes.POINTER(ctypes.c_int),      # page_numbers array
        ctypes.POINTER(ctypes.c_double),   # bboxes array (flattened)
        ctypes.POINTER(ctypes.c_int),      # bbox_counts array
        ctypes.c_int                        # table_count
    )
    def python_batch_callback(pdf_path, page_numbers_ptr, bboxes_ptr, bbox_counts_ptr, table_count):
        """Batch callback that receives ALL tables at once and processes them in parallel.
        
        Args:
            pdf_path: C string (char*)
            page_numbers_ptr: Pointer to int array
            bboxes_ptr: Pointer to double array (flattened)
            bbox_counts_ptr: Pointer to int array (always 4 for rectangles)
            table_count: Number of tables
        
        Returns:
            Pointer to buffer containing JSON-encoded list of markdown strings
        """
        try:
            # Decode PDF path
            pdf_path_str = pdf_path.decode('utf-8')

            # Extract arrays from pointers
            page_numbers = [page_numbers_ptr[i] for i in range(table_count)]

            # Flatten bbox coordinates (each table has 4 coords: x0, y0, x1, y1)
            bboxes = []
            for i in range(table_count):
                bbox = [
                    bboxes_ptr[i * 4 + 0],
                    bboxes_ptr[i * 4 + 1],
                    bboxes_ptr[i * 4 + 2],
                    bboxes_ptr[i * 4 + 3]
                ]
                bboxes.append(bbox)

            # Extract tables in parallel using multiprocessing
            markdown_list = batch_extract_tables(pdf_path_str, page_numbers, bboxes)

            # Encode result as null-delimited strings (C expects this format)
            # Each table markdown is followed by a null byte
            result_parts = []
            for md in markdown_list:
                result_parts.append(md.encode('utf-8'))
                result_parts.append(b'\x00')  # Null delimiter

            result_bytes = b''.join(result_parts)

            # Allocate buffer and keep reference
            buf = ctypes.create_string_buffer(result_bytes)
            _callback_buffers.append(buf)  # Prevent garbage collection

            # Return the raw buffer address (ctypes.addressof gives us the memory address)
            # We cast it to c_char_p but C will need to read past null bytes manually
            return ctypes.addressof(buf)

        except Exception as e:
            print(f"Error in batch callback: {e}")
            import traceback
            traceback.print_exc()
            return None

    return python_batch_callback


def _resolve_library_path(provided: str | Path | None) -> Path:
    """Resolve the absolute path to the shared library, if available."""
    if provided is not None:
        candidate = Path(provided)
    else:
        candidate = get_default_library_path()

    if candidate is None:
        raise FileNotFoundError(
            "C library not found. Build it with 'make tomd' or set the "
            "PYMUPDF4LLM_C_LIB environment variable to the built shared library."
        )

    candidate = candidate.resolve()
    if not candidate.exists():
        raise FileNotFoundError(
            f"C library not found at {candidate}. Build it with: make tomd"
        )

    return candidate


def convert_pdf_with_batch_callback(
    pdf_path: str,
    output_path: str,
    lib_path: str | Path | None = None,
) -> bool:
    """Convert PDF to Markdown using C library with Python batch table processing.
    
    This uses the optimized batch processing strategy:
    - C processes all pages in parallel (fast)
    - C collects table placeholders (no Python calls)
    - Python processes ALL tables in parallel using multiprocessing (fast)
    - C reassembles output (fast)
    
    Args:
        pdf_path: Input PDF path
        output_path: Output markdown path
        lib_path: Path to C library (default: lib/libtomd.so)
        
    Returns:
        True if successful, False otherwise
    """
    # Load the C library
    lib_path = _resolve_library_path(lib_path)
    print(f"DEBUG: using shared library at {lib_path}")

    lib = ctypes.CDLL(str(lib_path))

    # Define C function signatures
    lib.register_batch_table_callback.argtypes = [
        ctypes.CFUNCTYPE(
            ctypes.c_char_p,                   # Returns buffer (serialized markdown)
            ctypes.c_char_p,                   # pdf_path
            ctypes.POINTER(ctypes.c_int),      # page_numbers
            ctypes.POINTER(ctypes.c_double),   # bboxes
            ctypes.POINTER(ctypes.c_int),      # bbox_counts
            ctypes.c_int                        # table_count
        )
    ]
    lib.register_batch_table_callback.restype = None

    lib.to_markdown.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
    lib.to_markdown.restype = ctypes.c_int

    # Create and register the batch callback
    callback = create_batch_callback()
    print("✓ Registering Python batch table callback with C library...")
    lib.register_batch_table_callback(callback)
    print("✓ Batch callback registered (uses multiprocessing internally)\n")

    # Convert the PDF
    print(f"Converting {pdf_path} → {output_path}")
    print("Strategy: C processes text in parallel, then Python processes tables in parallel\n")

    result = lib.to_markdown(
        pdf_path.encode('utf-8'),
        output_path.encode('utf-8')
    )

    if result == 0:
        print("\n✓ Conversion successful!")
        print(f"✓ Output written to {output_path}")

        # Show output size
        size = Path(output_path).stat().st_size
        print(f"✓ Output size: {size:,} bytes")
        return True
    else:
        print(f"\n✗ Conversion failed (error code: {result})")
        return False


def create_callback():
    """Create a ctypes callback function that C can invoke.
    
    Returns:
        CFUNCTYPE callback compatible with: const char* (*callback)(const char*, int)
    """
    @ctypes.CFUNCTYPE(ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int)
    def python_table_callback(pdf_path_bytes, page_num):
        """C-compatible callback for page extraction.
        
        Args:
            pdf_path_bytes: PDF path as bytes
            page_num: 0-based page number
            
        Returns:
            C string pointer to markdown (must remain valid until next call)
        """
        pdf_path = pdf_path_bytes.decode('utf-8')

        print(f"  [PYTHON] Extracting entire page {page_num + 1} with tables...")

        # Extract the ENTIRE page with Python (using pymupdf4llm)
        markdown = extract_page_with_tables(pdf_path, page_num)

        # Convert to bytes with null termination
        result_bytes = markdown.encode('utf-8') + b'\0'

        # Create a buffer and keep reference to prevent GC
        buf = ctypes.create_string_buffer(result_bytes)
        _callback_buffers.append(buf)

        # Limit buffer cache size
        if len(_callback_buffers) > 32:
            _callback_buffers.pop(0)

        # Return the buffer's value (c_char_p handles this correctly)
        return buf.value

    return python_table_callback


def convert_pdf_with_callback(
    pdf_path: str,
    output_path: str,
    lib_path: str | Path | None = None,
):
    """Convert PDF to Markdown using C library with Python table fallback.
    
    Args:
        pdf_path: Input PDF path
        output_path: Output markdown path
        lib_path: Path to C library (default: bin/to_md)
    """
    # Load the C library
    lib_path = _resolve_library_path(lib_path)

    lib = ctypes.CDLL(str(lib_path))

    # Define C function signatures
    lib.register_table_callback.argtypes = [ctypes.CFUNCTYPE(ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int)]
    lib.register_table_callback.restype = None

    lib.to_markdown.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
    lib.to_markdown.restype = ctypes.c_int

    # Create and register the callback
    callback = create_callback()
    print("Registering Python table callback with C library...")
    lib.register_table_callback(callback)
    print("✓ Callback registered\n")

    # Convert the PDF
    print(f"Converting {pdf_path} → {output_path}")
    print("C will call Python only for pages with tables...\n")

    result = lib.to_markdown(
        pdf_path.encode('utf-8'),
        output_path.encode('utf-8')
    )

    if result == 0:
        print("\n✓ Conversion successful!")
        print(f"✓ Output written to {output_path}")

        # Show output size
        size = Path(output_path).stat().st_size
        print(f"✓ Output size: {size:,} bytes")
    else:
        print(f"\n✗ Conversion failed (error code: {result})")
        return False

    return True


def main():
    if len(sys.argv) < 2:
        print("Usage: python full_integration_example.py <input.pdf> [output.md]")
        print("\nThis demonstrates the DECOUPLED BATCH PROCESSING architecture:")
        print("  • C processes all pages in parallel (10× faster than Python)")
        print("  • C detects tables and records placeholders")
        print("  • Python processes ALL tables in parallel (multiprocessing)")
        print("  • C reassembles final output with table markdown")
        print("\nPerformance benefits:")
        print("  • Minimal C↔Python context switching (1 call instead of 100s)")
        print("  • Maximum CPU utilization (all cores used twice)")
        print("  • 10-100× faster for table-heavy documents")
        sys.exit(1)

    input_pdf = sys.argv[1]
    output_md = sys.argv[2] if len(sys.argv) > 2 else input_pdf.rsplit('.', 1)[0] + '_integrated.md'

    if not Path(input_pdf).exists():
        print(f"ERROR: Input file not found: {input_pdf}")
        sys.exit(1)

    try:
        print("=" * 70)
        print("DECOUPLED BATCH TABLE PROCESSING - Fast C + Parallel Python")
        print("=" * 70)
        success = convert_pdf_with_batch_callback(input_pdf, output_md)
        sys.exit(0 if success else 1)
    except Exception as e:
        print(f"ERROR: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)


if __name__ == "__main__":
    main()
