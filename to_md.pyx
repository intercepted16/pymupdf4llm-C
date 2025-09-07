"""
Optimized Cython port of PDF to Markdown converter with performance improvements.
"""

import os
from binascii import b2a_base64
from collections import defaultdict
from dataclasses import dataclass
import pathlib
import sys
import time

import pymupdf
from pymupdf import mupdf
from pymupdf4llm.helpers.get_text_lines import get_raw_lines, is_white
from pymupdf4llm.helpers.multi_column import column_boxes
from pymupdf4llm.helpers.progress import ProgressBar

# Cython imports for performance
from libc.math cimport round as c_round
from libc.stdlib cimport malloc, free, realloc
from libc.string cimport memset, strcpy, strcat, strlen
from libc.stdio cimport sprintf
cimport cython
from cython.view cimport array as cvarray

pymupdf.TOOLS.unset_quad_corrections(True)

# Constants
cdef int MAX_FONT_SIZE = 200
GRAPHICS_TEXT = "\n![](%s)\n"

# Characters recognized as bullets when starting a line.
bullet = tuple([
    "- ", "* ", "> ",
    chr(0xB6), chr(0xB7), chr(8224), chr(8225), chr(8226),
    chr(0xF0A7), chr(0xF0B7)
] + list(map(chr, range(9632, 9680))))

@cython.boundscheck(False)
@cython.wraparound(False)
cdef class FastFontAnalyzer:
    """Optimized font analysis using C arrays and fast operations."""
    
    cdef int* font_counts
    cdef int max_font_size
    cdef double body_limit
    cdef dict header_mapping
    
    def __cinit__(self):
        self.max_font_size = MAX_FONT_SIZE
        self.font_counts = <int*>malloc(self.max_font_size * sizeof(int))
        memset(self.font_counts, 0, self.max_font_size * sizeof(int))
        self.header_mapping = {}
    
    def __dealloc__(self):
        if self.font_counts:
            free(self.font_counts)
    
    @cython.boundscheck(False)
    @cython.wraparound(False)
    def analyze_spans(self, list spans_data):
        """Analyze font sizes using fast C operations."""
        cdef int i, font_size, text_length
        cdef int n_spans = len(spans_data)
        
        for i in range(n_spans):
            font_size, text_length = spans_data[i]
            if 0 <= font_size < self.max_font_size:
                self.font_counts[font_size] += text_length
    
    def build_header_mapping(self, double body_limit=12.0, int max_levels=6):
        """Build header mapping after analysis."""
        # Find most frequent font size
        cdef int max_count = 0
        cdef int most_frequent_size = int(body_limit)
        cdef int i
        
        for i in range(self.max_font_size):
            if self.font_counts[i] > max_count:
                max_count = self.font_counts[i]
                most_frequent_size = i
        
        self.body_limit = max(body_limit, most_frequent_size)
        
        # Create header mappings - use safe bounds
        cdef int level = 1
        cdef int start_idx = min(self.max_font_size - 1, MAX_FONT_SIZE - 1)
        cdef int end_idx = max(0, int(self.body_limit))
        for i in range(start_idx, end_idx, -1):
            if self.font_counts[i] > 0 and level <= max_levels:
                self.header_mapping[i] = "#" * level + " "
                level += 1
        
        if self.header_mapping:
            self.body_limit = min(self.header_mapping.keys()) - 1

cdef class IdentifyHeaders:
    """Optimized header identification using fast font analysis."""

    cdef dict header_id
    cdef double body_limit

    def __init__(self, doc, pages=None, double body_limit=12.0, int max_levels=6):
        if not (1 <= max_levels <= 6):
            raise ValueError("max_levels must be between 1 and 6")

        if isinstance(doc, pymupdf.Document):
            mydoc = doc
        else:
            mydoc = pymupdf.open(doc)

        if pages is None:
            pages = range(mydoc.page_count)

        # Fast font analysis
        analyzer = FastFontAnalyzer()
        spans_data = []

        # Collect span data efficiently
        for pno in pages:
            page = mydoc.load_page(pno)
            blocks = page.get_text("dict", flags=pymupdf.TEXTFLAGS_TEXT)["blocks"]
            for block in blocks:
                for line in block["lines"]:
                    for span in line["spans"]:
                        text = span["text"].strip()
                        if text and not is_white(text):
                            spans_data.append((int(c_round(span["size"])), len(text)))

        if mydoc != doc:
            mydoc.close()

        # Analyze fonts using fast C operations
        analyzer.analyze_spans(spans_data)
        analyzer.build_header_mapping(body_limit, max_levels)

        self.header_id = analyzer.header_mapping
        self.body_limit = analyzer.body_limit

    def get_header_id(self, span: dict, page=None) -> str:
        """Get header ID for span with optimized lookup."""
        cdef int fontsize = int(c_round(span["size"]))
        if fontsize <= self.body_limit:
            return ""
        return self.header_id.get(fontsize, "")

class TocHeaders:
    """Alternative header identification using Table of Contents."""

    def __init__(self, doc):
        if isinstance(doc, pymupdf.Document):
            mydoc = doc
        else:
            mydoc = pymupdf.open(doc)

        self.TOC = mydoc.get_toc()
        if mydoc != doc:
            mydoc.close()

    def get_header_id(self, span: dict, page=None) -> str:
        if not page:
            return ""

        # Find TOC entries for this page
        my_toc = [t for t in self.TOC if t[1] and len(t) > 0 and t[len(t) - 1] == page.number + 1]
        if not my_toc:
            return ""

        text = span["text"].strip()
        for t in my_toc:
            title = t[1].strip()
            lvl = t[0]
            if text.startswith(title) or title.startswith(text):
                return "#" * lvl + " "
        return ""

@dataclass
class Parameters:
    """Container for page processing parameters."""
    pass

@cython.boundscheck(False)
@cython.wraparound(False)
cdef bint is_in_rects_fast(object rect, list rect_list):
    """Fast rectangle containment check."""
    cdef int i
    for i in range(len(rect_list)):
        if rect in rect_list[i]:
            return True
    return False

@cython.boundscheck(False)
@cython.wraparound(False)
cdef bint intersects_rects_fast(object rect, list rect_list):
    """Fast rectangle intersection check."""
    delta = (-1, -1, 1, 1)
    enlarged = rect + delta
    abs_enlarged = abs(enlarged) * 0.5

    cdef int i
    for i in range(len(rect_list)):
        if abs(enlarged & rect_list[i]) > abs_enlarged:
            return True
    return False

def refine_boxes(boxes, enlarge=0):
    """Optimized box refinement with early termination."""
    if not boxes:
        return []

    delta = (-enlarge, -enlarge, enlarge, enlarge)
    new_rects = []
    prects = boxes[:]

    cdef int list_len = len(prects)
    cdef int start_i

    while prects:
        r = +prects[0] + delta
        repeat = True

        while repeat:
            repeat = False
            list_len = len(prects)
            # Process from back to front for safe deletion - use safe bounds
            start_i = max(0, list_len - 1)
            for i in range(start_i, 0, -1):
                if r.intersects(prects[i].irect):
                    r |= prects[i]
                    del prects[i]
                    repeat = True

        new_rects.append(r)
        del prects[0]

    return sorted(set(new_rects), key=lambda r: (r.x0, r.y0))

