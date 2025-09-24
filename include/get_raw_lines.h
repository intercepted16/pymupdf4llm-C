/*
 * MuPDF C Text Extractor Header
 * 
 * Header file for the C port of PyMuPDF text extraction functionality.
 * 
 * Copyright 2024 Artifex Software, Inc.
 * License GNU Affero GPL 3.0
 */

#ifndef GET_RAW_LINES_H
#define GET_RAW_LINES_H

#include "mupdf/fitz.h"

#define TYPE3_FONT_NAME "Unnamed-T3"

/**
 * @brief Represents a single text span with its properties.
 *
 * This struct holds all the relevant information about a piece of text
 * that shares the same styling and is part of the same line.
 */
typedef struct {
    fz_rect bbox;           /**< The bounding box of the span. */
    char *text;             /**< The text content of the span. The caller is responsible for freeing this. */
    float size;             /**< The font size of the text. */
    int flags;              /**< Font flags (e.g., bold, italic) from PyMuPDF. */
    int char_flags;         /**< Character flags (e.g., superscript) from PyMuPDF. */
    int alpha;              /**< The transparency value of the text (0-255). */
    char *font;             /**< The name of the font. The caller is responsible for freeing this. */
    int line;               /**< The line number within the text block. */
    int block;              /**< The block number on the page. */
} span_dict_t;

/**
 * @brief Represents a line of text, which contains one or more spans.
 *
 * A line is defined as a collection of spans that are vertically aligned.
 */
typedef struct {
    fz_rect rect;           /**< The bounding rectangle of the entire line. */
    span_dict_t *spans;     /**< A dynamic array of spans that make up this line. */
    int span_count;         /**< The number of spans in the `spans` array. */
    int capacity;           /**< The allocated capacity of the `spans` array. */
} line_dict_t;

/**
 * @brief Represents an array of lines, designed for interoperability with Python.
 *
 * This struct is the primary container for returning extracted line data
 * from the C library to a Python caller.
 */
typedef struct {
    line_dict_t *lines;     /**< A dynamic array of lines. */
    int line_count;         /**< The number of lines in the `lines` array. */
} line_array_t;

/**
 * @brief Frees the memory allocated for a `line_array_t` and all its contents.
 *
 * This function safely deallocates the line array, including all lines,
 * all spans within those lines, and the text/font strings within each span.
 *
 * @param arr A pointer to the `line_array_t` to be freed.
 */
void free_line_array(line_array_t *arr);

/**
 * @brief Extracts all text lines from a PDF document.
 *
 * This is the main public function for this module. It opens a PDF file,
 * processes all its pages, and extracts structured text line information.
 *
 * @param pdf_path The file path to the PDF document.
 * @return A pointer to a `line_array_t` containing all the extracted lines.
 *         The caller is responsible for freeing this structure using `free_line_array`.
 *         Returns `NULL` on failure.
 */
line_array_t *get_raw_lines(const char *pdf_path);

/**
 * @brief (Internal) Checks if a text string contains only whitespace characters.
 * @param text The string to check.
 * @return 1 if the string is all whitespace, 0 otherwise.
 */
static int is_white(const char *text);

/**
 * @brief (Internal) Computes the union of two rectangles.
 * @param a The first rectangle.
 * @param b The second rectangle.
 * @return The smallest rectangle that contains both input rectangles.
 */
static fz_rect rect_union(fz_rect a, fz_rect b);

/**
 * @brief (Internal) Calculates the area of a rectangle.
 * @param r The rectangle.
 * @return The area of the rectangle as a float.
 */
static float rect_area(fz_rect r);

/**
 * @brief (Internal) Computes the intersection of two rectangles.
 * @param a The first rectangle.
 * @param b The second rectangle.
 * @return The rectangle representing the overlapping area.
 */
static fz_rect rect_intersect(fz_rect a, fz_rect b);

/**
 * @brief (Internal) A qsort comparison function for sorting spans horizontally.
 * @param a A pointer to the first span.
 * @param b A pointer to the second span.
 * @return -1, 0, or 1 based on the horizontal position of the spans.
 */
static int compare_spans_horizontal(const void *a, const void *b);

/**
 * @brief (Internal) A qsort comparison function for sorting spans vertically.
 * @param a A pointer to the first span.
 * @param b A pointer to the second span.
 * @return -1, 0, or 1 based on the vertical position of the spans.
 */
static int compare_spans_vertical(const void *a, const void *b);

/**
 * @brief (Internal) Cleans up and joins adjacent spans on a single line.
 *
 * This function merges spans that are close together and share similar
 * font properties, and it removes duplicate spans.
 *
 * @param line A pointer to the line to be sanitized.
 */
static void sanitize_spans(line_dict_t *line);

/**
 * @brief (Internal) Extracts all text spans from a `fz_stext_page`.
 * @param ctx The MuPDF context.
 * @param page The structured text page to extract from.
 * @param clip The clipping rectangle to filter spans.
 * @param ignore_invisible If 1, ignore text that is not visible.
 * @param[out] span_count_out A pointer to an integer where the number of extracted spans will be stored.
 * @return A dynamically allocated array of `span_dict_t`. The caller is responsible for freeing this array.
 */
static span_dict_t *extract_spans_from_dict(fz_context *ctx, fz_stext_page *page,
                                           fz_rect clip, int ignore_invisible,
                                           int *span_count_out);

/**
 * @brief (Internal) Groups an array of spans into lines based on vertical position.
 * @param ctx The MuPDF context.
 * @param textpage The structured text page.
 * @param clip The clipping rectangle.
 * @param tolerance The vertical tolerance for grouping spans into the same line.
 * @param ignore_invisible If 1, ignore text that is not visible.
 * @param[out] line_count_out A pointer to an integer where the number of lines will be stored.
 * @return A dynamically allocated array of `line_dict_t`. The caller is responsible for freeing this array.
 */
line_dict_t *_get_raw_lines(fz_context *ctx, fz_stext_page *textpage,
                         fz_rect clip, float tolerance, int ignore_invisible,
                         int *line_count_out);

/**
 * @brief (Internal) Extracts text from a page and formats it into a single string.
 *
 * This function is a C port of the `get_text("lines")` functionality from PyMuPDF.
 *
 * @param ctx The MuPDF context.
 * @param page The PDF page to extract text from.
 * @param textpage_param An optional, pre-existing text page. Can be `NULL`.
 * @param clip The clipping rectangle.
 * @param sep The separator string to use between text elements.
 * @param tolerance The vertical tolerance for line grouping.
 * @param ocr Whether to use OCR mode (currently a placeholder).
 * @return A dynamically allocated string containing the extracted text. The caller must free this string.
 */
char *get_text_lines(fz_context *ctx, fz_page *page, fz_stext_page *textpage_param,
                   fz_rect clip, const char *sep, float tolerance, int ocr);

#endif /* GET_RAW_LINES_H */