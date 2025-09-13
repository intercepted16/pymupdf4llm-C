/*
 * Comprehensive PDF Layout Detection System
 *
 * Implements robust column detection, text/table classification,
 * and adaptive merging with proper preprocessing.
 */

#include "../include/multicolumn.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_RECTS 10000
#define MAX_COLUMNS 20
#define MAX_HISTOGRAM_BINS 1000

// === Data Structures ===

typedef enum
{
    BLOCK_TYPE_TEXT,
    BLOCK_TYPE_IMAGE,
    BLOCK_TYPE_LINE,
    BLOCK_TYPE_TABLE_CELL
} block_type_t;

typedef struct
{
    fz_rect bbox;
    block_type_t type;
    char* text;
    float font_size;
    int font_weight;
    float orientation;
    int column_id;
} pdf_block_t;

typedef struct
{
    float x0, x1;
    int block_count;
    pdf_block_t** blocks;
    float median_gap;
    float median_width;
    float median_height;
} column_t;

typedef struct
{
    pdf_block_t* blocks;
    int count;
    int capacity;
    column_t columns[MAX_COLUMNS];
    int column_count;
    float page_width;
    float page_height;
} page_layout_t;

typedef struct
{
    int* histogram;
    int bin_count;
    float bin_width;
    float page_width;
} vertical_projection_t;

// === Utility Functions ===

static int fz_rect_intersects(fz_rect a, fz_rect b)
{
    if (a.x1 < b.x0 || a.x0 > b.x1 || a.y1 < b.y0 || a.y0 > b.y1)
    {
        return 0; // No intersection
    }
    return 1; // Intersection
}

static float compute_overlap_ratio(float a0, float a1, float b0, float b1)
{
    float overlap = fz_max(0, fz_min(a1, b1) - fz_max(a0, b0));
    float min_span = fz_min(a1 - a0, b1 - b0);
    return (min_span > 0) ? (overlap / min_span) : 0;
}

static int compare_blocks_top_left(const void* a, const void* b)
{
    const pdf_block_t* ba = (const pdf_block_t*)a;
    const pdf_block_t* bb = (const pdf_block_t*)b;
    if (fabs(ba->bbox.y0 - bb->bbox.y0) > 2.0f)
    {
        return (ba->bbox.y0 < bb->bbox.y0) ? -1 : 1;
    }
    return (ba->bbox.x0 < bb->bbox.x0) ? -1 : (ba->bbox.x0 > bb->bbox.x0) ? 1 : 0;
}

static int compare_floats_asc(const void* a, const void* b)
{
    float fa = *(const float*)a;
    float fb = *(const float*)b;
    return (fa < fb) ? -1 : (fa > fb) ? 1 : 0;
}

// === Step 1: Page Preprocessing ===