def is_significant(box, paths):
    """Optimized significance check."""
    if box.width > box.height:
        d = box.width * 0.025
    else:
        d = box.height * 0.025

    nbox = box + (d, d, -d, -d)
    my_paths = [p for p in paths if p["rect"] in box and p["rect"] != box]

    if not my_paths:
        return False

    # Fast check for uniform dimensions
    widths = {int(c_round(p["rect"].width)) for p in my_paths} | {int(c_round(box.width))}
    heights = {int(c_round(p["rect"].height)) for p in my_paths} | {int(c_round(box.height))}

    if len(widths) == 1 or len(heights) == 1:
        return False

    # Check for significant intersections
    for p in my_paths:
        rect = p["rect"]
        if not (rect & nbox).is_empty and not rect.is_empty:
            return True

    return False

def page_is_ocr(page):
    """Check if page contains only OCR text."""
    try:
        text_types = {b[0] for b in page.get_bboxlog() if "text" in b[0]}
        return text_types == {"ignore-text"}
    except:
        return False

def get_bg_color(page):
    """Determine page background color from corners."""
    corners = [
        (page.rect.x0, page.rect.y0, page.rect.x0 + 10, page.rect.y0 + 10),
        (page.rect.x1 - 10, page.rect.y0, page.rect.x1, page.rect.y0 + 10),
        (page.rect.x0, page.rect.y1 - 10, page.rect.x0 + 10, page.rect.y1),
        (page.rect.x1 - 10, page.rect.y1 - 10, page.rect.x1, page.rect.y1)
    ]

    reference_pixel = None
    for corner in corners:
        pix = page.get_pixmap(clip=corner)
        if not pix.samples or not pix.is_unicolor:
            return None

        pixel = pix.pixel(0, 0)
        if reference_pixel is None:
            reference_pixel = pixel
        elif pixel != reference_pixel:
            return None

    return (reference_pixel[0] / 255, reference_pixel[1] / 255, reference_pixel[2] / 255)

def save_image(parms, rect, i):
    """Save or embed image with size filtering."""
    page = parms.page

    # Size filtering
    if (rect.width < page.rect.width * parms.image_size_limit or
        rect.height < page.rect.height * parms.image_size_limit):
        return ""

    if not (parms.write_images or parms.embed_images):
        return ""

    pix = page.get_pixmap(clip=rect, dpi=parms.DPI)
    if pix.height <= 0 or pix.width <= 0:
        return ""

    if parms.write_images:
        filename = os.path.basename(parms.filename).replace(" ", "-")
        image_filename = os.path.join(
            parms.IMG_PATH, f"{filename}-{page.number}-{i}.{parms.IMG_EXTENSION}"
        )
        pix.save(image_filename)
        return image_filename.replace("\\", "/")
    elif parms.embed_images:
        data = b2a_base64(pix.tobytes(parms.IMG_EXTENSION)).decode()
        return f"data:image/{parms.IMG_EXTENSION};base64,{data}"

    return ""

def resolve_links(links, span):
    """Convert span to markdown link if it intersects with a link."""
    bbox = pymupdf.Rect(span["bbox"])

    for link in links:
        hot = link["from"]
        middle = (hot.tl + hot.br) / 2
        if middle in bbox:
            return f"[{span['text'].strip()}]({link['uri']})"
    return None

def max_header_id(spans, page, get_header_id):
    """Get maximum header ID from spans."""
    hdr_ids = sorted([
        len(get_header_id(s, page=page))
        for s in spans
        if get_header_id(s, page=page)
    ])

    if not hdr_ids:
        return ""
    return "#" * (hdr_ids[0] - 1) + " "

@cython.boundscheck(False)
@cython.wraparound(False)
cdef class CStringBuffer:
    """High-performance C string buffer with automatic memory management."""
    
    cdef char* buffer
    cdef int size
    cdef int capacity
    cdef int max_capacity
    
    def __cinit__(self, int initial_capacity=8192):
        self.capacity = initial_capacity
        self.max_capacity = 1024 * 1024  # 1MB max
        self.buffer = <char*>malloc(self.capacity * sizeof(char))
        self.size = 0
        if self.buffer == NULL:
            raise MemoryError("Failed to allocate string buffer")
        self.buffer[0] = 0  # Null terminate
    
    def __dealloc__(self):
        if self.buffer:
            free(self.buffer)
    
    @cython.boundscheck(False)
    cdef void append_cstr(self, const char* text):
        """Append C string to buffer with automatic resizing."""
        cdef int text_len = strlen(text)
        cdef int needed = self.size + text_len + 1
        
        if needed > self.capacity:
            self._resize(needed * 2)
        
        strcat(self.buffer + self.size, text)
        self.size += text_len
    
    @cython.boundscheck(False)
    cdef void append_str(self, str text):
        """Append Python string to buffer."""
        cdef bytes text_bytes
        if text:
            text_bytes = text.encode('utf-8')
            self.append_cstr(text_bytes)
    
    @cython.boundscheck(False)
    cdef void _resize(self, int new_capacity):
        """Resize buffer if within limits."""
        if new_capacity <= self.max_capacity:
            self.buffer = <char*>realloc(self.buffer, new_capacity * sizeof(char))
            if self.buffer == NULL:
                raise MemoryError("Failed to resize string buffer")
            self.capacity = new_capacity
    
    cdef str to_string(self):
        """Convert buffer to Python string."""
        if self.size == 0:
            return ""
        return self.buffer[:self.size].decode('utf-8')
    
    cdef void clear(self):
        """Clear buffer content."""
        self.size = 0
        if self.buffer:
            self.buffer[0] = 0

