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
    """C-backed get_raw_lines keeping the same Python signature.

    Returns: list of [Rect, [spans]] like the original Python version.
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
    """C-backed to_markdown that writes to a file and returns a status."""
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
    """Check if a page likely contains a table based on column detection."""
    pdf_path_bytes = pdf_path.encode("utf-8")
    if not pdf_path_bytes:
        raise ValueError("PDF path must be provided.")

    with suppress_output():
        result = lib.page_has_table(pdf_path_bytes, page_number)

    return result
