#ifndef TABLE_DETECTION_H
#define TABLE_DETECTION_H

#include <mupdf/fitz.h>

/**
 * @brief Heuristically detect whether a PDF page contains tabular content.
 *
 * Opens the requested page via MuPDF, performs lightweight word clustering, and
 * looks for orthogonal edge intersections that indicate a table grid.
 *
 * @param pdf_path The file path to the PDF document.
 * @param page_number The 0-based index of the page to inspect.
 * @return 1 if a table is likely present, 0 otherwise.
 */
extern int page_has_table(const char *pdf_path, int page_number);

#endif // TABLE_DETECTION_H
