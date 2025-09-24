import traceback
from typing import List, Optional

import pymupdf
from utils import profiler
from wrappers import is_likely_table


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

            # Process all deferred tables found on this page
            for tab_data in parms.deferred_tables:
                table_index = tab_data["index"]
                table_id = (pno, table_index)  # Create a globally unique ID

                if table_id in global_written_tables:
                    print(f"    Skipping already written table {table_id}")
                    continue

                table_md = tab_data.get("markdown", "")
                page_table_md += table_md + "\n"
                global_written_tables.add(table_id)
                print(f"    Added table {table_id}: {len(table_md)} characters")

                # Add cell rectangles to line_rects if EXTRACT_WORDS is enabled
                if extract_words and tab_data.get("cells"):
                    if not hasattr(parms, "line_rects") or parms.line_rects is None:
                        parms.line_rects = []
                    for cell in tab_data["cells"]:
                        try:
                            parms.line_rects.append(tuple(cell))
                        except Exception:
                            pass

            # Add the table markdown to the page's markdown string
            parms.md_string += page_table_md
            print(
                f"  Page {pno}: Added {len(page_table_md)} characters of table markdown"
            )

        return all_page_results
