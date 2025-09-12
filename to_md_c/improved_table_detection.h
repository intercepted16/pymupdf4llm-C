#ifndef IMPROVED_TABLE_DETECTION_H
#define IMPROVED_TABLE_DETECTION_H

#include "mupdf/fitz.h"

// Original function signature
int improved_page_has_table(const char *pdf_path, int page_number);

// NEW: Function to find and return all table bounding boxes on a page
fz_rect* find_all_tables_on_page(fz_context *ctx, fz_document *doc, int page_number, int* count);

#endif // IMPROVED_TABLE_DETECTION_H
