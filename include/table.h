#ifndef TABLE_H
#define TABLE_H

#include "mupdf/fitz.h"
#include "block_info.h"
#include "font_metrics.h"

// A struct to represent a horizontal or vertical edge
typedef struct
{
    double x0, y0, x1, y1;
    char orientation; // 'h' or 'v'
} Edge;

// A dynamic array of edges
typedef struct
{
    Edge* items;
    int count;
    int capacity;
} EdgeArray;

// A struct to represent an intersection point
typedef struct
{
    double x, y;
} Point;

// A dynamic array of points
typedef struct
{
    Point* items;
    int count;
    int capacity;
} PointArray;

// A dynamic array of fz_rect (for cells)
typedef struct
{
    fz_rect* items;
    int count;
    int capacity;
} CellArray;

typedef struct
{
    fz_rect bbox;
    char* text;  // Cell text content
} TableCell;

typedef struct
{
    TableCell* cells;
    int count;
    fz_rect bbox;
} TableRow;

typedef struct
{
    TableRow* rows;
    int count;
    fz_rect bbox;
} Table;

typedef struct
{
    Table* tables;
    int count;
} TableArray;

TableArray* find_tables_on_page(fz_context* ctx, fz_document* doc, int page_number, BlockArray* blocks);
TableArray* find_tables_with_mupdf_native(const char* pdf_path, int page_number);
TableArray* synthesize_text_table_two_col(fz_context* ctx, fz_stext_page* textpage, const PageMetrics* metrics);
void free_table_array(TableArray* tables);

#endif // TABLE_H