static page_layout_t* extract_page_blocks(fz_context* ctx, fz_document* doc, int page_number,
                                          float header_margin, float footer_margin)
{
    page_layout_t* layout = malloc(sizeof(page_layout_t));
    memset(layout, 0, sizeof(page_layout_t));

    fz_page* page = fz_load_page(ctx, doc, page_number);
    fz_rect page_rect = fz_bound_page(ctx, page);

    // Normalize coordinates and apply margins
    layout->page_width = page_rect.x1 - page_rect.x0;
    layout->page_height = page_rect.y1 - page_rect.y0;

    fz_rect content_area = page_rect;
    content_area.y0 += header_margin;
    content_area.y1 -= footer_margin;

    // Extract text blocks
    fz_stext_options opts = {0};
    fz_stext_page* tp = fz_new_stext_page_from_page_number(ctx, doc, page_number, &opts);

    layout->capacity = 1000;
    layout->blocks = malloc(layout->capacity * sizeof(pdf_block_t));

    for (fz_stext_block* block = tp->first_block; block; block = block->next)
    {
        if (block->type != FZ_STEXT_BLOCK_TEXT) continue;

        fz_rect bbox = block->bbox;

        // Skip blocks outside content area
        if (!fz_rect_intersects(bbox, content_area)) continue;

        // Clip to content area
        bbox = fz_intersect_rect(bbox, content_area);

        if (layout->count >= layout->capacity)
        {
            layout->capacity *= 2;
            layout->blocks = realloc(layout->blocks, layout->capacity * sizeof(pdf_block_t));
        }

        pdf_block_t* pdf_block = &layout->blocks[layout->count];
        pdf_block->bbox = bbox;
        pdf_block->type = BLOCK_TYPE_TEXT;
        pdf_block->column_id = -1;

        // Extract text and font info
        char text_buffer[10000] = "";
        float total_font_size = 0;
        int char_count = 0;

        for (fz_stext_line* line = block->u.t.first_line; line; line = line->next)
        {
            for (fz_stext_char* ch = line->first_char; ch; ch = ch->next)
            {
                char utf8[8];
                int len = fz_runetochar(utf8, ch->c);
                if (len > 0 && len < 8)
                {
                    utf8[len] = '\0';
                    strncat(text_buffer, utf8, sizeof(text_buffer) - strlen(text_buffer) - 1);
                    total_font_size += ch->size;
                    char_count++;
                }
            }
        }

        pdf_block->text = malloc(strlen(text_buffer) + 1);
        strcpy(pdf_block->text, text_buffer);
        pdf_block->font_size = (char_count > 0) ? (total_font_size / char_count) : 12.0f;
        pdf_block->font_weight = 400; // Default weight
        pdf_block->orientation = 0;   // Default horizontal

        layout->count++;
    }

    fz_drop_stext_page(ctx, tp);
    fz_drop_page(ctx, page);

    // Sort blocks top-to-bottom, left-to-right
    qsort(layout->blocks, layout->count, sizeof(pdf_block_t), compare_blocks_top_left);

    return layout;
}

// === Step 2: Column Detection with XY-Cut ===

static vertical_projection_t* compute_vertical_projection(page_layout_t* layout)
{
    vertical_projection_t* proj = malloc(sizeof(vertical_projection_t));
    proj->bin_count = fz_min(MAX_HISTOGRAM_BINS, (int)(layout->page_width / 2.0f));
    proj->bin_width = layout->page_width / proj->bin_count;
    proj->page_width = layout->page_width;
    proj->histogram = calloc(proj->bin_count, sizeof(int));

    // Fill histogram with text block occupancy
    for (int i = 0; i < layout->count; i++)
    {
        pdf_block_t* block = &layout->blocks[i];
        int start_bin = (int)(block->bbox.x0 / proj->bin_width);
        int end_bin = (int)(block->bbox.x1 / proj->bin_width);

        start_bin = fz_max(0, fz_min(start_bin, proj->bin_count - 1));
        end_bin = fz_max(0, fz_min(end_bin, proj->bin_count - 1));

        for (int bin = start_bin; bin <= end_bin; bin++)
        {
            proj->histogram[bin]++;
        }
    }

    return proj;
}

