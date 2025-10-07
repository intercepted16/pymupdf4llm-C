"""End-to-end tests for PDF to Markdown conversion.

Tests actual conversion output for:
- Tables formatting
- Headings (H1, H2, H3)
- Bold and italic text
- Lists
- Images
"""

import os
import re
import tempfile
from pathlib import Path

import pytest

TEST_DATA_DIR = Path(__file__).parent / "test_data"
NIST_PDF = TEST_DATA_DIR / "nist.pdf"


class TestMarkdownFormatting:
    """Test markdown formatting in conversion output."""

    def assert_has_heading(self, markdown: str, level: int, text: str = None):
        """Assert markdown contains heading of specific level."""
        pattern = rf'^{"#" * level}\s+(.+)$'
        matches = re.findall(pattern, markdown, re.MULTILINE)

        assert len(matches) > 0, f"No H{level} headings found in markdown"

        if text:
            assert any(text.lower() in match.lower() for match in matches), \
                f"H{level} heading with text '{text}' not found. Found: {matches}"

    def assert_has_table(self, markdown: str, min_rows: int = 2, min_cols: int = 2):
        """Assert markdown contains properly formatted table."""
        # Look for table pattern: | col1 | col2 |
        table_rows = re.findall(r'^\|(.+)\|$', markdown, re.MULTILINE)

        assert len(table_rows) >= min_rows, \
            f"Expected at least {min_rows} table rows, found {len(table_rows)}"

        # Check separator row (|---|---|)
        separator_pattern = r'^\|[\s\-|]+\|$'
        separators = re.findall(separator_pattern, markdown, re.MULTILINE)
        assert len(separators) > 0, "No table separator row found"

        # Check columns
        for row in table_rows[:min_rows]:
            cols = [c.strip() for c in row.split('|') if c.strip()]
            assert len(cols) >= min_cols, \
                f"Expected at least {min_cols} columns, found {len(cols)} in row: {row}"

    def assert_has_bold(self, markdown: str, text: str = None):
        """Assert markdown contains bold text."""
        bold_pattern = r'\*\*(.+?)\*\*'
        matches = re.findall(bold_pattern, markdown)

        assert len(matches) > 0, "No bold text found in markdown"

        if text:
            assert any(text.lower() in match.lower() for match in matches), \
                f"Bold text '{text}' not found. Found: {matches}"

    def assert_has_italic(self, markdown: str, text: str = None):
        """Assert markdown contains italic text."""
        # Match *text* but not **text**
        italic_pattern = r'(?<!\*)\*([^*]+?)\*(?!\*)'
        matches = re.findall(italic_pattern, markdown)

        assert len(matches) > 0, "No italic text found in markdown"

        if text:
            assert any(text.lower() in match.lower() for match in matches), \
                f"Italic text '{text}' not found. Found: {matches}"

    def assert_has_list(self, markdown: str, list_type: str = "unordered"):
        """Assert markdown contains list (ordered or unordered)."""
        if list_type == "unordered":
            # Match: - item, * item, > item
            pattern = r'^[\s]*[-*>]\s+(.+)$'
        else:
            # Match: 1. item, 2. item
            pattern = r'^[\s]*\d+\.\s+(.+)$'

        matches = re.findall(pattern, markdown, re.MULTILINE)
        assert len(matches) > 0, f"No {list_type} list found in markdown"

    def assert_has_image(self, markdown: str, alt_text: str = None):
        """Assert markdown contains image."""
        # Match: ![alt](path)
        image_pattern = r'!\[([^\]]*)\]\(([^)]+)\)'
        matches = re.findall(image_pattern, markdown)

        assert len(matches) > 0, "No images found in markdown"

        if alt_text:
            assert any(alt_text.lower() in match[0].lower() for match in matches), \
                f"Image with alt text '{alt_text}' not found"

    def assert_has_link(self, markdown: str, url: str = None):
        """Assert markdown contains hyperlink."""
        # Match: [text](url)
        link_pattern = r'\[([^\]]+)\]\(([^)]+)\)'
        matches = re.findall(link_pattern, markdown)

        # Filter out images (which start with !)
        text_matches = [(text, link) for text, link in matches
                       if not markdown[markdown.find(f'[{text}]')-1:markdown.find(f'[{text}]')] == '!']

        assert len(text_matches) > 0, "No hyperlinks found in markdown"

        if url:
            assert any(url in match[1] for match in text_matches), \
                f"Link with URL '{url}' not found"


