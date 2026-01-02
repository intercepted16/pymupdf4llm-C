// table/table_core.c - Orchestrates grid and text column table detection

#include "table.h"
#include "table_grid.h"
#include "table_capture.h"
#include "table_horizontal.h"
#include "font_metrics.h"
#include "text_utils.h"
#include "spatial_hash.h"
#include "utils.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>

DEFINE_ARRAY_METHODS_PUBLIC(Edge, Edge, edge)

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

// Helper: Detect if page has columnar (sidebar) layout
// Returns true if text is naturally organized in columns without grid lines
static bool has_columnar_layout(fz_context* ctx, fz_page* page)
{
    fz_stext_page* textpage = fz_new_stext_page_from_page(ctx, page, NULL);
    if (!textpage)
        return false;

    // Collect LINE starting x-positions (not characters) to find column splits
    // This is more robust as it looks at where lines of text start
    float* x_starts = malloc(500 * sizeof(float));
    int start_count = 0;

    float page_min_x = 1000000.0f, page_max_x = 0.0f;

    for (fz_stext_block* block = textpage->first_block; block; block = block->next)
    {
        if (block->type != FZ_STEXT_BLOCK_TEXT)
            continue;

        for (fz_stext_line* line = block->u.t.first_line; line; line = line->next)
        {
            // Get the x-position of the first non-space character
            for (fz_stext_char* ch = line->first_char; ch; ch = ch->next)
            {
                if (ch->c > 32 && ch->c < 127)
                {
                    fz_rect char_box = fz_rect_from_quad(ch->quad);
                    if (start_count < 500)
                    {
                        x_starts[start_count++] = char_box.x0;
                    }
                    if (char_box.x0 < page_min_x)
                        page_min_x = char_box.x0;
                    if (char_box.x1 > page_max_x)
                        page_max_x = char_box.x1;
                    break; // Only first char per line
                }
            }
        }
    }

    if (start_count < 5 || page_max_x <= page_min_x)
    {
        free(x_starts);
        fz_drop_stext_page(ctx, textpage);
        return false;
    }

    // Sort line start positions
    qsort(x_starts, start_count, sizeof(float), cmp_floats);

    float page_width = page_max_x - page_min_x;
    float min_gap_for_column = page_width * 0.1f; // At least 10% of page width
    if (min_gap_for_column < 30.0f)
        min_gap_for_column = 30.0f;

    // Look for significant gap in line start positions
    float max_gap = 0;
    float gap_at_x = 0;
    for (int i = 1; i < start_count; i++)
    {
        float gap = x_starts[i] - x_starts[i - 1];
        if (gap > max_gap)
        {
            max_gap = gap;
            gap_at_x = (x_starts[i] + x_starts[i - 1]) / 2.0f;
        }
    }

    fprintf(stderr, "Page columnar check: %d line starts, max_gap=%.1f, threshold=%.1f\n", start_count, max_gap,
            min_gap_for_column);

    free(x_starts);
    fz_drop_stext_page(ctx, textpage);

    bool is_columnar = (max_gap >= min_gap_for_column);

    if (is_columnar)
    {
        fprintf(stderr, "Page: Detected columnar layout (gap=%.1f at x=%.1f)\n", max_gap, gap_at_x);
    }

    return is_columnar;
}

