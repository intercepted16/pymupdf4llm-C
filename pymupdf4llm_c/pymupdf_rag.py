"""This script accepts a PDF document filename and converts it to a text file in Markdown format.

Compatible with the GitHub standard.
It uses a C extension for performance-critical parts.

NOTE: This is a modified version; the following copyright information is retained from the
original project for compliance.

Dependencies
-------------
PyMuPDF v1.25.5 or later

Copyright and License
----------------------
Original Project: PyMuPDF
Copyright (C) 2024 Artifex Software, Inc.

Modifications: Your Name
Copyright (C) 2025 Your Name

This software is free: you can redistribute it and/or modify it under the terms of the
GNU Affero General Public License (AGPL) as published by the Free Software Foundation,
either version 3 of the License, or (at your option) any later version.

You must retain this copyright notice and make the source code available to users, including
any network interactions, in accordance with AGPL requirements.

Alternative licensing terms are available from the licensor.
For commercial licensing, see <https://www.artifex.com/> or contact Artifex Software, Inc.,
39 Mesa Street, Suite 108A, San Francisco, CA 94129, USA, for further information.
"""

import concurrent.futures
import ctypes
import os
import re
import time
from binascii import b2a_base64
from collections import defaultdict
from dataclasses import dataclass
from multiprocessing import cpu_count
from typing import Any, Callable, Dict, List, Optional, Tuple, Union

import numpy as np
import pymupdf
from geometry_utils import is_significant, pymupdf_rect_to_tuple, refine_boxes
from globals import LIB_PATH
from multi_column import column_boxes
from pymupdf import mupdf
from utils import profiler
from wrappers import is_likely_table
from wrappers import to_markdown as c_to_markdown

from table import (
    locate_and_extract_tables,
    output_tables,
    process_deferred_tables,
)

# Load the C library
lib = ctypes.CDLL(LIB_PATH)


def save_image(parms: "Parameters", rect: "pymupdf.Rect", i: int) -> str:
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


def page_is_ocr(page: "pymupdf.Page") -> bool:
    """Check if page exclusively contains OCR text."""
    try:
        text_types = set([b[0] for b in page.get_bboxlog() if "text" in b[0]])
        if text_types == {"ignore-text"}:
            return True
    except:
        pass
    return False


def output_images(
    parms: "Parameters", text_rect: Optional["pymupdf.Rect"], force_text: bool
) -> str:
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
                img_txt = write_text(
                    parms, img_rect, tables=False, images=False, force_text=True
                )
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
                img_txt = write_text(
                    parms, img_rect, tables=False, images=False, force_text=True
                )
                if not is_white(img_txt):
                    this_md += img_txt
    return this_md


def intersects_rects(rect: "pymupdf.Rect", rect_list: List["pymupdf.Rect"]) -> int:
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
    page: pymupdf.Page
    filename: str = ""
    md_string: str = ""
    images: Optional[List[Dict[str, Any]]] = None
    tables: Optional[List[Dict[str, Any]]] = None
    graphics: Optional[List[Dict[str, Any]]] = None
    words: Optional[List[Tuple[float, float, float, float, str, int]]] = None
    line_rects: Optional[List["pymupdf.Rect"]] = None
    written_tables: Optional[List[int]] = None
    written_images: Optional[List[int]] = None
    accept_invisible: bool = False
    tab_rects0: Optional[List[Tuple[int, "pymupdf.Rect"]]] = None
    tab_rects: Optional[Dict[int, "pymupdf.Rect"]] = None
    bg_color: Optional[Tuple[float, float, float]] = None
    clip: Optional["pymupdf.Rect"] = None
    links: Optional[List[Dict[str, Any]]] = None
    annot_rects: Optional[List["pymupdf.Rect"]] = None
    img_rects: Optional[List["pymupdf.Rect"]] = None
    tabs: Optional[List[Any]] = None
    vg_clusters0: Optional[List["pymupdf.Rect"]] = None
    vg_clusters: Optional[Dict[int, "pymupdf.Rect"]] = None
    textpage: Optional[Any] = None


DETECT_BG_COLOR: bool = True
image_size_limit: float = 0.02
table_strategy: str = "lines_strict"
force_text: bool = True
EXTRACT_WORDS: bool = False


