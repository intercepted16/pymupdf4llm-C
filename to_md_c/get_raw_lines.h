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

/* Structure to represent a text span with its properties */
typedef struct {
    fz_rect bbox;           /* Bounding box of the span */
    char *text;             /* Text content */
    float size;             /* Font size */
    int flags;              /* Font flags */
    int char_flags;         /* Character flags */
    int alpha;              /* Transparency value */
    char *font;             /* Font name */
    int line;               /* Line number within block */
    int block;              /* Block number */
} span_dict_t;

/* Structure to represent a line containing multiple spans */
typedef struct {
    fz_rect rect;           /* Bounding rectangle of the line */
    span_dict_t *spans;     /* Array of spans in this line */
    int span_count;         /* Number of spans in the line */
    int capacity;           /* Allocated capacity for spans array */
} line_dict_t;

/* Function prototypes */

/**
 * Check if a text string contains only whitespace characters
 * @param text String to check
 * @return 1 if string is all whitespace, 0 otherwise
 */
static int is_white(const char *text);

/**
 * Union of two rectangles
 * @param a First rectangle
 * @param b Second rectangle  
 * @return Union rectangle
 */
static fz_rect rect_union(fz_rect a, fz_rect b);

/**
 * Calculate area of a rectangle
 * @param r Rectangle
 * @return Area as float
 */
static float rect_area(fz_rect r);

/**
 * Intersection of two rectangles
 * @param a First rectangle
 * @param b Second rectangle
 * @return Intersection rectangle
 */
static fz_rect rect_intersect(fz_rect a, fz_rect b);

/**
 * Compare function for sorting spans horizontally
 * @param a First span
 * @param b Second span
 * @return Comparison result for qsort
 */
static int compare_spans_horizontal(const void *a, const void *b);

/**
 * Compare function for sorting spans vertically  
 * @param a First span
 * @param b Second span
 * @return Comparison result for qsort
 */
static int compare_spans_vertical(const void *a, const void *b);

/**
 * Clean up and join spans on a line
 * @param line Line containing spans to sanitize
 */
static void sanitize_spans(line_dict_t *line);

/**
 * Extract spans from a stext page using dictionary-like approach
 * @param ctx MuPDF context
 * @param page Structured text page
 * @param clip Clipping rectangle
 * @param ignore_invisible Whether to ignore invisible text
 * @param span_count_out Output parameter for number of spans
 * @return Array of extracted spans
 */
static span_dict_t *extract_spans_from_dict(fz_context *ctx, fz_stext_page *page, 
                                           fz_rect clip, int ignore_invisible, 
                                           int *span_count_out);

/**
 * Group spans into lines based on vertical position
 * @param ctx MuPDF context
 * @param textpage Structured text page
 * @param clip Clipping rectangle
 * @param tolerance Vertical tolerance for grouping spans into lines
 * @param ignore_invisible Whether to ignore invisible text
 * @param line_count_out Output parameter for number of lines
 * @return Array of lines containing grouped spans
 */
line_dict_t *_get_raw_lines(fz_context *ctx, fz_stext_page *textpage, 
                         fz_rect clip, float tolerance, int ignore_invisible,
                         int *line_count_out);

/**
 * Extract text lines from a PDF page
 * @param ctx MuPDF context
 * @param page PDF page
 * @param textpage_param Optional pre-made text page (can be NULL)
 * @param clip Clipping rectangle  
 * @param sep Separator string between text elements
 * @param tolerance Vertical tolerance for line grouping
 * @param ocr Whether to use OCR mode
 * @return Extracted text as string (caller must free)
 */
char *get_text_lines(fz_context *ctx, fz_page *page, fz_stext_page *textpage_param,
                   fz_rect clip, const char *sep, float tolerance, int ocr);


/* Structure to hold array of lines and its length for Python interop */
typedef struct {
    line_dict_t *lines;
    int line_count;
} line_array_t;

/* Function to free the line array and all its contents */
void free_line_array(line_array_t *arr);

/* Updated function to get raw lines and count */
line_array_t *get_raw_lines(const char *pdf_path);

#endif /* GET_RAW_LINES_H */