// Main orchestration function: ONLY detects actual grid-based tables
// Two-column layouts are NOT tables and should not be treated as such
TableArray* find_tables_on_page(fz_context* ctx, fz_document* doc, int page_number, BlockArray* blocks)
{
    (void)blocks;

    fz_page* page = NULL;
    CaptureDevice* capture_dev = NULL;
    TableArray* tables = NULL;
    SpatialHash hash;

    init_spatial_hash(&hash);

    fz_try(ctx)
    {
        page = fz_load_page(ctx, doc, page_number);

        // Detect grid lines for real table structure
        capture_dev = (CaptureDevice*)new_capture_device(ctx);
        fz_run_page(ctx, page, (fz_device*)capture_dev, fz_identity, NULL);
        fz_close_device(ctx, (fz_device*)capture_dev);

        EdgeArray h_edges, v_edges;
        init_edge_array(&h_edges);
        init_edge_array(&v_edges);
        h_edges.capacity = capture_dev->edges.count / 2 + 16;
        v_edges.capacity = capture_dev->edges.count / 2 + 16;
        h_edges.items = malloc(h_edges.capacity * sizeof(Edge));
        v_edges.items = malloc(v_edges.capacity * sizeof(Edge));

        for (int i = 0; i < capture_dev->edges.count; ++i)
        {
            if (capture_dev->edges.items[i].orientation == 'h')
            {
                add_to_edge_array(&h_edges, capture_dev->edges.items[i]);
            }
            else
            {
                add_to_edge_array(&v_edges, capture_dev->edges.items[i]);
            }
        }

        fz_rect page_rect = fz_bound_page(ctx, page);
        float page_width = page_rect.x1 - page_rect.x0;

        double snap_tolerance = page_width * SNAP_TOLERANCE_RATIO;
        double join_tolerance = page_width * JOIN_TOLERANCE_RATIO;

        merge_edges(&h_edges, snap_tolerance, join_tolerance);
        merge_edges(&v_edges, snap_tolerance, join_tolerance);

        fprintf(stderr, "Page %d: Detected %d h_edges, %d v_edges\n", page_number + 1, h_edges.count, v_edges.count);

        // Only try grid detection if we have enough edges for a real table
        bool has_grid = (h_edges.count >= 3 && v_edges.count >= 3);

        if (has_grid)
        {
            find_intersections(&v_edges, &h_edges, &hash);

            PointArray intersections;
            init_point_array(&intersections);
            collect_points_from_hash(&hash, &intersections);
            if (intersections.count > 0)
            {
                qsort(intersections.items, intersections.count, sizeof(Point), compare_points);
            }

            CellArray cells;
            init_cell_array(&cells);
            find_cells(&intersections, &hash, &cells, page_rect);

            int original_count = cells.count;
            int write_idx = 0;
            for (int i = 0; i < cells.count; i++)
            {
                fz_rect cell = cells.items[i];

                float out_top = fmaxf(0, page_rect.y0 - cell.y0);
                float out_bottom = fmaxf(0, cell.y1 - page_rect.y1);
                float out_left = fmaxf(0, page_rect.x0 - cell.x0);
                float out_right = fmaxf(0, cell.x1 - page_rect.x1);
                float max_out = fmaxf(fmaxf(out_top, out_bottom), fmaxf(out_left, out_right));

                if (max_out > 10.0f)
                {
                    continue; // Skip this cell
                }

                if (max_out > 0)
                {
                    cell = fz_intersect_rect(cell, page_rect);
                }

                cells.items[write_idx++] = cell;
            }
            cells.count = write_idx;

            if (original_count != cells.count)
            {
                fprintf(stderr, "Page %d: Removed %d out-of-bounds cells\n", page_number + 1,
                        original_count - cells.count);
            }

            if (cells.count > 0)
            {
                if (cells.count > 0)
                {
                    fz_rect cells_bbox = cells.items[0];
                    for (int i = 1; i < cells.count; i++)
                    {
                        cells_bbox = fz_union_rect(cells_bbox, cells.items[i]);
                    }
                    fprintf(stderr, "  Cells bbox range: (%.1f, %.1f, %.1f, %.1f)\n", cells_bbox.x0, cells_bbox.y0,
                            cells_bbox.x1, cells_bbox.y1);
                }

                deduplicate_cells(&cells);
                fprintf(stderr, "Page %d: After deduplication: %d cells\n", page_number + 1, cells.count);
            }

            free_point_array(&intersections);

            tables = group_cells_into_tables(&cells, page_rect);
            free_cell_array(&cells);

            bool tables_valid = validate_tables(tables, page_rect);

            fprintf(stderr, "Page %d: Grid detection valid=%d\n", page_number + 1, tables_valid);

            if (!tables_valid)
            {
                if (tables)
                {
                    free_table_array(tables);
                    tables = NULL;
                }
            }
        }

        // NOTE: We no longer do "two-column table synthesis" because two-column
        // document layouts (like resumes) are NOT tables. They're just layout.
        // The text blocks should be read in proper reading order, not forced into tables.

        free_edge_array(&h_edges);
        free_edge_array(&v_edges);
    }
    fz_always(ctx)
    {
        if (capture_dev)
            fz_drop_device(ctx, (fz_device*)capture_dev);
        if (page)
            fz_drop_page(ctx, page);
        free_spatial_hash(&hash);
    }
    fz_catch(ctx)
    {
        if (tables)
        {
            free_table_array(tables);
            tables = NULL;
        }
    }

    return tables;
}