def get_bg_color(page: "pymupdf.Page") -> Optional[Tuple[float, float, float]]:
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


def get_page_output(
    doc: "pymupdf.Document",
    pno: int,
    margins: Union[List[float], float],
    textflags: int,
    FILENAME: str,
    IGNORE_IMAGES: bool,
    IGNORE_GRAPHICS: bool,
    page_separators: bool,
    defer_tables: bool = False,
) -> Parameters:
    """Process one page and return parameters object."""
    # Ensure all required globals are available
    global DETECT_BG_COLOR, image_size_limit, table_strategy, force_text, EXTRACT_WORDS

    with profiler.time_block(f"get_page_output_page_{pno}"):
        page = doc[pno]
        page.remove_rotation()

    ignore_alpha = False  # <-- Move this before usage

    parms = Parameters(
        page=page,
        filename=FILENAME,
        md_string="",
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
        accept_invisible=page_is_ocr(page) or ignore_alpha,
        bg_color=None if not DETECT_BG_COLOR else get_bg_color(page),
    )

    left, top, right, bottom = margins
    parms.clip = page.rect + (left, top, -right, -bottom)

    parms.links = [l for l in page.get_links() if l["kind"] == pymupdf.LINK_URI]
    parms.annot_rects = [a.rect for a in page.annots()]

    # Extract images
    profiler.start_timer("extract_images")
    if not IGNORE_IMAGES:
        img_info = page.get_image_info()
        for i in img_info:
            i["bbox"] = pymupdf.Rect(i["bbox"])
        img_info = [
            i
            for i in img_info
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
    profiler.end_timer("extract_images")

    # Locate tables
    locate_and_extract_tables(
        parms,
        doc,
        pno,
        IGNORE_GRAPHICS,
        table_strategy,
        defer_tables,
        EXTRACT_WORDS,
    )

    # Graphics paths
    profiler.start_timer("cluster_graphics")
    if not IGNORE_GRAPHICS:
        tab_rects_only = [rect for i, rect in parms.tab_rects0]
        paths = [
            p
            for p in page.get_drawings()
            if p["rect"] in parms.clip
            and not intersects_rects(p["rect"], tab_rects_only)
            and not intersects_rects(p["rect"], parms.annot_rects)
        ]
    else:
        paths = []

    if paths:
        path_rects = np.array(
            [pymupdf_rect_to_tuple(p["rect"]) for p in paths], dtype=np.float64
        )
        vg_clusters0 = [
            bbox
            for bbox in page.cluster_drawings(drawings=paths)
            if is_significant(bbox, path_rects)
        ]
    else:
        vg_clusters0 = []

    vg_clusters0.extend(parms.img_rects)
    parms.vg_clusters0 = refine_boxes(vg_clusters0)
    parms.vg_clusters = dict((i, r) for i, r in enumerate(parms.vg_clusters0))
    profiler.end_timer("cluster_graphics")

    parms.md_string = ""

    # Process tables and images
    profiler.start_timer("process_columns")
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
        ignore_images=IGNORE_IMAGES,
    ):
        if not defer_tables:
            parms.md_string += output_tables(
                parms, rect, defer=defer_tables, extract_words=EXTRACT_WORDS
            )
        # parms.md_string += output_images(parms, rect, force_text)

    # Finalize remaining tables/images
    if not defer_tables:
        parms.md_string += output_tables(
            parms, None, defer=defer_tables, extract_words=EXTRACT_WORDS
        )
    profiler.end_timer("process_columns")
    # parms.md_string += output_images(parms, None, force_text)

    parms.md_string = parms.md_string.replace(" ,", ",").replace("-\n", "")
    while parms.md_string.startswith("\n"):
        parms.md_string = parms.md_string[1:]
    parms.md_string = parms.md_string.replace(chr(0), chr(0xFFFD))

    # Extract words if requested
    if EXTRACT_WORDS:
        try:
            if not hasattr(parms, "textpage"):
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
        if hasattr(parms, "clip") and parms.clip:
            parms.clip = tuple(parms.clip) if parms.clip else None

        # Clean up any PyMuPDF Rect objects in various lists
        if hasattr(parms, "img_rects") and parms.img_rects:
            parms.img_rects = [
                tuple(r) if hasattr(r, "x0") else r for r in parms.img_rects
            ]

        if hasattr(parms, "line_rects") and parms.line_rects:
            parms.line_rects = [
                tuple(r) if hasattr(r, "x0") else r for r in parms.line_rects
            ]

        if hasattr(parms, "vg_clusters0") and parms.vg_clusters0:
            parms.vg_clusters0 = [
                tuple(r) if hasattr(r, "x0") else r for r in parms.vg_clusters0
            ]

        if hasattr(parms, "tab_rects") and parms.tab_rects:
            parms.tab_rects = {
                k: tuple(v) if hasattr(v, "x0") else v
                for k, v in parms.tab_rects.items()
            }

        if hasattr(parms, "tab_rects0") and parms.tab_rects0:
            parms.tab_rects0 = [
                (i, tuple(r) if hasattr(r, "x0") else r) for i, r in parms.tab_rects0
            ]

        if hasattr(parms, "vg_clusters") and parms.vg_clusters:
            parms.vg_clusters = {
                k: tuple(v) if hasattr(v, "x0") else v
                for k, v in parms.vg_clusters.items()
            }

        # Clear the tabs list since we've extracted the data to deferred_tables
        if hasattr(parms, "tabs"):
            parms.tabs = []

    return parms