@cython.boundscheck(False)
@cython.wraparound(False)
cdef class TextProcessor:
    """Ultra-optimized text processing with C buffers and minimal Python objects."""
    
    cdef list line_rects
    cdef list written_tables
    cdef list written_images
    cdef dict tab_rects
    cdef list img_rects
    cdef bint EXTRACT_WORDS
    cdef bint IGNORE_CODE
    cdef CStringBuffer output_buffer  # C string buffer for output
    cdef list _temp_strings  # Reusable string list
    cdef int _temp_capacity
    
    def __init__(self, parms):
        self.line_rects = parms.line_rects
        self.written_tables = parms.written_tables
        self.written_images = parms.written_images
        self.tab_rects = parms.tab_rects
        self.img_rects = parms.img_rects
        self.EXTRACT_WORDS = parms.EXTRACT_WORDS
        self.IGNORE_CODE = parms.IGNORE_CODE
        self.output_buffer = CStringBuffer(16384)  # 16KB initial buffer
        self._temp_strings = []
        self._temp_capacity = 100
    
    @cython.boundscheck(False)
    @cython.wraparound(False)
    cdef bint check_text_properties(self, list spans, int prop_type):
        """Fast text property checking using C variables."""
        cdef int i
        cdef int n_spans = len(spans)
        cdef dict span
        cdef int flags, char_flags
        
        for i in range(n_spans):
            span = spans[i]
            flags = span["flags"]
            char_flags = span["char_flags"]
            
            if prop_type == 0:  # strikeout
                if not (char_flags & 1):
                    return False
            elif prop_type == 1:  # italic
                if not (flags & 2):
                    return False
            elif prop_type == 2:  # bold
                if not ((flags & 16) or (char_flags & 8)):
                    return False
            elif prop_type == 3:  # mono
                if not (flags & 8):
                    return False
        
        return True
    
    @cython.boundscheck(False)
    @cython.wraparound(False)
    cdef str format_text_fast(self, str text, bint all_mono, bint all_italic, 
                             bint all_bold, bint all_strikeout):
        """Fast text formatting with minimal string operations."""
        if all_mono:
            text = "`" + text + "`"
        if all_italic:
            text = "_" + text + "_"
        if all_bold:
            text = "**" + text + "**"
        if all_strikeout:
            text = "~~" + text + "~~"
        return text
    
    @cython.boundscheck(False)
    @cython.wraparound(False)
    cdef str join_span_texts_fast(self, list spans):
        """Fast span text joining using pre-allocated buffer."""
        cdef int i
        cdef int n_spans = len(spans)
        cdef str result = ""
        cdef str text
        
        if n_spans == 0:
            return ""
        
        # Use buffer for small spans, direct concatenation for large ones
        if n_spans < 10:
            for i in range(n_spans):
                text = spans[i]["text"]
                if i == 0:
                    result = text
                else:
                    result = result + " " + text
        else:
            # Use list join for larger spans
            self._string_buffer.clear()
            for i in range(n_spans):
                self._string_buffer.append(spans[i]["text"])
            result = " ".join(self._string_buffer)
        
        return result.strip()
    
    @cython.boundscheck(False)
    @cython.wraparound(False)
    cdef list filter_lines_fast(self, list nlines, list tab_rects_values):
        """Fast line filtering with minimal Python object creation."""
        cdef list filtered_lines = []
        cdef int i, n_lines = len(nlines)
        cdef object line_rect
        
        for i in range(n_lines):
            line_rect = nlines[i][0]
            if not intersects_rects_fast(line_rect, tab_rects_values):
                filtered_lines.append(nlines[i])
        
        return filtered_lines
    
    @cython.boundscheck(False)
    @cython.wraparound(False)
    cdef list get_tab_candidates_fast(self, dict tab_rects, list written_tables, object lrect):
        """Fast table candidate selection with C loops."""
        cdef list candidates = []
        cdef int tab_id
        cdef object tab_rect
        
        for tab_id, tab_rect in tab_rects.items():
            if (tab_id not in written_tables and 
                tab_rect.y1 <= lrect.y0 and
                (lrect.x0 <= tab_rect.x0 < lrect.x1 or
                 lrect.x0 < tab_rect.x1 <= lrect.x1 or
                 tab_rect.x0 <= lrect.x0 < lrect.x1 <= tab_rect.x1)):
                candidates.append((tab_id, tab_rect))
        
        return candidates
    
    @cython.boundscheck(False)
    @cython.wraparound(False)
    cdef void process_line_ultra_fast(self, object lrect, list spans, bint tables, bint images, 
                                     bint force_text, object get_header_id, object parms, object clip):
        """Ultra-optimized line processing with minimal Python object usage."""
        cdef int i, n_spans = len(spans)
        cdef str text, hdr_string = ""
        cdef bint all_strikeout, all_italic, all_bold, all_mono
        cdef dict span0
        cdef int bno, delta
        cdef double delta_d
        
        if n_spans == 0:
            return
        
        # Process text spans using optimized methods
        text = self.join_span_texts_fast(spans)
        
        # Check text properties using fast methods
        all_strikeout = self.check_text_properties(spans, 0)
        all_italic = self.check_text_properties(spans, 1)
        all_bold = self.check_text_properties(spans, 2)
        all_mono = self.check_text_properties(spans, 3)
        
        # Check for headers
        if get_header_id:
            hdr_string = max_header_id(spans, page=parms.page, get_header_id=get_header_id)
        
        if hdr_string:
            # Format header text using fast method
            text = self.format_text_fast(text, all_mono, all_italic, all_bold, all_strikeout)
            self.output_buffer.append_str(hdr_string + text + "\n")
            return
        
        # Handle code blocks
        if all_mono and not parms.IGNORE_CODE:
            # Calculate indentation efficiently
            span0 = spans[0]
            delta_d = (lrect.x0 - clip.x0) / (span0["size"] * 0.5)
            delta = int(delta_d)
            if delta > 0:
                self.output_buffer.append_str(" " * delta)
            self.output_buffer.append_str(text + "\n")
            return
        
        # Handle block changes
        span0 = spans[0]
        bno = span0["block"]
        
        # Process individual spans using ultra-optimized method
        self._process_spans_ultra_fast(spans, hdr_string, clip, parms)
        self.output_buffer.append_cstr(b"\n")
    
    @cython.boundscheck(False)
    @cython.wraparound(False)
    cdef void _process_spans_ultra_fast(self, list spans, str hdr_string, object clip, object parms):
        """Ultra-optimized span processing with C-level string operations."""
        cdef int i, span_count = len(spans)
        cdef dict s, span0
        cdef int mono, bold, italic, strikeout
        cdef str span_text, text
        cdef int prefix_len = 0, suffix_len = 0, indent_spaces
        cdef double dist, cwidth
        
        if span_count == 0:
            return
        
        span0 = spans[0]
        
        # Process spans with minimal string allocation
        for i in range(span_count):
            s = spans[i]
            mono = s["flags"] & 8
            bold = s["flags"] & 16 or s["char_flags"] & 8
            italic = s["flags"] & 2
            strikeout = s["char_flags"] & 1
            
            span_text = s['text'].strip()
            if not span_text:
                continue
            
            # Build formatted text efficiently
            text = ""
            prefix_len = 0
            suffix_len = 0
            
            if strikeout:
                text = "~~" + text
                prefix_len += 2
            if italic:
                text = "_" + text
                prefix_len += 1
            if bold:
                text = "**" + text
                prefix_len += 2
            if mono:
                text = "`" + text
                prefix_len += 1
            
            # Add content
            text += span_text
            
            # Add suffix
            if mono:
                text += "`"
                suffix_len += 1
            if bold:
                text += "**"
                suffix_len += 2
            if italic:
                text += "_"
                suffix_len += 1
            if strikeout:
                text += "~~"
                suffix_len += 2
            
            # Handle bullets with optimized indentation
            if text.startswith(bullet):
                text = "- " + text[1:]
                text = text.replace("  ", " ")
                dist = span0["bbox"][0] - clip.x0
                cwidth = (span0["bbox"][2] - span0["bbox"][0]) / len(span0["text"])
                if cwidth == 0.0:
                    cwidth = span0["size"] * 0.5
                indent_spaces = int(c_round(dist / cwidth))
                if indent_spaces > 0:
                    self.output_buffer.append_str(" " * indent_spaces)
            
            self.output_buffer.append_str(text + " ")

