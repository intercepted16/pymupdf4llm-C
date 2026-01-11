"""Light-weight data models used across the public API."""

from __future__ import annotations

from functools import cached_property
from typing import Optional, Union

from pydantic import BaseModel, ConfigDict


# Type alias for bbox - JSON uses [x0, y0, x1, y1] list
BBox = list[float]


class Span(BaseModel):
    """A text span with styling information."""

    text: str
    font_size: float
    bold: bool = False
    italic: bool = False
    monospace: bool = False
    strikeout: bool = False
    superscript: bool = False
    subscript: bool = False
    link: bool = False
    uri: Union[str, bool, None] = None  # JSON uses false for no URI


class TableCell(BaseModel):
    """A single cell in a table."""

    bbox: BBox
    spans: list[Span] = []


class TableRow(BaseModel):
    """A single row in a table."""

    bbox: BBox
    cells: list[TableCell] = []


class Block(BaseModel):
    """A PDF content block with cached markdown conversion."""

    model_config = ConfigDict(extra="allow")

    type: str
    bbox: BBox
    spans: list[Span] = []
    length: int = 0
    
    # Optional fields for specific block types
    lines: Optional[int] = None
    level: Optional[int] = None
    row_count: Optional[int] = None
    col_count: Optional[int] = None
    cell_count: Optional[int] = None
    rows: Optional[list[TableRow]] = None

    @cached_property
    def markdown(self) -> str:
        """Convert this block to markdown format (cached)."""
        from ._block_converter import block_to_markdown
        return block_to_markdown(self.model_dump())


class Page(list[Block]):
    """A single page: a list of Block objects with a `.markdown` property."""

    def __init__(self, items: list[Block | dict]):
        super().__init__()
        if items:
            for item in items:
                if isinstance(item, dict):
                    self.append(Block(**item))
                else:
                    self.append(item)

    @cached_property
    def markdown(self) -> str:
        """Convert all blocks to markdown format (cached)."""
        return "\n".join(block.markdown for block in self if block.markdown)

    def __repr__(self) -> str:
        return f"Page({super().__repr__()})"


class Pages(list[Page]):
    """All pages: a list of Page objects with a `.markdown` property."""

    def __init__(self, pages: list[Page]):
        super().__init__()
        if pages:
            for page in pages:
                self.append(page)

    @cached_property
    def markdown(self) -> str:
        """Convert all pages to markdown format (cached)."""
        page_mds = [page.markdown for page in self if page.markdown]
        return "\n---\n\n".join(page_mds)

    def __repr__(self) -> str:
        return f"Pages({super().__repr__()})"
