#include "mupdf/fitz.h"
#include "table.h"

fz_rect update_bbox(fz_rect bbox, fz_rect new_rect)
{
    return fz_is_empty_rect(bbox) ? new_rect : fz_union_rect(bbox, new_rect);
}

void populate_table_metrics(BlockInfo* info, int row_count, int column_count, float consistency)
{
    if (!info)
        return;
    info->row_count = row_count;
    info->column_count = column_count;
    info->cell_count = row_count * column_count;
    info->column_consistency = consistency;
    float base_score = consistency;
    if (column_count >= 4)
        base_score += 0.15f;
    if (row_count >= 6)
        base_score += 0.15f;
    if (base_score > 1.0f)
        base_score = 1.0f;
    info->confidence = base_score;
}

void calculate_column_metrics(int column_count, int rows_with_content, int lines_with_multiple_columns,
                              const int* column_line_counts, BlockInfo* info, int line_count)
{
    if (!info)
        return;

    int effective_rows = rows_with_content > 0 ? rows_with_content : line_count;

    if (column_count >= 2 && rows_with_content >= 2)
    {
        float consistency = 0.0f;
        for (int c = 0; c < column_count; ++c)
        {
            consistency += (float)column_line_counts[c] / (float)(rows_with_content ? rows_with_content : 1);
        }
        consistency = consistency / (float)(column_count ? column_count : 1);
        if (consistency > 1.0f)
            consistency = 1.0f;
        populate_table_metrics(info, rows_with_content, column_count, consistency);
        if (rows_with_content > 0 && lines_with_multiple_columns < rows_with_content / 2)
        {
            info->confidence *= 0.75f;
        }
    }
    else
    {
        info->row_count = effective_rows;
        info->cell_count = 0;
        info->confidence = 0.0f;
    }
}

int find_or_add_column(float* columns, int* column_count, float x, float tolerance)
{
    for (int i = 0; i < *column_count; ++i)
    {
        if (fabsf(columns[i] - x) <= tolerance)
        {
            return i;
        }
    }
    if (*column_count >= MAX_COLUMNS)
        return -1;
    columns[*column_count] = x;
    *column_count += 1;
    return *column_count - 1;
}
