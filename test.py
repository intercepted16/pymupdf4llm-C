from pymupdf4llm_c import to_markdown
from sys import argv

def main():
    if len(argv) < 3:
        print("enter a pdf input & output")
        return -1

    to_markdown(argv[1], output_path=argv[2])

if __name__ == "__main__":
    main()