def cleanup_markdown_text(text: str) -> str:
    """Clean up markdown text by removing unwanted tags and characters."""
    if not text:
        return ""

    # Replace <br> tags with "" (nothing)
    text = text.replace("<br>", "").replace("<br/>", "")
    text = re.sub(r"\*\*\*\*", "** **", text)
    text = re.sub(r"[^\x20-\x7E\n]", "", text)  # keeps standard ASCII + newline

    # Remove Unicode replacement character
    text = text.replace("\ufffd", "")

    # Normalize newlines (3 or more to 2)
    while "\n\n\n" in text:
        text = text.replace("\n\n\n", "\n\n")

    return text


# Define missing helper functions locally
def is_white(text: str) -> str:
    """Check if text is whitespace only."""
    return not text or text.isspace()


class ProgressBar:
    """Simple progress bar replacement."""

    def __init__(self, iterable: Any) -> None:
        self.iterable: Any = iterable
        self.total: Optional[int] = (
            len(iterable) if hasattr(iterable, "__len__") else None
        )

    def __iter__(self) -> Any:
        for i, item in enumerate(self.iterable):
            if self.total:
                print(f"Progress: {i + 1}/{self.total}")
            else:
                print(f"Processing item {i + 1}")
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

lib.to_markdown.restype = ctypes.c_int
lib.to_markdown.argtypes = [ctypes.c_char_p, ctypes.c_char_p]


pymupdf.TOOLS.unset_quad_corrections(True)


# Characters recognized as bullets when starting a line.
bullet: Tuple[str, ...] = tuple(
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

GRAPHICS_TEXT: str = "\n![](%s)\n"


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
        doc: Union[str, "pymupdf.Document"],
        pages: Optional[List[int]] = None,
        body_limit: float = 12,  # force this to be body text
        max_levels: int = 6,  # accept this many header levels
    ) -> None:
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
        self.header_id: Dict[float, str] = {}

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

    def get_header_id(
        self, span: Dict[str, Any], page: Optional["pymupdf.Page"] = None
    ) -> str:
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

    def __init__(self, doc: Union[str, "pymupdf.Document"]) -> None:
        """Read and store the TOC of the document."""
        if isinstance(doc, pymupdf.Document):
            mydoc = doc
        else:
            mydoc = pymupdf.open(doc)

        self.TOC: List[List[Any]] = doc.get_toc()
        if mydoc != doc:
            # if opened here, close it now
            mydoc.close()

    def get_header_id(
        self, span: Dict[str, Any], page: Optional["pymupdf.Page"] = None
    ) -> str:
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


