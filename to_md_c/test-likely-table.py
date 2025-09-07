import ctypes
import sys

lib = ctypes.CDLL("./get_raw_markdown.so")

result = lib.page_has_table(sys.argv[1].encode('utf-8'), int(sys.argv[2]))
print(result)