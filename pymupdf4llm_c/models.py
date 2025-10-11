"""Light-weight data models used across the public API."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import List


@dataclass(slots=True)
class PageParameters:
    """Parameters emitted during processing of a single page."""

    number: int = 0
    images: List[str] = field(default_factory=list)
    tables: List[str] = field(default_factory=list)
    md_string: str = ""
