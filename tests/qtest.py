import sys

from pymupdf4llm_c import to_json

to_json(sys.argv[1], output_file=sys.argv[2])
