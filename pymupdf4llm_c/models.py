"""Light-weight data models used across the public API."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import List, Optional


@dataclass(slots=True)
class BBox:
    """Bounding box coordinates."""
    x0: float
    y0: float
    x1: float
    y1: float


@dataclass(slots=True)
class TableCell:
    """A single cell in a table."""
    bbox: BBox
    text: str


@dataclass(slots=True)
class TableRow:
    """A single row in a table."""
    bbox: BBox
    cells: List[TableCell]


@dataclass(slots=True)
class Block:
    """A content block extracted from a PDF page."""
    type: str
    text: str
    bbox: BBox
    font_size: float
    font_weight: str
    page_number: int
    length: int
    
    # Optional fields for specific block types
    lines: Optional[int] = None
    row_count: Optional[int] = None
    col_count: Optional[int] = None
    cell_count: Optional[int] = None
    confidence: Optional[float] = None
    rows: Optional[List[TableRow]] = None


@dataclass(slots=True)
class PageParameters:
    """Parameters emitted during processing of a single page."""

    number: int = 0
    images: List[str] = field(default_factory=list)
    tables: List[str] = field(default_factory=list)
    md_string: str = ""
