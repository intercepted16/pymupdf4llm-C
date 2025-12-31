// table/table_horizontal.c - Detect tables with horizontal dividers only (no vertical lines)

#include "table.h"
#include "table_grid.h"
#include "table_capture.h"
#include "table_utils.h"
#include "spatial_hash.h"
#include "font_metrics.h"
#include "text_utils.h"
#include "utils.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>

// Helper function for sorting floats
static int cmp_floats(const void* a, const void* b)
{
    float fa = *(float*)a;
    float fb = *(float*)b;
    if (fa < fb)
        return -1;
    if (fa > fb)
        return 1;
    return 0;
}

// Detect horizontal-divider tables: tables with only horizontal lines, no vertical grid
// Returns a table structure with rows defined by horizontal lines and columns inferred from text
TableArray* find_horizontal_divider_tables(const EdgeArray* h_edges, fz_context* ctx, fz_stext_page* textpage,
                                           const PageMetrics* metrics)
{
    if (!h_edges || h_edges->count < 2)
    {
        fprintf(stderr, "  Horizontal-divider: Not enough edges (%d)\n", h_edges ? h_edges->count : 0);
        return NULL;
    }

    // Group horizontal edges into rows (edges at similar y-coordinates)
    // Each group defines a row boundary
    float* row_y_positions = malloc(h_edges->count * sizeof(float));
    int row_count = 0;

    for (int i = 0; i < h_edges->count; i++)
    {
        float y = h_edges->items[i].y0;

        // Check if this y-position is already in our list
        bool found = false;
        for (int j = 0; j < row_count; j++)
        {
            if (fabs(row_y_positions[j] - y) < 2.0f) // Tolerance for slight misalignment
            {
                found = true;
                break;
            }
        }

        if (!found && row_count < h_edges->count)
        {
            row_y_positions[row_count++] = y;
        }
    }

    if (row_count < 3) // Need at least 3 lines to define 2 rows
    {
        fprintf(stderr, "  Horizontal-divider: Not enough row positions (%d)\n", row_count);
        free(row_y_positions);
        return NULL;
    }

    fprintf(stderr, "  Horizontal-divider: Found %d row boundaries\n", row_count);

    // Sort row positions
    qsort(row_y_positions, row_count, sizeof(float), cmp_floats);

    // Get the bounding box of the table from the horizontal edges
    float table_x0 = 1e9, table_x1 = -1e9;
    float table_y0 = row_y_positions[0];
    float table_y1 = row_y_positions[row_count - 1];

    for (int i = 0; i < h_edges->count; i++)
    {
        table_x0 = fminf(table_x0, h_edges->items[i].x0);
        table_x1 = fmaxf(table_x1, h_edges->items[i].x1);
    }

    fprintf(stderr, "  Horizontal-divider: Table bbox [%.1f, %.1f, %.1f, %.1f]\n", table_x0, table_y0, table_x1,
            table_y1);

    // Extract text within table bounds and detect column positions from text
    // Use the same approach as page_extractor.c for column detection
    float column_x_positions[MAX_COLUMNS];
    int column_count = 0;

    // Analyze text to find column boundaries
    int lines_in_table = 0;
    for (fz_stext_block* block = textpage->first_block; block; block = block->next)
    {
        if (block->type != FZ_STEXT_BLOCK_TEXT)
            continue;

        for (fz_stext_line* line = block->u.t.first_line; line; line = line->next)
        {
            // Check if line is within table y-range
            if (line->bbox.y0 < table_y0 || line->bbox.y1 > table_y1)
                continue;

            lines_in_table++;

            // Detect column starts by looking for text segments with gaps
            float prev_x1 = NAN;
            int chars_processed = 0;

            for (fz_stext_char* ch = line->first_char; ch; ch = ch->next)
            {
                if (ch->c == 0)
                    continue;

                fz_rect char_bbox = fz_rect_from_quad(ch->quad);
                float x0 = char_bbox.x0;
                float x1 = char_bbox.x1;

                bool is_whitespace = (ch->c == ' ' || ch->c == '\t' || ch->c == '\r' || ch->c == '\n' || ch->c == 160);

                // Calculate gap from previous character
                float gap = (!isnan(prev_x1)) ? fabsf(x0 - prev_x1) : 0.0f;

                // Use font-size-based tolerance like page_extractor.c
                float tolerance = ch->size * 0.5f;
                if (tolerance < 3.0f)
                    tolerance = 3.0f;

                // Larger tolerance for table columns (need more significant gaps)
                float column_tolerance = fmaxf(tolerance * 2.0f, 15.0f);

                // Start of new column if significant gap
                if ((isnan(prev_x1) || gap > column_tolerance) && !is_whitespace)
                {
                    int result = find_or_add_column(column_x_positions, &column_count, x0, tolerance);
                    if (lines_in_table == 1 && result >= 0 && column_count <= 3)
                    {
                        fprintf(stderr, "    Added column %d at x=%.1f (gap=%.1f, tol=%.1f)\n", result, x0, gap,
                                column_tolerance);
                    }
                }

                prev_x1 = x1;
            }
        }
    }

    fprintf(stderr, "  Horizontal-divider: Found %d text lines in table area\n", lines_in_table);

    if (column_count < 2)
    {
        fprintf(stderr, "  Horizontal-divider: Not enough columns detected (%d)\n", column_count);
        free(row_y_positions);
        return NULL;
    }

    fprintf(stderr, "  Horizontal-divider: Detected %d columns\n", column_count);

    // Sort column positions
    qsort(column_x_positions, column_count, sizeof(float), cmp_floats);

    // Build table structure with rows and columns
    TableArray* tables = malloc(sizeof(TableArray));
    if (!tables)
    {
        free(row_y_positions);
        return NULL;
    }

    tables->count = 0;
    tables->tables = NULL;

    Table* table = malloc(sizeof(Table));
    if (!table)
    {
        free(tables);
        free(row_y_positions);
        return NULL;
    }

    table->bbox = (fz_rect){table_x0, table_y0, table_x1, table_y1};
    table->count = row_count - 1; // Number of rows
    table->rows = malloc(table->count * sizeof(TableRow));

    // Create rows based on horizontal line positions
    for (int r = 0; r < table->count; r++)
    {
        float row_y0 = row_y_positions[r];
        float row_y1 = row_y_positions[r + 1];

        TableRow* row = &table->rows[r];
        row->count = column_count;
        row->cells = malloc(column_count * sizeof(TableCell));
        row->bbox = (fz_rect){table_x0, row_y0, table_x1, row_y1};

        // Initialize cells and extract text
        for (int c = 0; c < column_count; c++)
        {
            float cell_x0 = column_x_positions[c];
            float cell_x1 = (c + 1 < column_count) ? column_x_positions[c + 1] : table_x1;

            row->cells[c].bbox = (fz_rect){cell_x0, row_y0, cell_x1, row_y1};

            // Extract text within this cell's bounding box
            fz_rect cell_rect = row->cells[c].bbox;
            row->cells[c].text = extract_text_with_spacing(ctx, textpage, &cell_rect);
        }
    }

    tables->count = 1;
    tables->tables = table;

    free(row_y_positions);

    return tables;
}
