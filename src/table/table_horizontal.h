#ifndef TABLE_HORIZONTAL_H
#define TABLE_HORIZONTAL_H

#include "table.h"
#include "mupdf/fitz.h"

/**
 * @brief Detect tables with only horizontal dividers (no vertical grid lines).
 *
 * This function handles tables that have horizontal lines separating rows but no
 * vertical lines defining columns. Column boundaries are inferred from text layout.
 *
 * @param h_edges Array of horizontal edges detected on the page
 * @param ctx MuPDF context
 * @param textpage MuPDF text page for extracting text content
 * @param metrics Page metrics for font statistics
 * @return TableArray with detected tables, or NULL if no horizontal-divider tables found
 */
TableArray* find_horizontal_divider_tables(const EdgeArray* h_edges, fz_context* ctx, fz_stext_page* textpage,
                                           const PageMetrics* metrics);

#endif // TABLE_HORIZONTAL_H
