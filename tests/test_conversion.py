"""End-to-end tests for the JSON extraction pipeline."""

from __future__ import annotations

import json
import tempfile
from pathlib import Path
from typing import Iterable, List, Sequence

import pytest

TEST_DATA_DIR = Path(__file__).parent / "test_data"
NIST_PDF = TEST_DATA_DIR / "nist.pdf"


def _load_blocks(paths: Sequence[Path]) -> List[dict]:
    blocks: List[dict] = []
    for path in sorted(paths):
        with path.open("r", encoding="utf-8") as fh:
            blocks.extend(json.load(fh))
    return blocks


class BlockAssertions:
    """Helper methods for asserting block properties within extracted JSON."""

    def __init__(self, blocks: Iterable[dict]):
        self._blocks = list(blocks)

    def of_type(self, block_type: str) -> List[dict]:
        return [block for block in self._blocks if block.get("type") == block_type]

    def containing(self, text: str) -> List[dict]:
        needle = text.lower()
        return [
            block for block in self._blocks if needle in block.get("text", "").lower()
        ]

    def has_block(self, block_type: str, *, text: str | None = None) -> bool:
        candidates = self.of_type(block_type)
        if text is None:
            return len(candidates) > 0
        needle = text.lower()
        return any(needle in block.get("text", "").lower() for block in candidates)


@pytest.mark.integration
@pytest.mark.requires_pdf
class TestJSONExtraction:
    """End-to-end coverage for the new JSON extractor."""

    @pytest.fixture
    def output_dir(self) -> Iterable[Path]:
        with tempfile.TemporaryDirectory() as tmpdir:
            yield Path(tmpdir)

    def test_table_detection(self, output_dir: Path):
        from pymupdf4llm_c import to_json
        from tests.pdf_fixtures import get_fixtures

        fixtures = get_fixtures()
        pdf_path = fixtures.create_pdf_with_table()

        try:
            json_files = to_json(pdf_path, output_dir=output_dir)
            blocks = BlockAssertions(_load_blocks(json_files))

            tables = blocks.of_type("table")
            assert tables, "Expected at least one table block"
            for table in tables:
                assert table.get("row_count", 0) >= 2
                assert table.get("col_count", 0) >= 2
                assert table.get("confidence", 0.0) >= 0.2
                assert table.get("text", "") == ""

        finally:
            fixtures.cleanup()

    def test_heading_detection(self, output_dir: Path):
        from pymupdf4llm_c import to_json
        from tests.pdf_fixtures import get_fixtures

        fixtures = get_fixtures()
        pdf_path = fixtures.create_pdf_with_headings()

        try:
            json_files = to_json(pdf_path, output_dir=output_dir)
            blocks = BlockAssertions(_load_blocks(json_files))

            headings = blocks.of_type("heading")
            assert headings, "Headings should be classified"
            assert blocks.has_block("heading", text="Main Title"), "Missing H1 heading"
            assert blocks.has_block(
                "heading", text="Section Title"
            ), "Missing H2 heading"
        finally:
            fixtures.cleanup()

    def test_list_detection(self, output_dir: Path):
        from pymupdf4llm_c import to_json
        from tests.pdf_fixtures import get_fixtures

        fixtures = get_fixtures()
        pdf_path = fixtures.create_pdf_with_lists()

        try:
            json_files = to_json(pdf_path, output_dir=output_dir)
            blocks = BlockAssertions(_load_blocks(json_files))

            lists = blocks.of_type("list")
            assert lists, "Expected list blocks for bullet content"
            assert any("first item" in block.get("text", "").lower() for block in lists)
        finally:
            fixtures.cleanup()

    def test_paragraph_presence(self, output_dir: Path):
        from pymupdf4llm_c import to_json
        from tests.pdf_fixtures import get_fixtures

        fixtures = get_fixtures()
        pdf_path = fixtures.create_pdf_with_formatting()

        try:
            json_files = to_json(pdf_path, output_dir=output_dir)
            blocks = BlockAssertions(_load_blocks(json_files))

            paragraphs = blocks.of_type("paragraph")
            assert paragraphs, "Paragraph text should be captured"
            assert blocks.has_block("paragraph", text="bold text")
            assert blocks.has_block("paragraph", text="italic text")
        finally:
            fixtures.cleanup()

    def test_collect_parameter(self, output_dir: Path):
        from pymupdf4llm_c import to_json
        from tests.pdf_fixtures import get_fixtures

        fixtures = get_fixtures()
        pdf_path = fixtures.create_pdf_with_headings()

        try:
            collected = to_json(pdf_path, output_dir=output_dir, collect=True)
            assert isinstance(collected, list)
            assert collected, "Collected result should contain block data"
            assert any(
                block.get("type") == "heading" for page in collected for block in page
            )
        finally:
            fixtures.cleanup()
