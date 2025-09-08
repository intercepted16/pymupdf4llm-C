"""This script accepts a PDF document filename and converts it to a text file
in Markdown format, compatible with the GitHub standard.

It must be invoked with the filename like this:

python pymupdf_rag.py input.pdf [-pages PAGES]

The "PAGES" parameter is a string (containing no spaces) of comma-separated
page numbers to consider. Each item is either a single page number or a
number range "m-n". Use "N" to address the document's last page number.
Example: "-pages 2-15,40,43-N"

It will produce a markdown text file called "input.md".

Text will be sorted in Western reading order. Any table will be included in
the text in markdwn format as well.

Dependencies
-------------
PyMuPDF v1.25.5 or later

Copyright and License
----------------------
Copyright (C) 2024-2025 Artifex Software, Inc.

PyMuPDF4LLM is free software: you can redistribute it and/or modify it under the
terms of the GNU Affero General Public License as published by the Free
Software Foundation, either version 3 of the License, or (at your option)
any later version.

Alternative licensing terms are available from the licensor.
For commercial licensing, see <https://www.artifex.com/> or contact
Artifex Software, Inc., 39 Mesa Street, Suite 108A, San Francisco,
CA 94129, USA, for further information.
"""

import os
import string
import time
from binascii import b2a_base64
import pymupdf
import ctypes
from pymupdf import mupdf
# from pymupdf4llm.helpers.get_text_lines import is_white
# from pymupdf4llm.helpers.multi_column import column_boxes
# from pymupdf4llm.helpers.progress import ProgressBar
from multi_column import column_boxes
from dataclasses import dataclass
from collections import defaultdict
from ctypes import CDLL
import numba
import numpy as np
from multiprocessing import Pool, cpu_count
import concurrent.futures
from typing import Tuple, List
from numba.core import types
from utils import suppress_output

# Load the C library
lib = ctypes.CDLL("./get_raw_markdown.so")

# Performance profiler
class PerformanceProfiler:
    def __init__(self):
        self.timings = defaultdict(list)
        self.start_times = {}
    
    def start_timer(self, name):
        self.start_times[name] = time.perf_counter()
    
    def end_timer(self, name):
        if name in self.start_times:
            elapsed = time.perf_counter() - self.start_times[name]
            self.timings[name].append(elapsed)
            del self.start_times[name]
            return elapsed
        return 0
    
    def get_stats(self):
        stats = {}
        for name, times in self.timings.items():
            stats[name] = {
                'total': sum(times),
                'average': sum(times) / len(times),
                'count': len(times),
                'min': min(times),
                'max': max(times)
            }
        return stats
    
    def print_report(self):
        print("\n" + "="*60)
        print("PERFORMANCE REPORT")
        print("="*60)
        stats = self.get_stats()
        
        # Sort by total time descending
        sorted_stats = sorted(stats.items(), key=lambda x: x[1]['total'], reverse=True)
        
        print(f"{'Function':<25} {'Total (s)':<10} {'Avg (s)':<10} {'Count':<8} {'Min (s)':<10} {'Max (s)':<10}")
        print("-" * 75)
        
        for name, data in sorted_stats:
            print(f"{name:<25} {data['total']:<10.4f} {data['average']:<10.4f} {data['count']:<8} {data['min']:<10.4f} {data['max']:<10.4f}")
        
        print("="*60)

    # convenience context manager for timing blocks
    from contextlib import contextmanager

    @contextmanager
    def time_block(self, name: str):
        """Context manager to time a block and automatically record it."""
        try:
            self.start_timer(name)
            yield
        finally:
            elapsed = self.end_timer(name)
            print(f"[perf] {name}: {elapsed:.6f}s")

    def timeit(self, name: str):
        """Decorator to time a function and record timings under `name`."""
        def decorator(fn):
            def wrapper(*a, **k):
                self.start_timer(name)
                try:
                    return fn(*a, **k)
                finally:
                    elapsed = self.end_timer(name)
                    # short console hint
                    print(f"[perf] {name} -> {fn.__name__}: {elapsed:.6f}s")
            return wrapper
        return decorator

# Global profiler instance
profiler = PerformanceProfiler()

def save_image(parms, rect, i):
    """Optionally render the rect part of a page."""
    page = parms.page
    image_size_limit_val = getattr(parms, "image_size_limit", image_size_limit)
    write_images = getattr(parms, "write_images", False)
    embed_images = getattr(parms, "embed_images", False)
    DPI = getattr(parms, "DPI", 150)
    IMG_PATH = getattr(parms, "IMG_PATH", "images")
    IMG_EXTENSION = getattr(parms, "IMG_EXTENSION", "png")
    if (
        rect.width < page.rect.width * image_size_limit_val
        or rect.height < page.rect.height * image_size_limit_val
    ):
        return ""
    if write_images or embed_images:
        pix = page.get_pixmap(clip=rect, dpi=DPI)
    else:
        return ""
    if pix.height <= 0 or pix.width <= 0:
        return ""
    if write_images:
        filename_clean = os.path.basename(parms.filename).replace(" ", "-")
        image_filename = os.path.join(
            IMG_PATH, f"{filename_clean}-{page.number}-{i}.{IMG_EXTENSION}"
        )
        pix.save(image_filename)
        return image_filename.replace("\\", "/")
    elif embed_images:
        data = b2a_base64(pix.tobytes(IMG_EXTENSION)).decode()
        data = f"data:image/{IMG_EXTENSION};base64," + data
        return data
    return ""



def page_is_ocr(page):
    """Check if page exclusively contains OCR text."""
    try:
        text_types = set([b[0] for b in page.get_bboxlog() if "text" in b[0]])
        if text_types == {"ignore-text"}:
            return True
    except:
        pass
    return False

def output_tables(parms, text_rect, defer=False, global_written_tables=None):
    """Output tables above given text rectangle."""
    this_md = ""
    written_tables = global_written_tables if global_written_tables is not None else parms.written_tables
    
    if defer:
        # In defer mode, just collect table information without outputting
        return this_md
    
    if text_rect is not None:
        for i, trect in sorted(
            [j for j in parms.tab_rects0 if j[1].y1 <= text_rect.y0],
            key=lambda j: (j[1].y1, j[1].x0),
        ):
            if i in written_tables:
                continue
            this_md += parms.tabs[i].to_markdown(clean=False) + "\n"
            if EXTRACT_WORDS:
                cells = sorted(
                    set([
                        pymupdf.Rect(c)
                        for c in parms.tabs[i].header.cells + parms.tabs[i].cells
                        if c is not None
                    ]),
                    key=lambda c: (c.y1, c.x0),
                )
                parms.line_rects.extend(cells)
            written_tables.append(i)
    else:
        for i, trect in parms.tab_rects0:
            if i in written_tables:
                continue
            this_md += parms.tabs[i].to_markdown(clean=False) + "\n"
            if EXTRACT_WORDS:
                cells = sorted(
                    set([
                        pymupdf.Rect(c)
                        for c in parms.tabs[i].header.cells + parms.tabs[i].cells
                        if c is not None
                    ]),
                    key=lambda c: (c.y1, c.x0),
                )
                parms.line_rects.extend(cells)
            written_tables.append(i)
    return this_md