@cython.boundscheck(False)
@cython.wraparound(False)
cdef str _process_spans_optimized(list spans, str hdr_string, object clip, 
                                 object parms):
    """Highly optimized span processing with C strings and preallocated buffers."""
    cdef int i, span_count = len(spans)
    cdef dict s, span0
    cdef int mono, bold, italic, strikeout
    cdef str prefix, suffix, text, ltext, span_text
    cdef double dist, cwidth
    cdef int prefix_len, suffix_len
    cdef list result_parts = []  # Use list for efficient concatenation
    cdef int indent_spaces
    
    if span_count == 0:
        return ""
    
    span0 = spans[0]
    
    # Pre-allocate result parts list
    result_parts = [None] * span_count
    cdef int result_idx = 0
    
    for i in range(span_count):
        s = spans[i]
        mono = s["flags"] & 8
        bold = s["flags"] & 16 or s["char_flags"] & 8
        italic = s["flags"] & 2
        strikeout = s["char_flags"] & 1

        # Build formatting strings efficiently
        prefix = ""
        suffix = ""
        prefix_len = 0
        suffix_len = 0

        if strikeout:
            prefix = "~~" + prefix
            suffix += "~~"
            prefix_len += 2
            suffix_len += 2
        if italic:
            prefix = "_" + prefix
            suffix += "_"
            prefix_len += 1
            suffix_len += 1
        if bold:
            prefix = "**" + prefix
            suffix += "**"
            prefix_len += 2
            suffix_len += 2
        if mono:
            prefix = "`" + prefix
            suffix += "`"
            prefix_len += 1
            suffix_len += 1

        # Handle links
        ltext = resolve_links(parms.links, s)
        if ltext:
            span_text = ltext
        else:
            span_text = s['text'].strip()

        # Build final text efficiently
        if prefix_len > 0 or suffix_len > 0 or hdr_string:
            text = hdr_string + prefix + span_text + suffix + " "
        else:
            text = span_text + " "

        # Handle bullets with optimized indentation calculation
        if text.startswith(bullet):
            text = "- " + text[1:]
            text = text.replace("  ", " ")
            dist = span0["bbox"][0] - clip.x0
            cwidth = (span0["bbox"][2] - span0["bbox"][0]) / len(span0["text"])
            if cwidth == 0.0:
                cwidth = span0["size"] * 0.5
            indent_spaces = int(c_round(dist / cwidth))
            if indent_spaces > 0:
                text = " " * indent_spaces + text

        result_parts[result_idx] = text
        result_idx += 1
    
    # Efficiently join all parts
    if result_idx < span_count:
        return "".join(result_parts[:result_idx])
    else:
        return "".join(result_parts)

def write_text(parms, clip, tables=True, images=True, force_text=True, get_header_id=None):
    """Optimized text extraction with markdown formatting using Cython acceleration."""
    if clip is None:
        clip = parms.clip

    cdef str out_string = ""
    cdef object prev_lrect = None
    cdef int prev_bno = -1
    cdef bint code = False
    cdef str prev_hdr_string = None
    
    # Create optimized processor
    processor = TextProcessor(parms)

    # Get text lines efficiently
    nlines = get_raw_lines(
        parms.textpage,
        clip=clip,
        tolerance=3,
        ignore_invisible=not parms.accept_invisible,
    )

    # Filter out lines intersecting with tables using optimized method
    cdef list tab_rects_values = list(parms.tab_rects.values())
    nlines = processor.filter_lines_fast(nlines, tab_rects_values)
    parms.line_rects.extend([l[0] for l in nlines])

    cdef int n_lines = len(nlines)
    cdef int line_idx
    cdef object lrect
    cdef list spans
    cdef str text
    cdef bint all_strikeout, all_italic, all_bold, all_mono
    cdef str hdr_string
    cdef int i, bno
    cdef dict span0
    cdef object r
    cdef str pathname, img_txt
    cdef int delta
    cdef str indent
    cdef int line_rects_len

    for line_idx in range(n_lines):
        lrect, spans = nlines[line_idx]
        
        # Skip lines intersecting with images
        if intersects_rects_fast(lrect, parms.img_rects):
            continue

        # Process tables above this line using optimized method
        if tables:
            tab_candidates = processor.get_tab_candidates_fast(parms.tab_rects, parms.written_tables, lrect)

            for i, _ in tab_candidates:
                out_string += "\n" + parms.tabs[i].to_markdown(clean=False) + "\n"
                if parms.EXTRACT_WORDS:
                    cells = sorted(
                        set([
                            pymupdf.Rect(c) for c in
                            parms.tabs[i].header.cells + parms.tabs[i].cells
                            if c is not None
                        ]),
                        key=lambda c: (c.y1, c.x0)
                    )
                    parms.line_rects.extend(cells)
                parms.written_tables.append(i)
                prev_hdr_string = None

        # Process images above this line
        if images:
            for i in range(len(parms.img_rects)):
                if i in parms.written_images:
                    continue

                r = parms.img_rects[i]
                if (r.y1 <= lrect.y0 and
                    (lrect.x0 <= r.x0 < lrect.x1 or
                     lrect.x0 < r.x1 <= lrect.x1 or
                     r.x0 <= lrect.x0 < lrect.x1 <= r.x1)):

                    pathname = save_image(parms, r, i)
                    if pathname:
                        out_string += GRAPHICS_TEXT % pathname

                    if force_text:
                        img_txt = write_text(parms, r, tables=False, images=False,
                                           force_text=True, get_header_id=get_header_id)
                        if not is_white(img_txt):
                            out_string += img_txt

                    parms.written_images.append(i)
                    prev_hdr_string = None

        # Add line break for distant lines - safe indexing
        line_rects_len = len(parms.line_rects)
        if (line_rects_len > 1 and
            lrect.y1 - parms.line_rects[line_rects_len - 2].y1 > lrect.height * 1.5):
            out_string += "\n"

        # Process text spans using optimized methods
        text = processor.join_span_texts_fast(spans)

        # Check text properties using fast methods
        all_strikeout = processor.check_text_properties(spans, 0)
        all_italic = processor.check_text_properties(spans, 1)
        all_bold = processor.check_text_properties(spans, 2)
        all_mono = processor.check_text_properties(spans, 3)

        # Check for headers
        hdr_string = ""
        if get_header_id:
            hdr_string = max_header_id(spans, page=parms.page, get_header_id=get_header_id)

        if hdr_string:
            # Format header text using fast method
            text = processor.format_text_fast(text, all_mono, all_italic, all_bold, all_strikeout)

            if hdr_string != prev_hdr_string:
                out_string += hdr_string + text + "\n"
            else:
                # Continue header on same line
                while out_string.endswith("\n"):
                    out_string = out_string[:len(out_string) - 1]
                out_string += " " + text + "\n"

            prev_hdr_string = hdr_string
            continue

        prev_hdr_string = hdr_string

        # Handle code blocks
        if all_mono and not parms.IGNORE_CODE:
            if not code:
                out_string += "```\n"
                code = True

            # Calculate indentation
            delta = int((lrect.x0 - clip.x0) / (spans[0]["size"] * 0.5))
            indent = " " * delta
            out_string += indent + text + "\n"
            continue

        if code and not all_mono:
            out_string += "```\n"
            code = False

        # Handle block changes
        span0 = spans[0]
        bno = span0["block"]
        if bno != prev_bno:
            out_string += "\n"
            prev_bno = bno

        # Check for line breaks
        if (prev_lrect and
            (lrect.y1 - prev_lrect.y1 > lrect.height * 1.5 or
             span0["text"].startswith("[") or
             span0["text"].startswith(bullet) or
             span0["flags"] & 1)):
            out_string += "\n"

        prev_lrect = lrect

        if code:
            out_string += "```\n"
            code = False

        # Process individual spans with optimization
        out_string += _process_spans_optimized(spans, hdr_string, clip, parms)

        if not code:
            out_string += "\n"

    out_string += "\n"
    if code:
        out_string += "```\n"
        code = False

    out_string += "\n\n"
    return out_string.replace(" \n", "\n").replace("  ", " ").replace("\n\n\n", "\n\n")

