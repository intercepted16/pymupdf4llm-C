#include "column_detector.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#define MAX_COLUMNS 8
#define PAGE_WIDTH_RESOLUTION 1000
#define GAP_THRESHOLD 20.0f

typedef struct
{
    float x0, x1;
} ColumnRange;

void detect_and_assign_columns(BlockArray* blocks)
{
    if (!blocks || blocks->count == 0)
        return;

    float min_x = 100000.0f, max_x = -100000.0f;
    for (size_t i = 0; i < blocks->count; i++)
    {
        fz_rect b = blocks->items[i].bbox;
        if (b.x0 < min_x)
            min_x = b.x0;
        if (b.x1 > max_x)
            max_x = b.x1;
    }
    if (max_x <= min_x)
        return;

    float page_width = max_x - min_x;
    if (page_width < 100.0f)
        return;

    unsigned char occupancy[PAGE_WIDTH_RESOLUTION] = {0};

    float spanning_threshold = page_width * 0.60f;

    for (size_t i = 0; i < blocks->count; i++)
    {
        BlockInfo* b = &blocks->items[i];
        float bw = b->bbox.x1 - b->bbox.x0;

        if (bw > spanning_threshold)
        {
            continue;
        }

        if (bw < 10.0f)
            continue;

        int idx0 = (int)((b->bbox.x0 - min_x) / page_width * (PAGE_WIDTH_RESOLUTION - 1));
        int idx1 = (int)((b->bbox.x1 - min_x) / page_width * (PAGE_WIDTH_RESOLUTION - 1));

        if (idx0 < 0)
            idx0 = 0;
        if (idx1 >= PAGE_WIDTH_RESOLUTION)
            idx1 = PAGE_WIDTH_RESOLUTION - 1;

        for (int k = idx0; k <= idx1; k++)
        {
            occupancy[k] = 1;
        }
    }

    ColumnRange columns[MAX_COLUMNS];
    int col_count = 0;

    int gap_bins = (int)(GAP_THRESHOLD / page_width * PAGE_WIDTH_RESOLUTION);
    if (gap_bins < 1)
        gap_bins = 1;

    bool inside_content = false;
    int content_start = 0;

    for (int i = 0; i < PAGE_WIDTH_RESOLUTION; i++)
    {
        if (occupancy[i])
        {
            if (!inside_content)
            {
                inside_content = true;
                content_start = i;
            }
        }
        else
        {
            if (inside_content)
            {
                int gap_len = 0;
                while (i + gap_len < PAGE_WIDTH_RESOLUTION && !occupancy[i + gap_len])
                {
                    gap_len++;
                }

                if (gap_len >= gap_bins || i + gap_len == PAGE_WIDTH_RESOLUTION)
                {
                    if (col_count < MAX_COLUMNS)
                    {
                        columns[col_count].x0 = min_x + (float)content_start / PAGE_WIDTH_RESOLUTION * page_width;
                        columns[col_count].x1 = min_x + (float)(i - 1) / PAGE_WIDTH_RESOLUTION * page_width;
                        col_count++;
                    }
                    inside_content = false;
                }
            }
        }
    }

    if (inside_content && col_count < MAX_COLUMNS)
    {
        columns[col_count].x0 = min_x + (float)content_start / PAGE_WIDTH_RESOLUTION * page_width;
        col_count++;
    }

    if (col_count <= 1)
    {
        for (size_t i = 0; i < blocks->count; i++)
        {
            blocks->items[i].column_index = 0;
        }
        return;
    }


    for (size_t i = 0; i < blocks->count; i++)
    {
        BlockInfo* b = &blocks->items[i];
        float bx0 = b->bbox.x0;
        float bx1 = b->bbox.x1;

        int overlap_count = 0;
        int last_col_idx = 0;

        for (int c = 0; c < col_count; c++)
        {
            float cx0 = columns[c].x0;
            float cx1 = columns[c].x1;

            float ix0 = fz_max(bx0, cx0);
            float ix1 = fz_min(bx1, cx1);

            if (ix1 > ix0)
            {
                if (ix1 - ix0 > 10.0f)
                {
                    overlap_count++;
                    last_col_idx = c + 1;
                }
            }
        }

        if (overlap_count > 1)
        {
            b->column_index = 0;
        }
        else if (overlap_count == 1)
        {
            b->column_index = last_col_idx;
        }
        else
        {
            float bx_center = (bx0 + bx1) * 0.5f;
            int best_col = 0;
            float min_dist = 100000.0f;

            for (int c = 0; c < col_count; c++)
            {
                float cx_center = (columns[c].x0 + columns[c].x1) * 0.5f;
                float dist = fabsf(bx_center - cx_center);
                if (dist < min_dist)
                {
                    min_dist = dist;
                    best_col = c + 1;
                }
            }
            b->column_index = best_col;
        }
    }
}
