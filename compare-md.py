import sys
import hashlib
import re

def normalize(text):
    # Remove all whitespace and non-ASCII characters
    text = re.sub(r'\s+', '', text)
    text = re.sub(r'[^\x00-\x7F]+', '', text)
    return text

def file_hash(path):
    with open(path, 'r', encoding='utf-8') as f:
        content = f.read()
    normalized = normalize(content)
    return hashlib.md5(normalized.encode('utf-8')).hexdigest()

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} file1.md file2.md")
        sys.exit(1)

    hash1 = file_hash(sys.argv[1])
    hash2 = file_hash(sys.argv[2])

    if hash1 == hash2:
        print("Files are the same (after normalization).")
    else:
        print("Files differ.")
