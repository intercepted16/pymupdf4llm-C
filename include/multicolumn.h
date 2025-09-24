/*
 * MuPDF C Multi-Column Detector Header
 *
 * Header file for the C port of PyMuPDF multi-column detection functionality.
 *
 * Copyright 2024 Artifex Software, Inc.
 * License GNU Affero GPL 3.0
 */

#ifndef MULTICOLUMN_H
#define MULTICOLUMN_H

#include "mupdf/fitz.h"

/**
 * @brief Options for configuring the `column_boxes` function.
 *
 * This struct allows for fine-tuned control over the column detection logic,
 * such as setting margins, providing pre-existing text pages, and specifying
 * areas to avoid.
 */
typedef struct
{
    float footer_margin;          /**< The margin to ignore at the bottom of the page. */
    float header_margin;          /**< The margin to ignore at the top of the page. */
    int no_image_text;            /**< If 1, ignore text that is located over images. */
    fz_stext_page* textpage_param;/**< An optional, pre-existing structured text page. Can be `NULL`. */
    fz_rect* paths;               /**< An optional array of rectangles representing background regions. */
    int path_count;               /**< The number of rectangles in the `paths` array. */
    fz_rect* avoid;               /**< An optional array of rectangles to avoid (e.g., images, tables). */
    int avoid_count;              /**< The number of rectangles in the `avoid` array. */
    int ignore_images;            /**< If 1, ignore all image regions during analysis. */
    int* result_count;            /**< A pointer to an integer where the number of resulting columns will be stored. */
    int preset;                   /**< A preset for default options (e.g., 1 for default margins). 0 for none. */
} ColumnBoxesOptions;

/**
 * @brief Detects column boundaries on a page and returns their bounding boxes.
 *
 * This function analyzes the layout of a PDF page to identify distinct
 * text columns. It can be configured to ignore headers, footers, images,
 * and other specified regions.
 *
 * @param pdf_path The file path to the PDF document.
 * @param page_number The 0-based index of the page to analyze.
 * @param[out] table_count A pointer to an integer where the number of detected columns will be stored.
 * @param opts An optional pointer to a `ColumnBoxesOptions` struct for configuration.
 *             If `NULL`, default settings will be used.
 * @return A dynamically allocated array of `fz_rect` representing the column bounding boxes.
 *         The caller is responsible for freeing this array. Returns `NULL` on failure.
 */
fz_rect* column_boxes(const char* pdf_path, int page_number, int* table_count,
                      ColumnBoxesOptions* opts);

/**
 * @brief Checks if a page is likely to contain a table.
 *
 * This function uses a heuristic-based approach, including column detection,
 * to determine if a page has tabular data.
 *
 * @param pdf_path The file path to the PDF document.
 * @param page_number The 0-based index of the page to check.
 * @return 1 if a table is likely present, 0 if not, and -1 on error.
 */
int page_has_table(const char* pdf_path, int page_number);

/**
 * @brief Checks if a given rectangle intersects with any rectangle in a list.
 *
 * @param bb The rectangle to check.
 * @param bboxes An array of rectangles to check against.
 * @param bbox_count The number of rectangles in the `bboxes` array.
 * @return 1 if `bb` intersects with any rectangle in `bboxes`, 0 otherwise.
 */
int intersects_bboxes_rect(fz_rect bb, fz_rect* bboxes, int bbox_count);

#endif /* MULTICOLUMN_H */