static void detect_columns(page_layout_t* layout)
{
    vertical_projection_t* proj = compute_vertical_projection(layout);

    // Find zero-occupancy gaps for column boundaries
    int* gap_starts = malloc(proj->bin_count * sizeof(int));
    int* gap_ends = malloc(proj->bin_count * sizeof(int));
    int gap_count = 0;

    int in_gap = 0;
    for (int i = 0; i < proj->bin_count; i++)
    {
        if (proj->histogram[i] == 0)
        {
            if (!in_gap)
            {
                gap_starts[gap_count] = i;
                in_gap = 1;
            }
        }
        else
        {
            if (in_gap)
            {
                gap_ends[gap_count] = i - 1;
                gap_count++;
                in_gap = 0;
            }
        }
    }
    if (in_gap)
    {
        gap_ends[gap_count] = proj->bin_count - 1;
        gap_count++;
    }

    // Create columns based on gaps
    layout->column_count = 0;
    float current_x = 0;

    for (int g = 0; g < gap_count; g++)
    {
        if (gap_starts[g] * proj->bin_width - current_x > proj->bin_width * 5)
        { // Minimum column width
            column_t* col = &layout->columns[layout->column_count];
            col->x0 = current_x;
            col->x1 = gap_starts[g] * proj->bin_width;
            col->blocks = malloc(layout->count * sizeof(pdf_block_t*));
            col->block_count = 0;
            layout->column_count++;
        }
        current_x = (gap_ends[g] + 1) * proj->bin_width;
    }

    // Add final column if needed
    if (current_x < layout->page_width && layout->column_count < MAX_COLUMNS)
    {
        column_t* col = &layout->columns[layout->column_count];
        col->x0 = current_x;
        col->x1 = layout->page_width;
        col->blocks = malloc(layout->count * sizeof(pdf_block_t*));
        col->block_count = 0;
        layout->column_count++;
    }

    // If no columns detected, create single column
    if (layout->column_count == 0)
    {
        column_t* col = &layout->columns[0];
        col->x0 = 0;
        col->x1 = layout->page_width;
        col->blocks = malloc(layout->count * sizeof(pdf_block_t*));
        col->block_count = 0;
        layout->column_count = 1;
    }

    // Assign blocks to columns
    for (int i = 0; i < layout->count; i++)
    {
        pdf_block_t* block = &layout->blocks[i];

        int best_column = 0;
        float best_overlap = 0;

        for (int c = 0; c < layout->column_count; c++)
        {
            column_t* col = &layout->columns[c];
            float overlap = compute_overlap_ratio(block->bbox.x0, block->bbox.x1, col->x0, col->x1);
            if (overlap > best_overlap)
            {
                best_overlap = overlap;
                best_column = c;
            }
        }

        block->column_id = best_column;
        layout->columns[best_column].blocks[layout->columns[best_column].block_count++] = block;
    }

    free(gap_starts);
    free(gap_ends);
    free(proj->histogram);
    free(proj);
}

// === Step 3: Text vs Table Classification ===

static float compute_alignment_score(pdf_block_t* block, pdf_block_t** neighbors,
                                     int neighbor_count)
{
    if (neighbor_count == 0) return 0;

    int aligned_count = 0;
    for (int i = 0; i < neighbor_count; i++)
    {
        if (fabs(neighbors[i]->bbox.x0 - block->bbox.x0) < 5.0f ||
            fabs(neighbors[i]->bbox.x1 - block->bbox.x1) < 5.0f)
        {
            aligned_count++;
        }
    }

    return (float)aligned_count / neighbor_count;
}

static void classify_blocks(page_layout_t* layout)
{
    for (int c = 0; c < layout->column_count; c++)
    {
        column_t* col = &layout->columns[c];

        // Compute column statistics
        float* widths = malloc(col->block_count * sizeof(float));
        float* heights = malloc(col->block_count * sizeof(float));

        for (int i = 0; i < col->block_count; i++)
        {
            widths[i] = col->blocks[i]->bbox.x1 - col->blocks[i]->bbox.x0;
            heights[i] = col->blocks[i]->bbox.y1 - col->blocks[i]->bbox.y0;
        }

        if (col->block_count > 0)
        {
            qsort(widths, col->block_count, sizeof(float), compare_floats_asc);
            qsort(heights, col->block_count, sizeof(float), compare_floats_asc);
            col->median_width = widths[col->block_count / 2];
            col->median_height = heights[col->block_count / 2];
        }

        // Classify each block
        for (int i = 0; i < col->block_count; i++)
        {
            pdf_block_t* block = col->blocks[i];
            float width = block->bbox.x1 - block->bbox.x0;

            // Feature computation
            float width_ratio = width / col->median_width;
            float column_span = width / (col->x1 - col->x0);
            float alignment_score = compute_alignment_score(block, col->blocks, col->block_count);

            // Classification heuristics
            int is_table_candidate = 0;

            // Small, regularly-sized blocks with high alignment
            if (width_ratio < 0.7f && alignment_score > 0.3f)
            {
                is_table_candidate = 1;
            }

            // Narrow blocks that don't span most of column
            if (column_span < 0.6f && alignment_score > 0.2f)
            {
                is_table_candidate = 1;
            }

            // Multiple similar-width blocks suggest table
            int similar_width_count = 0;
            for (int j = 0; j < col->block_count; j++)
            {
                if (i != j)
                {
                    float other_width = col->blocks[j]->bbox.x1 - col->blocks[j]->bbox.x0;
                    if (fabs(width - other_width) / width < 0.2f)
                    {
                        similar_width_count++;
                    }
                }
            }
            if (similar_width_count >= 2)
            {
                is_table_candidate = 1;
            }

            if (is_table_candidate)
            {
                block->type = BLOCK_TYPE_TABLE_CELL;
            }
        }

        free(widths);
        free(heights);
    }
}

