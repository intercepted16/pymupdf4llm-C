"""PDF fixtures for test setup and teardown.

This module provides a class-based approach to creating and managing test PDFs.
Each test can call the specific PDF creation method it needs.
"""

from pathlib import Path
from typing import Callable, Optional
import shutil

try:  # Optional dependency used only when available.
    from reportlab.lib.pagesizes import letter
    from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
    from reportlab.lib.units import inch
    from reportlab.lib import colors
    from reportlab.platypus import (
        Paragraph,
        SimpleDocTemplate,
        Spacer,
        Table,
        TableStyle,
    )
    from reportlab.lib.enums import TA_CENTER
except ModuleNotFoundError:  # pragma: no cover - exercised when dependency missing
    REPORTLAB_AVAILABLE = False
else:  # pragma: no cover - exercised when dependency present
    REPORTLAB_AVAILABLE = True


DEFAULT_DATA_DIR = Path(__file__).resolve().parent / "test_data"


class PDFTestFixtures:
    """Class for creating and managing test PDF files."""

    def __init__(self, test_data_dir: Optional[Path] = None):
        """Initialize the PDF fixtures manager and output directory."""
        self.test_data_dir = Path(test_data_dir) if test_data_dir else DEFAULT_DATA_DIR
        self.created_files = []

    def setup(self):
        """Create test_data directory if it doesn't exist."""
        if not self.test_data_dir.exists():
            self.test_data_dir.mkdir(exist_ok=True, parents=True)

    def _provide_fixture(self, filename: str, builder: Callable[[Path], None]) -> Path:
        """Return an existing fixture or build it when possible."""
        target = self.test_data_dir / filename
        if target.exists():
            return target

        if REPORTLAB_AVAILABLE:
            target.parent.mkdir(exist_ok=True, parents=True)
            builder(target)
            self.created_files.append(target)
            return target

        bundled = DEFAULT_DATA_DIR / filename
        if not bundled.exists():
            raise RuntimeError(
                "reportlab is not available and bundled fixture is missing: " + filename
            )

        if target != bundled:
            target.parent.mkdir(exist_ok=True, parents=True)
            shutil.copy2(bundled, target)
            self.created_files.append(target)
            return target

        return bundled

    def create_pdf_with_table(self) -> Path:
        """Create a PDF with tables for testing.

        Returns:
            Path to the created PDF file.
        """

        def builder(output_path: Path) -> None:
            doc = SimpleDocTemplate(str(output_path), pagesize=letter)
            styles = getSampleStyleSheet()
            story = []
            title_style = ParagraphStyle(
                "CustomTitle",
                parent=styles["Heading1"],
                fontSize=24,
                textColor=colors.HexColor("#000000"),
                spaceAfter=30,
                alignment=TA_CENTER,
            )
            story.append(Paragraph("Document with Tables", title_style))
            story.append(Spacer(1, 0.2 * inch))
            story.append(
                Paragraph(
                    "This document contains sample tables for testing.",
                    styles["Normal"],
                )
            )
            story.append(Spacer(1, 0.3 * inch))

            data = [
                ["Product", "Quantity", "Price", "Total"],
                ["Widget A", "10", "$5.00", "$50.00"],
                ["Widget B", "5", "$10.00", "$50.00"],
                ["Widget C", "3", "$25.00", "$75.00"],
                ["Total", "", "", "$175.00"],
            ]
            table = Table(
                data, colWidths=[2 * inch, 1.5 * inch, 1.5 * inch, 1.5 * inch]
            )
            table.setStyle(
                TableStyle(
                    [
                        ("BACKGROUND", (0, 0), (-1, 0), colors.grey),
                        ("TEXTCOLOR", (0, 0), (-1, 0), colors.whitesmoke),
                        ("ALIGN", (0, 0), (-1, -1), "CENTER"),
                        ("FONTNAME", (0, 0), (-1, 0), "Helvetica-Bold"),
                        ("FONTSIZE", (0, 0), (-1, 0), 14),
                        ("BOTTOMPADDING", (0, 0), (-1, 0), 12),
                        ("BACKGROUND", (0, 1), (-1, -1), colors.beige),
                        ("GRID", (0, 0), (-1, -1), 1, colors.black),
                    ]
                )
            )
            story.append(table)
            story.append(Spacer(1, 0.5 * inch))

            story.append(Paragraph("Employee Information", styles["Heading2"]))
            story.append(Spacer(1, 0.2 * inch))
            data2 = [
                ["Name", "Department", "Role"],
                ["John Doe", "Engineering", "Developer"],
                ["Jane Smith", "Marketing", "Manager"],
                ["Bob Johnson", "Sales", "Representative"],
            ]
            table2 = Table(data2, colWidths=[2 * inch, 2 * inch, 2 * inch])
            table2.setStyle(
                TableStyle(
                    [
                        ("BACKGROUND", (0, 0), (-1, 0), colors.HexColor("#4CAF50")),
                        ("TEXTCOLOR", (0, 0), (-1, 0), colors.whitesmoke),
                        ("ALIGN", (0, 0), (-1, -1), "LEFT"),
                        ("FONTNAME", (0, 0), (-1, 0), "Helvetica-Bold"),
                        ("BOTTOMPADDING", (0, 0), (-1, 0), 12),
                        ("GRID", (0, 0), (-1, -1), 1, colors.black),
                    ]
                )
            )
            story.append(table2)
            doc.build(story)

        return self._provide_fixture("sample_with_table.pdf", builder)

    def create_pdf_with_headings(self) -> Path:
        """Create a PDF with various heading levels.

        Returns:
            Path to the created PDF file.
        """

        def builder(output_path: Path) -> None:
            doc = SimpleDocTemplate(str(output_path), pagesize=letter)
            styles = getSampleStyleSheet()
            story = []
            story.append(Paragraph("Main Title (H1)", styles["Heading1"]))
            story.append(Spacer(1, 0.2 * inch))
            story.append(
                Paragraph("This is the main title of the document.", styles["Normal"])
            )
            story.append(Spacer(1, 0.3 * inch))
            story.append(Paragraph("Section Title (H2)", styles["Heading2"]))
            story.append(Spacer(1, 0.2 * inch))
            story.append(
                Paragraph("This is a section with H2 heading.", styles["Normal"])
            )
            story.append(Spacer(1, 0.3 * inch))
            story.append(Paragraph("Subsection Title (H3)", styles["Heading3"]))
            story.append(Spacer(1, 0.2 * inch))
            story.append(
                Paragraph("This is a subsection with H3 heading.", styles["Normal"])
            )
            story.append(Spacer(1, 0.3 * inch))
            story.append(Paragraph("Another Section (H2)", styles["Heading2"]))
            story.append(Spacer(1, 0.2 * inch))
            story.append(
                Paragraph("Content under another H2 heading.", styles["Normal"])
            )
            story.append(Spacer(1, 0.3 * inch))
            story.append(Paragraph("Nested Subsection (H3)", styles["Heading3"]))
            story.append(Spacer(1, 0.2 * inch))
            story.append(
                Paragraph("This H3 is nested under the second H2.", styles["Normal"])
            )
            doc.build(story)

        return self._provide_fixture("sample_with_headings.pdf", builder)

    def create_pdf_with_formatting(self) -> Path:
        """Create a PDF with bold and italic text.

        Returns:
            Path to the created PDF file.
        """

        def builder(output_path: Path) -> None:
            doc = SimpleDocTemplate(str(output_path), pagesize=letter)
            styles = getSampleStyleSheet()
            story = []
            story.append(Paragraph("Text Formatting Examples", styles["Heading1"]))
            story.append(Spacer(1, 0.3 * inch))
            story.append(
                Paragraph(
                    "This paragraph contains <b>bold text</b> in the middle.",
                    styles["Normal"],
                )
            )
            story.append(Spacer(1, 0.2 * inch))
            story.append(
                Paragraph(
                    "This paragraph contains <i>italic text</i> for emphasis.",
                    styles["Normal"],
                )
            )
            story.append(Spacer(1, 0.2 * inch))
            story.append(
                Paragraph(
                    "You can also have <b>bold</b> and <i>italic</i> in the same paragraph.",
                    styles["Normal"],
                )
            )
            story.append(Spacer(1, 0.2 * inch))
            story.append(
                Paragraph("And even <b><i>bold italic</i></b> text.", styles["Normal"])
            )
            story.append(Spacer(1, 0.3 * inch))
            story.append(
                Paragraph(
                    "Multiple <b>bold words</b> and <b>more bold</b> text.",
                    styles["Normal"],
                )
            )
            doc.build(story)

        return self._provide_fixture("sample_with_formatting.pdf", builder)

    def create_pdf_with_lists(self) -> Path:
        """Create a PDF with lists.

        Returns:
            Path to the created PDF file.
        """

        def builder(output_path: Path) -> None:
            doc = SimpleDocTemplate(str(output_path), pagesize=letter)
            styles = getSampleStyleSheet()
            story = []
            story.append(Paragraph("Document with Lists", styles["Heading1"]))
            story.append(Spacer(1, 0.3 * inch))
            story.append(Paragraph("Unordered List:", styles["Heading2"]))
            story.append(Spacer(1, 0.1 * inch))
            story.append(Paragraph("• First item", styles["Normal"]))
            story.append(Paragraph("• Second item", styles["Normal"]))
            story.append(Paragraph("• Third item", styles["Normal"]))
            story.append(Spacer(1, 0.3 * inch))
            story.append(Paragraph("Ordered List:", styles["Heading2"]))
            story.append(Spacer(1, 0.1 * inch))
            story.append(Paragraph("1. First step", styles["Normal"]))
            story.append(Paragraph("2. Second step", styles["Normal"]))
            story.append(Paragraph("3. Third step", styles["Normal"]))
            story.append(Spacer(1, 0.3 * inch))
            story.append(Paragraph("Nested List:", styles["Heading2"]))
            story.append(Spacer(1, 0.1 * inch))
            story.append(Paragraph("- Main item 1", styles["Normal"]))
            story.append(Paragraph("  - Sub item 1.1", styles["Normal"]))
            story.append(Paragraph("  - Sub item 1.2", styles["Normal"]))
            story.append(Paragraph("- Main item 2", styles["Normal"]))
            doc.build(story)

        return self._provide_fixture("sample_with_lists.pdf", builder)

    def cleanup(self):
        """Remove any artifacts generated during the test run."""
        for path in list(self.created_files):
            try:
                path.unlink()
            except FileNotFoundError:
                pass
            else:
                self.created_files.remove(path)

        # Remove the directory if it was created just for the test and is empty.
        if self.test_data_dir != DEFAULT_DATA_DIR:
            try:
                self.test_data_dir.rmdir()
            except OSError:
                # Directory still contains pre-existing data or subdirectories.
                pass

    def cleanup_all(self):
        """Fully remove generated artifacts, including the container directory."""
        self.cleanup()
        if self.test_data_dir != DEFAULT_DATA_DIR and self.test_data_dir.exists():
            shutil.rmtree(self.test_data_dir, ignore_errors=True)


# Convenience functions for standalone usage
def get_fixtures(test_data_dir: Optional[Path] = None) -> PDFTestFixtures:
    """Get a PDFTestFixtures instance.

    Args:
        test_data_dir: Directory to store test PDFs.

    Returns:
        Initialized PDFTestFixtures instance.
    """
    fixtures = PDFTestFixtures(test_data_dir)
    fixtures.setup()
    return fixtures