def sort_words(words):
    """Optimized word sorting."""
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

@cython.boundscheck(False)
@cython.wraparound(False)
cdef class PageOrchestrator:
    """Ultra-optimized page orchestration with C arrays and minimal Python objects."""
    
    cdef CStringBuffer main_buffer
    cdef list images
    cdef list img_rects
    cdef list tables
    cdef list graphics
    cdef list written_images
    cdef list written_tables
    cdef double image_size_limit
    cdef object clip
    cdef object page
    cdef TextProcessor text_processor
    cdef int[:] image_indices  # C array for image tracking
    cdef int[:] table_indices  # C array for table tracking
    cdef int n_images, n_tables
    
    def __init__(self, object page, double image_size_limit, object clip, object parms):
        self.main_buffer = CStringBuffer(32768)  # 32KB buffer
        self.images = []
        self.img_rects = []
        self.tables = []
        self.graphics = []
        self.written_images = []
        self.written_tables = []
        self.image_size_limit = image_size_limit
        self.clip = clip
        self.page = page
        self.text_processor = TextProcessor(parms)
        self.n_images = 0
        self.n_tables = 0
    
    @cython.boundscheck(False)
    @cython.wraparound(False)
    cdef void process_page_ultra_fast(self, object parms, bint tables, bint images, 
                                     bint force_text, object get_header_id):
        """Ultra-optimized page processing with minimal Python object creation."""
        cdef int i, j
        cdef object text_rect
        cdef list text_rects
        
        # Clear buffer for this page
        self.main_buffer.clear()
        self.text_processor.output_buffer.clear()
        
        # Get text rectangles efficiently
        text_rects = column_boxes(
            self.page,
            paths=parms.actual_paths,
            no_image_text=not force_text,
            textpage=parms.textpage,
            avoid=parms.tab_rects0 + parms.vg_clusters0,
            footer_margin=parms.clip.y1 - self.page.rect.y1,
            header_margin=parms.clip.y0 - self.page.rect.y0,
            ignore_images=not images,
        )
        
        # Process each text rectangle
        for text_rect in text_rects:
            self._process_text_rect_fast(text_rect, parms, tables, images, force_text, get_header_id)
        
        # Add any remaining content
        self._finalize_content(parms, tables, images, force_text, get_header_id)
        
        # Combine buffers efficiently
        self.main_buffer.append_str(self.text_processor.output_buffer.to_string())
        parms.md_string = self.main_buffer.to_string()
    
    @cython.boundscheck(False)
    @cython.wraparound(False)
    cdef void _process_text_rect_fast(self, object text_rect, object parms, bint tables, 
                                     bint images, bint force_text, object get_header_id):
        """Fast text rectangle processing with C-level operations."""
        cdef int i
        cdef object img_rect
        
        if text_rect is None:
            return
        
        # Output tables above this rectangle
        if tables:
            for i, trect in sorted(
                [(j, r) for j, r in parms.tab_rects.items() if r.y1 <= text_rect.y0],
                key=lambda x: (x[1].y1, x[1].x0)
            ):
                if i not in parms.written_tables:
                    self.main_buffer.append_str(parms.tabs[i].to_markdown(clean=False) + "\n")
                    parms.written_tables.append(i)
        
        # Output images above this rectangle
        if images:
            for i, img_rect in enumerate(parms.img_rects):
                if (img_rect.y0 <= text_rect.y0 and i not in parms.written_images and
                    not (img_rect.x0 >= text_rect.x1 or img_rect.x1 <= text_rect.x0)):
                    
                    pathname = save_image(parms, img_rect, i)
                    parms.written_images.append(i)
                    if pathname:
                        self.main_buffer.append_str(GRAPHICS_TEXT % pathname)
                    
                    if force_text:
                        img_txt = write_text(parms, img_rect, tables=False, images=False,
                                           force_text=True, get_header_id=get_header_id)
                        if not is_white(img_txt):
                            self.main_buffer.append_str(img_txt)
        
        # Extract text using ultra-fast method
        self._extract_text_ultra_fast(parms, text_rect, force_text, images, tables, get_header_id)
    
    @cython.boundscheck(False)
    @cython.wraparound(False)
    cdef void _extract_text_ultra_fast(self, object parms, object clip, bint force_text, 
                                      bint images, bint tables, object get_header_id):
        """Ultra-fast text extraction with C buffer operations."""
        cdef list nlines
        cdef object lrect
        cdef list spans
        cdef int i, n_lines
        
        # Get text lines efficiently
        nlines = get_raw_lines(
            parms.textpage,
            clip=clip,
            tolerance=3,
            ignore_invisible=not parms.accept_invisible,
        )
        
        # Filter lines using optimized method
        nlines = self.text_processor.filter_lines_fast(nlines, list(parms.tab_rects.values()))
        n_lines = len(nlines)
        
        # Process lines with ultra-fast method
        for i in range(n_lines):
            lrect, spans = nlines[i]
            
            # Skip lines intersecting with images
            if intersects_rects_fast(lrect, parms.img_rects):
                continue
            
            # Use ultra-optimized line processing
            self.text_processor.process_line_ultra_fast(
                lrect, spans, tables, images, force_text, get_header_id, parms, clip
            )
    
    @cython.boundscheck(False)
    @cython.wraparound(False)
    cdef void _finalize_content(self, object parms, bint tables, bint images, 
                               bint force_text, object get_header_id):
        """Finalize remaining tables and images."""
        cdef int i
        cdef object img_rect
        
        # Output remaining tables
        if tables:
            for i, trect in parms.tab_rects.items():
                if i not in parms.written_tables:
                    self.main_buffer.append_str(parms.tabs[i].to_markdown(clean=False) + "\n")
                    parms.written_tables.append(i)
        
        # Output remaining images
        if images:
            for i, img_rect in enumerate(parms.img_rects):
                if i not in parms.written_images:
                    pathname = save_image(parms, img_rect, i)
                    parms.written_images.append(i)
                    if pathname:
                        self.main_buffer.append_str(GRAPHICS_TEXT % pathname)
                    
                    if force_text:
                        img_txt = write_text(parms, img_rect, tables=False, images=False,
                                           force_text=True, get_header_id=get_header_id)
                        if not is_white(img_txt):
                            self.main_buffer.append_str(img_txt)