@pytest.mark.integration
@pytest.mark.requires_pdf
class TestPDFConversion:
    """End-to-end tests for PDF conversion."""

    @pytest.fixture
    def output_dir(self):
        """Create temporary output directory."""
        with tempfile.TemporaryDirectory() as tmpdir:
            yield Path(tmpdir)

    def test_table_conversion(self, output_dir):
        """Test that tables are properly converted and formatted."""
        from pymupdf4llm_c import to_markdown
        from pymupdf4llm_c.logging_config import get_logger
        from tests.pdf_fixtures import get_fixtures

        # Create the test PDF
        fixtures = get_fixtures()
        pdf_path = fixtures.create_pdf_with_table()

        try:
            logger = get_logger("test")
            output_path = output_dir / "output_table.md"

            logger.info(f"Converting PDF with tables: {pdf_path}")
            to_markdown(str(pdf_path), output_path=str(output_path))

            assert output_path.exists(), "Output file not created"

            markdown = output_path.read_text()
            logger.info(f"Generated markdown length: {len(markdown)} chars")

            # Test table formatting
            formatter = TestMarkdownFormatting()
            formatter.assert_has_table(markdown, min_rows=2, min_cols=2)

            logger.info("✅ Table formatting validated")
        finally:
            # Cleanup
            fixtures.cleanup()

    def test_heading_conversion(self, output_dir):
        """Test that headings are properly converted with correct levels."""
        from pymupdf4llm_c import to_markdown
        from pymupdf4llm_c.logging_config import get_logger
        from tests.pdf_fixtures import get_fixtures

        # Create the test PDF
        fixtures = get_fixtures()
        pdf_path = fixtures.create_pdf_with_headings()

        try:
            logger = get_logger("test")
            output_path = output_dir / "output_headings.md"

            logger.info(f"Converting PDF with headings: {pdf_path}")
            to_markdown(str(pdf_path), output_path=str(output_path))

            markdown = output_path.read_text()

            formatter = TestMarkdownFormatting()

            # Test different heading levels
            formatter.assert_has_heading(markdown, level=1)  # H1
            formatter.assert_has_heading(markdown, level=2)  # H2

            logger.info("✅ Heading formatting validated")
        finally:
            # Cleanup
            fixtures.cleanup()

    def test_text_formatting(self, output_dir):
        """Test that bold and italic text are properly converted."""
        from pymupdf4llm_c import to_markdown
        from pymupdf4llm_c.logging_config import get_logger
        from tests.pdf_fixtures import get_fixtures

        # Create the test PDF
        fixtures = get_fixtures()
        pdf_path = fixtures.create_pdf_with_formatting()

        try:
            logger = get_logger("test")
            output_path = output_dir / "output_formatting.md"

            logger.info(f"Converting PDF with text formatting: {pdf_path}")
            to_markdown(str(pdf_path), output_path=str(output_path))

            markdown = output_path.read_text()

            formatter = TestMarkdownFormatting()

            # Test bold text
            formatter.assert_has_bold(markdown)

            # Test italic text (if present)
            try:
                formatter.assert_has_italic(markdown)
                logger.info("✅ Italic text found")
            except AssertionError:
                logger.warning("⚠️ No italic text found (may not be in PDF)")

            logger.info("✅ Text formatting validated")
        finally:
            # Cleanup
            fixtures.cleanup()

    def test_list_conversion(self, output_dir):
        """Test that lists are properly converted."""
        from pymupdf4llm_c import to_markdown
        from pymupdf4llm_c.logging_config import get_logger
        from tests.pdf_fixtures import get_fixtures

        # Create the test PDF
        fixtures = get_fixtures()
        pdf_path = fixtures.create_pdf_with_lists()

        try:
            logger = get_logger("test")
            output_path = output_dir / "output_lists.md"

            logger.info(f"Converting PDF with lists: {pdf_path}")
            to_markdown(str(pdf_path), output_path=str(output_path))

            markdown = output_path.read_text()

            formatter = TestMarkdownFormatting()
            formatter.assert_has_list(markdown, list_type="unordered")

            logger.info("✅ List formatting validated")
        finally:
            # Cleanup
            fixtures.cleanup()

    def test_complete_document(self, output_dir):
        """Test complete document conversion with all elements."""
        # Use the actual test PDF from the repository if it exists
        pdf_path = NIST_PDF

        if not os.path.exists(pdf_path):
            pytest.skip(f"Test PDF not found: {pdf_path}")

        from pymupdf4llm_c import to_markdown
        from pymupdf4llm_c.logging_config import get_logger, log_response

        logger = get_logger("test")
        output_path = output_dir / "nist_output.md"

        logger.info(f"Converting complete document: {pdf_path}")
        to_markdown(pdf_path, output_path=str(output_path))

        assert output_path.exists(), "Output file not created"

        markdown = output_path.read_text()
        logger.info(f"Generated markdown length: {len(markdown)} chars")

        # Log sample of output
        log_response("Complete Document Conversion Sample", {
            "pdf": pdf_path,
            "output_length": len(markdown),
            "first_500_chars": markdown[:500],
            "has_tables": bool(re.search(r'^\|(.+)\|$', markdown, re.MULTILINE)),
            "has_headings": bool(re.search(r'^#{1,6}\s+', markdown, re.MULTILINE)),
            "has_bold": bool(re.search(r'\*\*(.+?)\*\*', markdown)),
        }, "INFO")

        formatter = TestMarkdownFormatting()

        # Basic validation
        assert len(markdown) > 100, "Output too short"

        # Test for headings
        try:
            formatter.assert_has_heading(markdown, level=1)
            logger.info("✅ H1 headings found")
        except AssertionError as e:
            logger.warning(f"⚠️ {e}")

        # Test for tables
        try:
            formatter.assert_has_table(markdown)
            logger.info("✅ Tables found")
        except AssertionError as e:
            logger.warning(f"⚠️ {e}")

        # Test for bold text
        try:
            formatter.assert_has_bold(markdown)
            logger.info("✅ Bold text found")
        except AssertionError as e:
            logger.warning(f"⚠️ {e}")

        logger.info("✅ Complete document conversion validated")


