"""
Test the table extraction capabilities of the Python script using PyMuPDF.
"""
import sys
import pymupdf  # type: ignore
from multi_column import column_boxes
from new_table import GridReconstructionSettings, TableProcessor
from pymupdf import Rect


def box_to_md(bbox: Rect) -> str:
    settings = GridReconstructionSettings(
        snap_tolerance=3.0,
        join_tolerance=3.0,
        intersection_tolerance=3.0
    )
    processor = TableProcessor(page, settings)
    return processor.process_tables([bbox], strategy="lines")

# 1. Get the PDF document
def main():
    file_name = sys.argv[1]
    doc = pymupdf.open(file_name)
    for page in doc:
        print(f"Page {page.number + 1}")
        cols = column_boxes(page)
        # Initialize table processor
        settings = GridReconstructionSettings(
            snap_tolerance=3.0,
            join_tolerance=3.0,
            intersection_tolerance=3.0
        )
        processor = TableProcessor(page, settings)
            
        # Process tables using detected bounding boxes
        tables = processor.process_tables(cols, strategy="lines")
        for table in tables:
            print(table.to_pandas())
        print(f"Extracted {len(tables)} tables")

    doc.close()



if __name__ == "__main__":
    main()