def output_images(parms, text_rect, force_text):
    """Output images and graphics above text rectangle."""
    if not parms.img_rects:
        return ""
    this_md = ""
    if text_rect is not None:
        for i, img_rect in enumerate(parms.img_rects):
            if img_rect.y0 > text_rect.y0:
                continue
            if img_rect.x0 >= text_rect.x1 or img_rect.x1 <= text_rect.x0:
                continue
            if i in parms.written_images:
                continue
            pathname = save_image(parms, img_rect, i)
            parms.written_images.append(i)
            if pathname:
                this_md += GRAPHICS_TEXT % pathname
            if force_text:
                img_txt = write_text(parms, img_rect, tables=False, images=False, force_text=True)
                if not is_white(img_txt):
                    this_md += img_txt
    else:
        for i, img_rect in enumerate(parms.img_rects):
            if i in parms.written_images:
                continue
            pathname = save_image(parms, img_rect, i)
            parms.written_images.append(i)
            if pathname:
                this_md += GRAPHICS_TEXT % pathname
            if force_text:
                img_txt = write_text(parms, img_rect, tables=False, images=False, force_text=True)
                if not is_white(img_txt):
                    this_md += img_txt
    return this_md


def intersects_rects(rect, rect_list):
    """Check if middle of rect is contained in a rect of the list."""
    delta = (-1, -1, 1, 1)
    enlarged = rect + delta
    abs_enlarged = abs(enlarged) * 0.5
    for i, r in enumerate(rect_list, start=1):
        if abs(enlarged & r) > abs_enlarged:
            return i
    return 0

@dataclass
class Parameters:
    page: object = None
    filename: str = ""
    md_string: str = ""
    images: list = None
    tables: list = None
    graphics: list = None
    words: list = None
    line_rects: list = None
    written_tables: list = None
    written_images: list = None
    accept_invisible: bool = False
    tab_rects0: list = None
    tab_rects: dict = None
    bg_color: object = None
    clip: object = None
    links: list = None
    annot_rects: list = None
    img_rects: list = None
    tabs: list = None
    vg_clusters0: list = None
    vg_clusters: dict = None
    textpage: object = None


DETECT_BG_COLOR = True
image_size_limit = 0.02
table_strategy = "lines_strict"
force_text = True
EXTRACT_WORDS = False

def get_bg_color(page):
    """Determine the background color of the page."""
    try:
        pix = page.get_pixmap(
            clip=(page.rect.x0, page.rect.y0, page.rect.x0 + 10, page.rect.y0 + 10)
        )
        if not pix.samples or not pix.is_unicolor:
            return None
        pixel_ul = pix.pixel(0, 0)
        
        pix = page.get_pixmap(
            clip=(page.rect.x1 - 10, page.rect.y0, page.rect.x1, page.rect.y0 + 10)
        )
        if not pix.samples or not pix.is_unicolor:
            return None
        pixel_ur = pix.pixel(0, 0)
        
        if pixel_ul != pixel_ur:
            return None
            
        pix = page.get_pixmap(
            clip=(page.rect.x0, page.rect.y1 - 10, page.rect.x0 + 10, page.rect.y1)
        )
        if not pix.samples or not pix.is_unicolor:
            return None
        pixel_ll = pix.pixel(0, 0)
        
        if pixel_ul != pixel_ll:
            return None
            
        pix = page.get_pixmap(
            clip=(page.rect.x1 - 10, page.rect.y1 - 10, page.rect.x1, page.rect.y1)
        )
        if not pix.samples or not pix.is_unicolor:
            return None
        pixel_lr = pix.pixel(0, 0)
        
        if pixel_ul != pixel_lr:
            return None
            
        return (pixel_ul[0] / 255, pixel_ul[1] / 255, pixel_ul[2] / 255)
    except:
        return None


