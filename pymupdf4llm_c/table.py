import traceback
from typing import List, Optional

import pymupdf
from .utils import profiler
from .wrappers import is_likely_table


def locate_and_extract_tables(
    parms: "pymupdf_rag.Parameters",
    doc: "pymupdf.Document",
    pno: int,
    ignore_graphics: bool,
    table_strategy: str,
    defer_tables: bool,
    extract_words: bool,
):
    """Locates and extracts tables from a page, modifying the `parms` object in place.

    First, it checks if a table is likely to be on the page. If so, it uses
    PyMuPDF's `find_tables` to locate them. If `defer_tables` is True, it
    extracts the Markdown and cell data immediately for later processing.

    Args:
        parms: The Parameters object for the current page. This object is
               modified in place.
        doc: The pymupdf.Document object.
        pno: The 0-based page number.
        ignore_graphics: If True, graphical elements are ignored during table
                         detection.
        table_strategy: The strategy to use for `find_tables` (e.g., "lines_strict").
        defer_tables: If True, table processing is deferred.
        extract_words: If True, extract word-level information from table cells.
    """
    profiler.start_timer("locate_tables")
    if is_likely_table(doc.name, pno):
        parms.tabs = []
        if not ignore_graphics and table_strategy:
            try:
                tabs = parms.page.find_tables(clip=parms.clip, strategy=table_strategy)
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
                            if extract_words:
                                cells = sorted(
                                    set(
                                        [
                                            pymupdf.Rect(c)
                                            for c in tab_obj.header.cells
                                            + tab_obj.cells
                                            if c is not None
                                        ]
                                    ),
                                    key=lambda c: (c.y1, c.x0),
                                )
                                cells_data = [[c.x0, c.y0, c.x1, c.y1] for c in cells]

                            # Store safe table data
                            tab_data = {
                                "index": i,
                                "rect": list(tab_obj.bbox),
                                "bbox": list(tab_obj.bbox),
                                "markdown": tab_markdown,
                                "cells": cells_data,
                            }
                            parms.deferred_tables.append(tab_data)
                        except Exception as e:
                            print(f"Warning: Failed to extract table {i} markdown: {e}")
                            traceback.print_exc()

    parms.tables = [
        {
            "bbox": tuple(pymupdf.Rect(t.bbox) | pymupdf.Rect(t.header.bbox)),
            "rows": t.row_count,
            "columns": t.col_count,
        }
        for t in parms.tabs
    ]
    parms.tab_rects0 = [
        (i, pymupdf.Rect(t["bbox"])) for i, t in enumerate(parms.tables)
    ]
    parms.tab_rects = {i: pymupdf.Rect(t["bbox"]) for i, t in enumerate(parms.tables)}

    # Initialize empty deferred_tables for non-defer mode
    if not defer_tables:
        parms.deferred_tables = []
    profiler.end_timer("locate_tables")


def output_tables(
    parms: "pymupdf_rag.Parameters",
    text_rect: Optional["pymupdf.Rect"],
    defer: bool = False,
    global_written_tables: Optional[List[int]] = None,
    extract_words: bool = False,
) -> str:
    """Generates Markdown for tables located above a given text rectangle.

    This function iterates through the tables found on the page and outputs
    them as Markdown if they are positioned above the `text_rect`.

    Args:
        parms: The Parameters object for the current page.
        text_rect: An optional pymupdf.Rect. If provided, only tables
                   above this rectangle are processed.
        defer: If True, table processing is skipped.
        global_written_tables: An optional list of table indices that have
                               already been written.
        extract_words: If True, extract word-level information from table cells.

    Returns:
        A string containing the Markdown for the processed tables.
    """
    this_md = ""
    written_tables = (
        global_written_tables
        if global_written_tables is not None
        else parms.written_tables
    )

    if defer:
        # In defer mode, emit stable placeholders so we can later replace
        # them with the actual (already extracted) markdown in
        # process_deferred_tables. This preserves the relative ordering of
        # tables to surrounding text blocks and avoids end-of-page dumping.
        # We intentionally only depend on rect metadata, not on live table
        # objects (parms.tabs), because those objects are not picklable.
        if text_rect is not None:
            candidates = sorted(
                [j for j in parms.tab_rects0 if j[1].y1 <= text_rect.y0],
                key=lambda j: (j[1].y1, j[1].x0),
            )
        else:
            candidates = parms.tab_rects0
        for i, _ in candidates:
            if i in written_tables:
                continue
            # Placeholder token – unlikely to collide with real text.
            this_md += f"\n[[TABLE_{i}]]\n"
            written_tables.append(i)
        return this_md

    if text_rect is not None:
        for i, trect in sorted(
            [j for j in parms.tab_rects0 if j[1].y1 <= text_rect.y0],
            key=lambda j: (j[1].y1, j[1].x0),
        ):
            if i in written_tables:
                continue
            this_md += parms.tabs[i].to_markdown(clean=False) + "\n"
            if extract_words:
                cells = sorted(
                    {
                        pymupdf.Rect(c)
                        for c in parms.tabs[i].header.cells + parms.tabs[i].cells
                        if c is not None
                    },
                    key=lambda c: (c.y1, c.x0),
                )
                parms.line_rects.extend(cells)
            written_tables.append(i)
    else:
        for i, trect in parms.tab_rects0:
            if i in written_tables:
                continue
            this_md += parms.tabs[i].to_markdown(clean=False) + "\n"
            if extract_words:
                cells = sorted(
                    set(
                        [
                            pymupdf.Rect(c)
                            for c in parms.tabs[i].header.cells + parms.tabs[i].cells
                            if c is not None
                        ]
                    ),
                    key=lambda c: (c.y1, c.x0),
                )
                parms.line_rects.extend(cells)
            written_tables.append(i)
    return this_md


