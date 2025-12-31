#ifndef TABLE_H
#define TABLE_H

#include "block_info.h"
#define EDGE_MIN_LENGTH 3.0
#define MAX_COLUMNS 32

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
    float x, y;
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

void process_tables_for_page(fz_context* ctx, fz_stext_page* textpage, TableArray* tables, int page_number,
                              BlockArray* blocks);

void free_table_array(TableArray* tables);


void init_edge_array(EdgeArray* arr);
void add_to_edge_array(EdgeArray* arr, Edge item);
void free_edge_array(EdgeArray* arr);

#endif // TABLE_H

