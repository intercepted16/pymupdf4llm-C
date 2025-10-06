"""PDF fixtures for test setup and teardown.

This module provides a class-based approach to creating and managing test PDFs.
Each test can call the specific PDF creation method it needs.
"""

from pathlib import Path
from typing import Optional
from reportlab.lib.pagesizes import letter
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.lib.units import inch
from reportlab.lib import colors
from reportlab.platypus import SimpleDocTemplate, Table, TableStyle, Paragraph, Spacer
from reportlab.lib.enums import TA_CENTER


class PDFTestFixtures:
    """Class for creating and managing test PDF files."""

    def __init__(self, test_data_dir: Optional[Path] = None):
        """Initialize the PDF fixtures manager.
        
        Args:
            test_data_dir: Directory to store test PDFs. Defaults to 'test_data'.
        """
        self.test_data_dir = test_data_dir or Path("test_data")
        self.created_files = []

    def setup(self):
        """Create test_data directory if it doesn't exist."""
        if not self.test_data_dir.exists():
            self.test_data_dir.mkdir(exist_ok=True, parents=True)

    def create_pdf_with_table(self) -> Path:
        """Create a PDF with tables for testing.
        
        Returns:
            Path to the created PDF file.
        """
        output_path = self.test_data_dir / "sample_with_table.pdf"
        if output_path.exists():
            return output_path
        doc = SimpleDocTemplate(str(output_path), pagesize=letter)
        styles = getSampleStyleSheet()
        story = []
        # Add title
        title_style = ParagraphStyle(
            'CustomTitle',
            parent=styles['Heading1'],
            fontSize=24,
            textColor=colors.HexColor('#000000'),
            spaceAfter=30,
            alignment=TA_CENTER
        )
        story.append(Paragraph("Document with Tables", title_style))
        story.append(Spacer(1, 0.2 * inch))
        # Add some text
        story.append(Paragraph("This document contains sample tables for testing.", styles['Normal']))
        story.append(Spacer(1, 0.3 * inch))
        # Create a table
        data = [
            ['Product', 'Quantity', 'Price', 'Total'],
            ['Widget A', '10', '$5.00', '$50.00'],
            ['Widget B', '5', '$10.00', '$50.00'],
            ['Widget C', '3', '$25.00', '$75.00'],
            ['Total', '', '', '$175.00']
        ]
        table = Table(data, colWidths=[2*inch, 1.5*inch, 1.5*inch, 1.5*inch])
        table.setStyle(TableStyle([
            ('BACKGROUND', (0, 0), (-1, 0), colors.grey),
            ('TEXTCOLOR', (0, 0), (-1, 0), colors.whitesmoke),
            ('ALIGN', (0, 0), (-1, -1), 'CENTER'),
            ('FONTNAME', (0, 0), (-1, 0), 'Helvetica-Bold'),
            ('FONTSIZE', (0, 0), (-1, 0), 14),
            ('BOTTOMPADDING', (0, 0), (-1, 0), 12),
            ('BACKGROUND', (0, 1), (-1, -1), colors.beige),
            ('GRID', (0, 0), (-1, -1), 1, colors.black)
        ]))
        story.append(table)
        story.append(Spacer(1, 0.5 * inch))
        # Add another table
        story.append(Paragraph("Employee Information", styles['Heading2']))
        story.append(Spacer(1, 0.2 * inch))
        data2 = [
            ['Name', 'Department', 'Role'],
            ['John Doe', 'Engineering', 'Developer'],
            ['Jane Smith', 'Marketing', 'Manager'],
            ['Bob Johnson', 'Sales', 'Representative']
        ]
        table2 = Table(data2, colWidths=[2*inch, 2*inch, 2*inch])
        table2.setStyle(TableStyle([
            ('BACKGROUND', (0, 0), (-1, 0), colors.HexColor('#4CAF50')),
            ('TEXTCOLOR', (0, 0), (-1, 0), colors.whitesmoke),
            ('ALIGN', (0, 0), (-1, -1), 'LEFT'),
            ('FONTNAME', (0, 0), (-1, 0), 'Helvetica-Bold'),
            ('BOTTOMPADDING', (0, 0), (-1, 0), 12),
            ('GRID', (0, 0), (-1, -1), 1, colors.black)
        ]))
        story.append(table2)
        doc.build(story)
        self.created_files.append(output_path)
        return output_path

    def create_pdf_with_headings(self) -> Path:
        """Create a PDF with various heading levels.
        
        Returns:
            Path to the created PDF file.
        """
        output_path = self.test_data_dir / "sample_with_headings.pdf"
        if output_path.exists():
            return output_path
        doc = SimpleDocTemplate(str(output_path), pagesize=letter)
        styles = getSampleStyleSheet()
        story = []
        # H1
        story.append(Paragraph("Main Title (H1)", styles['Heading1']))
        story.append(Spacer(1, 0.2 * inch))
        story.append(Paragraph("This is the main title of the document.", styles['Normal']))
        story.append(Spacer(1, 0.3 * inch))
        # H2
        story.append(Paragraph("Section Title (H2)", styles['Heading2']))
        story.append(Spacer(1, 0.2 * inch))
        story.append(Paragraph("This is a section with H2 heading.", styles['Normal']))
        story.append(Spacer(1, 0.3 * inch))
        # H3
        story.append(Paragraph("Subsection Title (H3)", styles['Heading3']))
        story.append(Spacer(1, 0.2 * inch))
        story.append(Paragraph("This is a subsection with H3 heading.", styles['Normal']))
        story.append(Spacer(1, 0.3 * inch))
        # Another H2
        story.append(Paragraph("Another Section (H2)", styles['Heading2']))
        story.append(Spacer(1, 0.2 * inch))
        story.append(Paragraph("Content under another H2 heading.", styles['Normal']))
        story.append(Spacer(1, 0.3 * inch))
        # H3 under second H2
        story.append(Paragraph("Nested Subsection (H3)", styles['Heading3']))
        story.append(Spacer(1, 0.2 * inch))
        story.append(Paragraph("This H3 is nested under the second H2.", styles['Normal']))
        doc.build(story)
        self.created_files.append(output_path)
        return output_path

    def create_pdf_with_formatting(self) -> Path:
        """Create a PDF with bold and italic text.
        
        Returns:
            Path to the created PDF file.
        """
        output_path = self.test_data_dir / "sample_with_formatting.pdf"
        if output_path.exists():
            return output_path
        doc = SimpleDocTemplate(str(output_path), pagesize=letter)
        styles = getSampleStyleSheet()
        story = []
        # Title
        story.append(Paragraph("Text Formatting Examples", styles['Heading1']))
        story.append(Spacer(1, 0.3 * inch))
        # Bold text
        story.append(Paragraph("This paragraph contains <b>bold text</b> in the middle.", styles['Normal']))
        story.append(Spacer(1, 0.2 * inch))
        # Italic text
        story.append(Paragraph("This paragraph contains <i>italic text</i> for emphasis.", styles['Normal']))
        story.append(Spacer(1, 0.2 * inch))
        # Both
        story.append(Paragraph("You can also have <b>bold</b> and <i>italic</i> in the same paragraph.", styles['Normal']))
        story.append(Spacer(1, 0.2 * inch))
        # Bold italic
        story.append(Paragraph("And even <b><i>bold italic</i></b> text.", styles['Normal']))
        story.append(Spacer(1, 0.3 * inch))
        # Multiple instances
        story.append(Paragraph("Multiple <b>bold words</b> and <b>more bold</b> text.", styles['Normal']))
        doc.build(story)
        self.created_files.append(output_path)
        return output_path

    def create_pdf_with_lists(self) -> Path:
        """Create a PDF with lists.
        
        Returns:
            Path to the created PDF file.
        """
        output_path = self.test_data_dir / "sample_with_lists.pdf"
        if output_path.exists():
            return output_path
        doc = SimpleDocTemplate(str(output_path), pagesize=letter)
        styles = getSampleStyleSheet()
        story = []
        # Title
        story.append(Paragraph("Document with Lists", styles['Heading1']))
        story.append(Spacer(1, 0.3 * inch))
        # Unordered list
        story.append(Paragraph("Unordered List:", styles['Heading2']))
        story.append(Spacer(1, 0.1 * inch))
        story.append(Paragraph("• First item", styles['Normal']))
        story.append(Paragraph("• Second item", styles['Normal']))
        story.append(Paragraph("• Third item", styles['Normal']))
        story.append(Spacer(1, 0.3 * inch))
        # Ordered list
        story.append(Paragraph("Ordered List:", styles['Heading2']))
        story.append(Spacer(1, 0.1 * inch))
        story.append(Paragraph("1. First step", styles['Normal']))
        story.append(Paragraph("2. Second step", styles['Normal']))
        story.append(Paragraph("3. Third step", styles['Normal']))
        story.append(Spacer(1, 0.3 * inch))
        # Nested list
        story.append(Paragraph("Nested List:", styles['Heading2']))
        story.append(Spacer(1, 0.1 * inch))
        story.append(Paragraph("- Main item 1", styles['Normal']))
        story.append(Paragraph("  - Sub item 1.1", styles['Normal']))
        story.append(Paragraph("  - Sub item 1.2", styles['Normal']))
        story.append(Paragraph("- Main item 2", styles['Normal']))
        doc.build(story)
        self.created_files.append(output_path)
        return output_path

    def cleanup(self):
        """No-op: Do not clean up test data folder or files."""
        pass
    def cleanup_all(self):
        """No-op: Do not clean up test data folder or files."""
        pass


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