def process_deferred_tables(
    all_page_results: List[tuple[int, Optional["pymupdf_rag.Parameters"]]],
    doc: "pymupdf.Document",
    extract_words: bool = False,
) -> List[tuple[int, Optional["pymupdf_rag.Parameters"]]]:
    """Processes tables that were deferred during initial page processing.

    This function is called after all pages have been processed in parallel.
    It iterates through the results and adds the Markdown for any deferred
    tables to the final output.

    Args:
        all_page_results: A list of tuples, where each tuple contains the
                          page number and the corresponding Parameters object.
        doc: The pymupdf.Document object.
        extract_words: If True, extract word-level information from table cells.

    Returns:
        The `all_page_results` list with the table Markdown added to the
        `md_string` of the respective page Parameters.
    """
    with profiler.time_block("process_deferred_tables"):
        global_written_tables = set()  # Use a set for efficient lookups

        # Sort results by page number to ensure correct order
        all_page_results.sort(key=lambda x: x[0])

        print(f"Processing deferred tables for {len(all_page_results)} pages...")

        for pno, parms in all_page_results:
            if parms is None:
                continue

            # Only process pages that have deferred tables
            if not hasattr(parms, "deferred_tables") or not parms.deferred_tables:
                continue

            print(f"  Page {pno}: Found {len(parms.deferred_tables)} deferred tables")

            # Process all tables for this page using global state
            page_table_md = ""

            # Build a map for quick lookup by original index
            index_map = {td["index"]: td for td in parms.deferred_tables}

            # Replace placeholders inline first to preserve ordering
            # Pattern [[TABLE_<n>]]
            new_md_parts = []
            for line in parms.md_string.splitlines():
                if line.startswith("[[TABLE_") and line.endswith("]]"):
                    try:
                        table_index = int(line[len("[[TABLE_") : -2])
                    except Exception:
                        new_md_parts.append(line)
                        continue
                    tab_data = index_map.get(table_index)
                    if not tab_data:
                        # No data – leave placeholder
                        new_md_parts.append(line)
                        continue
                    table_id = (pno, table_index)
                    if table_id in global_written_tables:
                        # Already emitted elsewhere – drop duplicate placeholder
                        continue
                    table_md = tab_data.get("markdown", "")
                    new_md_parts.append(table_md)
                    global_written_tables.add(table_id)
                    if extract_words and tab_data.get("cells"):
                        if not hasattr(parms, "line_rects") or parms.line_rects is None:
                            parms.line_rects = []
                        for cell in tab_data["cells"]:
                            try:
                                parms.line_rects.append(tuple(cell))
                            except Exception:
                                pass
                else:
                    new_md_parts.append(line)

            parms.md_string = "\n".join(new_md_parts) + ("\n" if new_md_parts else "")

            # Inline reconstruction: remove overlapping raw text fragments that fall inside table rects.
            try:
                if hasattr(parms, "raw_lines") and parms.raw_lines:
                    # Build rect objects for tables
                    table_rects = []
                    for td in parms.deferred_tables:
                        try:
                            tr = pymupdf.Rect(td["rect"]) if not isinstance(td["rect"], pymupdf.Rect) else td["rect"]
                            table_rects.append((td["index"], tr, td))
                        except Exception:
                            pass
                    table_rects.sort(key=lambda x: (x[1].y0, x[1].x0))

                    # Filter line texts outside table areas
                    cleaned_lines = []
                    for line in parms.raw_lines:
                        rect_tuple = line.get("rect")
                        line_rect = pymupdf.Rect(*rect_tuple)
                        if any(line_rect.intersects(tr) and abs(line_rect & tr) / max(1.0, abs(line_rect)) > 0.3 for _, tr, _ in table_rects):
                            continue
                        txt = line.get("text", "").strip()
                        if txt:
                            cleaned_lines.append((line_rect, txt))

                    # Build insertion plan: determine anchor index per table
                    anchors = []
                    for tidx, tr, td in table_rects:
                        # Find first cleaned line with y0 greater than table y1 (table appears before that line)
                        insert_at = None
                        for i, (lr, _) in enumerate(cleaned_lines):
                            if lr.y0 >= tr.y1 - 1:  # small tolerance
                                insert_at = i
                                break
                        if insert_at is None:
                            insert_at = len(cleaned_lines)
                        anchors.append((insert_at, td.get("markdown", "")))
                    # Sort anchors descending so insertion indices remain valid
                    anchors.sort(key=lambda a: a[0], reverse=True)

                    lines_only = [t for _, t in cleaned_lines]
                    for idx, table_md in anchors:
                        if table_md:
                            lines_only.insert(idx, table_md.strip())

                    # Reconstruct page markdown
                    eop_marker = f"--- end of page={pno} ---"
                    has_eop = eop_marker in parms.md_string
                    parms.md_string = "\n\n".join(lines_only)
                    if has_eop:
                        parms.md_string += f"\n\n{eop_marker}\n\n"
            except Exception:
                pass

            # Process any deferred tables that never had placeholders (e.g., edge cases)
            for tab_data in parms.deferred_tables:
                table_index = tab_data["index"]
                table_id = (pno, table_index)  # Create a globally unique ID

                if table_id in global_written_tables:
                    continue
                table_md = tab_data.get("markdown", "")
                # Append at end since no placeholder found
                parms.md_string += table_md + "\n"
                global_written_tables.add(table_id)
                if extract_words and tab_data.get("cells"):
                    if not hasattr(parms, "line_rects") or parms.line_rects is None:
                        parms.line_rects = []
                    for cell in tab_data["cells"]:
                        try:
                            parms.line_rects.append(tuple(cell))
                        except Exception:
                            pass

        return all_page_results