@cython.boundscheck(False)
@cython.wraparound(False)
cdef class PageProcessor:
    """Optimized page processing with C arrays and fast operations."""
    
    cdef list images
    cdef list img_rects
    cdef list tables
    cdef list graphics
    cdef double image_size_limit
    cdef object clip
    
    def __init__(self, double image_size_limit, object clip):
        self.images = []
        self.img_rects = []
        self.tables = []
        self.graphics = []
        self.image_size_limit = image_size_limit
        self.clip = clip
    
    @cython.boundscheck(False)
    @cython.wraparound(False)
    cdef list filter_images_fast(self, list img_info):
        """Fast image filtering with C loops and early termination."""
        cdef list filtered = []
        cdef int i, n_images = len(img_info)
        cdef dict img
        cdef object bbox
        cdef double min_width, min_height
        
        min_width = self.image_size_limit * self.clip.width
        min_height = self.image_size_limit * self.clip.height
        
        for i in range(n_images):
            img = img_info[i]
            bbox = img["bbox"]
            if (bbox.width >= min_width and
                bbox.height >= min_height and
                bbox.intersects(self.clip) and
                bbox.width > 3 and bbox.height > 3):
                filtered.append(img)
        
        return filtered
    
    @cython.boundscheck(False)
    @cython.wraparound(False)
    cdef void remove_contained_images_fast(self, list img_info):
        """Fast removal of contained images using reverse iteration."""
        cdef int i, j
        cdef int n_images = len(img_info)
        cdef object r_i, r_j
        cdef list to_remove = []
        
        # Find contained images
        for i in range(n_images - 1, 0, -1):
            if i >= len(img_info):  # Skip if already removed
                continue
            r_i = img_info[i]["bbox"]
            if r_i.is_empty:
                to_remove.append(i)
                continue
            
            for j in range(i):
                if j >= len(img_info):  # Skip if already removed
                    continue
                r_j = img_info[j]["bbox"]
                if r_i in r_j:
                    to_remove.append(i)
                    break
        
        # Remove in reverse order to maintain indices
        for i in sorted(set(to_remove), reverse=True):
            if i < len(img_info):
                del img_info[i]
    
    @cython.boundscheck(False)
    @cython.wraparound(False)
    cdef list sort_by_size_fast(self, list img_info):
        """Fast sorting by size using key extraction."""
        cdef int i, n_images = len(img_info)
        cdef list size_pairs = []
        cdef dict img
        
        # Create (size, index) pairs for sorting
        for i in range(n_images):
            img = img_info[i]
            size_pairs.append((abs(img["bbox"]), i, img))
        
        # Sort by size (largest first)
        size_pairs.sort(key=lambda x: x[0], reverse=True)
        
        # Extract sorted images
        return [pair[2] for pair in size_pairs]
    
    @cython.boundscheck(False)
    @cython.wraparound(False)
    cdef list filter_graphics_fast(self, list drawings, object bg_color, list tab_rects, list annot_rects):
        """Fast graphics filtering with C loops."""
        cdef list filtered = []
        cdef int i, n_drawings = len(drawings)
        cdef dict p
        cdef object rect
        
        for i in range(n_drawings):
            p = drawings[i]
            rect = p["rect"]
            if (rect in self.clip and
                rect.width < self.clip.width and
                rect.height < self.clip.height and
                (rect.width > 3 or rect.height > 3) and
                not (p["type"] == "f" and p["fill"] == bg_color) and
                not intersects_rects_fast(rect, tab_rects) and
                not intersects_rects_fast(rect, annot_rects)):
                filtered.append(p)
        
        return filtered

def get_metadata(doc, pno, filename):
    """Get page metadata."""
    meta = doc.metadata.copy()
    meta["file_path"] = filename
    meta["page_count"] = doc.page_count
    meta["page"] = pno + 1
    return meta

