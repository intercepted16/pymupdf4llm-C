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

typedef struct
{
    float footer_margin;
    float header_margin;
    int no_image_text;
    fz_stext_page* textpage_param;
    fz_rect* paths;
    int path_count;
    fz_rect* avoid;
    int avoid_count;
    int ignore_images;
    int* result_count;
    int preset; // 0=none, 1=default, etc.
} ColumnBoxesOptions;

/**
 * Detect column boundaries and return text rectangles
 *
 * @param pdf_path Path to the PDF document
 * @param page_number Page number to analyze
 * @param table_count Output parameter for number of columns found
 * @param opts Optional pointer to ColumnBoxesOptions struct (can be NULL for defaults/presets)
 * @return Array of column rectangles (caller must free)
 */
fz_rect* column_boxes(const char* pdf_path, int page_number, int* table_count,
                      ColumnBoxesOptions* opts);

/**
 * Check if a page likely contains a table based on column detection
 * @param pdf_path Path to the PDF document
 * @param page_number Page number to check
 * @return 1 if table likely present, 0 if not, -1 on error
 */
int page_has_table(const char* pdf_path, int page_number);

/**
 * Check if a rectangle intersects any in a list of rectangles
 * @param bb The rectangle to check
 * @param bboxes Array of rectangles to check against
 * @param bbox_count Number of rectangles in bboxes
 * @return 1 if intersects any, 0 if not
 */
int intersects_bboxes_rect(fz_rect bb, fz_rect* bboxes, int bbox_count);

#endif /* MULTICOLUMN_H */
