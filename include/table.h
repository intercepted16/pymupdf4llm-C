#ifndef IMPROVED_TABLE_DETECTION_H
#define IMPROVED_TABLE_DETECTION_H

#include <mupdf/fitz.h>

/**
 * @brief (Deprecated) Checks if a page likely contains a table using an older, simpler algorithm.
 *
 * This function uses a basic clustering algorithm to find 2x2 groups of text blocks,
 * which is a simple indicator of a table. It is less accurate than the improved `page_has_table`.
 *
 * @deprecated This function is deprecated and will be removed in a future version.
 *             Use `page_has_table` instead for better accuracy.
 *
 * @param pdf_path The file path to the PDF document.
 * @param page_number The 0-based index of the page to check.
 * @return 1 if a table is likely present, 0 otherwise.
 */
extern int original_page_has_table(const char *pdf_path, int page_number);

/**
 * @brief Checks if a page is likely to contain a table using an improved detection algorithm.
 *
 * This function employs a more sophisticated approach, including spatial clustering,
 * grid structure analysis, and content analysis to achieve higher accuracy in table detection.
 * It also considers adjacent pages to inform its decision.
 *
 * @param pdf_path The file path to the PDF document.
 * @param page_number The 0-based index of the page to check.
 * @return 1 if a table is likely present, 0 if not, and -1 on error.
 */
extern int page_has_table(const char *pdf_path, int page_number);

#endif // IMPROVED_TABLE_DETECTION_H
