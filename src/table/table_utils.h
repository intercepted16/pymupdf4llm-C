#ifndef TABLE_UTILS_H
#define TABLE_UTILS_H
#include "mupdf/fitz/geometry.h"
#include "block_info.h"
#endif

fz_rect update_bbox(fz_rect bbox, fz_rect new_rect);

void populate_table_metrics(BlockInfo* info, int row_count, int column_count, float consistency);

void calculate_column_metrics(int column_count, int rows_with_content, int lines_with_multiple_columns,
                              const int* column_line_counts, BlockInfo* info, int line_count);

int find_or_add_column(float* columns, int* column_count, float x, float tolerance);