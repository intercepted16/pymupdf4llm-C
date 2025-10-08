import fitz  # PyMuPDF
import sys


def count_tables(pdf_path):
    doc = fitz.open(pdf_path)
    total_tables = 0
    for page_num, page in enumerate(doc, start=1):
        try:
            tabs = page.find_tables()
            print(f"Page {page_num}: {len(tabs.tables)} tables")
            total_tables += len(tabs.tables)
        except Exception as e:
            print(f"Page {page_num}: error detecting tables ({e})")
    doc.close()
    print(f"\nTotal tables detected: {total_tables}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python count_tables.py input.pdf")
        sys.exit(1)
    count_tables(sys.argv[1])