// === Step 4: Adaptive Text Block Merging ===

static void compute_column_gaps(column_t* col)
{
    if (col->block_count < 2)
    {
        col->median_gap = 10.0f;
        return;
    }

    // Sort blocks by vertical position
    pdf_block_t** text_blocks = malloc(col->block_count * sizeof(pdf_block_t*));
    int text_count = 0;

    for (int i = 0; i < col->block_count; i++)
    {
        if (col->blocks[i]->type == BLOCK_TYPE_TEXT)
        {
            text_blocks[text_count++] = col->blocks[i];
        }
    }

    if (text_count < 2)
    {
        col->median_gap = 10.0f;
        free(text_blocks);
        return;
    }

    qsort(text_blocks, text_count, sizeof(pdf_block_t*), compare_blocks_top_left);

    float* gaps = malloc((text_count - 1) * sizeof(float));
    int gap_count = 0;

    for (int i = 0; i < text_count - 1; i++)
    {
        if (text_blocks[i + 1]->bbox.y0 > text_blocks[i]->bbox.y1)
        {
            float overlap_ratio =
                compute_overlap_ratio(text_blocks[i]->bbox.x0, text_blocks[i]->bbox.x1,
                                      text_blocks[i + 1]->bbox.x0, text_blocks[i + 1]->bbox.x1);
            if (overlap_ratio > 0.4f)
            { // Only consider overlapping blocks
                gaps[gap_count++] = text_blocks[i + 1]->bbox.y0 - text_blocks[i]->bbox.y1;
            }
        }
    }

    if (gap_count > 0)
    {
        qsort(gaps, gap_count, sizeof(float), compare_floats_asc);
        col->median_gap = gaps[gap_count / 2];
    }
    else
    {
        col->median_gap = 10.0f;
    }

    free(gaps);
    free(text_blocks);
}

static void merge_text_blocks_adaptive(column_t* col)
{
    compute_column_gaps(col);

    pdf_block_t** text_blocks = malloc(col->block_count * sizeof(pdf_block_t*));
    int text_count = 0;

    for (int i = 0; i < col->block_count; i++)
    {
        if (col->blocks[i]->type == BLOCK_TYPE_TEXT)
        {
            text_blocks[text_count++] = col->blocks[i];
        }
    }

    if (text_count < 2)
    {
        free(text_blocks);
        return;
    }

    int merged_any = 1;
    while (merged_any)
    {
        merged_any = 0;
        qsort(text_blocks, text_count, sizeof(pdf_block_t*), compare_blocks_top_left);

        for (int i = 0; i < text_count - 1; i++)
        {
            pdf_block_t* b1 = text_blocks[i];
            pdf_block_t* b2 = text_blocks[i + 1];

            // Check vertical gap
            float v_gap = b2->bbox.y0 - b1->bbox.y1;
            if (v_gap < 0 || v_gap > col->median_gap * 1.8f) continue;

            // Check horizontal overlap
            float h_overlap =
                compute_overlap_ratio(b1->bbox.x0, b1->bbox.x1, b2->bbox.x0, b2->bbox.x1);
            if (h_overlap < 0.45f) continue;

            // Check font size compatibility
            if (fabs(b1->font_size - b2->font_size) > b1->font_size * 0.3f) continue;

            // Merge blocks
            b1->bbox = fz_union_rect(b1->bbox, b2->bbox);

            // Merge text
            char* new_text = malloc(strlen(b1->text) + strlen(b2->text) + 2);
            sprintf(new_text, "%s %s", b1->text, b2->text);
            free(b1->text);
            b1->text = new_text;

            // Remove b2
            free(b2->text);
            for (int j = i + 1; j < text_count - 1; j++)
            {
                text_blocks[j] = text_blocks[j + 1];
            }
            text_count--;
            merged_any = 1;
            break;
        }
    }

    free(text_blocks);
}