def get_page_output(doc, pno: int, margins, textflags, FILENAME, IGNORE_IMAGES, IGNORE_GRAPHICS, page_separators, defer_tables=False):
    """Process one page and return parameters object."""
    # Ensure all required globals are available
    global DETECT_BG_COLOR, image_size_limit, table_strategy, force_text, EXTRACT_WORDS

    with profiler.time_block(f'get_page_output_page_{pno}'):
        page = doc[pno]
        page.remove_rotation()

    ignore_alpha = False  # <-- Move this before usage

    parms = Parameters(
        images=[],
        tables=[],
        graphics=[],
        words=[],
        line_rects=[],
        written_tables=[],
        written_images=[],
        tab_rects0=[],
        tab_rects={},
        img_rects=[],
        tabs=[],
        vg_clusters0=[],
        vg_clusters={},
    )
    parms.page = page
    parms.filename = FILENAME
    parms.md_string = ""
    parms.accept_invisible = page_is_ocr(page) or ignore_alpha

    parms.bg_color = None if not DETECT_BG_COLOR else get_bg_color(page)

    left, top, right, bottom = margins
    parms.clip = page.rect + (left, top, -right, -bottom)

    parms.links = [l for l in page.get_links() if l["kind"] == pymupdf.LINK_URI]
    parms.annot_rects = [a.rect for a in page.annots()]

    # Extract images
    profiler.start_timer('extract_images')
    if not IGNORE_IMAGES:
        img_info = page.get_image_info()
        for i in img_info:
            i["bbox"] = pymupdf.Rect(i["bbox"])
        img_info = [
            i for i in img_info
            if i["bbox"].width >= image_size_limit * parms.clip.width
            and i["bbox"].height >= image_size_limit * parms.clip.height
            and i["bbox"].intersects(parms.clip)
            and i["bbox"].width > 3
            and i["bbox"].height > 3
        ]
        img_info.sort(key=lambda i: abs(i["bbox"]), reverse=True)
        parms.images = img_info[:30]
        parms.img_rects = [i["bbox"] for i in parms.images]
    else:
        parms.images = []
        parms.img_rects = []

    # End extract_images timer
    profiler.end_timer('extract_images')

    # Locate tables
    profiler.start_timer('locate_tables')
    if is_likely_table(doc.name, pno):
        parms.tabs = []
        if not IGNORE_GRAPHICS and table_strategy:
            try:
                tabs = page.find_tables(clip=parms.clip, strategy=table_strategy)
            except Exception:
                tabs = None
            if tabs:
                for t in tabs.tables:
                    try:
                        if t.row_count >= 2 and t.col_count >= 2:
                            parms.tabs.append(t)
                    except Exception:
                        continue
                parms.tabs.sort(key=lambda t: (t.bbox[0], t.bbox[1]))

                # IMMEDIATELY extract markdown for defer_tables mode before any other processing
                if defer_tables:
                    parms.deferred_tables = []
                    for i, tab_obj in enumerate(parms.tabs):
                        try:
                            # Extract the markdown immediately while objects are fresh
                            tab_markdown = tab_obj.to_markdown(clean=False)

                            # Extract cell rectangles for EXTRACT_WORDS if needed
                            cells_data = []
                            if EXTRACT_WORDS:
                                cells = sorted(
                                    set([
                                        pymupdf.Rect(c)
                                        for c in tab_obj.header.cells + tab_obj.cells
                                        if c is not None
                                    ]),
                                    key=lambda c: (c.y1, c.x0),
                                )
                                cells_data = [[c.x0, c.y0, c.x1, c.y1] for c in cells]

                            # Store safe table data
                            tab_data = {
                                'index': i,
                                'rect': list(tab_obj.bbox),
                                'bbox': list(tab_obj.bbox),
                                'markdown': tab_markdown,
                                'cells': cells_data,
                            }
                            parms.deferred_tables.append(tab_data)
                        except Exception as e:
                            print(f"Warning: Failed to extract table {i} markdown: {e}")
                            import traceback
                            traceback.print_exc()
        
    parms.tables = [
        {
            "bbox": tuple(pymupdf.Rect(t.bbox) | pymupdf.Rect(t.header.bbox)),
            "rows": t.row_count,
            "columns": t.col_count,
        }
        for t in parms.tabs
    ]
    parms.tab_rects0 = [(i, pymupdf.Rect(t["bbox"])) for i, t in enumerate(parms.tables)]
    parms.tab_rects = {i: pymupdf.Rect(t["bbox"]) for i, t in enumerate(parms.tables)}
        
    # Initialize empty deferred_tables for non-defer mode
    if not defer_tables:
        parms.deferred_tables = []
    profiler.end_timer('locate_tables')

    # Graphics paths
    profiler.start_timer('cluster_graphics')
    if not IGNORE_GRAPHICS:
        tab_rects_only = [rect for i, rect in parms.tab_rects0]
        paths = [
            p for p in page.get_drawings()
            if p["rect"] in parms.clip
            and not intersects_rects(p["rect"], tab_rects_only)
            and not intersects_rects(p["rect"], parms.annot_rects)
        ]
    else:
        paths = []

    vg_clusters0 = [bbox for bbox in page.cluster_drawings(drawings=paths) if is_significant(bbox, paths)]
    vg_clusters0.extend(parms.img_rects)
    parms.vg_clusters0 = refine_boxes(vg_clusters0)
    parms.vg_clusters = dict((i, r) for i, r in enumerate(parms.vg_clusters0))
    profiler.end_timer('cluster_graphics')

    parms.md_string = ""

    # Process tables and images
    profiler.start_timer('process_columns')
    tab_rects_only = [rect for i, rect in parms.tab_rects0]
    for rect in column_boxes(
        page=parms.page, 
        paths=paths, 
        file_path=doc.name,
        no_image_text=not force_text,
        # textpage=None, 
        avoid=tab_rects_only + parms.vg_clusters0,
        footer_margin=margins[3], 
        header_margin=margins[1],
        ignore_images=IGNORE_IMAGES
    ):
        if not defer_tables:
            parms.md_string += output_tables(parms, rect, defer=defer_tables)
        # parms.md_string += output_images(parms, rect, force_text)

    # Finalize remaining tables/images
    if not defer_tables:
        parms.md_string += output_tables(parms, None, defer=defer_tables)
    profiler.end_timer('process_columns')
    # parms.md_string += output_images(parms, None, force_text)

    parms.md_string = parms.md_string.replace(" ,", ",").replace("-\n", "")
    while parms.md_string.startswith("\n"):
        parms.md_string = parms.md_string[1:]
    parms.md_string = parms.md_string.replace(chr(0), chr(0xFFFD))

    # Extract words if requested
    if EXTRACT_WORDS:
        try:
            if not hasattr(parms, 'textpage'):
                parms.textpage = page.get_textpage(flags=textflags)
            rawwords = parms.textpage.extractWORDS()
            rawwords.sort(key=lambda w: (w[3], w[0]))
            parms.words = rawwords
        except Exception as e:
            print(f"Warning: Could not extract words from page {pno}: {e}")
            parms.words = []

    if page_separators:
        parms.md_string += f"\n\n--- end of page={parms.page.number} ---\n\n"

    # Clean up non-picklable objects for parallel processing
    if defer_tables:
        # Remove references to PyMuPDF objects that can't be pickled
        parms.page = None
        parms.textpage = None
        # Convert PyMuPDF Rects to tuples
        if hasattr(parms, 'clip') and parms.clip:
            parms.clip = tuple(parms.clip) if parms.clip else None
        
        # Clean up any PyMuPDF Rect objects in various lists
        if hasattr(parms, 'img_rects') and parms.img_rects:
            parms.img_rects = [tuple(r) if hasattr(r, 'x0') else r for r in parms.img_rects]
        
        if hasattr(parms, 'line_rects') and parms.line_rects:
            parms.line_rects = [tuple(r) if hasattr(r, 'x0') else r for r in parms.line_rects]
        
        if hasattr(parms, 'vg_clusters0') and parms.vg_clusters0:
            parms.vg_clusters0 = [tuple(r) if hasattr(r, 'x0') else r for r in parms.vg_clusters0]
        
        if hasattr(parms, 'tab_rects') and parms.tab_rects:
            parms.tab_rects = {k: tuple(v) if hasattr(v, 'x0') else v for k, v in parms.tab_rects.items()}
            
        if hasattr(parms, 'tab_rects0') and parms.tab_rects0:
            parms.tab_rects0 = [(i, tuple(r) if hasattr(r, 'x0') else r) for i, r in parms.tab_rects0]
        
        if hasattr(parms, 'vg_clusters') and parms.vg_clusters:
            parms.vg_clusters = {k: tuple(v) if hasattr(v, 'x0') else v for k, v in parms.vg_clusters.items()}
        
        # Clear the tabs list since we've extracted the data to deferred_tables
        if hasattr(parms, 'tabs'):
            parms.tabs = []

    return parms


def process_deferred_tables(all_page_results, doc):
    """Process tables from all pages sequentially to avoid race conditions."""
    with profiler.time_block('process_deferred_tables'):
        global_written_tables = []

        # Sort results by page number to ensure correct order
        all_page_results.sort(key=lambda x: x[0])

        print(f"Processing deferred tables for {len(all_page_results)} pages...")

        for pno, parms in all_page_results:
            if parms is None:
                continue

            # Only process pages that have deferred tables
            if not hasattr(parms, 'deferred_tables') or not parms.deferred_tables:
                continue

            print(f"  Page {pno}: Found {len(parms.deferred_tables)} deferred tables")

            # Process all tables for this page using global state
            page_table_md = ""

            # Process all deferred tables found on this page
            for tab_data in parms.deferred_tables:
                table_index = tab_data['index']
                if table_index in global_written_tables:
                    continue

                table_md = tab_data.get('markdown', '')
                page_table_md += table_md + "\n"
                global_written_tables.append(table_index)
                print(f"    Added table {table_index}: {len(table_md)} characters")

                # Add cell rectangles to line_rects if EXTRACT_WORDS is enabled
                if EXTRACT_WORDS and tab_data.get('cells'):
                    if not hasattr(parms, 'line_rects') or parms.line_rects is None:
                        parms.line_rects = []
                    for cell in tab_data['cells']:
                        try:
                            parms.line_rects.append(tuple(cell))
                        except Exception:
                            pass

            # Add the table markdown to the page's markdown string
            parms.md_string += page_table_md
            print(f"  Page {pno}: Added {len(page_table_md)} characters of table markdown")

        return all_page_results


# Fixed Numba-optimized geometric operations
@numba.jit(nopython=True, cache=True)
def rect_intersects_numba(
    x0_1: float, y0_1: float, x1_1: float, y1_1: float,
    x0_2: float, y0_2: float, x1_2: float, y1_2: float
) -> bool:
    """Check if two rectangles intersect."""
    return not (x1_1 <= x0_2 or x1_2 <= x0_1 or y1_1 <= y0_2 or y1_2 <= y0_1)

@numba.jit(nopython=True, cache=True)
def rect_contains_numba(x0_o: float, y0_o: float, x1_o: float, y1_o: float,
                        x0_i: float, y0_i: float, x1_i: float, y1_i: float) -> bool:
    """Check if outer rectangle contains inner rectangle."""
    return x0_o <= x0_i and y0_o <= y0_i and x1_o >= x1_i and y1_o >= y1_i

@numba.jit(nopython=True, cache=True)
def rect_area_numba(x0: float, y0: float, x1: float, y1: float) -> float:
    """Calculate area of rectangle."""
    return max(0.0, (x1 - x0) * (y1 - y0))

@numba.jit(nopython=True, cache=True)
def rect_union_numba(x0_1: float, y0_1: float, x1_1: float, y1_1: float,
                        x0_2: float, y0_2: float, x1_2: float, y1_2: float):
    """Calculate union of two rectangles and return as separate values."""
    x0 = min(x0_1, x0_2)
    y0 = min(y0_1, y0_2)
    x1 = max(x1_1, x1_2)
    y1 = max(y1_1, y1_2)
    return x0, y0, x1, y1

