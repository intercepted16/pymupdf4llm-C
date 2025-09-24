"""Provides high-level wrappers around C functions for PDF processing."""

import ctypes
from typing import Any, Dict, List, Optional, Tuple

import pymupdf
from globals import LIB_PATH
from utils import suppress_output

# Load the C library
lib = ctypes.CDLL(LIB_PATH)


def get_raw_lines(
    pdf_path: str,
    clip: Optional["pymupdf.Rect"] = None,
    tolerance: int = 3,
    ignore_invisible: bool = True,
) -> List[Tuple["pymupdf.Rect", List[Dict[str, Any]]]]:
    """Extracts structured text lines from a PDF using the C library.

    This function serves as a Python wrapper for the C function `get_raw_lines`.
    It takes a PDF file path and other options, calls the C library to perform
    the extraction, and then converts the C-level data structures back into
    Python objects.

    Args:
        pdf_path: The file path to the PDF document.
        clip: An optional `pymupdf.Rect` to specify a clipping area.
        tolerance: The vertical tolerance for grouping spans into lines.
        ignore_invisible: If True, ignore invisible text.

    Returns:
        A list of tuples, where each tuple represents a line and contains a
        `pymupdf.Rect` for the line's bounding box and a list of span
        dictionaries.
    """
    pdf_path_bytes = pdf_path.encode("utf-8")
    if not pdf_path_bytes:
        raise ValueError("PDF path must be provided.")

    with suppress_output():
        arr_ptr = lib.get_raw_lines(pdf_path_bytes)
    if not arr_ptr:
        return []
    arr = arr_ptr.contents
    result: List[Tuple["pymupdf.Rect", List[Dict[str, Any]]]] = []
    for i in range(arr.line_count):
        line = arr.lines[i]
        rect = pymupdf.Rect(*line.rect)
        spans: List[Dict[str, Any]] = []
        for j in range(line.span_count):
            s = line.spans[j]
            sbbox = pymupdf.Rect(*s.bbox)
            span: Dict[str, Any] = {
                "text": s.text.decode("utf-8") if s.text else "",
                "bbox": sbbox,
                "size": s.size,
                "flags": s.flags,
                "char_flags": s.char_flags,
                "alpha": s.alpha,
                "font": s.font.decode("utf-8") if s.font else "",
                "line": s.line,
                "block": s.block,
            }
            spans.append(span)
        if clip is not None:
            spans = [s for s in spans if rect & clip]
        result.append((rect, spans))
    with suppress_output():
        lib.free_line_array(arr_ptr)

    return result


def to_markdown(pdf_path: str, output_path: str) -> int:
    """Converts a PDF to Markdown using the C library and writes it to a file.

    Args:
        pdf_path: The file path to the input PDF document.
        output_path: The file path for the output Markdown file.

    Returns:
        The status code returned by the C function (0 for success).
    """
    pdf_path_bytes = pdf_path.encode("utf-8")
    output_path_bytes = output_path.encode("utf-8")
    if not pdf_path_bytes:
        raise ValueError("PDF path must be provided.")
    if not output_path_bytes:
        raise ValueError("Output path must be provided.")

    with suppress_output():
        status = lib.to_markdown(pdf_path_bytes, output_path_bytes)

    return status


def is_likely_table(pdf_path: str, page_number: int) -> int:
    """Checks if a page is likely to contain a table using the C library.

    Args:
        pdf_path: The file path to the PDF document.
        page_number: The 0-based page number to check.

    Returns:
        1 if a table is likely present, 0 if not, and -1 on error.
    """
    pdf_path_bytes = pdf_path.encode("utf-8")
    if not pdf_path_bytes:
        raise ValueError("PDF path must be provided.")

    with suppress_output():
        result = lib.page_has_table(pdf_path_bytes, page_number)

    return result
