"""Geometry utilities for working with PDF page layouts."""

from typing import Any, List, Tuple

# the below ignores are due to `numba` constraints
# pyright: reportUnknownMemberType=false
# pyright: reportUntypedFunctionDecorator=false
import numba  # type: ignore
import numpy as np
import pymupdf  # type: ignore


# Numba-optimized geometric operations
@numba.jit(nopython=True, cache=True)
def rect_intersects_numba(
    x0_1: float,
    y0_1: float,
    x1_1: float,
    y1_1: float,
    x0_2: float,
    y0_2: float,
    x1_2: float,
    y1_2: float,
) -> bool:  # type: ignore
    """Check if two rectangles intersect."""
    return not (x1_1 <= x0_2 or x1_2 <= x0_1 or y1_1 <= y0_2 or y1_2 <= y0_1)


@numba.jit(nopython=True, cache=True)
def rect_contains_numba(
    x0_o: float,
    y0_o: float,
    x1_o: float,
    y1_o: float,
    x0_i: float,
    y0_i: float,
    x1_i: float,
    y1_i: float,
) -> bool:  # type: ignore
    """Check if outer rectangle contains inner rectangle."""
    return x0_o <= x0_i and y0_o <= y0_i and x1_o >= x1_i and y1_o >= y1_i


@numba.jit(nopython=True, cache=True)
def rect_area_numba(x0: float, y0: float, x1: float, y1: float) -> float:  # type: ignore
    """Calculate area of rectangle."""
    return max(0.0, (x1 - x0) * (y1 - y0))


@numba.jit(nopython=True, cache=True)
def rect_union_numba(
    x0_1: float,
    y0_1: float,
    x1_1: float,
    y1_1: float,
    x0_2: float,
    y0_2: float,
    x1_2: float,
    y1_2: float,
) -> Tuple[float, float, float, float]:  # type: ignore
    """Calculate union of two rectangles and return as separate values."""
    x0 = min(x0_1, x0_2)
    y0 = min(y0_1, y0_2)
    x1 = max(x1_1, x1_2)
    y1 = max(y1_1, y1_2)
    return x0, y0, x1, y1


@numba.jit(nopython=True, cache=True)
def rect_intersection_numba(
    x0_1: float,
    y0_1: float,
    x1_1: float,
    y1_1: float,
    x0_2: float,
    y0_2: float,
    x1_2: float,
    y1_2: float,
) -> Tuple[float, float, float, float]:  # type: ignore
    """Calculate intersection of two rectangles and return as separate values."""
    x0 = max(x0_1, x0_2)
    y0 = max(y0_1, y0_2)
    x1 = min(x1_1, x1_2)
    y1 = min(y1_1, y1_2)
    if x0 <= x1 and y0 <= y1:
        return x0, y0, x1, y1
    else:
        return 0.0, 0.0, 0.0, 0.0  # Empty rectangle


@numba.jit(nopython=True, cache=True)
def optimize_rect_overlaps_numba(
    rects: np.ndarray[Any, np.dtype[np.float64]], enlarge: float = 0.0
) -> np.ndarray[Any, np.dtype[np.float64]]:  # type: ignore
    """Optimized version of refine_boxes using Numba with fixed return type."""
    n = len(rects)
    if n == 0:
        return np.empty((0, 4), dtype=np.float64)

    merged = np.zeros(n, dtype=np.bool_)
    result_rects = []

    for i in range(n):
        if merged[i]:
            continue

        # Start with current rectangle (enlarged)
        current_x0 = rects[i, 0] - enlarge
        current_y0 = rects[i, 1] - enlarge
        current_x1 = rects[i, 2] + enlarge
        current_y1 = rects[i, 3] + enlarge
        merged[i] = True

        # Keep looking for overlaps
        changed = True
        while changed:
            changed = False
            for j in range(n):
                if merged[j]:
                    continue

                if rect_intersects_numba(
                    current_x0,
                    current_y0,
                    current_x1,
                    current_y1,
                    rects[j, 0],
                    rects[j, 1],
                    rects[j, 2],
                    rects[j, 3],
                ):
                    # Merge rectangles
                    current_x0, current_y0, current_x1, current_y1 = rect_union_numba(
                        current_x0,
                        current_y0,
                        current_x1,
                        current_y1,
                        rects[j, 0],
                        rects[j, 1],
                        rects[j, 2],
                        rects[j, 3],
                    )
                    merged[j] = True
                    changed = True

        result_rects.append((current_x0, current_y0, current_x1, current_y1))

    # Convert list to numpy array
    if result_rects:
        return np.array(result_rects, dtype=np.float64)
    else:
        return np.empty((0, 4), dtype=np.float64)