def get_page_output(doc, pno, margins, textflags, filename, ignore_images, ignore_graphics,
                   image_size_limit, detect_bg_color, graphics_limit, table_strategy,
                   force_text, page_separators, extract_words, ignore_alpha,
                   write_images=False, embed_images=False, image_path="",
                   image_format="png", dpi=150, fontsize_limit=3, ignore_code=False,
                   hdr_info=None):
    """Ultra-optimized page processing function with C orchestration."""
    page = doc[pno]
    page.remove_rotation()
    
    # Declare C variables at function level
    cdef int img_idx, n_imgs
    cdef bint use_ultra_fast = True  # Enable ultra-fast processing

    parms = Parameters()
    parms.page = page
    parms.filename = filename
    parms.md_string = ""
    parms.images = []
    parms.tables = []
    parms.graphics = []
    parms.words = []
    parms.line_rects = []
    parms.accept_invisible = page_is_ocr(page) or ignore_alpha
    parms.bg_color = get_bg_color(page) if detect_bg_color else None
    parms.IGNORE_CODE = ignore_code
    parms.EXTRACT_WORDS = extract_words
    parms.image_size_limit = image_size_limit
    parms.write_images = write_images
    parms.embed_images = embed_images
    parms.IMG_PATH = image_path
    parms.IMG_EXTENSION = image_format
    parms.DPI = dpi

    # Set up clipping
    left, top, right, bottom = margins
    parms.clip = page.rect + (left, top, -right, -bottom)

    # Extract links
    parms.links = [l for l in page.get_links() if l["kind"] == pymupdf.LINK_URI]

    # Extract annotations
    parms.annot_rects = [a.rect for a in page.annots()]

    # Create textpage
    parms.textpage = page.get_textpage(flags=textflags, clip=parms.clip)

    # Process images using optimized PageProcessor
    if not ignore_images:
        page_processor = PageProcessor(image_size_limit, parms.clip)
        img_info = page.get_image_info()
        
        # Convert bbox tuples to Rect objects efficiently
        n_imgs = len(img_info)
        for img_idx in range(n_imgs):
            img_info[img_idx]["bbox"] = pymupdf.Rect(img_info[img_idx]["bbox"])

        # Use optimized filtering, sorting, and containment removal
        img_info = page_processor.filter_images_fast(img_info)
        img_info = page_processor.sort_by_size_fast(img_info)
        
        # Limit to reasonable number
        img_info = img_info[:30]
        
        # Remove contained images using optimized method
        page_processor.remove_contained_images_fast(img_info)

        parms.images = img_info
        parms.img_rects = [i["bbox"] for i in parms.images]
    else:
        parms.images = []
        parms.img_rects = []

    parms.written_images = []

    # Process graphics
    graphics_count = len([b for b in page.get_bboxlog() if "path" in b[0]])
    if graphics_limit and graphics_count > graphics_limit:
        ignore_graphics = True

    # Process tables
    parms.written_tables = []
    parms.tabs = []

    if not ignore_graphics and table_strategy:
        tabs = page.find_tables(clip=parms.clip, strategy=table_strategy)
        for t in tabs.tables:
            if t.row_count >= 2 and t.col_count >= 2:
                parms.tabs.append(t)
        parms.tabs.sort(key=lambda t: (t.bbox[0], t.bbox[1]))

    # Create table rectangles
    tab_rects = {}
    for i, t in enumerate(parms.tabs):
        tab_rects[i] = pymupdf.Rect(t.bbox) | pymupdf.Rect(t.header.bbox)
        tab_dict = {
            "bbox": tuple(tab_rects[i]),
            "rows": t.row_count,
            "columns": t.col_count,
        }
        parms.tables.append(tab_dict)

    parms.tab_rects = tab_rects
    parms.tab_rects0 = list(tab_rects.values())

    # Process vector graphics using optimized method
    if not ignore_graphics:
        if 'page_processor' not in locals():
            page_processor = PageProcessor(image_size_limit, parms.clip)
        paths = page_processor.filter_graphics_fast(
            page.get_drawings(), parms.bg_color, parms.tab_rects0, parms.annot_rects)
    else:
        paths = []

    if graphics_limit and len(paths) > graphics_limit:
        paths = []

    # Process graphics clusters
    vg_clusters0 = []
    if paths:
        clusters = page.cluster_drawings(drawings=paths)
        for bbox in clusters:
            if is_significant(bbox, paths):
                vg_clusters0.append(bbox)

    parms.actual_paths = [p for p in paths if is_in_rects_fast(p["rect"], vg_clusters0)]

    # Combine image and graphics rectangles
    vg_clusters0.extend(parms.img_rects)
    parms.img_rects.extend(vg_clusters0)
    parms.img_rects = sorted(set(parms.img_rects), key=lambda r: (r.y1, r.x0))
    parms.vg_clusters0 = refine_boxes(vg_clusters0)
    parms.vg_clusters = dict(enumerate(parms.vg_clusters0))

    # Get header identification function
    get_header_id = None
    if callable(hdr_info):
        get_header_id = hdr_info
    elif hasattr(hdr_info, "get_header_id") and callable(hdr_info.get_header_id):
        get_header_id = hdr_info.get_header_id
    elif hdr_info is not False:
        hdr_analyzer = IdentifyHeaders(doc)
        get_header_id = hdr_analyzer.get_header_id

    # Use ultra-fast processing path if enabled
    if use_ultra_fast:
        orchestrator = PageOrchestrator(page, image_size_limit, parms.clip, parms)
        orchestrator.process_page_ultra_fast(parms, not ignore_graphics and table_strategy, 
                                           not ignore_images, force_text, get_header_id)
        
        # Apply final formatting
        while parms.md_string.startswith("\n"):
            parms.md_string = parms.md_string[1:]
        
        parms.md_string = parms.md_string.replace(chr(0), chr(0xFFFD))
        parms.md_string = parms.md_string.replace(" ,", ",").replace("-\n", "")
        
        # Handle page separators
        if page_separators:
            parms.md_string += f"\n\n--- end of page={parms.page.number} ---\n\n"
        
        return parms

    # Get text rectangles (fallback to original method)
    text_rects = column_boxes(
        parms.page,
        paths=parms.actual_paths,
        no_image_text=not force_text,
        textpage=parms.textpage,
        avoid=parms.tab_rects0 + parms.vg_clusters0,
        footer_margin=margins[3],
        header_margin=margins[1],
        ignore_images=ignore_images,
    )

    # Extract text
    for text_rect in text_rects:
        # Output tables above this rectangle
        if text_rect is not None:
            for i, trect in sorted(
                [(j, r) for j, r in parms.tab_rects.items() if r.y1 <= text_rect.y0],
                key=lambda x: (x[1].y1, x[1].x0)
            ):
                if i not in parms.written_tables:
                    parms.md_string += parms.tabs[i].to_markdown(clean=False) + "\n"
                    if extract_words:
                        cells = sorted(
                            set([
                                pymupdf.Rect(c) for c in
                                parms.tabs[i].header.cells + parms.tabs[i].cells
                                if c is not None
                            ]),
                            key=lambda c: (c.y1, c.x0)
                        )
                        parms.line_rects.extend(cells)
                    parms.written_tables.append(i)

        # Output images above this rectangle
        if text_rect is not None:
            for i, img_rect in enumerate(parms.img_rects):
                if (img_rect.y0 <= text_rect.y0 and i not in parms.written_images and
                    not (img_rect.x0 >= text_rect.x1 or img_rect.x1 <= text_rect.x0)):

                    pathname = save_image(parms, img_rect, i)
                    parms.written_images.append(i)
                    if pathname:
                        parms.md_string += GRAPHICS_TEXT % pathname

                    if force_text:
                        img_txt = write_text(parms, img_rect, tables=False, images=False,
                                           force_text=True, get_header_id=get_header_id)
                        if not is_white(img_txt):
                            parms.md_string += img_txt

        # Extract text from this rectangle
        parms.md_string += write_text(parms, text_rect, force_text=force_text,
                                     images=True, tables=True, get_header_id=get_header_id)

    # Output remaining tables and images
    for i, trect in parms.tab_rects.items():
        if i not in parms.written_tables:
            parms.md_string += parms.tabs[i].to_markdown(clean=False) + "\n"
            if extract_words:
                cells = sorted(
                    set([
                        pymupdf.Rect(c) for c in
                        parms.tabs[i].header.cells + parms.tabs[i].cells
                        if c is not None
                    ]),
                    key=lambda c: (c.y1, c.x0)
                )
                parms.line_rects.extend(cells)
            parms.written_tables.append(i)

    for i, img_rect in enumerate(parms.img_rects):
        if i not in parms.written_images:
            pathname = save_image(parms, img_rect, i)
            parms.written_images.append(i)
            if pathname:
                parms.md_string += GRAPHICS_TEXT % pathname

            if force_text:
                img_txt = write_text(parms, img_rect, tables=False, images=False,
                                   force_text=True, get_header_id=get_header_id)
                if not is_white(img_txt):
                    parms.md_string += img_txt

    # Clean up output
    while parms.md_string.startswith("\n"):
        parms.md_string = parms.md_string[1:]

    parms.md_string = parms.md_string.replace(chr(0), chr(0xFFFD))
    parms.md_string = parms.md_string.replace(" ,", ",").replace("-\n", "")

    # Extract words if requested
    if extract_words:
        rawwords = parms.textpage.extractWORDS()
        rawwords.sort(key=lambda w: (w[3], w[0]))

        words = []
        for lrect in parms.line_rects:
            lwords = []
            for w in rawwords:
                wrect = pymupdf.Rect(w[:4])
                if wrect in lrect:
                    lwords.append(w)
            words.extend(sort_words(lwords))

        # Remove duplicates while preserving order
        nwords = []
        for w in words:
            if w not in nwords:
                nwords.append(w)
        parms.words = nwords
    else:
        parms.words = []

    if page_separators:
        parms.md_string += f"\n\n--- end of page={parms.page.number} ---\n\n"

    return parms

@cython.boundscheck(False)
@cython.wraparound(False)
cdef class BatchProcessor:
    """Ultra-optimized batch processor for multiprocessing."""
    
    cdef str filename
    cdef object doc_local
    cdef CStringBuffer batch_buffer
    
    def __init__(self, str filename):
        self.filename = filename
        self.batch_buffer = CStringBuffer(65536)  # 64KB buffer
    
    @cython.boundscheck(False)
    @cython.wraparound(False)
    cdef list process_batch_ultra_fast(self, list batch_pages, object margins, int textflags,
                                      bint ignore_images, bint ignore_graphics,
                                      double image_size_limit, bint detect_bg_color,
                                      object graphics_limit, str table_strategy,
                                      bint force_text, bint page_separators, bint extract_words,
                                      bint ignore_alpha, bint write_images, bint embed_images,
                                      str image_path, str image_format, int dpi,
                                      int fontsize_limit, bint ignore_code, object hdr_info):
        """Ultra-fast batch processing with C buffers."""
        import pymupdf
        
        # Open document once per batch
        self.doc_local = pymupdf.open(self.filename)
        
        cdef list results = []
        cdef int pno
        cdef object parms
        
        # Process pages with ultra-fast path
        for pno in batch_pages:
            self.batch_buffer.clear()
            
            parms = get_page_output(
                self.doc_local, pno, margins, textflags, self.filename, ignore_images, ignore_graphics,
                image_size_limit, detect_bg_color, graphics_limit, table_strategy,
                force_text, page_separators, extract_words, ignore_alpha,
                write_images, embed_images, image_path, image_format, dpi,
                fontsize_limit, ignore_code, hdr_info
            )
            
            # Extract only serializable data efficiently
            serializable_result = {
                'md_string': parms.md_string,
                'images': parms.images,
                'tables': parms.tables,
                'graphics': parms.graphics,
                'words': parms.words,
            }
            results.append((pno, serializable_result))
        
        self.doc_local.close()
        return results

