import sys
from typing import TextIO


def files_equal_ignore_whitespace(file1: str, file2: str) -> bool:
    def clean_lines(f: TextIO) -> list[str]:
        return [line.strip() for line in f if line.strip()]

    with (
        open(file1, "r", encoding="utf-8") as f1,
        open(file2, "r", encoding="utf-8") as f2,
    ):
        lines1 = clean_lines(f1)
        lines2 = clean_lines(f2)

    return lines1 == lines2


# Example usage
if __name__ == "__main__":
    file_a = sys.argv[1]
    file_b = sys.argv[2]
    if files_equal_ignore_whitespace(file_a, file_b):
        print("Files are equal (ignoring whitespace and blank lines).")
    else:
        print("Files differ.")
