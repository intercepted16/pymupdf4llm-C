#ifndef TABLE_TWO_COLUMN_H
#define TABLE_TWO_COLUMN_H
#endif

#include "table_utils.h"
#include "table.h"
#include "font_metrics.h"


TableArray* synthesize_text_table_two_col(fz_context* ctx, fz_stext_page* textpage, const PageMetrics* metrics);

// Implemented by macro in utils.h & table_two_column.c
void init_point_array(PointArray* arr);
void add_to_point_array(PointArray* arr, Point item);
void free_point_array(PointArray* arr);