def process_page_batch(
    args: Tuple[str, List[int], Tuple[Any, ...], Callable, bool],
) -> List[Tuple[int, Optional[Parameters]]]:
    """Process a batch of pages in parallel."""
    doc_name, page_numbers, doc_args, get_page_output, page_separators = args
    name = f"batch_processing_{page_numbers[0]}_{page_numbers[-1]}"
    with profiler.time_block(name):
        # Open document in this process
        doc = pymupdf.open(doc_name)
        results = []

        try:
            for pno in page_numbers:
                try:
                    with profiler.time_block(f"process_page_{pno}"):
                        # doc_args is a tuple: (margins, textflags, FILENAME, IGNORE_IMAGES, IGNORE_GRAPHICS)
                        result = get_page_output(
                            doc, pno, *doc_args, page_separators, defer_tables=True
                        )
                        results.append((pno, result))
                except Exception as e:
                    print(f"Error processing page {pno}: {e}")
                    results.append((pno, None))
        finally:
            doc.close()

    return results


# Global variable for the worker process
worker_doc = None


def init_worker(doc_path: str):
    """Initializer for each worker process. Opens the document and stores it in a
    global variable private to each worker.
    """
    global worker_doc
    if worker_doc is None:
        worker_doc = pymupdf.open(doc_path)


def process_page_worker(pno: int, doc_args: tuple, page_separators: bool) -> tuple:
    """Worker function to process a single page.
    Uses the global 'worker_doc' object initialized by 'init_worker'.
    """
    global worker_doc
    if worker_doc is None:
        raise RuntimeError("Worker document not initialized.")

    try:
        result = get_page_output(
            worker_doc, pno, *doc_args, page_separators, defer_tables=True
        )
        return (pno, result)
    except Exception as e:
        print(f"Error processing page {pno} in worker: {e}")
        return (pno, None)