@numba.jit(nopython=True, cache=True)
def rect_intersection_numba(x0_1: float, y0_1: float, x1_1: float, y1_1: float,
                            x0_2: float, y0_2: float, x1_2: float, y1_2: float):
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
def optimize_rect_overlaps_numba(rects: np.ndarray, enlarge: float = 0.0) -> np.ndarray:
    """Optimized version of refine_boxes using Numba with fixed return type."""
    n = len(rects)
    if n == 0:
        return np.empty((0, 4), dtype=np.float64) 

    merged = np.zeros(n, dtype=numba.boolean)
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
                
                if rect_intersects_numba(current_x0, current_y0, current_x1, current_y1,
                                        rects[j, 0], rects[j, 1], rects[j, 2], rects[j, 3]):
                    # Merge rectangles
                    current_x0, current_y0, current_x1, current_y1 = rect_union_numba(
                        current_x0, current_y0, current_x1, current_y1,
                        rects[j, 0], rects[j, 1], rects[j, 2], rects[j, 3]
                    )
                    merged[j] = True
                    changed = True
        
        result_rects.append((current_x0, current_y0, current_x1, current_y1))

    # Convert list to numpy array
    if result_rects:
        return np.array(result_rects, dtype=np.float64)
    else:
        return np.empty((0, 4), dtype=np.float64)

def pymupdf_rect_to_tuple(rect) -> Tuple[float, float, float, float]:
    """Convert PyMuPDF Rect to tuple for Numba compatibility."""
    return (rect.x0, rect.y0, rect.x1, rect.y1)

def tuple_to_pymupdf_rect(rect_tuple: Tuple[float, float, float, float]):
    """Convert tuple back to PyMuPDF Rect."""
    return pymupdf.Rect(*rect_tuple)

# Define missing helper functions locally
def is_white(text):
    """Check if text is whitespace only."""
    return not text or text.isspace()

class ProgressBar:
    """Simple progress bar replacement."""
    def __init__(self, iterable):
        self.iterable = iterable
        self.total = len(iterable) if hasattr(iterable, '__len__') else None
        
    def __iter__(self):
        for i, item in enumerate(self.iterable):
            if self.total:
                print(f"Progress: {i+1}/{self.total}")
            else:
                print(f"Processing item {i+1}")
            yield item


# Define ctypes structures matching C
class SpanDict(ctypes.Structure):
    _fields_ = [
        ("bbox", ctypes.c_float * 4),
        ("text", ctypes.c_char_p),
        ("size", ctypes.c_float),
        ("flags", ctypes.c_int),
        ("char_flags", ctypes.c_int),
        ("alpha", ctypes.c_int),
        ("font", ctypes.c_char_p),
        ("line", ctypes.c_int),
        ("block", ctypes.c_int),
    ]

class LineDict(ctypes.Structure):
    _fields_ = [
        ("rect", ctypes.c_float * 4),
        ("spans", ctypes.POINTER(SpanDict)),
        ("span_count", ctypes.c_int),
        ("capacity", ctypes.c_int),
    ]

class LineArray(ctypes.Structure):
    _fields_ = [
        ("lines", ctypes.POINTER(LineDict)),
        ("line_count", ctypes.c_int),
    ]

lib.get_raw_lines.restype = ctypes.POINTER(LineArray)
lib.get_raw_lines.argtypes = [ctypes.c_char_p]
lib.free_line_array.restype = None
lib.free_line_array.argtypes = [ctypes.POINTER(LineArray)]

lib.to_markdown.restype = ctypes.c_char_p
lib.to_markdown.argtypes = [ctypes.c_char_p]

