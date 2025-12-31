// table/table_core.c - Orchestrates grid and text column table detection

#include "table.h"
#include "table_grid.h"
#include "table_two_column.h"
#include "table_capture.h"
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

// Main orchestration function: tries grid-based detection first, falls back to text column synthesis
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

        merge_edges(&h_edges, SNAP_TOLERANCE, JOIN_TOLERANCE);
        merge_edges(&v_edges, SNAP_TOLERANCE, JOIN_TOLERANCE);

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
        find_cells(&intersections, &hash, &cells);

        free_edge_array(&h_edges);
        free_edge_array(&v_edges);
        free_point_array(&intersections);

        tables = group_cells_into_tables(&cells);
        free_cell_array(&cells);

        fz_rect page_rect = fz_bound_page(ctx, page);
        bool tables_valid = validate_tables(tables, page_rect);

        // Step 5: Fallback to text column synthesis if grid detection failed
        if (!tables_valid)
        {
            if (tables)
            {
                free_table_array(tables);
                tables = NULL;
            }

            // Synthesize 2-column table from text layout
            fz_stext_page* textpage = fz_new_stext_page_from_page(ctx, page, NULL);
            if (textpage)
            {
                FontStats font_stats;
                font_stats_reset(&font_stats);
                for (fz_stext_block* block = textpage->first_block; block; block = block->next)
                {
                    if (block->type != FZ_STEXT_BLOCK_TEXT)
                        continue;
                    for (fz_stext_line* line = block->u.t.first_line; line; line = line->next)
                    {
                        for (fz_stext_char* ch = line->first_char; ch; ch = ch->next)
                        {
                            if (ch->c != 0)
                                font_stats_add(&font_stats, ch->size);
                        }
                    }
                }

                PageMetrics metrics = compute_page_metrics(&font_stats);
                tables = synthesize_text_table_two_col(ctx, textpage, &metrics);
                fz_drop_stext_page(ctx, textpage);
            }
        }
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
            table_block->confidence = 1.0f;
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