def to_markdown(
    doc: Union[str, "pymupdf.Document"],
    *,
    output_path: str,
    pages: Optional[List[int]] = None,
    hdr_info: Optional[Union[Callable, Any]] = None,
    write_images: bool = False,
    embed_images: bool = False,
    ignore_images: bool = False,
    ignore_graphics: bool = False,
    detect_bg_color: bool = True,
    image_path: str = "",
    image_format: str = "png",
    image_size_limit: float = 0.05,
    filename: Optional[str] = None,
    force_text: bool = True,
    page_chunks: bool = False,
    page_separators: bool = False,
    margins: Union[float, List[float]] = 0,
    dpi: int = 150,
    page_width: float = 612,
    page_height: Optional[float] = None,
    table_strategy: str = "lines_strict",
    graphics_limit: Optional[int] = None,
    fontsize_limit: int = 3,
    ignore_code: bool = False,
    extract_words: bool = False,
    show_progress: bool = False,
    use_glyphs: bool = False,
    ignore_alpha: bool = False,
    batch_size: int = 16,
    num_workers: Optional[int] = None,
) -> Any:
    """Process the document and return the text of the selected pages.

    Args:
        doc: pymupdf.Document or string.
        output_path: (str) path to the output markdown file.
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
        num_workers = cpu_count()

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
                text = f"[{span['text'].strip()}]({link['uri']})"
                return text
        return None

    def get_metadata(doc: "pymupdf.Document", pno: int) -> Dict[str, Any]:
        """Get document metadata for a specific page."""
        meta = doc.metadata.copy()
        meta["file_path"] = FILENAME
        meta["page_count"] = doc.page_count
        meta["page"] = pno + 1
        return meta

    def sort_words(
        words: List[Tuple[float, float, float, float, str, int]],
    ) -> List[Tuple[float, float, float, float, str, int]]:
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
    if page_chunks is True:
        document_output = []

    # Read the Table of Contents
    toc = doc.get_toc()

    if show_progress:
        print(f"Processing {FILENAME}...")
        iterable_pages = ProgressBar(pages) if not page_chunks else pages
    else:
        iterable_pages = pages

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

    # Unified processing logic for both sequential and parallel
    all_results = []
    use_parallel = len(pages) > 1 and not page_chunks

    if use_parallel:
        print(f"Using parallel processing for {len(pages)} pages...")
        doc_args = (margins, textflags, FILENAME, IGNORE_IMAGES, IGNORE_GRAPHICS)

        from functools import partial

        worker_func = partial(
            process_page_worker, doc_args=doc_args, page_separators=page_separators
        )

        with profiler.time_block("parallel_page_processing"):
            with concurrent.futures.ProcessPoolExecutor(
                max_workers=num_workers, initializer=init_worker, initargs=(FILENAME,)
            ) as executor:
                all_results = list(executor.map(worker_func, pages))
    else:
        print(f"Using sequential processing for {len(pages)} pages...")
        for pno in iterable_pages:
            try:
                parms = get_page_output(
                    doc,
                    pno,
                    margins,
                    textflags,
                    FILENAME,
                    IGNORE_IMAGES,
                    IGNORE_GRAPHICS,
                    page_separators,
                    defer_tables=True,
                )
                all_results.append((pno, parms))
            except Exception as e:
                print(f"Error processing page {pno}: {e}")
                all_results.append((pno, None))

    # Sort results by page number to ensure order is always correct
    all_results.sort(key=lambda x: x[0])

    # Process deferred tables sequentially to avoid race conditions, for both modes
    if (
        has_tables or not use_parallel
    ):  # Always run for sequential, or if tables found in parallel
        print("Processing tables sequentially...")
        all_results = process_deferred_tables(
            all_results, doc, extract_words=EXTRACT_WORDS
        )

    # Assemble final output
    if not page_chunks:
        try:
            with profiler.time_block("c_to_markdown_call"):
                if pages:
                    status = c_to_markdown(FILENAME, output_path)
                    if status != 0:
                        print(f"C to_markdown failed with status {status}")
        except Exception as e:
            print(f"Warning: Could not get full document markdown: {e}")
            with open(output_path, "w") as f:
                f.write("")

        python_part = ""
        with profiler.time_block("assemble_page_results"):
            for pno, parms in all_results:
                if parms is not None:
                    python_part += parms.md_string

        # Clean the final Python-generated markdown part
        python_part = cleanup_markdown_text(python_part)

        with open(output_path, "a", encoding="utf-8") as f:
            f.write("\n---TABLES---\n")
            f.write(python_part)
        return
    else:  # page_chunks is True
        for pno, parms in all_results:
            if parms:
                page_tocs = [t for t in toc if t[-1] == pno + 1]
                metadata = get_metadata(doc, pno)
                document_output.append(
                    {
                        "metadata": metadata,
                        "toc_items": page_tocs,
                        "tables": parms.tables,
                        "images": parms.images,
                        "graphics": getattr(parms, "graphics", []),
                        "text": parms.md_string,
                        "words": parms.words,
                    }
                )
            else:
                document_output.append(
                    {
                        "metadata": get_metadata(doc, pno),
                        "toc_items": [],
                        "tables": [],
                        "images": [],
                        "graphics": [],
                        "text": f"Error processing page {pno + 1}",
                        "words": [],
                    }
                )
        return document_output


def extract_images_on_page_simple(
    page: "pymupdf.Page", parms: "Parameters", image_size_limit: float
) -> List[Dict[str, Any]]:
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


def filter_small_images(
    page: "pymupdf.Page", parms: "Parameters", image_size_limit: float
) -> List[Dict[str, Any]]:
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


def extract_images_on_page_simple_drop(
    page: "pymupdf.Page", parms: "Parameters", image_size_limit: float
) -> List[Dict[str, Any]]:
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
        print(f"Usage:\npython {os.path.basename(__file__)} input.pdf output.md")
        sys.exit()

    t0 = time.perf_counter()  # start a timer

    doc = pymupdf.open(filename)  # open input file
    parms = sys.argv[2:]  # contains ["-pages", "PAGES"] or empty list
    pages = range(doc.page_count)  # default page range
    if len(parms) > 1 and parms[0] == "-pages":  # page sub-selection given
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

    print(
        f"Processing {len(pages)} pages with batch size 16 and parallel processing..."
    )

    # get the markdown string with performance improvements
    profiler.start_timer("total_to_markdown")
    to_markdown(
        doc,
        output_path=str(output_name),
        pages=pages,
        batch_size=16,  # Process 16 pages per batch
        num_workers=min(cpu_count(), 16),  # Limit to 16 workers max
    )
    profiler.end_timer("total_to_markdown")

    t1 = time.perf_counter()  # stop timer
    print(f"Markdown creation time for {doc.name=} {round(t1 - t0, 2)} sec.")

    # Print performance report
    profiler.print_report()
