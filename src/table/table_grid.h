// Table grid logic.

#ifndef TABLE_GRID_H
#define TABLE_GRID_H

#include "table.h"
#include "spatial_hash.h"

// Grid detection constants
#define MAX_COLUMNS 32
#define SNAP_TOLERANCE 3.0
#define JOIN_TOLERANCE 3.0
#define INTERSECTION_TOLERANCE 1.0
#define ROW_Y_TOLERANCE 2.0
#define COL_X_TOLERANCE 2.0
#define TABLE_SPLIT_GAP_THRESHOLD 50.0

// Optimization: scale factor for integer coordinates (1000x precision)
#define COORD_SCALE 1000
#define COORD_TO_INT(x) ((int)((x) * COORD_SCALE + 0.5))
#define INT_TO_COORD(i) ((double)(i) / COORD_SCALE)

// Grid-based table detection functions
void merge_edges(EdgeArray* edges, double snap_tolerance, double join_tolerance);
void find_intersections(const EdgeArray* v_edges, const EdgeArray* h_edges, SpatialHash* hash);
void find_cells(const PointArray* intersections, SpatialHash* hash, CellArray* cells);
TableArray* group_cells_into_tables(const CellArray* cells);
bool validate_tables(TableArray* tables, fz_rect page_rect);

// Helper functions
int compare_edges_v(const void* a, const void* b);
int compare_edges_h(const void* a, const void* b);
int compare_points(const void* a, const void* b);

// Array helper functions
void init_cell_array(CellArray* arr);
void free_cell_array(CellArray* arr);

#endif // TABLE_GRID_H