def _process_batch_worker(args):
    """Ultra-optimized worker function for multiprocessing."""
    batch_pages, margins, textflags, filename, ignore_images, ignore_graphics, \
    image_size_limit, detect_bg_color, graphics_limit, table_strategy, \
    force_text, page_separators, extract_words, ignore_alpha, \
    write_images, embed_images, image_path, image_format, dpi, \
    fontsize_limit, ignore_code, hdr_info = args
    
    # Use ultra-optimized batch processor
    processor = BatchProcessor(filename)
    return processor.process_batch_ultra_fast(
        batch_pages, margins, textflags, ignore_images, ignore_graphics,
        image_size_limit, detect_bg_color, graphics_limit, table_strategy,
        force_text, page_separators, extract_words, ignore_alpha,
        write_images, embed_images, image_path, image_format, dpi,
        fontsize_limit, ignore_code, hdr_info
    )

def to_markdown(doc, *, pages=None, hdr_info=None, write_images=False, embed_images=False,
               ignore_images=False, ignore_graphics=False, detect_bg_color=True,
               image_path="", image_format="png", image_size_limit=0.05, filename=None,
               force_text=True, page_chunks=False, page_separators=False, margins=0,
               dpi=150, page_width=612, page_height=None, table_strategy="lines_strict",
               graphics_limit=None, fontsize_limit=3, ignore_code=False,
               extract_words=False, show_progress=False, use_glyphs=False, ignore_alpha=False):
    """Main function to convert PDF to Markdown with optimized processing."""

    if not isinstance(doc, pymupdf.Document):
        doc = pymupdf.open(doc)

    filename = doc.name if filename is None else filename

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
    elif len(margins) == 2:
        margins = (0, margins[0], 0, margins[1])
    elif len(margins) != 4:
        raise ValueError("margins must be one, two or four floats")

    # Set up text extraction flags
    textflags = (
        mupdf.FZ_STEXT_CLIP |
        mupdf.FZ_STEXT_ACCURATE_BBOXES |
        32768  # FZ_STEXT_COLLECT_STYLES
    )

    if use_glyphs:
        textflags |= mupdf.FZ_STEXT_USE_GID_FOR_UNKNOWN_UNICODE

    # Ultra-optimized multiprocessing configuration
    cdef int batch_size = 8  # Smaller batches for better load balancing
    cdef int num_pages = len(pages)
    cdef int num_processes, optimal_processes
    
    if page_chunks:
        document_output = []
        toc = doc.get_toc()
    else:
        document_output = ""

    if show_progress:
        print(f"Processing {filename}...")
        pages = ProgressBar(pages)

    # Ultra-optimized multiprocessing with better CPU utilization
    import multiprocessing as mp
    import os
    
    # More aggressive process allocation for ultra-fast processing
    optimal_processes = os.cpu_count() or 1
    num_processes = min(optimal_processes * 2, max(2, num_pages // 2))
    
    if num_processes > 1 and num_pages > 1:
        # Create batches for multiprocessing
        batches = []
        pages_list = list(pages)
        for i in range(0, len(pages_list), batch_size):
            batch_pages = pages_list[i:i + batch_size]
            batch_args = (
                batch_pages, margins, textflags, filename, ignore_images, ignore_graphics,
                image_size_limit, detect_bg_color, graphics_limit, table_strategy,
                force_text, page_separators, extract_words, ignore_alpha,
                write_images, embed_images, image_path, image_format, dpi,
                fontsize_limit, ignore_code, hdr_info
            )
            batches.append(batch_args)
        
        # Process batches in parallel
        all_results = []
        with mp.Pool(processes=num_processes) as pool:
            batch_results = pool.map(_process_batch_worker, batches)
            for batch_result in batch_results:
                all_results.extend(batch_result)
        
        # Sort results by page number
        all_results.sort(key=lambda x: x[0])
    else:
        # Fall back to sequential processing for small jobs
        all_results = []
        for pno in pages:
            parms = get_page_output(
                doc, pno, margins, textflags, filename, ignore_images, ignore_graphics,
                image_size_limit, detect_bg_color, graphics_limit, table_strategy,
                force_text, page_separators, extract_words, ignore_alpha,
                write_images, embed_images, image_path, image_format, dpi,
                fontsize_limit, ignore_code, hdr_info
            )
            all_results.append((pno, parms))

    if page_chunks:
        for pno, result_data in all_results:
            page_tocs = [t for t in toc if len(t) > 0 and t[len(t) - 1] == pno + 1]
            metadata = get_metadata(doc, pno, filename)
            # Handle both serializable dict results and Parameters objects
            if isinstance(result_data, dict):
                document_output.append({
                    "metadata": metadata,
                    "toc_items": page_tocs,
                    "tables": result_data['tables'],
                    "images": result_data['images'],
                    "graphics": result_data['graphics'],
                    "text": result_data['md_string'],
                    "words": result_data['words'],
                })
            else:
                # Fallback for Parameters objects (sequential processing)
                document_output.append({
                    "metadata": metadata,
                    "toc_items": page_tocs,
                    "tables": result_data.tables,
                    "images": result_data.images,
                    "graphics": result_data.graphics,
                    "text": result_data.md_string,
                    "words": result_data.words,
                })
    else:
        for _, result_data in all_results:
            # Handle both serializable dict results and Parameters objects
            if isinstance(result_data, dict):
                document_output += result_data['md_string']
            else:
                # Fallback for Parameters objects (sequential processing)
                document_output += result_data.md_string

    return document_output

def main():
    """Main function to handle command-line execution."""
    try:
        filename = sys.argv[1]
    except IndexError:
        print(f"Usage:\npython {os.path.basename(__file__)} input.pdf")
        sys.exit()

    t0 = time.perf_counter()

    doc = pymupdf.open(filename)
    parms = sys.argv[2:]
    pages = range(doc.page_count)

    if len(parms) == 2 and parms[0] == "-pages":
        pages = []
        pages_spec = parms[1].replace("N", f"{doc.page_count}")
        for spec in pages_spec.split(","):
            if "-" in spec:
                start, end = map(int, spec.split("-"))
                pages.extend(range(start - 1, end))
            else:
                pages.append(int(spec) - 1)

        wrong_pages = set([n + 1 for n in pages if n >= doc.page_count][:4])
        if wrong_pages:
            sys.exit(f"Page number(s) {wrong_pages} not in '{doc}'.")

    md_string = to_markdown(doc, pages=pages)

    outname = filename + ".md"
    pathlib.Path(outname).write_bytes(md_string.encode())

    t1 = time.perf_counter()
    print(f"Markdown creation time for {filename=} {round(t1 - t0, 2)} sec.")

# Main execution
if __name__ == "__main__":
    main()