from typing import TYPE_CHECKING, Any, List, Optional, Union

if TYPE_CHECKING:
    import pymupdf

import ctypes

import pymupdf  # type: ignore
from globals import LIB_PATH

pymupdf.TOOLS.unset_quad_corrections(True)  # type: ignore

lib = ctypes.CDLL(LIB_PATH)

RectLike = Union["pymupdf.Rect", dict[str, Any], tuple[float, ...], list[float]]


def _ensure_rects(rects: Optional[List[RectLike]]) -> List[Any]:
    """Convert a list of rectangle-like objects (dicts, tuples, lists) to pymupdf.Rect.
    Accepts:
        - pymupdf.Rect
        - dict with 'bbox' key or x0/y0/x1/y1 keys
        - tuple/list of 4 floats
    Returns:
        List of pymupdf.Rect
    """
    result: List[Any] = []
    if not rects:
        return result
    for r in rects:
        if isinstance(r, pymupdf.Rect):
            result.append(r)
        elif isinstance(r, dict):
            if "bbox" in r:
                result.append(pymupdf.Rect(*r["bbox"]))
            elif all(k in r for k in ("x0", "y0", "x1", "y1")):
                result.append(pymupdf.Rect(r["x0"], r["y0"], r["x1"], r["y1"]))
        elif isinstance(r, (tuple, list)) and len(r) == 4:  # type: ignore
            result.append(pymupdf.Rect(*r))
        else:
            raise TypeError(f"Cannot convert {r} to pymupdf.Rect")
    return result


def column_boxes(
    file_path: Union[str, bytes],
    page: "pymupdf.Page",
    *,
    footer_margin: float = 50,
    header_margin: float = 50,
    no_image_text: bool = True,
    paths: Optional[List[RectLike]] = None,
    avoid: Optional[List[RectLike]] = None,
    ignore_images: bool = False,
) -> List[Any]:
    """Determine bounding boxes which wrap a column on the page.

    Args:
        file_path: Path to PDF file (str or bytes).
        page: pymupdf.Page object.
        footer_margin: Margin to ignore at bottom of page.
        header_margin: Margin to ignore at top of page.
        no_image_text: If True, ignore text over images.
        paths: List of rectangles (Rect/dict/tuple/list) for background regions.
        avoid: List of rectangles to avoid (e.g., images/tables).
        ignore_images: If True, ignore image regions.

    Returns:
        List of pymupdf.Rect bounding boxes for columns.
    """
    # Ensure file_path is bytes
    file_path_bytes: bytes = (
        file_path.encode("utf-8") if isinstance(file_path, str) else file_path
    )

    result_count = ctypes.c_int()

    # Normalize rectangle lists
    norm_paths: List[pymupdf.Rect] = _ensure_rects(paths)
    norm_avoid: List[pymupdf.Rect] = _ensure_rects(avoid)

    def to_ctypes_flat(array: List[Any]) -> tuple[Any, int]:
        if not array:
            return ctypes.POINTER(ctypes.c_float)(), 0
        flat_array = (ctypes.c_float * (len(array) * 4))()
        for i, r in enumerate(array):
            flat_array[i * 4 + 0] = r.x0
            flat_array[i * 4 + 1] = r.y0
            flat_array[i * 4 + 2] = r.x1
            flat_array[i * 4 + 3] = r.y1
        return ctypes.cast(flat_array, ctypes.POINTER(ctypes.c_float)), len(array)

    paths_ptr, path_count = to_ctypes_flat(norm_paths)
    avoid_ptr, avoid_count = to_ctypes_flat(norm_avoid)

    # Ensure function prototype is correct
    lib.column_boxes.argtypes = [
        ctypes.c_char_p,
        ctypes.c_int,
        ctypes.c_float,
        ctypes.c_float,
        ctypes.c_int,
        ctypes.POINTER(ctypes.c_float),
        ctypes.c_int,
        ctypes.POINTER(ctypes.c_float),
        ctypes.c_int,
        ctypes.c_int,
        ctypes.POINTER(ctypes.c_int),
    ]
    lib.column_boxes.restype = ctypes.POINTER(ctypes.c_float)

    result = lib.column_boxes(
        file_path_bytes,
        page.number,
        float(footer_margin),
        float(header_margin),
        int(no_image_text),
        paths_ptr,
        path_count,
        avoid_ptr,
        avoid_count,
        int(ignore_images),
        ctypes.byref(result_count),
    )

    bboxes: List[pymupdf.Rect] = []
    for i in range(result_count.value):
        x0 = result[i * 4 + 0]
        y0 = result[i * 4 + 1]
        x1 = result[i * 4 + 2]
        y1 = result[i * 4 + 3]
        bboxes.append(pymupdf.Rect(x0, y0, x1, y1))
        print(f"Detected bbox: {bboxes[-1]}")

    # Free memory allocated in C
    if result:
        lib.free(result)

    return bboxes
