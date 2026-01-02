// Table grid logic.

#ifndef TABLE_GRID_H
#define TABLE_GRID_H

#include "table.h"
#include "spatial_hash.h"

#define MAX_COLUMNS 32

#define SNAP_TOLERANCE_RATIO 0.005      // 0.5% of page width
#define JOIN_TOLERANCE_RATIO 0.005      // 0.5% of page width
#define INTERSECTION_TOLERANCE_RATIO 0.0015  // 0.15% of page diagonal
#define ROW_Y_TOLERANCE_RATIO 0.025     // 2.5% of page height (increased to handle varying cell alignments)
#define COL_X_TOLERANCE_RATIO 0.003     // 0.3% of page width
#define TABLE_SPLIT_GAP_RATIO 0.10      // 10% of page height
#define MIN_CELL_SIZE_RATIO 0.005       // 0.5% of page dimension (min cell size)
#define MAX_CELL_HEIGHT_RATIO 0.20      // 20% of page height (max cell height)
#define MAX_CELL_WIDTH_RATIO 0.95       // 95% of page width (max cell width)

// Optimization: scale factor for integer coordinates (1000x precision)
#define COORD_SCALE 1000
#define COORD_TO_INT(x) ((int)((x) * COORD_SCALE + 0.5))
#define INT_TO_COORD(i) ((double)(i) / COORD_SCALE)

// Grid-based table detection functions
void merge_edges(EdgeArray* edges, double snap_tolerance, double join_tolerance);
void find_intersections(const EdgeArray* v_edges, const EdgeArray* h_edges, SpatialHash* hash);
void find_cells(const PointArray* intersections, SpatialHash* hash, CellArray* cells, fz_rect page_rect);
void deduplicate_cells(CellArray* cells);  // Remove overlapping/duplicate cells
TableArray* group_cells_into_tables(const CellArray* cells, fz_rect page_rect);
bool validate_tables(TableArray* tables, fz_rect page_rect);

// Helper functions
int compare_edges_v(const void* a, const void* b);
int compare_edges_h(const void* a, const void* b);
int compare_points(const void* a, const void* b);

// Array helper functions
void init_cell_array(CellArray* arr);
void free_cell_array(CellArray* arr);

// Word-cutting validation
void init_word_rect_array(WordRectArray* arr);
void free_word_rect_array(WordRectArray* arr);
void extract_word_rects(fz_context* ctx, fz_stext_page* textpage, fz_rect bounds, WordRectArray* words);
bool intersects_words_h(float y, fz_rect table_bbox, const WordRectArray* words);
bool intersects_words_v(float x, fz_rect table_bbox, const WordRectArray* words);

#endif // TABLE_GRID_H