def get_raw_lines(pdf_path, clip=None, tolerance=3, ignore_invisible=True):
    """
    C-backed get_raw_lines keeping the same Python signature.
    Returns: list of [Rect, [spans]] like the original Python version.
    """
    profiler.start_timer('get_raw_lines')

    pdf_path_bytes = pdf_path.encode("utf-8")
    if not pdf_path_bytes:
        raise ValueError("PDF path must be provided.")

    with suppress_output():
        arr_ptr = lib.get_raw_lines(pdf_path_bytes)
    if not arr_ptr:
        profiler.end_timer('get_raw_lines')
        return []
    arr = arr_ptr.contents
    result = []
    for i in range(arr.line_count):
        line = arr.lines[i]
        rect = pymupdf.Rect(*line.rect)
        spans = []
        for j in range(line.span_count):
            s = line.spans[j]
            sbbox = pymupdf.Rect(*s.bbox)
            span = {
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
        result.append([rect, spans])
    with suppress_output():
        lib.free_line_array(arr_ptr)

    elapsed = profiler.end_timer('get_raw_lines')
    print(f"done get_raw_lines ({elapsed:.4f}s)")
    return result

def c_to_markdown(pdf_path: str) -> str:
    """C-backed to_markdown keeping the same Python signature."""
    profiler.start_timer('c_to_markdown')

    pdf_path_bytes = pdf_path.encode("utf-8")
    if not pdf_path_bytes:
        raise ValueError("PDF path must be provided.")

    with suppress_output():
        md_bytes = lib.to_markdown(pdf_path_bytes)
    if not md_bytes:
        profiler.end_timer('c_to_markdown')
        return ""
    md_string = md_bytes.decode("utf-8")
    # ctypes.free(md_bytes)  # Free the allocated memory in C

    elapsed = profiler.end_timer('c_to_markdown')
    print(f"C markdown generation completed ({elapsed:.4f}s)")
    return md_string

def is_likely_table(pdf_path: str, page_number: int) -> int:
    """Check if a page likely contains a table based on column detection."""
    profiler.start_timer('is_likely_table')

    pdf_path_bytes = pdf_path.encode("utf-8")
    if not pdf_path_bytes:
        raise ValueError("PDF path must be provided.")

    with suppress_output():
        result = lib.page_has_table(pdf_path_bytes, page_number)

    elapsed = profiler.end_timer('is_likely_table')
    print(f"Table detection completed ({elapsed:.4f}s)")
    return result

pymupdf.TOOLS.unset_quad_corrections(True)


# Characters recognized as bullets when starting a line.
bullet = tuple(
    [
        "- ",
        "* ",
        "> ",
        chr(0xB6),
        chr(0xB7),
        chr(8224),
        chr(8225),
        chr(8226),
        chr(0xF0A7),
        chr(0xF0B7),
    ]
    + list(map(chr, range(9632, 9680)))
)

GRAPHICS_TEXT = "\n![](%s)\n"


class IdentifyHeaders:
    """Compute data for identifying header text.

    All non-white text from all selected pages is extracted and its font size
    noted as a rounded value.
    The most frequent font size (and all smaller ones) is taken as body text
    font size.
    Larger font sizes are mapped to strings of multiples of '#', the header
    tag in Markdown, which in turn is Markdown's representation of HTML's
    header tags <h1> to <h6>.
    Larger font sizes than body text but smaller than the <h6> font size are
    represented as <h6>.
    """

    def __init__(
        self,
        doc: str,
        pages: list = None,
        body_limit: float = 12,  # force this to be body text
        max_levels: int = 6,  # accept this many header levels
    ):
        """Read all text and make a dictionary of fontsizes.

        Args:
            doc: PDF document or filename
            pages: consider these page numbers only
            body_limit: treat text with larger font size as a header
        """
        if not isinstance(max_levels, int) or max_levels not in range(1, 7):
            raise ValueError("max_levels must be an integer between 1 and 6")
        if isinstance(doc, pymupdf.Document):
            mydoc = doc
        else:
            mydoc = pymupdf.open(doc)

        if pages is None:  # use all pages if omitted
            pages = range(mydoc.page_count)

        fontsizes = defaultdict(int)
        for pno in pages:
            page = mydoc.load_page(pno)
            blocks = page.get_text("dict", flags=pymupdf.TEXTFLAGS_TEXT)["blocks"]
            for span in [  # look at all non-empty horizontal spans
                s
                for b in blocks
                for l in b["lines"]
                for s in l["spans"]
                if not is_white(s["text"])
            ]:
                fontsz = round(span["size"])  # # compute rounded fontsize
                fontsizes[fontsz] += len(span["text"].strip())  # add character count

        if mydoc != doc:
            # if opened here, close it now
            mydoc.close()

        # maps a fontsize to a string of multiple # header tag characters
        self.header_id = {}

        # If not provided, choose the most frequent font size as body text.
        # If no text at all on all pages, just use body_limit.
        # In any case all fonts not exceeding
        temp = sorted(
            [(k, v) for k, v in fontsizes.items()], key=lambda i: (i[1], i[0])
        )
        if temp:
            # most frequent font size
            self.body_limit = max(body_limit, temp[-1][0])
        else:
            self.body_limit = body_limit

        # identify up to 6 font sizes as header candidates
        sizes = sorted(
            [f for f in fontsizes.keys() if f > self.body_limit],
            reverse=True,
        )[:max_levels]

        # make the header tag dictionary
        for i, size in enumerate(sizes, start=1):
            self.header_id[size] = "#" * i + " "
        if self.header_id.keys():
            self.body_limit = min(self.header_id.keys()) - 1

    def get_header_id(self, span: dict, page=None) -> str:
        """Return appropriate markdown header prefix.

        Given a text span from a "dict"/"rawdict" extraction, determine the
        markdown header prefix string of 0 to n concatenated '#' characters.
        """
        fontsize = round(span["size"])  # compute fontsize
        if fontsize <= self.body_limit:
            return ""
        hdr_id = self.header_id.get(fontsize, "")
        return hdr_id


class TocHeaders:
    """Compute data for identifying header text.

    This is an alternative to IdentifyHeaders. Instead of running through the
    full document to identify font sizes, it uses the document's Table Of
    Contents (TOC) to identify headers on pages.
    Like IdentifyHeaders, this also is no guarantee to find headers, but it
    represents a good chance for appropriately built documents. In such cases,
    this method can be very much faster and more accurate, because we can
    directly use the hierarchy level of TOC items to ientify the header level.
    Examples where this works very well are the Adobe PDF documents.
    """

    def __init__(self, doc: str):
        """Read and store the TOC of the document."""
        if isinstance(doc, pymupdf.Document):
            mydoc = doc
        else:
            mydoc = pymupdf.open(doc)

        self.TOC = doc.get_toc()
        if mydoc != doc:
            # if opened here, close it now
            mydoc.close()

    def get_header_id(self, span: dict, page=None) -> str:
        """Return appropriate markdown header prefix.

        Given a text span from a "dict"/"rawdict" extraction, determine the
        markdown header prefix string of 0 to n concatenated '#' characters.
        """
        if not page:
            return ""
        # check if this page has TOC entries with an actual title
        my_toc = [t for t in self.TOC if t[1] and t[-1] == page.number + 1]
        if not my_toc:  # no TOC items present on this page
            return ""
        # Check if the span matches a TOC entry. This must be done in the
        # most forgiving way: exact matches are rare animals.
        text = span["text"].strip()  # remove leading and trailing whitespace
        for t in my_toc:
            title = t[1].strip()  # title of TOC entry
            lvl = t[0]  # level of TOC entry
            if text.startswith(title) or title.startswith(text):
                # found a match: return the header tag
                return "#" * lvl + " "
        return ""


def refine_boxes(boxes, enlarge=0):
    """Join any rectangles with a pairwise non-empty overlap using Numba optimization."""
    if not boxes:
        return []

    # Convert to numpy array for Numba processing
    rect_array = np.array([pymupdf_rect_to_tuple(box) for box in boxes], dtype=np.float64)

    # Use optimized Numba function
    result_array = optimize_rect_overlaps_numba(rect_array, enlarge)

    # Convert back to PyMuPDF Rects and sort
    new_rects = [tuple_to_pymupdf_rect(tuple(r)) for r in result_array]
    new_rects = sorted(set(new_rects), key=lambda r: (r.x0, r.y0))
    return new_rects

NUMBA_AVAILABLE = True

if NUMBA_AVAILABLE:
    @numba.jit(nopython=True, cache=True)
    def is_significant_numba(box_x0: float, box_y0: float, box_x1: float, box_y1: float, 
                            path_rects: np.ndarray) -> bool:
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
            px0, py0, px1, py1 = path_rects[i, 0], path_rects[i, 1], path_rects[i, 2], path_rects[i, 3]
            
            # Check if path is contained in box but not equal to box
            if rect_contains_numba(box_x0, box_y0, box_x1, box_y1, px0, py0, px1, py1):
                if not (px0 == box_x0 and py0 == box_y0 and px1 == box_x1 and py1 == box_y1):
                    p_width = int(px1 - px0)
                    p_height = int(py1 - py0)
                    widths_set.add(p_width)
                    heights_set.add(p_height)
                    
                    # Check intersection with interior
                    if rect_intersects_numba(nbox_x0, nbox_y0, nbox_x1, nbox_y1, px0, py0, px1, py1):
                        has_interior_intersection = True

        if len(widths_set) == 1 or len(heights_set) == 1:
            return False

        return has_interior_intersection
else:
    def is_significant_numba(box_x0: float, box_y0: float, box_x1: float, box_y1: float, 
                            path_rects: np.ndarray) -> bool:
        """Pure Python fallback for is_significant."""
        width = box_x1 - box_x0
        height = box_y1 - box_y0

        if width > height:
            d = width * 0.025
        else:
            d = height * 0.025

        nbox_x0 = box_x0 + d
        nbox_y0 = box_y0 + d
        nbox_x1 = box_x1 - d
        nbox_y1 = box_y1 - d

        widths_set = {int(width)}
        heights_set = {int(height)}

        has_interior_intersection = False

        for i in range(len(path_rects)):
            px0, py0, px1, py1 = path_rects[i, 0], path_rects[i, 1], path_rects[i, 2], path_rects[i, 3]
            
            # Check if path is contained in box but not equal to box
            if rect_contains_numba(box_x0, box_y0, box_x1, box_y1, px0, py0, px1, py1):
                if not (px0 == box_x0 and py0 == box_y0 and px1 == box_x1 and py1 == box_y1):
                    p_width = int(px1 - px0)
                    p_height = int(py1 - py0)
                    widths_set.add(p_width)
                    heights_set.add(p_height)
                    
                    # Check intersection with interior
                    if rect_intersects_numba(nbox_x0, nbox_y0, nbox_x1, nbox_y1, px0, py0, px1, py1):
                        has_interior_intersection = True

        if len(widths_set) == 1 or len(heights_set) == 1:
            return False

        return has_interior_intersection


def is_significant(box, paths):
    """Optimized version using Numba for the core computation."""
    if not paths:
        return False

    # Convert paths to numpy array
    path_rects = np.array([pymupdf_rect_to_tuple(p["rect"]) for p in paths], dtype=np.float64)
    box_x0, box_y0, box_x1, box_y1 = pymupdf_rect_to_tuple(box)

    return is_significant_numba(box_x0, box_y0, box_x1, box_y1, path_rects)


def process_page_batch(args):
    """Process a batch of pages in parallel."""
    doc_name, page_numbers, doc_args, get_page_output, page_separators = args
    name = f'batch_processing_{page_numbers[0]}_{page_numbers[-1]}'
    with profiler.time_block(name):
        # Open document in this process
        doc = pymupdf.open(doc_name)
        results = []

        try:
            for pno in page_numbers:
                try:
                    with profiler.time_block(f'process_page_{pno}'):
                        # doc_args is a tuple: (margins, textflags, FILENAME, IGNORE_IMAGES, IGNORE_GRAPHICS)
                        result = get_page_output(doc, pno, *doc_args, page_separators, defer_tables=True)
                        results.append((pno, result))
                except Exception as e:
                    print(f"Error processing page {pno}: {e}")
                    results.append((pno, None))
        finally:
            doc.close()

    return results


def to_markdown(
    doc,
    *,
    pages=None,
    hdr_info=None,
    write_images=False,
    embed_images=False,
    ignore_images=False,
    ignore_graphics=False,
    detect_bg_color=True,
    image_path="",
    image_format="png",
    image_size_limit=0.05,
    filename=None,
    force_text=True,
    page_chunks=False,
    page_separators=False,
    margins=0,
    dpi=150,
    page_width=612,
    page_height=None,
    table_strategy="lines_strict",
    graphics_limit=None,
    fontsize_limit=3,
    ignore_code=False,
    extract_words=False,
    show_progress=False,
    use_glyphs=False,
    ignore_alpha=False,
    batch_size=16,
    num_workers=None,
) -> str:
    """Process the document and return the text of the selected pages.

    Args:
        doc: pymupdf.Document or string.
        pages: list of page numbers to consider (0-based).
        hdr_info: callable or object having method 'get_hdr_info'.
        write_images: (bool) save images / graphics as files.
        embed_images: (bool) embed images in markdown text (base64 encoded)
        image_path: (str) store images in this folder.
        image_format: (str) use this image format. Choose a supported one.
        force_text: (bool) output text despite of image background.
        page_chunks: (bool) whether to segment output by page.
        page_separators: (bool) whether to include page separators in output.
        margins: omit content overlapping margin areas.
        dpi: (int) desired resolution for generated images.
        page_width: (float) assumption if page layout is variable.
        page_height: (float) assumption if page layout is variable.
        table_strategy: choose table detection strategy
        graphics_limit: (int) if vector graphics count exceeds this, ignore all.
        ignore_code: (bool) suppress code-like formatting (mono-space fonts)
        extract_words: (bool, False) include "words"-like output in page chunks
        show_progress: (bool, False) print progress as each page is processed.
        use_glyphs: (bool, False) replace the Invalid Unicode by glyph numbers.
        ignore_alpha: (bool, True) ignore text with alpha = 0 (transparent).
        batch_size: (int, 16) number of pages to process in each batch.
        num_workers: (int, None) number of worker processes. Defaults to cpu_count().
    """

    # Initialize text flags
    textflags = (
        0
        | mupdf.FZ_STEXT_CLIP
        | mupdf.FZ_STEXT_ACCURATE_BBOXES
        | 32768  # mupdf.FZ_STEXT_COLLECT_STYLES
    )

    if use_glyphs:
        textflags |= mupdf.FZ_STEXT_USE_GID_FOR_UNKNOWN_UNICODE

    # Validation
    if write_images is False and embed_images is False and force_text is False:
        raise ValueError("Image and text on images cannot both be suppressed.")
    if embed_images is True:
        write_images = False
        image_path = ""
    if not 0 <= image_size_limit < 1:
        raise ValueError("'image_size_limit' must be non-negative and less than 1.")

    # Set up parallel processing
    if num_workers is None:
        num_workers = min(cpu_count(), batch_size)

    # Global variables setup
    DPI = dpi
    IGNORE_CODE = ignore_code
    IMG_EXTENSION = image_format
    EXTRACT_WORDS = extract_words
    if EXTRACT_WORDS is True:
        page_chunks = True
        ignore_code = True
    IMG_PATH = image_path
    if IMG_PATH and write_images is True and not os.path.exists(IMG_PATH):
        os.makedirs(IMG_PATH, exist_ok=True)

    if not isinstance(doc, pymupdf.Document):
        doc = pymupdf.open(doc)

    FILENAME = doc.name if filename is None else filename
    GRAPHICS_LIMIT = graphics_limit
    FONTSIZE_LIMIT = fontsize_limit
    IGNORE_IMAGES = ignore_images
    IGNORE_GRAPHICS = ignore_graphics
    DETECT_BG_COLOR = detect_bg_color

    if doc.is_form_pdf or (doc.is_pdf and doc.has_annots()):
        doc.bake()

    # Handle reflowable documents
    if doc.is_reflowable:
        if hasattr(page_height, "__float__"):
            doc.layout(width=page_width, height=page_height)
        else:
            doc.layout(width=page_width, height=792)
            page_count = doc.page_count
            height = 792 * page_count
            doc.layout(width=page_width, height=height)

    if pages is None:
        pages = list(range(doc.page_count))

    # Handle margins
    if hasattr(margins, "__float__"):
        margins = [margins] * 4
    if len(margins) == 2:
        margins = (0, margins[0], 0, margins[1])
    if len(margins) != 4:
        raise ValueError("margins must be one, two or four floats")
    elif not all(hasattr(m, "__float__") for m in margins):
        raise ValueError("margin values must be floats")

    # Header identification setup
    if callable(hdr_info):
        get_header_id = hdr_info
    elif hasattr(hdr_info, "get_header_id") and callable(hdr_info.get_header_id):
        get_header_id = hdr_info.get_header_id
    elif hdr_info is False:
        get_header_id = lambda s, page=None: ""
    else:
        # Assuming IdentifyHeaders class exists
        hdr_info = IdentifyHeaders(doc)
        get_header_id = hdr_info.get_header_id

    def max_header_id(spans, page):
        hdr_ids = sorted(
            [l for l in set([len(get_header_id(s, page=page)) for s in spans]) if l > 0]
        )
        if not hdr_ids:
            return ""
        return "#" * (hdr_ids[0] - 1) + " "

    def resolve_links(links, span):
        """Accept a span and return a markdown link string."""
        bbox = pymupdf.Rect(span["bbox"])
        for link in links:
            hot = link["from"]
            middle = (hot.tl + hot.br) / 2
            if middle in bbox:
                text = f'[{span["text"].strip()}]({link["uri"]})'
                return text
        return None

    # Cache raw lines for the document - only do this once
    profiler.start_timer('cache_raw_lines')
    raw_lines_cache = get_raw_lines(
        doc.name if hasattr(doc, "name") else doc,
        clip=None,
        tolerance=3,
        ignore_invisible=True,
    )
    profiler.end_timer('cache_raw_lines')

    def write_text(parms, clip: pymupdf.Rect, tables=True, images=True, force_text=force_text):
        """Output the text found inside the given clip."""
        with profiler.time_block('write_text_total'):
            if clip is None:
                clip = parms.clip
            out_string = ""

        # Filter cached raw lines for those intersecting the clip
        nlines = [
            l for l in raw_lines_cache
            if clip.intersects(l[0]) and not intersects_rects(l[0], list(parms.tab_rects.values()))
        ]

        parms.line_rects.extend([l[0] for l in nlines])

        prev_lrect = None
        prev_bno = -1
        code = False
        prev_hdr_string = None

        for lrect, spans in nlines:
            if len(spans) > 30:
                profiler.start_timer(f'write_text_long_line_{int(lrect.y0)}')
            if intersects_rects(lrect, parms.img_rects):
                continue

            # Handle tables above this text block
            if tables:
                tab_candidates = [
                    (i, tab_rect)
                    for i, tab_rect in parms.tab_rects.items()
                    if tab_rect.y1 <= lrect.y0
                    and i not in parms.written_tables
                    and (
                        lrect.x0 <= tab_rect.x0 < lrect.x1
                        or lrect.x0 < tab_rect.x1 <= lrect.x1
                        or tab_rect.x0 <= lrect.x0 < lrect.x1 <= tab_rect.x1
                    )
                ]
                for i, _ in tab_candidates:
                    out_string += "\n" + parms.tabs[i].to_markdown(clean=False) + "\n"
                    if EXTRACT_WORDS:
                        cells = sorted(
                            set([
                                pymupdf.Rect(c)
                                for c in parms.tabs[i].header.cells + parms.tabs[i].cells
                                if c is not None
                            ]),
                            key=lambda c: (c.y1, c.x0),
                        )
                        parms.line_rects.extend(cells)
                    parms.written_tables.append(i)
                    prev_hdr_string = None

            # Handle images above this text block
            if images:
                for i in range(len(parms.img_rects)):
                    if i in parms.written_images:
                        continue
                    r = parms.img_rects[i]
                    if r.y1 <= lrect.y0 and (
                        lrect.x0 <= r.x0 < lrect.x1
                        or lrect.x0 < r.x1 <= lrect.x1
                        or r.x0 <= lrect.x0 < lrect.x1 <= r.x1
                    ):
                        pathname = save_image(parms, r, i)
                        if pathname:
                            out_string += GRAPHICS_TEXT % pathname

                        if force_text is True:
                            img_txt = write_text(parms, r, tables=False, images=False, force_text=True)
                            if not is_white(img_txt):
                                out_string += img_txt
                        parms.written_images.append(i)
                        prev_hdr_string = None

            parms.line_rects.append(lrect)
            
            # Add line break if far from previous line
            if (
                len(parms.line_rects) > 1
                and lrect.y1 - parms.line_rects[-2].y1 > lrect.height * 1.5
            ):
                out_string += "\n"

            text = " ".join([s["text"] for s in spans]).strip()

            # Check text formatting
            all_strikeout = all([s.get("char_flags", 0) & 1 for s in spans])
            all_italic = all([s.get("flags", 0) & 2 for s in spans])
            all_bold = all([(s.get("flags", 0) & 16) or (s.get("char_flags", 0) & 8) for s in spans])
            all_mono = all([s.get("flags", 0) & 8 for s in spans])

            hdr_string = max_header_id(spans, page=parms.page)

            if hdr_string:
                if all_mono:
                    text = "`" + text + "`"
                if all_italic:
                    text = "_" + text + "_"
                if all_bold:
                    text = "**" + text + "**"
                if all_strikeout:
                    text = "~~" + text + "~~"
                if hdr_string != prev_hdr_string:
                    out_string += hdr_string + text + "\n"
                else:
                    while out_string.endswith("\n"):
                        out_string = out_string[:-1]
                    out_string += " " + text + "\n"
                prev_hdr_string = hdr_string
                continue

            prev_hdr_string = hdr_string

            # Handle code blocks
            if all_mono and not IGNORE_CODE:
                if not code:
                    out_string += "```\n"
                    code = True
                delta = int((lrect.x0 - clip.x0) / (spans[0].get("size", 12) * 0.5))
                indent = " " * delta
                out_string += indent + text + "\n"
                continue

            if code and not all_mono:
                out_string += "```\n"
                code = False

            span0 = spans[0]
            bno = span0.get("block", 0)
            if bno != prev_bno:
                out_string += "\n"
                prev_bno = bno

            # Check for line breaks
            if (
                prev_lrect
                and lrect.y1 - prev_lrect.y1 > lrect.height * 1.5
                or span0["text"].startswith("[")
                or span0["text"].startswith(bullet)
                or span0.get("flags", 0) & 1
            ):
                out_string += "\n"
            prev_lrect = lrect

            if code:
                out_string += "```\n"
                code = False

            # Process individual spans
            for i, s in enumerate(spans):
                mono = s.get("flags", 0) & 8
                bold = (s.get("flags", 0) & 16) or (s.get("char_flags", 0) & 8)
                italic = s.get("flags", 0) & 2
                strikeout = s.get("char_flags", 0) & 1

                prefix = ""
                suffix = ""
                if mono:
                    prefix = "`" + prefix
                    suffix += "`"
                if bold:
                    prefix = "**" + prefix
                    suffix += "**"
                if italic:
                    prefix = "_" + prefix
                    suffix += "_"
                if strikeout:
                    prefix = "~~" + prefix
                    suffix += "~~"

                ltext = resolve_links(parms.links, s)
                if ltext:
                    text = f"{hdr_string}{prefix}{ltext}{suffix} "
                else:
                    text = f"{hdr_string}{prefix}{s['text'].strip()}{suffix} "
                
                if text.startswith(bullet):
                    text = "- " + text[1:]
                    text = text.replace("  ", " ")
                    dist = span0.get("bbox", [0, 0, 0, 0])[0] - clip.x0
                    cwidth = (span0.get("bbox", [0, 0, 0, 0])[2] - span0.get("bbox", [0, 0, 0, 0])[0]) / len(span0["text"])
                    if cwidth == 0.0:
                        cwidth = span0.get("size", 12) * 0.5
                    text = " " * int(round(dist / cwidth)) + text

                out_string += text
            
            if not code:
                out_string += "\n"
            if len(spans) > 30:
                elapsed = profiler.end_timer(f'write_text_long_line_{int(lrect.y0)}')
                if elapsed and elapsed > 0.001:
                    print(f"[perf] long line at y={lrect.y0} took {elapsed:.6f}s with {len(spans)} spans")
        
        out_string += "\n"
        if code:
            out_string += "```\n"
            code = False
        out_string += "\n\n"
        
        return out_string.replace(" \n", "\n").replace("  ", " ").replace("\n\n\n", "\n\n")

    def get_metadata(doc, pno):
        """Get document metadata for a specific page."""
        meta = doc.metadata.copy()
        meta["file_path"] = FILENAME
        meta["page_count"] = doc.page_count
        meta["page"] = pno + 1
        return meta

    def sort_words(words: list) -> list:
        """Reorder words in lines."""
        if not words:
            return []
        nwords = []
        line = [words[0]]
        lrect = pymupdf.Rect(words[0][:4])
        for w in words[1:]:
            if abs(w[1] - lrect.y0) <= 3 or abs(w[3] - lrect.y1) <= 3:
                line.append(w)
                lrect |= w[:4]
            else:
                line.sort(key=lambda w: w[0])
                nwords.extend(line)
                line = [w]
                lrect = pymupdf.Rect(w[:4])
        line.sort(key=lambda w: w[0])
        nwords.extend(line)
        return nwords

    # Main processing logic
    if page_chunks is False:
        document_output = ""
    else:
        document_output = []

    # Read the Table of Contents
    toc = doc.get_toc()

    if show_progress:
        print(f"Processing {FILENAME}...")
        pages = ProgressBar(pages) if not page_chunks else pages

    # Check if any pages likely have tables
    has_tables = False
    if not page_chunks:
        try:
            for pno in pages:  # Check all pages for tables
                if is_likely_table(FILENAME, pno):
                    has_tables = True
                    break
        except Exception:
            has_tables = False  # If table detection fails, assume no tables
    
    # Handle parallel processing for large documents (including those with tables)
    if len(pages) > batch_size and not page_chunks:
        print(f"Using parallel processing for {len(pages)} pages...")
        # Split pages into batches
        page_batches = []
        for i in range(0, len(pages), batch_size):
            batch = pages[i:i + batch_size]
            page_batches.append(batch)
        
        # Prepare arguments for each batch (defer tables for parallel processing)
        doc_args = (margins, textflags, FILENAME, IGNORE_IMAGES, IGNORE_GRAPHICS)
        batch_args = [(FILENAME, batch, doc_args, get_page_output, page_separators) for batch in page_batches]
        
        # Process batches in parallel with deferred table processing
        with profiler.time_block('parallel_page_processing'):
            all_results = []
            with concurrent.futures.ProcessPoolExecutor(max_workers=num_workers) as executor:
                future_to_batch = {executor.submit(process_page_batch, args): args for args in batch_args}

                for future in concurrent.futures.as_completed(future_to_batch):
                    try:
                        batch_results = future.result()
                        all_results.extend(batch_results)
                    except Exception as e:
                        print(f"Error processing batch: {e}")
                        continue

        # Sort results by page number
        all_results.sort(key=lambda x: x[0])

        # Process deferred tables sequentially to avoid race conditions
        if has_tables:
            print("Processing tables sequentially to avoid race conditions...")
            all_results = process_deferred_tables(all_results, doc)

        # Get the full document markdown once from C function
        try:
            with profiler.time_block('c_to_markdown_call'):
                full_document_md = c_to_markdown(FILENAME) if pages else ""
            document_output += full_document_md
        except Exception as e:
            print(f"Warning: Could not get full document markdown: {e}")
            document_output = ""

        # Add content from each page
        with profiler.time_block('assemble_page_results'):
            for pno, parms in all_results:
                if parms is not None:
                    document_output += parms.md_string

        return document_output
    
    # Sequential processing for smaller documents or page chunks
    print(f"Using sequential processing for {len(pages)} pages...")

    # Sequential processing for smaller documents or page chunks
    try:
        # Get the full document markdown once from C function (for non-chunked output)
        full_document_md = c_to_markdown(FILENAME) if pages and not page_chunks else ""
    except Exception as e:
        print(f"Warning: Could not get full document markdown: {e}")
        full_document_md = ""

    # Global table tracking for sequential processing
    global_written_tables = []
    
    for pno in pages:
        try:
            # For sequential processing, don't defer tables
            parms = get_page_output(doc, pno, margins, textflags, FILENAME, IGNORE_IMAGES, IGNORE_GRAPHICS, page_separators, defer_tables=False)
            
            # Synchronize table state for sequential processing
            if hasattr(parms, 'written_tables') and global_written_tables:
                # Merge previously written tables into current page params
                parms.written_tables.extend([t for t in global_written_tables if t not in parms.written_tables])
        
            if page_chunks is False:
                # For non-chunked output, only add the full markdown once (for the first page)
                if pno == pages[0]:
                    document_output += full_document_md
                # Add tables and images from this page
                document_output += parms.md_string
                
                # Update global table tracking
                if hasattr(parms, 'written_tables'):
                    global_written_tables.extend([t for t in parms.written_tables if t not in global_written_tables])
            else:
                # For page chunks, create page-specific output
                page_tocs = [t for t in toc if t[-1] == pno + 1]
                metadata = get_metadata(doc, pno)
                
                document_output.append({
                    "metadata": metadata,
                    "toc_items": page_tocs,
                    "tables": parms.tables,
                    "images": parms.images,
                    "graphics": getattr(parms, 'graphics', []),
                    "text": parms.md_string,
                    "words": parms.words,
                })
            
            del parms
            
        except Exception as e:
            print(f"Error processing page {pno}: {e}")
            if page_chunks:
                # Add empty page chunk for failed pages
                document_output.append({
                    "metadata": get_metadata(doc, pno),
                    "toc_items": [],
                    "tables": [],
                    "images": [],
                    "graphics": [],
                    "text": f"Error processing page {pno + 1}: {str(e)}",
                    "words": [],
                })
            continue

    return document_output


def extract_images_on_page_simple(page, parms, image_size_limit):
    """Extract images on page, ignoring images contained in some other one (simplified mechanism)."""
    img_info = page.get_image_info()
    for i in range(len(img_info)):
        item = img_info[i]
        item["bbox"] = pymupdf.Rect(item["bbox"]) & parms.clip
        img_info[i] = item

    # sort descending by image area size
    img_info.sort(key=lambda i: abs(i["bbox"]), reverse=True)
    # run from back to front (= small to large)
    for i in range(len(img_info) - 1, 0, -1):
        r = img_info[i]["bbox"]
        if r.is_empty:
            del img_info[i]
            continue
        for j in range(i):  # image areas larger than r
            if r in img_info[j]["bbox"]:
                del img_info[i]  # contained in some larger image
                break

    return img_info


def filter_small_images(page, parms, image_size_limit):
    """Filter out small images based on size limit."""
    img_info = []
    for item in page.get_image_info():
        r = pymupdf.Rect(item["bbox"]) & parms.clip
        if r.is_empty or (
            max(r.width / page.rect.width, r.height / page.rect.height)
            < image_size_limit
        ):
            continue
        item["bbox"] = r
        img_info.append(item)
    return img_info


def extract_images_on_page_simple_drop(page, parms, image_size_limit):
    """Extract images with size filtering and overlap removal."""
    img_info = filter_small_images(page, parms, image_size_limit)

    # sort descending by image area size
    img_info.sort(key=lambda i: abs(i["bbox"]), reverse=True)
    # run from back to front (= small to large)
    for i in range(len(img_info) - 1, 0, -1):
        r = img_info[i]["bbox"]
        if r.is_empty:
            del img_info[i]
            continue
        for j in range(i):  # image areas larger than r
            if r in img_info[j]["bbox"]:
                del img_info[i]  # contained in some larger image
                break

    return img_info


# Add missing functions needed by the C library
lib.page_has_table.restype = ctypes.c_int
lib.page_has_table.argtypes = [ctypes.c_char_p, ctypes.c_int]


if __name__ == "__main__":
    import pathlib
    import sys
    import time

    try:
        filename = sys.argv[1]
        output_name = pathlib.Path(sys.argv[2])
    except IndexError:
        print(f"Usage:\npython {os.path.basename(__file__)} input.pdf")
        sys.exit()

    t0 = time.perf_counter()  # start a timer

    doc = pymupdf.open(filename)  # open input file
    parms = sys.argv[2:]  # contains ["-pages", "PAGES"] or empty list
    pages = range(doc.page_count)  # default page range
    if len(parms) == 2 and parms[0] == "-pages":  # page sub-selection given
        pages = []  # list of desired page numbers

        # replace any variable "N" by page count
        pages_spec = parms[1].replace("N", f"{doc.page_count}")
        for spec in pages_spec.split(","):
            if "-" in spec:
                start, end = map(int, spec.split("-"))
                pages.extend(range(start - 1, end))
            else:
                pages.append(int(spec) - 1)

        # make a set of invalid page numbers
        wrong_pages = set([n + 1 for n in pages if n >= doc.page_count][:4])
        if wrong_pages != set():  # if any invalid numbers given, exit.
            sys.exit(f"Page number(s) {wrong_pages} not in '{doc}'.")

    print(f"Processing {len(pages)} pages with batch size 16 and parallel processing...")
    
    # get the markdown string with performance improvements
    profiler.start_timer('total_to_markdown')
    md_string = to_markdown(
        doc,
        pages=pages,
        batch_size=16,  # Process 16 pages per batch
        num_workers=min(cpu_count(), 16),  # Limit to 16 workers max
    )
    profiler.end_timer('total_to_markdown')
    
    FILENAME = doc.name
    # output to a text file with extension ".md"
    profiler.start_timer('file_output')
    pathlib.Path(output_name).write_bytes(md_string.encode())
    profiler.end_timer('file_output')
    
    t1 = time.perf_counter()  # stop timer
    print(f"Markdown creation time for {FILENAME=} {round(t1-t0,2)} sec.")
    
    # Print performance report
    profiler.print_report()
