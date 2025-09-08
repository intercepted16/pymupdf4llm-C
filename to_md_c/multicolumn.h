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
 * Detect column boundaries and return text rectangles
 * 
 * @param pdf_path Path to the PDF document
 * @param page_number Page number to analyze
 * @param footer_margin Footer margin to ignore
 * @param header_margin Header margin to ignore
 * @param no_image_text Whether to ignore text over images
 * @param textpage_param Optional pre-made text page
 * @param paths Array of path rectangles to consider
 * @param path_count Number of path rectangles
 * @param avoid Array of rectangles to avoid
 * @param avoid_count Number of avoid rectangles
 * @param ignore_images Whether to ignore images
 * @param result_count Output parameter for number of columns found
 * @return Array of column rectangles (caller must free)
 */
float *column_boxes(const char *pdf_path, int page_number,
                      float footer_margin, float header_margin,
                      int no_image_text, fz_stext_page *textpage_param,
                      fz_rect *paths, int path_count,
                      fz_rect *avoid, int avoid_count,
                      int ignore_images,
                      int *result_count);


/**
 * Check if a page likely contains a table based on column detection
 * @param pdf_path Path to the PDF document
 * @param page_number Page number to check
 * @return 1 if table likely present, 0 if not, -1 on error
 */
int page_has_table(const char *pdf_path, int page_number);
#endif /* MULTICOLUMN_H */
