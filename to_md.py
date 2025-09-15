import ctypes
import os
import sys
import time
from dotenv import load_dotenv

load_dotenv(".env")

LIB_PATH = os.getenv("PYMUPDF4LLM_C_LIB_PATH")
if not LIB_PATH:
    raise ValueError("Environment variable PYMUPDF4LLM_C_LIB_PATH not set")

# load the library

c_lib = ctypes.CDLL(LIB_PATH)

# declare argument types (optional but safer)
c_lib.to_markdown.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
c_lib.to_markdown.restype = None

def to_markdown(input_pdf: str, output_md: str):
    """
    Convert a PDF file to Markdown format using the compiled C library.

    Args:
        input_pdf (str): Path to the input PDF file.
        output_md (str): Path to the output Markdown file.

    Returns:
        None
    """
    c_lib.to_markdown(input_pdf.encode('utf-8'), output_md.encode('utf-8'))


def main():
    start = time.time()
    input = sys.argv[1]
    output = sys.argv[2]
    to_markdown(input, output)
    end = time.time()
    print(f"Conversion took {(end - start):.2f} seconds")

if __name__ == "__main__":
    main()
