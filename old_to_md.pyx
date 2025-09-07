cdef public dict header_id
self.body_limit = min(self.header_id.keys()) - 1

### Top-level optimized write_text for Cython
cdef str write_text(
    object parms,
    object clip,
    bint tables,
    bint images,
    bint force_text,
    bint EXTRACT_WORDS,
    bint IGNORE_CODE,
    tuple bullet,
    object get_header_id,
    object output_images
):
    cdef int i, block_num, line_num, flags
    cdef bint code = False
    cdef object prev_lrect = None, prev_hdr_string = None
    cdef int prev_bno = -1
    cdef str font, text, hdr_string, line_string, stripped
    cdef list nlines, tab_candidates, spans, row, cell
    cdef object lrect
    cdef list out_chunks = []

    if clip is None:
        clip = parms.clip

    nlines = get_raw_lines(
        parms.textpage,
        clip=clip,
        tolerance=3,
        ignore_invisible=not parms.accept_invisible,
    )
    nlines = [l for l in nlines if not intersects_rects(l[0], parms.tab_rects.values())]
    parms.line_rects.extend([l[0] for l in nlines])

    for lrect, spans in nlines:
        if intersects_rects(lrect, parms.img_rects):
            continue

        if tables:
            tab_candidates = []
            for i, tab_rect in parms.tab_rects.items():
                if tab_rect.y1 <= lrect.y0 and i not in parms.written_tables and (
                    lrect.x0 <= tab_rect.x0 < lrect.x1 or
                    lrect.x0 < tab_rect.x1 <= lrect.x1 or
                    tab_rect.x0 <= lrect.x0 < lrect.x1 <= tab_rect.x1
                ):
                    tab_candidates.append((i, tab_rect))
            for i, _ in tab_candidates:
                out_chunks.append("\n")
                out_chunks.append(parms.tabs[i].to_markdown(clean=False))
                out_chunks.append("\n")
                if EXTRACT_WORDS:
                    tab_words = parms.tabs[i].extract()
                    for row in tab_words:
                        for cell in row:
                            parms.words.extend(cell)
                parms.written_tables.append(i)

        if images:
            out_chunks.append(output_images(parms, lrect, force_text))

        text = " ".join([s["text"] for s in spans])
        if not text.strip():
            out_chunks.append("\n")
            continue
        prev_bno = block_num
    return out_string.replace(" \n", "\n").replace("  ", " ").replace("\n\n\n", "\n\n")


