"""Tests for API enhancements: page range filtering and metadata."""

from __future__ import annotations

import tempfile
from pathlib import Path

import pytest


@pytest.mark.integration
@pytest.mark.requires_pdf
class TestAPIEnhancements:
    """Test new API features for page filtering and metadata extraction."""

    def test_get_metadata(self):
        """Test metadata extraction returns page count."""
        from pymupdf4llm_c import get_metadata
        from tests.pdf_fixtures import get_fixtures

        fixtures = get_fixtures()
        pdf_path = fixtures.create_pdf_with_headings()

        try:
            metadata = get_metadata(pdf_path)
            assert "page_count" in metadata
            assert isinstance(metadata["page_count"], int)
            assert metadata["page_count"] >= 1
        finally:
            fixtures.cleanup()

    def test_page_range_filtering(self):
        """Test extracting specific page range."""
        from pymupdf4llm_c import to_json
        from tests.pdf_fixtures import get_fixtures

        fixtures = get_fixtures()
        # Create a multi-page PDF
        pdf_path = fixtures.create_pdf_with_headings()

        try:
            with tempfile.TemporaryDirectory() as tmpdir:
                output_dir = Path(tmpdir)

                # Extract only first page (page 0)
                json_files = to_json(
                    pdf_path, output_dir=output_dir, pages=(0, 0)
                )

                # Should only have one page
                assert len(json_files) == 1
                assert json_files[0].name == "page_000.json"
        finally:
            fixtures.cleanup()

    def test_collect_with_page_range(self):
        """Test collect parameter works with page range."""
        from pymupdf4llm_c import to_json
        from tests.pdf_fixtures import get_fixtures

        fixtures = get_fixtures()
        pdf_path = fixtures.create_pdf_with_headings()

        try:
            with tempfile.TemporaryDirectory() as tmpdir:
                output_dir = Path(tmpdir)

                # Extract and collect first page
                collected = to_json(
                    pdf_path,
                    output_dir=output_dir,
                    pages=(0, 0),
                    collect=True,
                )

                assert isinstance(collected, list)
                assert len(collected) == 1
                assert isinstance(collected[0], list)
        finally:
            fixtures.cleanup()
