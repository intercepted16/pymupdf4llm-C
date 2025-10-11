import re
import sys
from collections import Counter


def extract_unicode_placeholders(filename, outname="unicode_report.txt"):
    # Regex to capture <U+XXXX> or <U+XXXXX>
    pattern = re.compile(r"<U\+[0-9A-F]{4,6}>")

    with open(filename, "r", encoding="utf-8", errors="ignore") as f:
        text = f.read()

    matches = pattern.findall(text)
    counts = Counter(matches)

    # Sort by codepoint value
    unique_sorted = sorted(counts.items(), key=lambda x: int(x[0][3:-1], 16))

    with open(outname, "w", encoding="utf-8") as out:
        for code, count in unique_sorted:
            out.write(f"{code}  count={count}\n")

    print(f"✅ Extracted {len(unique_sorted)} unique placeholders into {outname}")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python extract_unicode.py inputfile.txt [outputfile.txt]")
        sys.exit(1)

    infile = sys.argv[1]
    outfile = sys.argv[2] if len(sys.argv) > 2 else "unicode_report.txt"
    extract_unicode_placeholders(infile, outfile)
