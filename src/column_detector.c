#include "column_detector.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#define MAX_COLUMNS 8
#define PAGE_WIDTH_RESOLUTION 1000
#define GAP_THRESHOLD 15.0f // Minimum gap width to be considered a column separator

typedef struct
{
    float x0, x1;
} ColumnRange;

void detect_and_assign_columns(BlockArray* blocks)
{
    if (!blocks || blocks->count == 0)
        return;

    // 1. Determine page content bounds
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
        return; // Too narrow for columns

    // 2. Build occupancy map (projection onto X axis)
    // We use a simple boolean array representing slice of the page width
    // Resolution: PAGE_WIDTH_RESOLUTION bins
    unsigned char occupancy[PAGE_WIDTH_RESOLUTION] = {0};

    // Debug output
    // Filter blocks for column detection (ignore headers/footers/page numbers)
    // We assume headers/footers are at extreme Y positions, but simplest is to
    // just ignore blocks that are extremely wide (spanning) for detection purposes.
    // Lowered threshold to 0.6 to catch headers that don't span full width but cover potential gaps
    float spanning_threshold = page_width * 0.60f;

    for (size_t i = 0; i < blocks->count; i++)
    {
        BlockInfo* b = &blocks->items[i];
        float bw = b->bbox.x1 - b->bbox.x0;

        // Ignore spanning blocks for column split detection
        if (bw > spanning_threshold)
        {
            // printf("  Ignoring spanning block %zu (Width %.2f)\n", i, bw);
            continue;
        }

        // Ignore very small blocks (bullets, icons)
        if (bw < 10.0f)
            continue;

        // Map block X range to bins
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

    // 3. Find gaps in occupancy
    // A gap is a sequence of 0s
    ColumnRange columns[MAX_COLUMNS];
    int col_count = 0;

    // Convert gap threshold to bins
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
            // We are in a gap (or margin)
            if (inside_content)
            {
                // Ended a content block
                // Check if this gap is real (large enough) or just small spacing
                // Look ahead
                int gap_len = 0;
                while (i + gap_len < PAGE_WIDTH_RESOLUTION && !occupancy[i + gap_len])
                {
                    gap_len++;
                }

                if (gap_len >= gap_bins || i + gap_len == PAGE_WIDTH_RESOLUTION)
                {
                    // It's a significant gap -> End of a column
                    if (col_count < MAX_COLUMNS)
                    {
                        columns[col_count].x0 = min_x + (float)content_start / PAGE_WIDTH_RESOLUTION * page_width;
                        columns[col_count].x1 = min_x + (float)(i - 1) / PAGE_WIDTH_RESOLUTION * page_width;
                        col_count++;
                    }
                    inside_content = false;
                    i += gap_len - 1; // Advance
                }
            }
        }
    }

    if (inside_content && col_count < MAX_COLUMNS)
    {
        columns[col_count].x0 = min_x + (float)content_start / PAGE_WIDTH_RESOLUTION * page_width;
        columns[col_count].x1 = min_x + page_width; // End
        col_count++;
    }

    if (col_count <= 1)
    {
        // No columns detected (or single column)
        for (size_t i = 0; i < blocks->count; i++)
        {
            blocks->items[i].column_index = 0;
        }
        return;
    }

    // 4. Assign blocks to columns
    // Refined logic: A block is spanning (col=0) only if it significantly overlaps
    // MULTIPLE columns. Simple width threshold caused wide right-column blocks
    // to be treated as spanning and sorted first.

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
                // Significant overlap? (> 10pt or > 20% of block?)
                if (ix1 - ix0 > 10.0f)
                {
                    overlap_count++;
                    last_col_idx = c + 1;
                }
            }
        }

        if (overlap_count > 1)
        {
            // Spans multiple columns -> Global flow
            b->column_index = 0;
        }
        else if (overlap_count == 1)
        {
            b->column_index = last_col_idx;
        }
        else
        {
            // No significant overlap (maybe in gap?). Assign to closest.
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
