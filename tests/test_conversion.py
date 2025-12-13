"""End-to-end tests for the JSON extraction pipeline."""

from __future__ import annotations

from pathlib import Path
from typing import List, Protocol

import pytest

from pymupdf4llm_c import to_json
from pymupdf4llm_c.api import Block

TEST_DATA_DIR = Path(__file__).parent / "test_data"


class ExtractorCallable(Protocol):
    """Protocol for the test fixture to allow type hinting."""

    def __call__(self, filename: str) -> List[Block]:
        """For type hinting."""
        ...


@pytest.fixture
def extract_blocks(tmp_path: Path) -> ExtractorCallable:
    """Fixture that returns a function.

    The function processes a PDF and returns the flattened list of JSON blocks.
    """

    def _process(filename: str) -> List[Block]:
        pdf_path = TEST_DATA_DIR / filename
        if not pdf_path.exists():
            pytest.skip(f"Test PDF not found: {pdf_path}")

        pages = to_json(pdf_path, output_file=tmp_path / "output.json", collect=True)
        # Flatten page structure: [{page: N, data: [...]}, ...] -> [block, block, ...]
        blocks = []
        for page in pages:
            if isinstance(page, dict) and "data" in page:
                blocks.extend(page["data"])
            else:
                blocks.append(page)
        return blocks

    return _process


# --- Tests ---


@pytest.mark.integration
@pytest.mark.requires_pdf
class TestJsonExtraction:
    """End-to-end coverage for the JSON extractor."""

    def test_table_detection(self, extract_blocks: ExtractorCallable):
        """Test the extraction of tables."""
        blocks = extract_blocks("sample_with_table.pdf")

        tables = [b for b in blocks if b.get("type") == "table"]

        assert tables, "Expected at least one table block"
        for table in tables:
            assert table.get("row_count", 0) >= 2
            assert table.get("col_count", 0) >= 2
            assert table.get("confidence", 0.0) >= 0.2
            assert table.get("text", "") == ""

    def test_heading_detection(self, extract_blocks: ExtractorCallable):
        """Test the extraction of headings."""
        blocks = extract_blocks("sample_with_headings.pdf")

        headings = [b for b in blocks if b.get("type") == "heading"]
        assert headings, "Headings should be classified"

        # Helper for case-insensitive search
        def has_text(text: str) -> bool:
            needle = text.lower()
            return any(needle in b.get("text", "").lower() for b in headings)

        assert has_text("main title"), "Missing H1 heading"
        assert has_text("section title"), "Missing H2 heading"

    def test_list_detection(self, extract_blocks: ExtractorCallable):
        """Test the extraction of lists."""
        blocks = extract_blocks("sample_with_lists.pdf")

        lists = [b for b in blocks if b.get("type") == "list"]
        assert lists, "Expected list blocks for bullet content"

        # List content is in 'items' field, not 'text'
        def has_first_item(block):
            items = block.get("items", [])
            for item in items:
                if "first" in item.lower():
                    return True
            return False

        has_item = any(has_first_item(b) for b in lists)
        assert has_item, "List content text was not preserved"

    def test_paragraph_presence(self, extract_blocks: ExtractorCallable):
        """Test that paragraph text is extracted correctly."""
        blocks = extract_blocks("sample_with_formatting.pdf")

        # The extractor uses "text" type for paragraphs
        paragraphs = [b for b in blocks if b.get("type") in ("paragraph", "text")]
        assert paragraphs, "Paragraph text should be captured"

        text_content = " ".join(b.get("text", "").lower() for b in paragraphs)
        assert "bold text" in text_content
        assert "italic text" in text_content

    def test_collect(self, tmp_path: Path):
        """Test the 'collect' parameter which returns Python objects directly."""
        pdf_path = TEST_DATA_DIR / "sample_with_headings.pdf"
        if not pdf_path.exists():
            pytest.skip(f"Test PDF not found: {pdf_path}")

        pages = to_json(pdf_path, output_file=tmp_path / "output.json", collect=True)

        assert isinstance(pages, list)
        assert pages, "Result should contain page data"

        # Flatten pages to check content
        blocks = []
        for page in pages:
            if isinstance(page, dict) and "data" in page:
                blocks.extend(page["data"])
            else:
                blocks.append(page)
        assert any(b.get("type") == "heading" for b in blocks)