// === Step 5: Enhanced Table Detection ===

typedef struct
{
    pdf_block_t** cells;
    int cell_count;
    fz_rect bbox;
} table_cluster_t;

static table_cluster_t* detect_table_clusters(column_t* col, int* cluster_count)
{
    pdf_block_t** table_blocks = malloc(col->block_count * sizeof(pdf_block_t*));
    int table_block_count = 0;

    for (int i = 0; i < col->block_count; i++)
    {
        if (col->blocks[i]->type == BLOCK_TYPE_TABLE_CELL)
        {
            table_blocks[table_block_count++] = col->blocks[i];
        }
    }

    if (table_block_count < 4)
    { // Need at least 2x2 for a table
        *cluster_count = 0;
        free(table_blocks);
        return NULL;
    }

    // Group blocks into rows and columns using alignment
    table_cluster_t* clusters = malloc(table_block_count * sizeof(table_cluster_t));
    *cluster_count = 0;

    int* assigned = calloc(table_block_count, sizeof(int));

    for (int i = 0; i < table_block_count; i++)
    {
        if (assigned[i]) continue;

        table_cluster_t* cluster = &clusters[*cluster_count];
        cluster->cells = malloc(table_block_count * sizeof(pdf_block_t*));
        cluster->cell_count = 0;
        cluster->bbox = table_blocks[i]->bbox;

        // Add seed block
        cluster->cells[cluster->cell_count++] = table_blocks[i];
        assigned[i] = 1;

        // Find aligned blocks
        int added_any = 1;
        while (added_any)
        {
            added_any = 0;
            for (int j = 0; j < table_block_count; j++)
            {
                if (assigned[j]) continue;

                // Check alignment with any block in current cluster
                int is_aligned = 0;
                for (int k = 0; k < cluster->cell_count; k++)
                {
                    pdf_block_t* cluster_block = cluster->cells[k];
                    pdf_block_t* candidate = table_blocks[j];

                    // Row alignment (y-overlap >= 70%)
                    float y_overlap =
                        compute_overlap_ratio(cluster_block->bbox.y0, cluster_block->bbox.y1,
                                              candidate->bbox.y0, candidate->bbox.y1);

                    // Column alignment (x-overlap >= 70%)
                    float x_overlap =
                        compute_overlap_ratio(cluster_block->bbox.x0, cluster_block->bbox.x1,
                                              candidate->bbox.x0, candidate->bbox.x1);

                    // Check proximity
                    float x_gap = fz_max(0, fz_max(cluster_block->bbox.x0, candidate->bbox.x0) -
                                                fz_min(cluster_block->bbox.x1, candidate->bbox.x1));
                    float y_gap = fz_max(0, fz_max(cluster_block->bbox.y0, candidate->bbox.y0) -
                                                fz_min(cluster_block->bbox.y1, candidate->bbox.y1));

                    if ((y_overlap >= 0.7f && x_gap < col->median_width) ||
                        (x_overlap >= 0.7f && y_gap < col->median_height))
                    {
                        // Check size compatibility
                        float width_ratio = fz_max(cluster_block->bbox.x1 - cluster_block->bbox.x0,
                                                   candidate->bbox.x1 - candidate->bbox.x0) /
                                            fz_min(cluster_block->bbox.x1 - cluster_block->bbox.x0,
                                                   candidate->bbox.x1 - candidate->bbox.x0);

                        if (width_ratio <= 2.5f)
                        {
                            is_aligned = 1;
                            break;
                        }
                    }
                }

                if (is_aligned)
                {
                    cluster->cells[cluster->cell_count++] = table_blocks[j];
                    cluster->bbox = fz_union_rect(cluster->bbox, table_blocks[j]->bbox);
                    assigned[j] = 1;
                    added_any = 1;
                }
            }
        }

        // Only keep clusters with multiple cells
        if (cluster->cell_count >= 2)
        {
            (*cluster_count)++;
        }
        else
        {
            free(cluster->cells);
        }
    }

    free(assigned);
    free(table_blocks);
    return clusters;
}