void process_tables_for_page(fz_context* ctx, fz_stext_page* textpage, TableArray* tables, int page_number,
                             BlockArray* blocks)
{
    if (!tables || !tables->count || !blocks)
        return;

    // Extract text from each table and add as new blocks
    for (int t = 0; t < tables->count; t++)
    {
        Table* table = &tables->tables[t];

        // Extract text from each cell and store it
        for (int r = 0; r < table->count; r++)
        {
            TableRow* row = &table->rows[r];

            for (int c = 0; c < row->count; c++)
            {
                TableCell* cell = &row->cells[c];
                fz_rect cell_rect = cell->bbox;

                // Extract text with proper spacing using text_utils function
                char* cell_text = extract_text_with_spacing(ctx, textpage, &cell_rect);

                if (cell_text && cell_text[0])
                {
                    for (char* p = cell_text; *p; p++)
                    {
                        if (*p == '\n' || *p == '\r')
                            *p = ' ';
                    }
                    cell->text = cell_text;
                }
                else
                {
                    free(cell_text);
                    cell->text = strdup("");
                }
            }
        }

        // Remove empty columns (whitelist visible ASCII)
        if (table->count > 0 && table->rows[0].count > 0)
        {
            int col_count = table->rows[0].count;
            int* keep_col = (int*)calloc(col_count, sizeof(int));
            int keep_count = 0;

            // Identify which columns have actual content
            for (int c = 0; c < col_count; c++)
            {
                int has_text = 0;
                for (int r = 0; r < table->count; r++)
                {
                    char* txt = table->rows[r].cells[c].text;
                    bool has_text = has_visible_content(txt);
                    if (has_text)
                        break;
                }

                if (has_text)
                {
                    keep_col[c] = 1;
                    keep_count++;
                }
            }

            // Compress table if we found empty columns
            if (keep_count > 0 && keep_count < col_count)
            {
                for (int r = 0; r < table->count; r++)
                {
                    TableRow* row = &table->rows[r];
                    int target_idx = 0;

                    for (int source_idx = 0; source_idx < col_count; source_idx++)
                    {
                        if (keep_col[source_idx])
                        {
                            if (target_idx != source_idx)
                            {
                                row->cells[target_idx] = row->cells[source_idx];
                            }
                            target_idx++;
                        }
                        else
                        {
                            // Free empty column text
                            if (row->cells[source_idx].text)
                            {
                                free(row->cells[source_idx].text);
                                row->cells[source_idx].text = NULL;
                            }
                        }
                    }
                    row->count = keep_count;
                }
            }
            free(keep_col);
        }

        // Skip this table if it has no visible content after filtering
        bool has_content = false;
        for (int r = 0; r < table->count && !has_content; r++)
        {
            for (int c = 0; c < table->rows[r].count && !has_content; c++)
            {
                if (table->rows[r].cells[c].text && has_visible_content(table->rows[r].cells[c].text))
                {
                    has_content = true;
                }
            }
        }

        if (!has_content)
        {
            // Skip this table - it's empty
            continue;
        }

        // Remove overlapping text blocks
        for (size_t b = 0; b < blocks->count; b++)
        {
            BlockInfo* block = &blocks->items[b];

            float overlap_x = fminf(block->bbox.x1, table->bbox.x1) - fmaxf(block->bbox.x0, table->bbox.x0);
            float overlap_y = fminf(block->bbox.y1, table->bbox.y1) - fmaxf(block->bbox.y0, table->bbox.y0);

            if (overlap_x > 0 && overlap_y > 0)
            {
                float block_area = (block->bbox.x1 - block->bbox.x0) * (block->bbox.y1 - block->bbox.y0);
                float overlap_area = overlap_x * overlap_y;

                if (block_area > 0 && overlap_area / block_area > 0.7)
                {
                    free(block->text);
                    block->text = strdup("");
                    block->text_chars = 0;
                }
            }
        }

        // Add table block
        BlockInfo* table_block = block_array_push(blocks);
        if (table_block)
        {
            table_block->text = strdup("");
            table_block->text_chars = 0;
            table_block->bbox = table->bbox;
            table_block->type = BLOCK_TABLE;
            table_block->avg_font_size = 0.0f;
            table_block->bold_ratio = 0.0f;
            table_block->line_count = table->count;
            table_block->line_spacing_avg = 0.0f;
            table_block->column_count = (table->count > 0) ? table->rows[0].count : 0;
            table_block->row_count = table->count;
            table_block->cell_count = table_block->row_count * table_block->column_count;
            table_block->page_number = page_number;
            table_block->column_consistency = 1.0f;
            table_block->spans = NULL;
            table_block->links = NULL;
            table_block->has_superscript = 0;
            table_block->is_footnote = 0;
            table_block->heading_level = 0;
            table_block->column_index = 0;
            table_block->list_items = NULL;

            // Deep copy the table structure
            table_block->table_data = malloc(sizeof(Table));
            if (table_block->table_data)
            {
                Table* new_table = (Table*)table_block->table_data;
                new_table->bbox = table->bbox;
                new_table->count = table->count;
                new_table->rows = table->rows;

                // Transfer ownership
                table->rows = NULL;
                table->count = 0;
            }
        }
    }
}

void free_table_array(TableArray* tables)
{
    if (!tables)
        return;
    for (int i = 0; i < tables->count; ++i)
    {
        Table* table = &tables->tables[i];
        if (table->rows)
        {
            for (int j = 0; j < table->count; ++j)
            {
                if (table->rows[j].cells)
                {
                    for (int k = 0; k < table->rows[j].count; ++k)
                    {
                        free(table->rows[j].cells[k].text);
                    }
                    free(table->rows[j].cells);
                }
            }
            free(table->rows);
        }
    }
    free(tables->tables);
    free(tables);
}
