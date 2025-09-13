#ifndef IMPROVED_TABLE_DETECTION_H
#define IMPROVED_TABLE_DETECTION_H

#include <mupdf/fitz.h>

// Deprecated: original method of table detection
extern int original_page_has_table(const char *pdf_path, int page_number);

// Improved function signature
extern int page_has_table(const char *pdf_path, int page_number);

#endif // IMPROVED_TABLE_DETECTION_H