# Top-level optimized write_text for Cython
cdef str write_text(
    object parms,
    object clip,
    bint tables,
    bint images,
    bint force_text,
    bint EXTRACT_WORDS,
    bint IGNORE_CODE,
    tuple bullet,
    object get_header_id,
    object output_images
):
    cdef int i, block_num, line_num, flags
    cdef bint code = False
    cdef object prev_lrect = None, prev_hdr_string = None
    cdef int prev_bno = -1
    cdef str font, text, hdr_string, line_string, stripped
    cdef list nlines, tab_candidates, spans, row, cell
    cdef object lrect
    cdef list out_chunks = []

    if clip is None:
        clip = parms.clip

    nlines = get_raw_lines(
        parms.textpage,
        clip=clip,
        tolerance=3,
        ignore_invisible=not parms.accept_invisible,
    )
    nlines = [l for l in nlines if not intersects_rects(l[0], parms.tab_rects.values())]
    parms.line_rects.extend([l[0] for l in nlines])

    for lrect, spans in nlines:
        if intersects_rects(lrect, parms.img_rects):
            continue

        if tables:
            tab_candidates = []
            for i, tab_rect in parms.tab_rects.items():
                if tab_rect.y1 <= lrect.y0 and i not in parms.written_tables and (
                    lrect.x0 <= tab_rect.x0 < lrect.x1 or
                    lrect.x0 < tab_rect.x1 <= lrect.x1 or
                    tab_rect.x0 <= lrect.x0 < lrect.x1 <= tab_rect.x1
                ):
                    tab_candidates.append((i, tab_rect))
            for i, _ in tab_candidates:
                out_chunks.append("\n")
                out_chunks.append(parms.tabs[i].to_markdown(clean=False))
                out_chunks.append("\n")
                if EXTRACT_WORDS:
                    tab_words = parms.tabs[i].extract()
                    for row in tab_words:
                        for cell in row:
                            parms.words.extend(cell)
                parms.written_tables.append(i)

        if images:
            out_chunks.append(output_images(parms, lrect, force_text))

        text = " ".join([s["text"] for s in spans])
        if not text.strip():
            out_chunks.append("\n")
            continue

        block_num = spans[0]["block"]
        line_num = spans[0]["line"]

        # Check for potential code block
        if (
            not IGNORE_CODE
            and spans[0]["font"].endswith(("-mono", "-Mono", "Mono"))
            and text.strip() and block_num == prev_bno
        ):
            if not code:
                out_chunks.append("\n```")
                code = True
            out_chunks.append(text)
            out_chunks.append("\n")
            prev_bno = block_num
            prev_lrect = lrect
            continue
        elif code and prev_bno != block_num:
            out_chunks.append("```")
            code = False

        # Header detection
        hdr_string = get_header_id(spans[0], page=parms.page)
        if hdr_string:
            out_chunks.append("\n")
            out_chunks.append(hdr_string)
            out_chunks.append(text.strip())
            out_chunks.append("\n\n")
            prev_hdr_string = hdr_string
            prev_bno = block_num
            prev_lrect = lrect
            continue

        # Format spans for bold, italic, etc.
        line_string = ""
        for span in spans:
            font = span["font"]
            flags = span["flags"]
            text = span["text"]

            # Check for monospace/code
            if (
                not IGNORE_CODE 
                and font.endswith(("-mono", "-Mono", "Mono"))
                and not hdr_string
            ):
                line_string += "`" + text + "`"
                continue

            # Format based on font flags
            if flags & 16:  # bold
                if flags & 2:  # italic
                    line_string += "**_" + text + "_**"
                else:
                    line_string += "**" + text + "**"
            elif flags & 2:  # italic
                line_string += "_" + text + "_"
            else:
                line_string += text

        # Handle list detection
        stripped = line_string.lstrip()
        if any(stripped.startswith(b) for b in bullet):
            if not line_string.startswith("- "):
                line_string = "- " + stripped[2:]

        # Check if this looks like a bullet
        if (
            text.strip().startswith(tuple(bullet))
            and len(text.strip()) > 2
        ):
            out_chunks.append("\n")
            out_chunks.append(line_string)
            out_chunks.append("\n")
        else:
            out_chunks.append(line_string)
            out_chunks.append("\n\n")

        prev_bno = block_num
        prev_lrect = lrect

    out_chunks.append("\n")
    if code:
        out_chunks.append("```
    pass
    out_chunks.append("\n\n")
    out_string = "".join(out_chunks)
    return out_string.replace(" \n", "\n").replace("  ", " ").replace("\n\n\n", "\n\n")


def refine_boxes(list boxes, double enlarge=0):
    """Join any rectangles with a pairwise non-empty overlap.

    Accepts and returns a list of Rect items.
    Note that rectangles that only "touch" each other (common point or edge)
    are not considered as overlapping.
    Use a positive "enlarge" parameter to enlarge rectangle by these many
    points in every direction.

    TODO: Consider using a sweeping line algorithm for this.
    """
    cdef tuple delta = (-enlarge, -enlarge, enlarge, enlarge)
    cdef list new_rects = []
    cdef list prects = boxes[:]  # list of all vector graphic rectangles
    cdef object r
    cdef bint repeat
    cdef int i

    while prects:  # the algorithm will empty this list
        r = +prects[0] + delta  # copy of first rectangle
        repeat = True  # initialize condition
        while repeat:
            repeat = False  # set false as default
            i = len(prects) - 1
            while i > 0:  # from back to front
                if r.intersects(prects[i].irect):  # enlarge first rect with this
                    r |= prects[i]
                    del prects[i]  # delete this rect
                    repeat = True  # indicate must try again
                i -= 1

        # first rect now includes all overlaps
        new_rects.append(r)
        del prects[0]

    # Sort without lambda - Cython doesn't support lambdas in cpdef functions
    new_rects = list(set(new_rects))
    new_rects.sort(key=lambda r: (r.x0, r.y0))
    return new_rects


def is_significant(object box, list paths):
    """Check whether the rectangle "box" contains 'signifiant' drawings.

    This means that some path is contained in the "interior" of box.
    To this end, we build a sub-box of 90% of the original box and check
    whether this still contains drawing paths.
    """
    cdef double d
    cdef object nbox, rect
    cdef list my_paths
    cdef set widths, heights
    cdef dict p
    
    if box.width > box.height:
        d = box.width * 0.025
    else:
        d = box.height * 0.025
    nbox = box + (d, d, -d, -d)  # nbox covers 90% of box interior
    # paths contained in, but not equal to box:
    my_paths = [p for p in paths if p["rect"] in box and p["rect"] != box]
    widths = set(c_round(p["rect"].width) for p in my_paths) | {c_round(box.width)}
    heights = set(c_round(p["rect"].height) for p in my_paths) | {c_round(box.height)}
    if len(widths) == 1 or len(heights) == 1:
        return False  # all paths are horizontal or vertical lines / rectangles
    for p in my_paths:
        rect = p["rect"]
        if (
            not (rect & nbox).is_empty and not p["rect"].is_empty
        ):  # intersects interior: significant!
            return True
        # Remaining case: a horizontal or vertical line
        # horizontal line:
        if (
            1
            and rect.y0 == rect.y1
            and nbox.y0 <= rect.y0 <= nbox.y1
            and rect.x0 < nbox.x1
            and rect.x1 > nbox.x0
        ):
            pass  # return True
        # vertical line
        if (
            1
            and rect.x0 == rect.x1
            and nbox.x0 <= rect.x0 <= nbox.x1
            and rect.y0 < nbox.y1
            and rect.y1 > nbox.y0
        ):
            pass  # return True
    return False


def to_markdown(
    doc,
    *,
    pages=None,
    hdr_info=None,
    bint write_images=False,
    bint embed_images=False,
    bint ignore_images=False,
    bint ignore_graphics=False,
    bint detect_bg_color=True,
    str image_path="",
    str image_format="png",
    double image_size_limit=0.05,
    filename=None,
    bint force_text=True,
    bint page_chunks=False,
    bint page_separators=False,
    margins=0,
    int dpi=150,
    double page_width=612,
    page_height=None,
    str table_strategy="lines_strict",
    graphics_limit=None,
    double fontsize_limit=3,
    bint ignore_code=False,
    bint extract_words=False,
    bint show_progress=False,
    bint use_glyphs=False,
    bint ignore_alpha=False,
):
    """Process the document and return the text of the selected pages."""
    
    if write_images is False and embed_images is False and force_text is False:
        raise ValueError("Image and text on images cannot both be suppressed.")
    if embed_images is True:
        write_images = False
        image_path = ""
    if not 0 <= image_size_limit < 1:
        raise ValueError("'image_size_limit' must be non-negative and less than 1.")
    
    cdef int DPI = dpi
    cdef bint IGNORE_CODE = ignore_code
    cdef str IMG_EXTENSION = image_format
    cdef bint EXTRACT_WORDS = extract_words
    if EXTRACT_WORDS is True:
        page_chunks = True
        ignore_code = True
    cdef str IMG_PATH = image_path
    if IMG_PATH and write_images is True and not os.path.exists(IMG_PATH):
        os.mkdir(IMG_PATH)

    if not isinstance(doc, pymupdf.Document):
        doc = pymupdf.open(doc)

    cdef str FILENAME = doc.name if filename is None else filename
    cdef int GRAPHICS_LIMIT
    if graphics_limit is not None:
        GRAPHICS_LIMIT = int(graphics_limit)
    else:
        GRAPHICS_LIMIT = 0
    cdef double FONTSIZE_LIMIT = fontsize_limit
    cdef bint IGNORE_IMAGES = ignore_images
    cdef bint IGNORE_GRAPHICS = ignore_graphics
    cdef bint DETECT_BG_COLOR = detect_bg_color
    
    if doc.is_form_pdf or (doc.is_pdf and doc.has_annots()):
        doc.bake()

    # for reflowable documents allow making 1 page for the whole document
    if doc.is_reflowable:
        if hasattr(page_height, "__float__"):
            doc.layout(width=page_width, height=page_height)
        cdef str write_text(parms, clip, tables=True, images=True, force_text=force_text):
            """Output the text found inside the given clip. Optimized for Cython."""
            cdef int i, block_num, line_num, flags
            cdef bint code = False
            cdef object prev_lrect = None, prev_hdr_string = None
            cdef int prev_bno = -1
            cdef str font, text, hdr_string, line_string, stripped
            cdef list nlines, tab_candidates, spans, row, cell
            cdef object lrect
            cdef list out_chunks = []

            if clip is None:
                clip = parms.clip

            nlines = get_raw_lines(
                parms.textpage,
                clip=clip,
                tolerance=3,
                ignore_invisible=not parms.accept_invisible,
            )
            nlines = [l for l in nlines if not intersects_rects(l[0], parms.tab_rects.values())]
            parms.line_rects.extend([l[0] for l in nlines])

            for lrect, spans in nlines:
                if intersects_rects(lrect, parms.img_rects):
                    continue

                if tables:
                    tab_candidates = []
                    for i, tab_rect in parms.tab_rects.items():
                        if tab_rect.y1 <= lrect.y0 and i not in parms.written_tables and (
                            lrect.x0 <= tab_rect.x0 < lrect.x1 or
                            lrect.x0 < tab_rect.x1 <= lrect.x1 or
                            tab_rect.x0 <= lrect.x0 < lrect.x1 <= tab_rect.x1
                        ):
                            tab_candidates.append((i, tab_rect))
                    for i, _ in tab_candidates:
                        out_chunks.append("\n")
                        out_chunks.append(parms.tabs[i].to_markdown(clean=False))
                        out_chunks.append("\n")
                        if EXTRACT_WORDS:
                            tab_words = parms.tabs[i].extract()
                            for row in tab_words:
                                for cell in row:
                                    parms.words.extend(cell)
                        parms.written_tables.append(i)

                if images:
                    out_chunks.append(output_images(parms, lrect, force_text))

                text = " ".join([s["text"] for s in spans])
                if not text.strip():
                    out_chunks.append("\n")
                    continue

                block_num = spans[0]["block"]
                line_num = spans[0]["line"]

                # Check for potential code block
                if (
                    not IGNORE_CODE
                    and spans[0]["font"].endswith(("-mono", "-Mono", "Mono"))
                    and text.strip() and block_num == prev_bno
                ):
                    if not code:
                        out_chunks.append("\n```")
                        code = True
                    out_chunks.append(text)
                    out_chunks.append("\n")
                    prev_bno = block_num
                    prev_lrect = lrect
                    continue
                elif code and prev_bno != block_num:
                    out_chunks.append("```")
                    code = False

                # Header detection
                hdr_string = get_header_id(spans[0], page=parms.page)
                if hdr_string:
                    out_chunks.append("\n")
                    out_chunks.append(hdr_string)
                    out_chunks.append(text.strip())
                    out_chunks.append("\n\n")
                    prev_hdr_string = hdr_string
                    prev_bno = block_num
                    prev_lrect = lrect
                    continue

                # Format spans for bold, italic, etc.
                line_string = ""
                for span in spans:
                    font = span["font"]
                    flags = span["flags"]
                    text = span["text"]

                    # Check for monospace/code
                    if (
                        not IGNORE_CODE 
                        and font.endswith(("-mono", "-Mono", "Mono"))
                        and not hdr_string
                    ):
                        line_string += "`" + text + "`"
                        continue

                    # Format based on font flags
                    if flags & 16:  # bold
                        if flags & 2:  # italic
                            line_string += "**_" + text + "_**"
                        else:
                            line_string += "**" + text + "**"
                    elif flags & 2:  # italic
                        line_string += "_" + text + "_"
                    else:
                        line_string += text

                # Handle list detection
                stripped = line_string.lstrip()
                if any(stripped.startswith(b) for b in bullet):
                    if not line_string.startswith("- "):
                        line_string = "- " + stripped[2:]

                # Check if this looks like a bullet
                if (
                    text.strip().startswith(tuple(bullet))
                    and len(text.strip()) > 2
                ):
                    out_chunks.append("\n")
                    out_chunks.append(line_string)
                    out_chunks.append("\n")
                else:
                    out_chunks.append(line_string)
                    out_chunks.append("\n\n")

                prev_bno = block_num
                prev_lrect = lrect

            out_chunks.append("\n")
            if code:
                out_chunks.append("```
                and text.strip() and block_num == prev_bno
            out_chunks.append("\n\n")
            out_string = "".join(out_chunks)
            return out_string.replace(" \n", "\n").replace("  ", " ").replace("\n\n\n", "\n\n")
            ):
                if not code:
                    out_string += "\n```\n"
                    code = True
                out_string += text + "\n"
                prev_bno = block_num
                prev_lrect = lrect
                continue
            elif code and prev_bno != block_num:
                out_string += "```\n\n"
                code = False

            # Header detection
            hdr_string = get_header_id(spans[0], page=parms.page)
            if hdr_string:
                out_string += "\n" + hdr_string + text.strip() + "\n\n"
                prev_hdr_string = hdr_string
                prev_bno = block_num
                prev_lrect = lrect
                continue

            # Format spans for bold, italic, etc.
            line_string = ""
            for span in spans:
                font = span["font"]
                flags = span["flags"]
                text = span["text"]

                # Check for monospace/code
                if (
                    not IGNORE_CODE 
                    and font.endswith(("-mono", "-Mono", "Mono"))
                    and not hdr_string
                ):
                    line_string += f"`{text}`"
                    continue

                # Format based on font flags
                if flags & 2**4:  # bold
                    if flags & 2**1:  # italic
                        line_string += f"**_{text}_**"
                    else:
                        line_string += f"**{text}**"
                elif flags & 2**1:  # italic
                    line_string += f"_{text}_"
                else:
                    line_string += text

            # Handle list detection
            stripped = line_string.lstrip()
            if any(stripped.startswith(b) for b in bullet):
                if not line_string.startswith("- "):
                    line_string = "- " + stripped[2:]

            # Check if this looks like a bullet
            if (
                text.strip().startswith(tuple(bullet))
                and len(text.strip()) > 2
            ):
                out_string += "\n" + line_string + "\n"
            else:
                out_string += line_string + "\n\n"

            prev_bno = block_num
            prev_lrect = lrect

        out_string += "\n"
        if code:
            out_string += "```\n"
        out_string += "\n\n"
        return (
            out_string.replace(" \n", "\n").replace("  ", " ").replace("\n\n\n", "\n\n")
        )

    def is_in_rects(rect, rect_list):
        """Check if rect is contained in a rect of the list."""
        for i, r in enumerate(rect_list, start=1):
            if rect in r:
                return i
        return 0

    def intersects_rects(rect, rect_list):
        """Check if middle of rect is contained in a rect of the list."""
        delta = (-1, -1, 1, 1)
        enlarged = rect + delta
        abs_enlarged = abs(enlarged) * 0.5
        for i, r in enumerate(rect_list, start=1):
            if abs(enlarged & r) >= abs_enlarged:
                return i
        return 0

    def output_tables(parms, text_rect):
        """Output tables above given text rectangle."""
        this_md = ""
        if text_rect is not None:
            candidates = [
                (i, r) for i, r in parms.tab_rects.items() if r.y1 <= text_rect.y0
            ]
        else:
            candidates = [(i, r) for i, r in parms.tab_rects.items()]

        for i, r in candidates:
            if i in parms.written_tables:
                continue
            this_md += "\n" + parms.tabs[i].to_markdown(clean=False) + "\n"
            if EXTRACT_WORDS:
                tab_words = parms.tabs[i].extract()
                for row in tab_words:
                    for cell in row:
                        parms.words.extend(cell)
            parms.written_tables.append(i)
        return this_md

    def output_images(parms, text_rect, force_text):
        """Output images and graphics above text rectangle."""
        if not parms.img_rects:
            return ""
        this_md = ""
        if text_rect is not None:
            candidates = [r for r in parms.img_rects if r.y1 <= text_rect.y0]
        else:
            candidates = parms.img_rects[:]

        for r in candidates:
            if r in parms.written_images:
                continue
            i = parms.img_rects.index(r)
            this_md += save_image(parms, r, i)
            parms.written_images.append(r)

        return this_md

    def page_is_ocr(page):
        """Check if page exclusively contains OCR text."""
        try:
            flags = pymupdf.TEXT_DEHYPHENATE | pymupdf.TEXT_MEDIABOX_CLIP
            blocks = page.get_text("dict", flags=flags)["blocks"]
            for block in blocks:
                if block.get("type", 1) != 0:
                    continue
                for line in block["lines"]:
                    for span in line["spans"]:
                        if span["flags"] != 8:
                            return False
            return True
        except:
            return False

    def get_bg_color(page):
        """Determine the background color of the page."""
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
        if not pixel_ul == pixel_ur:
            return None
        pix = page.get_pixmap(
            clip=(page.rect.x0, page.rect.y1 - 10, page.rect.x0 + 10, page.rect.y1)
        )
        if not pix.samples or not pix.is_unicolor:
            return None
        pixel_ll = pix.pixel(0, 0)
        if not pixel_ul == pixel_ll:
            return None
        pix = page.get_pixmap(
            clip=(page.rect.x1 - 10, page.rect.y1 - 10, page.rect.x1, page.rect.y1)
        )
        if not pix.samples or not pix.is_unicolor:
            return None
        pixel_lr = pix.pixel(0, 0)
        if not pixel_ul == pixel_lr:
            return None
        return (pixel_ul[0] / 255, pixel_ul[1] / 255, pixel_ul[2] / 255)

    def get_metadata(doc, pno):
        meta = doc.metadata.copy()
        meta["file_path"] = FILENAME
        meta["page_count"] = doc.page_count
        meta["page"] = pno + 1
        return meta

    def sort_words(words: list) -> list:
        """Reorder words in lines."""
        if not words:
            return words
        nwords = []
        line = [words[0]]
        lrect = pymupdf.Rect(words[0][:4])
        for w in words[1:]:
            wrect = pymupdf.Rect(w[:4])
            if abs(wrect.y0 - lrect.y0) <= 2:
                line.append(w)
                lrect |= wrect
            else:
                line.sort(key=lambda w: w[0])
                nwords.extend(line)
                line = [w]
                lrect = wrect
        line.sort(key=lambda w: w[0])
        nwords.extend(line)
        return nwords

    def get_page_output(
        doc, pno, margins, textflags, FILENAME, IGNORE_IMAGES, IGNORE_GRAPHICS
    ):
        """Process one page."""
        page = doc[pno]
        page.remove_rotation()
        parms = Parameters()
        parms.page = page
        parms.filename = FILENAME
        parms.md_string = ""
        parms.images = []
        parms.tables = []
        parms.graphics = []
        parms.words = []
        parms.line_rects = []
        parms.accept_invisible = (
            page_is_ocr(page) or ignore_alpha
        )

        parms.bg_color = None if not DETECT_BG_COLOR else get_bg_color(page)

        left, top, right, bottom = margins
        parms.clip = page.rect + (left, top, -right, -bottom)

        parms.links = [l for l in page.get_links() if l["kind"] == pymupdf.LINK_URI]
        parms.annot_rects = [a.rect for a in page.annots()]
        parms.textpage = page.get_textpage(flags=textflags, clip=parms.clip)

        # Extract images on page
        if not IGNORE_IMAGES:
            img_info = extract_images_on_page_simple_drop(page, parms, image_size_limit)
        else:
            img_info = []
        for i in range(len(img_info)):
            img_info[i]["bbox"] = pymupdf.Rect(img_info[i]["bbox"])

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

        if img_info:
            img_info = [i for i in img_info if i["bbox"] in parms.clip]

        img_info = img_info[:30]
        for i in range(len(img_info) - 1, 0, -1):
            r = img_info[i]["bbox"]
            if r.is_empty:
                del img_info[i]
                continue
            for j in range(i):
                if r in img_info[j]["bbox"]:
                    del img_info[i]
                    break
        parms.images = img_info

        parms.img_rects = [i["bbox"] for i in parms.images]

        graphics_count = len([b for b in page.get_bboxlog() if "path" in b[0]])
        if GRAPHICS_LIMIT and graphics_count > GRAPHICS_LIMIT:
            IGNORE_GRAPHICS = True

        parms.written_tables = []
        omitted_table_rects = []
        parms.tabs = []
        if IGNORE_GRAPHICS or not table_strategy:
            pass
        else:
            try:
                parms.tabs = page.find_tables(strategy=table_strategy)
            except:
                pass

        tab_rects = {}
        for i, t in enumerate(parms.tabs):
                r = t.bbox
                if not isinstance(r, pymupdf.Rect):
                    r = pymupdf.Rect(r)
                if hasattr(t, "header") and t.header.bbox != r:
                    header_bbox = t.header.bbox
                    if not isinstance(header_bbox, pymupdf.Rect):
                        header_bbox = pymupdf.Rect(header_bbox)
                    r |= header_bbox
                tab_rects[i] = r
        parms.tab_rects = tab_rects
        parms.tab_rects0 = list(tab_rects.values())

        if not IGNORE_GRAPHICS:
            paths = page.get_drawings()
            if parms.bg_color:
                paths = [
                    p for p in paths
                    if p.get("fill", None) != parms.bg_color
                ]
        else:
            paths = []
        if GRAPHICS_LIMIT and len(paths) > GRAPHICS_LIMIT:
            paths = []

        vg_clusters0 = []

        clusters = page.cluster_drawings(drawings=paths)
        for bbox in clusters:
            if is_significant(bbox, paths):
                vg_clusters0.append(bbox)

        parms.actual_paths = [p for p in paths if is_in_rects(p["rect"], vg_clusters0)]

        vg_clusters0.extend(parms.img_rects)
        parms.img_rects.extend(vg_clusters0)
        parms.img_rects = sorted(set(parms.img_rects), key=lambda r: (r.y1, r.x0))
        parms.written_images = []
        parms.vg_clusters0 = refine_boxes(vg_clusters0)

        parms.vg_clusters = dict((i, r) for i, r in enumerate(parms.vg_clusters0))
        text_rects = column_boxes(
            parms.page,
            paths=parms.actual_paths,
            no_image_text=not force_text,
            textpage=parms.textpage,
            avoid=parms.tab_rects0 + parms.vg_clusters0,
            footer_margin=margins[3],
            header_margin=margins[1],
            ignore_images=IGNORE_IMAGES,
        )

        for text_rect in text_rects:
            parms.md_string += output_tables(parms, text_rect)
            parms.md_string += output_images(parms, text_rect, force_text)
            parms.md_string += write_text(
                parms,
                text_rect,
                tables=True,
                images=True,
                force_text=force_text,
                EXTRACT_WORDS=EXTRACT_WORDS,
                IGNORE_CODE=IGNORE_CODE,
                bullet=bullet,
                get_header_id=get_header_id,
                output_images=output_images
            )

        parms.md_string = parms.md_string.replace(" ,", ",").replace("-\n", "")

        parms.md_string += output_tables(parms, None)
        parms.md_string += output_images(parms, None, force_text)

        while parms.md_string.startswith("\n"):
            parms.md_string = parms.md_string[1:]
        parms.md_string = parms.md_string.replace(chr(0), chr(0xFFFD))

        if EXTRACT_WORDS is True:
            words = [
                w for w in parms.textpage.extractWords() 
                if pymupdf.Rect(w[:4]).intersects(parms.clip)
            ]
        else:
            words = []
        parms.words = words
        if page_separators:
            parms.md_string += f"\n\n-----\n\n"
        return parms

    if page_chunks is False:
        document_output = ""
    else:
        document_output = []

    toc = doc.get_toc()

    textflags = (
        0
        | mupdf.FZ_STEXT_CLIP
        | mupdf.FZ_STEXT_ACCURATE_BBOXES
        | 32768
    )
    if use_glyphs:
        textflags |= mupdf.FZ_STEXT_USE_GID_FOR_UNKNOWN_UNICODE

    if show_progress:
        print(f"Processing {FILENAME}...")
        pages = ProgressBar(pages)
    for pno in pages:
        parms = get_page_output(
            doc, pno, margins, textflags, FILENAME, IGNORE_IMAGES, IGNORE_GRAPHICS
        )
        if page_chunks is False:
            document_output += parms.md_string
        else:
            metadata = get_metadata(doc, pno)
            page_tocs = [t for t in toc if t[-1] == pno + 1]
            
            document_output.append({
                "metadata": metadata,
                "toc_items": page_tocs,
                "tables": [t.to_markdown(clean=False) for t in parms.tabs],
                "images": parms.images,
                "graphics": parms.graphics,
                "text": parms.md_string,
                "words": sort_words(parms.words) if EXTRACT_WORDS else [],
            })
        del parms

    return document_output


def extract_images_on_page_simple(object page, object parms, double image_size_limit):
    """Extract images on page using simplified mechanism."""
    cdef list img_info
    cdef int i, j
    cdef dict item
    cdef object r
    
    # extract images on page
    # ignore images contained in some other one (simplified mechanism)
    img_info = page.get_image_info()
    for i in range(len(img_info)):
        item = img_info[i]
        item["bbox"] = pymupdf.Rect(item["bbox"]) & parms.clip
        img_info[i] = item

    # sort descending by image area size
    img_info.sort(key=lambda i: abs(i["bbox"]), reverse=True)
    # run from back to front (= small to large)
    i = len(img_info) - 1
    while i > 0:
        r = img_info[i]["bbox"]
        if r.is_empty:
            del img_info[i]
            i -= 1
            continue
        for j in range(i):  # image areas larger than r
            if r in img_info[j]["bbox"]:
                del img_info[i]  # contained in some larger image
                break
        i -= 1

    return img_info


def filter_small_images(object page, object parms, double image_size_limit):
    """Filter out small images based on size limit."""
    cdef list img_info = []
    cdef dict item
    cdef object r
    
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


def extract_images_on_page_simple_drop(object page, object parms, double image_size_limit):
    """Extract images with small image dropping."""
    cdef list img_info = filter_small_images(page, parms, image_size_limit)
    cdef int i, j
    cdef object r

    # sort descending by image area size
    img_info.sort(key=lambda i: abs(i["bbox"]), reverse=True)
    # run from back to front (= small to large)
    i = len(img_info) - 1
    while i > 0:
        r = img_info[i]["bbox"]
        if r.is_empty:
            del img_info[i]
            i -= 1
            continue
        for j in range(i):  # image areas larger than r
            if r in img_info[j]["bbox"]:
                del img_info[i]  # contained in some larger image
                break
        i -= 1

    return img_info


def main():
    """Main program: process command line arguments and call to_markdown."""
    import pathlib
    import sys
    import time

    try:
        filename = sys.argv[1]
    except IndexError:
        print(f"Usage:\npython {os.path.basename(__file__)} input.pdf")
        sys.exit()

    t0 = time.perf_counter()  # start a time

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

    # get the markdown string
    md_string = to_markdown(
        doc,
        pages=pages,
    )
    FILENAME = doc.name
    # output to a text file with extension ".md"
    outname = FILENAME + ".md"
    pathlib.Path(outname).write_bytes(md_string.encode())
    t1 = time.perf_counter()  # stop timer
    print(f"Markdown creation time for {FILENAME=} {round(t1-t0,2)} sec.")


if __name__ == "__main__":
    main()