// === Step 6: Final Assembly ===

static fz_rect* assemble_final_blocks(page_layout_t* layout, int* result_count)
{
    int total_blocks = 0;

    // Count final blocks
    for (int c = 0; c < layout->column_count; c++)
    {
        column_t* col = &layout->columns[c];

        // Merge text blocks first
        merge_text_blocks_adaptive(col);

        // Count text blocks
        for (int i = 0; i < col->block_count; i++)
        {
            if (col->blocks[i]->type == BLOCK_TYPE_TEXT)
            {
                total_blocks++;
            }
        }

        // Count table clusters
        int cluster_count;
        table_cluster_t* clusters = detect_table_clusters(col, &cluster_count);
        total_blocks += cluster_count;

        if (clusters)
        {
            for (int i = 0; i < cluster_count; i++)
            {
                free(clusters[i].cells);
            }
            free(clusters);
        }
    }

    fz_rect* final_blocks = malloc(total_blocks * sizeof(fz_rect));
    int block_index = 0;

    // Assemble blocks column by column
    for (int c = 0; c < layout->column_count; c++)
    {
        column_t* col = &layout->columns[c];

        // Add text blocks
        for (int i = 0; i < col->block_count; i++)
        {
            if (col->blocks[i]->type == BLOCK_TYPE_TEXT)
            {
                final_blocks[block_index++] = col->blocks[i]->bbox;
            }
        }

        // Add table clusters
        int cluster_count;
        table_cluster_t* clusters = detect_table_clusters(col, &cluster_count);
        if (clusters)
        {
            for (int i = 0; i < cluster_count; i++)
            {
                final_blocks[block_index++] = clusters[i].bbox;
                free(clusters[i].cells);
            }
            free(clusters);
        }
    }

    *result_count = total_blocks;
    return final_blocks;
}

// === Main Entry Point ===

fz_rect* _column_boxes(fz_context* ctx, fz_document* doc, int page_number, float footer_margin,
                       float header_margin, int* result_count)
{
    // Step 1: Extract and preprocess page blocks
    page_layout_t* layout =
        extract_page_blocks(ctx, doc, page_number, header_margin, footer_margin);

    if (layout->count == 0)
    {
        *result_count = 0;
        free(layout->blocks);
        free(layout);
        return NULL;
    }

    // Step 2: Detect columns using XY-cut
    detect_columns(layout);

    // Step 3: Classify blocks into text vs table candidates
    classify_blocks(layout);

    // Step 6: Final assembly with adaptive merging
    fz_rect* result = assemble_final_blocks(layout, result_count);

    // Cleanup
    for (int i = 0; i < layout->count; i++)
    {
        free(layout->blocks[i].text);
    }
    for (int c = 0; c < layout->column_count; c++)
    {
        free(layout->columns[c].blocks);
    }
    free(layout->blocks);
    free(layout);

    return result;
}

// define the wrapper function for Python
fz_rect* column_boxes(const char* pdf_path, int page_number, int* table_count,
                      ColumnBoxesOptions* options)
{
    fz_context* ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
    if (!ctx)
    {
        fprintf(stderr, "Failed to create MuPDF context\n");
        return NULL;
    }

    fz_document* doc = NULL;
    fz_rect* boxes = NULL;
    int succeeded = 0;

    fz_try(ctx)
    {
        fz_register_document_handlers(ctx);
        doc = fz_open_document(ctx, pdf_path);

        int page_count = fz_count_pages(ctx, doc);
        if (page_number < 0 || page_number >= page_count)
        {
            fz_throw(ctx, FZ_ERROR_GENERIC, "Invalid page number: %d", page_number);
        }

        boxes = _column_boxes(ctx, doc, page_number, options->footer_margin,
                                       options->header_margin, table_count);
        succeeded = 1;
    }
    fz_catch(ctx)
    {
        fprintf(stderr, "Error: %s\n", fz_caught_message(ctx));
        // boxes is already NULL, succeeded is 0
    }

    if (doc)
        fz_drop_document(ctx, doc);
    fz_drop_context(ctx);

    if (succeeded)
        return boxes;
    else
    {
        *table_count = 0;
        return NULL;
    }
}