def pymupdf_rect_to_tuple(
    rect: Any,
) -> Tuple[float, float, float, float]:
    """Convert PyMuPDF Rect or tuple to a tuple of floats."""
    if isinstance(rect, tuple):
        return rect  # type: ignore
    return (rect.x0, rect.y0, rect.x1, rect.y1)  # type: ignore[attr-defined]


def tuple_to_pymupdf_rect(
    rect_tuple: Tuple[float, float, float, float],
) -> Any:
    """Convert tuple back to PyMuPDF Rect."""
    return pymupdf.Rect(*rect_tuple)  # type: ignore[attr-defined]


def refine_boxes(
    boxes: List[Any],
    enlarge: float = 0,
) -> List[Any]:
    """Join rectangles with pairwise non-empty overlap using Numba optimization."""
    if not boxes:
        return []

    rect_array: np.ndarray[Any, np.dtype[np.float64]] = np.array(
        [pymupdf_rect_to_tuple(box) for box in boxes], dtype=np.float64
    )
    result_array: np.ndarray[Any, np.dtype[np.float64]] = optimize_rect_overlaps_numba(
        rect_array, enlarge
    )
    new_rects = [
        tuple_to_pymupdf_rect(
            (
                float(result_array[i][0]),
                float(result_array[i][1]),
                float(result_array[i][2]),
                float(result_array[i][3]),
            )
        )
        for i in range(result_array.shape[0])
    ]
    new_rects = sorted(
        set(new_rects),
        key=lambda r: (r.x0, r.y0),  # type: ignore[attr-defined]
    )
    return new_rects


@numba.jit(nopython=True, cache=True)
def is_significant_numba(
    box_x0: float,
    box_y0: float,
    box_x1: float,
    box_y1: float,
    path_rects: np.ndarray[Any, np.dtype[np.float64]],
) -> bool:  # type: ignore
    """Numba-optimized version of is_significant."""
    width = box_x1 - box_x0
    height = box_y1 - box_y0

    if width > height:
        d = width * 0.025
    else:
        d = height * 0.025

    # Create 90% interior box
    nbox_x0 = box_x0 + d
    nbox_y0 = box_y0 + d
    nbox_x1 = box_x1 - d
    nbox_y1 = box_y1 - d

    # Track unique dimensions
    widths_set = {int(width)}
    heights_set = {int(height)}

    has_interior_intersection = False

    for i in range(len(path_rects)):
        px0, py0, px1, py1 = (
            path_rects[i, 0],
            path_rects[i, 1],
            path_rects[i, 2],
            path_rects[i, 3],
        )

        # Check if path is contained in box but not equal to box
        if rect_contains_numba(box_x0, box_y0, box_x1, box_y1, px0, py0, px1, py1):
            if not (
                px0 == box_x0 and py0 == box_y0 and px1 == box_x1 and py1 == box_y1
            ):
                p_width = int(px1 - px0)
                p_height = int(py1 - py0)
                widths_set.add(p_width)
                heights_set.add(p_height)

                # Check intersection with interior
                if rect_intersects_numba(
                    nbox_x0, nbox_y0, nbox_x1, nbox_y1, px0, py0, px1, py1
                ):
                    has_interior_intersection = True

    if len(widths_set) == 1 or len(heights_set) == 1:
        return False

    return has_interior_intersection


def is_significant(
    box: Any,
    path_rects: np.ndarray[Any, np.dtype[np.float64]],
) -> bool:
    """Optimized version using Numba for the core computation."""
    if path_rects.size == 0:
        return False
    box_x0, box_y0, box_x1, box_y1 = pymupdf_rect_to_tuple(box)
    return is_significant_numba(box_x0, box_y0, box_x1, box_y1, path_rects)
