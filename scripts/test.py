import sys
from time import perf_counter
from pymupdf4llm_c import to_json


def main(input_pdf: str, output_file: str):
    start = perf_counter()
    result = to_json(input_pdf, output=output_file)
    print(f"extracted to: {result.path}")
    end = perf_counter()
    elapsed = end - start
    print(f"Time taken: {elapsed:.2f}")


if __name__ == "__main__":
    argv = sys.argv.copy()
    argv.pop(0)
    
    if len(argv) < 2:
        print(f"Usage: `python3 {sys.argv[0]} <input_pdf> <output_file>`")
        sys.exit(1)
    
    _in = argv[0]
    _out = argv[1]
    
    main(_in, _out)