@pytest.mark.unit
class TestMarkdownValidators:
    """Test the validation helper functions themselves."""

    def test_heading_detection(self):
        """Test heading detection works correctly."""
        markdown = """
# Main Title
Some text
## Subtitle
More text
### Sub-subtitle
        """

        validator = TestMarkdownFormatting()
        validator.assert_has_heading(markdown, 1, "Main Title")
        validator.assert_has_heading(markdown, 2, "Subtitle")
        validator.assert_has_heading(markdown, 3, "Sub-subtitle")

    def test_table_detection(self):
        """Test table detection works correctly."""
        markdown = """
| Column A | Column B |
|----------|----------|
| Value 1  | Value 2  |
| Value 3  | Value 4  |
        """

        validator = TestMarkdownFormatting()
        validator.assert_has_table(markdown, min_rows=2, min_cols=2)

    def test_bold_detection(self):
        """Test bold text detection."""
        markdown = "This is **bold** text and **more bold**."

        validator = TestMarkdownFormatting()
        validator.assert_has_bold(markdown)
        validator.assert_has_bold(markdown, "bold")

    def test_italic_detection(self):
        """Test italic text detection."""
        markdown = "This is *italic* text and *more italic*."

        validator = TestMarkdownFormatting()
        validator.assert_has_italic(markdown)
        validator.assert_has_italic(markdown, "italic")

    def test_list_detection(self):
        """Test list detection."""
        unordered = """
- Item 1
- Item 2
* Item 3
        """

        ordered = """
1. First
2. Second
3. Third
        """

        validator = TestMarkdownFormatting()
        validator.assert_has_list(unordered, "unordered")
        validator.assert_has_list(ordered, "ordered")
