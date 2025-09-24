/*
 * Improved Table Detection for MuPDF C Library
 *
 * This file implements an enhanced table detection algorithm that offers better
 * accuracy and performance compared to simpler methods. It uses a combination of
 * spatial clustering, grid analysis, and content analysis to identify tables.
 */

#include <ctype.h>
#include <math.h>
#include <mupdf/fitz.h>
#include <stdlib.h>
#include <string.h>

// =============================================================================
// Constants and Struct Definitions
// =============================================================================

#define EPSILON 2.0f        // Epsilon for float comparisons, used in clustering.
#define MAX_CANDIDATES 100  // Maximum number of table candidates to consider on a page.
#define MAX_BLOCKS 3000     // Maximum number of text blocks to process on a page.

// Represents a potential table region found on a page.
typedef struct
{
    fz_rect bbox;       // The bounding box of the candidate region.
    int block_start;    // The starting index of blocks belonging to this candidate.
    int block_count;    // The number of blocks in this candidate.
    float score;        // A confidence score (0.0 to 1.0) indicating the likelihood of it being a table.
} table_candidate_t;

// Stores features extracted from a potential table region for scoring.
typedef struct
{
    int row_count;          // Number of detected rows.
    int col_count;          // Number of detected columns.
    int cell_count;         // Total number of cells (rows * columns).
    float regularity_score; // Score indicating how regular the grid structure is.
    float alignment_score;  // Score indicating how well cells are aligned.
    float content_score;    // Score based on the likelihood of tabular content (e.g., numeric data).
} table_features_t;

// A spatial grid data structure for efficiently finding neighboring text blocks.
typedef struct
{
    fz_rect* blocks;        // Array of all text blocks on the page.
    int* grid_cells;        // The main grid array, storing indices of blocks.
    int* cell_counts;       // Array storing the number of blocks in each grid cell.
    int grid_width;         // Width of the grid in cells.
    int grid_height;        // Height of the grid in cells.
    fz_rect bounds;         // The overall bounds of the page content.
    float cell_width;       // Width of a single grid cell.
    float cell_height;      // Height of a single grid cell.
    int block_count;        // Total number of blocks.
} spatial_grid_t;

// A rectangle with an associated index, used for spatial operations.
typedef struct
{
    fz_rect bbox;
    int index;
} indexed_rect_t;

// Helper functions
static int is_white(const char* text)
{
    if (!text) return 1;
    while (*text)
    {
        if (!isspace((unsigned char)*text)) return 0;
        text++;
    }
    return 1;
}

static fz_rect fz_rect_union_custom(fz_rect a, fz_rect b)
{
    if (fz_is_empty_rect(a)) return b;
    if (fz_is_empty_rect(b)) return a;
    fz_rect result;
    result.x0 = fz_min(a.x0, b.x0);
    result.y0 = fz_min(a.y0, b.y0);
    result.x1 = fz_max(a.x1, b.x1);
    result.y1 = fz_max(a.y1, b.y1);
    return result;
}

static int fz_rect_contains_custom(fz_rect container, fz_rect contained)
{
    return (contained.x0 >= container.x0 && contained.y0 >= container.y0 &&
            contained.x1 <= container.x1 && contained.y1 <= container.y1);
}

static int compare_rects_by_top_left(const void* a, const void* b)
{
    const fz_rect* ra = (const fz_rect*)a;
    const fz_rect* rb = (const fz_rect*)b;
    if (ra->y0 != rb->y0) return (ra->y0 < rb->y0) ? -1 : 1;
    return (ra->x0 < rb->x0) ? -1 : (ra->x0 > rb->x0) ? 1 : 0;
}

static int compare_floats(const void* a, const void* b)
{
    float fa = *(const float*)a;
    float fb = *(const float*)b;
    if (fa < fb) return -1;
    if (fa > fb) return 1;
    return 0;
}

// Spatial grid functions
static spatial_grid_t* create_spatial_grid(fz_rect* blocks, int block_count, fz_rect page_bounds)
{
    if (block_count == 0) return NULL;

    spatial_grid_t* grid = malloc(sizeof(spatial_grid_t));
    if (!grid) return NULL;

    grid->bounds = page_bounds;
    grid->block_count = block_count;
    grid->blocks = malloc(block_count * sizeof(fz_rect));
    if (!grid->blocks)
    {
        free(grid);
        return NULL;
    }
    memcpy(grid->blocks, blocks, block_count * sizeof(fz_rect));

    // Calculate grid dimensions
    float width = page_bounds.x1 - page_bounds.x0;
    float height = page_bounds.y1 - page_bounds.y0;
    grid->grid_width = (int)fz_max(10, width / 50.0f); // Approx 50pt per cell
    grid->grid_height = (int)fz_max(10, height / 50.0f);

    grid->cell_width = width / grid->grid_width;
    grid->cell_height = height / grid->grid_height;

    // Allocate grid cells
    int total_cells = grid->grid_width * grid->grid_height;
    grid->grid_cells = malloc(block_count * total_cells * sizeof(int));
    grid->cell_counts = calloc(total_cells, sizeof(int));

    if (!grid->grid_cells || !grid->cell_counts)
    {
        free(grid->blocks);
        free(grid->grid_cells);
        free(grid->cell_counts);
        free(grid);
        return NULL;
    }

    // Assign blocks to grid cells
    for (int i = 0; i < block_count; i++)
    {
        fz_rect block = blocks[i];
        // Calculate which cells this block overlaps
        int start_x = (int)((block.x0 - page_bounds.x0) / grid->cell_width);
        int end_x = (int)((block.x1 - page_bounds.x0) / grid->cell_width);
        int start_y = (int)((block.y0 - page_bounds.y0) / grid->cell_height);
        int end_y = (int)((block.y1 - page_bounds.y0) / grid->cell_height);

        // Clamp to grid bounds
        start_x = fz_max(0, start_x);
        end_x = fz_min(grid->grid_width - 1, end_x);
        start_y = fz_max(0, start_y);
        end_y = fz_min(grid->grid_height - 1, end_y);

        // Add block to all overlapping cells
        for (int y = start_y; y <= end_y; y++)
        {
            for (int x = start_x; x <= end_x; x++)
            {
                int cell_index = y * grid->grid_width + x;
                int cell_offset = cell_index * block_count + grid->cell_counts[cell_index];
                grid->grid_cells[cell_offset] = i;
                grid->cell_counts[cell_index]++;
            }
        }
    }

    return grid;
}

static void destroy_spatial_grid(spatial_grid_t* grid)
{
    if (grid)
    {
        free(grid->blocks);
        free(grid->grid_cells);
        free(grid->cell_counts);
        free(grid);
    }
}

// Find nearby blocks using spatial grid
static int find_nearby_blocks(spatial_grid_t* grid, fz_rect query, int* results, int max_results)
{
    if (!grid || !results) return 0;

    int count = 0;
    int found[MAX_BLOCKS] = {0}; // Track already found blocks

    // Calculate which cells this query overlaps
    int start_x = (int)((query.x0 - grid->bounds.x0) / grid->cell_width);
    int end_x = (int)((query.x1 - grid->bounds.x0) / grid->cell_width);
    int start_y = (int)((query.y0 - grid->bounds.y0) / grid->cell_height);
    int end_y = (int)((query.y1 - grid->bounds.y0) / grid->cell_height);

    // Clamp to grid bounds
    start_x = fz_max(0, start_x);
    end_x = fz_min(grid->grid_width - 1, end_x);
    start_y = fz_max(0, start_y);
    end_y = fz_min(grid->grid_height - 1, end_y);

    // Check all overlapping cells
    for (int y = start_y; y <= end_y; y++)
    {
        for (int x = start_x; x <= end_x; x++)
        {
            int cell_index = y * grid->grid_width + x;
            int cell_count = grid->cell_counts[cell_index];

            // Add blocks from this cell
            for (int i = 0; i < cell_count && count < max_results; i++)
            {
                int block_index = grid->grid_cells[cell_index * grid->block_count + i];
                if (!found[block_index])
                {
                    results[count++] = block_index;
                    found[block_index] = 1;
                }
            }
        }
    }

    return count;
}

// Analyze content patterns typical of tables
static float analyze_table_content(fz_stext_page* textpage, fz_rect table_bbox)
{
    int numeric_count = 0;
    int total_count = 0;
    int short_text_count = 0;

    // Iterate through text in table area
    for (fz_stext_block* block = textpage->first_block; block; block = block->next)
    {
        if (block->type != FZ_STEXT_BLOCK_TEXT) continue;

        // Check if block is within table bounds
        if (!fz_rect_contains_custom(table_bbox, block->bbox)) continue;

        for (fz_stext_line* line = block->u.t.first_line; line; line = line->next)
        {
            // Extract text and analyze
            char text[1000] = "";
            for (fz_stext_char* ch = line->first_char; ch; ch = ch->next)
            {
                char utf8[8];
                int len = fz_runetochar(utf8, ch->c);
                if (len > 0 && len < 8)
                {
                    utf8[len] = '\0';
                    if (strlen(text) + len < sizeof(text) - 1)
                    {
                        strcat(text, utf8);
                    }
                }
            }

            if (is_white(text))
            {
                continue;
            }

            total_count++;

            // Check for numeric content (common in tables)
            int has_numeric = 0;
            for (int i = 0; text[i]; i++)
            {
                if (isdigit((unsigned char)text[i]))
                {
                    has_numeric = 1;
                    break;
                }
            }
            if (has_numeric)
            {
                numeric_count++;
            }

            // Check for short text (typical of table cells)
            if (strlen(text) < 30)
            {
                short_text_count++;
            }
        }
    }

    if (total_count == 0) return 0.0f;

    // Return score based on table-like content
    float numeric_ratio = (float)numeric_count / total_count;
    float short_text_ratio = (float)short_text_count / total_count;

    // Weight numeric content more heavily as it's a strong indicator of tables
    return (numeric_ratio * 0.7f + short_text_ratio * 0.3f);
}

// Detect grid structure
static int detect_grid_structure(fz_rect* blocks, int block_count, float* h_lines, int* h_count,
                                 float* v_lines, int* v_count)
{
    // Simple approach: cluster y-coordinates for horizontal lines,
    // cluster x-coordinates for vertical lines

    if (block_count < 4) return 0;

    // Collect all coordinates
    float* y_coords = malloc(block_count * 2 * sizeof(float));
    float* x_coords = malloc(block_count * 2 * sizeof(float));
    int y_count = 0, x_count = 0;

    for (int i = 0; i < block_count; i++)
    {
        y_coords[y_count++] = blocks[i].y0;
        y_coords[y_count++] = blocks[i].y1;
        x_coords[x_count++] = blocks[i].x0;
        x_coords[x_count++] = blocks[i].x1;
    }

    // Sort coordinates
    qsort(y_coords, y_count, sizeof(float), compare_floats);
    qsort(x_coords, x_count, sizeof(float), compare_floats);

    // Cluster similar coordinates (simplified)
    *h_count = 0;
    *v_count = 0;

    // Find horizontal lines (cluster y-coordinates)
    for (int i = 0; i < y_count && *h_count < 50; i++)
    {
        int found = 0;
        for (int j = 0; j < *h_count; j++)
        {
            if (fabs(h_lines[j] - y_coords[i]) < EPSILON)
            {
                found = 1;
                break;
            }
        }
        if (!found)
        {
            h_lines[(*h_count)++] = y_coords[i];
        }
    }

    // Find vertical lines (cluster x-coordinates)
    for (int i = 0; i < x_count && *v_count < 50; i++)
    {
        int found = 0;
        for (int j = 0; j < *v_count; j++)
        {
            if (fabs(v_lines[j] - x_coords[i]) < EPSILON)
            {
                found = 1;
                break;
            }
        }
        if (!found)
        {
            v_lines[(*v_count)++] = x_coords[i];
        }
    }

    free(y_coords);
    free(x_coords);

    // A table should have at least 2 rows and 1 column, or 1 row and 2 columns
    return (*h_count >= 3 && *v_count >= 2) || (*h_count >= 2 && *v_count >= 3);
}

// Analyze table structure features
static table_features_t analyze_table_structure(fz_rect* blocks, int block_count,
                                                fz_stext_page* textpage)
{
    table_features_t features = {0};

    if (block_count < 4) return features;

    // 1. Count potential rows and columns by clustering coordinates
    float h_lines[100], v_lines[100];
    int h_count = 0, v_count = 0;

    if (detect_grid_structure(blocks, block_count, h_lines, &h_count, v_lines, &v_count))
    {
        features.row_count = h_count - 1;
        features.col_count = v_count - 1;
        features.cell_count = features.row_count * features.col_count;

        // Regularity score based on even spacing
        if (h_count > 2)
        {
            float avg_spacing = 0;
            for (int i = 1; i < h_count; i++)
            {
                avg_spacing += (h_lines[i] - h_lines[i - 1]);
            }
            avg_spacing /= (h_count - 1);

            float variance = 0;
            for (int i = 1; i < h_count; i++)
            {
                float diff = (h_lines[i] - h_lines[i - 1]) - avg_spacing;
                variance += diff * diff;
            }
            variance /= (h_count - 1);

            // Lower variance means more regular spacing
            features.regularity_score = 1.0f / (1.0f + variance / (avg_spacing * avg_spacing));
        }

        // Alignment score (simplified)
        features.alignment_score = 0.8f; // Assume good alignment for now
    }

    // 2. Content analysis
    if (textpage && block_count > 0)
    {
        // Calculate bounding box of all blocks
        fz_rect bbox = blocks[0];
        for (int i = 1; i < block_count; i++)
        {
            bbox = fz_rect_union_custom(bbox, blocks[i]);
        }
        features.content_score = analyze_table_content(textpage, bbox);
    }

    return features;
}

// Enhanced table detection
static float is_likely_table(fz_rect* blocks, int block_count, fz_context* ctx,
                             fz_stext_page* textpage)
{
    if (block_count < 4) return 0.0f;

    table_features_t features = analyze_table_structure(blocks, block_count, textpage);

    // Combined scoring approach
    float score = 0.0f;
    score += features.regularity_score * 0.4f; // Grid regularity is most important
    score += features.alignment_score * 0.3f;  // Alignment is also important
    score += features.content_score * 0.3f;    // Content helps distinguish tables

    // Boost score for reasonable table dimensions
    if (((features.row_count >= 2 && features.col_count >= 1) ||
         (features.row_count >= 1 && features.col_count >= 2)) &&
        features.row_count <= 50 && features.col_count <= 20)
    {
        score *= 1.2f;
    }

    // Cap score at 1.0
    return score > 1.0f ? 1.0f : score;
}

// Find table candidates using spatial clustering
static int find_table_candidates(fz_rect* blocks, int block_count, table_candidate_t* candidates,
                                 int max_candidates, fz_context* ctx, fz_stext_page* textpage)
{
    int candidate_count = 0;

    if (block_count < 4) return 0;

    // Create spatial grid for efficient neighbor finding
    fz_rect page_bounds = blocks[0];
    for (int i = 1; i < block_count; i++)
    {
        page_bounds = fz_rect_union_custom(page_bounds, blocks[i]);
    }

    spatial_grid_t* grid = create_spatial_grid(blocks, block_count, page_bounds);
    if (!grid) return 0;

    // Process blocks to find dense regions that might be tables
    int processed[MAX_BLOCKS] = {0};

    for (int i = 0; i < block_count && candidate_count < max_candidates; i++)
    {
        if (processed[i]) continue;

        // Start a new candidate region
        fz_rect region = blocks[i];
        int region_blocks[100];
        int region_count = 1;
        region_blocks[0] = i;
        processed[i] = 1;

        // Find connected blocks (within reasonable distance)
        int nearby[100];
        int nearby_count = find_nearby_blocks(grid, region, nearby, 100);

        for (int j = 0; j < nearby_count && region_count < 100; j++)
        {
            int block_idx = nearby[j];
            if (processed[block_idx] || block_idx == i) continue;

            // Check if this block should be part of the same table
            fz_rect expanded = fz_rect_union_custom(region, blocks[block_idx]);

            // Simple heuristic: if expanding doesn't increase area too much,
            // and the aspect ratio is reasonable, include it
            float original_area = (region.x1 - region.x0) * (region.y1 - region.y0);
            float expanded_area = (expanded.x1 - expanded.x0) * (expanded.y1 - expanded.y0);

            if (expanded_area < original_area * 4.0f)
            { // Not too much expansion
                float aspect_ratio = (expanded.x1 - expanded.x0) / (expanded.y1 - expanded.y0);
                if (aspect_ratio > 0.2f && aspect_ratio < 10.0f)
                { // Reasonable aspect
                    region = expanded;
                    region_blocks[region_count++] = block_idx;
                    processed[block_idx] = 1;
                }
            }
        }

        // Analyze if this region looks like a table
        if (region_count >= 4)
        {
            float score = is_likely_table(&blocks[region_blocks[0]], region_count, ctx, textpage);
            if (score > 0.4f)
            { // Threshold for table classification
                candidates[candidate_count].bbox = region;
                candidates[candidate_count].block_start = region_blocks[0];
                candidates[candidate_count].block_count = region_count;
                candidates[candidate_count].score = score;
                candidate_count++;
            }
        }
    }

    destroy_spatial_grid(grid);
    return candidate_count;
}

// =============================================================================
// Public API Functions
// =============================================================================

/**
 * @brief Checks if a page is likely to contain a table using the enhanced algorithm.
 *
 * This function analyzes the target page and its immediate neighbors (previous and next)
 * to make a more context-aware decision. If a high-confidence table is found on an
 * adjacent page, it increases the likelihood that the current page also contains a table.
 *
 * @param pdf_path The file path to the PDF document.
 * @param page_number The 0-based index of the page to check.
 * @return 1 if a table is likely present, 0 if not. Returns -1 on error (though current
 *         implementation returns 0 on error).
 */
extern int page_has_table(const char* pdf_path, int page_number)
{
    fz_context* ctx = NULL;
    fz_document* volatile doc = NULL;
    float volatile best_score = 0.0f;
    int surrounding_table_found = 0;

    ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
    if (!ctx) return 0; // Return 0 on context creation failure.

    fz_try(ctx)
    {
        fz_register_document_handlers(ctx);
        doc = fz_open_document(ctx, pdf_path);
        if (!doc) fz_throw(ctx, FZ_ERROR_GENERIC, "Cannot open document");

        int page_count = fz_count_pages(ctx, doc);
        // Define the range of pages to check (target page and its neighbors).
        int start = page_number > 0 ? page_number - 1 : 0;
        int end = page_number < page_count - 1 ? page_number + 1 : page_count - 1;

        fz_rect* blocks = malloc(sizeof(fz_rect) * MAX_BLOCKS);
        if (!blocks) fz_throw(ctx, FZ_ERROR_GENERIC, "Cannot allocate memory for blocks");

        // --- Step 1: Check surrounding pages for high-confidence tables.
        for (int p = start; p <= end; p++)
        {
            if (p == page_number) continue; // Skip the target page for now.

            fz_page* page = fz_load_page(ctx, doc, p);
            if (!page) continue;

            fz_stext_options opts = {0};
            fz_stext_page* textpage = fz_new_stext_page_from_page(ctx, page, &opts);
            if (!textpage) { fz_drop_page(ctx, page); continue; }

            // Extract text blocks from the surrounding page.
            int surrounding_block_count = 0;
            for (fz_stext_block* block = textpage->first_block; block && surrounding_block_count < MAX_BLOCKS; block = block->next)
            {
                if (block->type == FZ_STEXT_BLOCK_TEXT) {
                    blocks[surrounding_block_count++] = block->bbox;
                }
            }

            if (surrounding_block_count >= 4)
            {
                qsort(blocks, surrounding_block_count, sizeof(fz_rect), compare_rects_by_top_left);
                table_candidate_t candidates[MAX_CANDIDATES];
                int candidate_count = find_table_candidates(blocks, surrounding_block_count, candidates, MAX_CANDIDATES, ctx, textpage);

                // If a high-confidence table is found, set the flag and break early.
                for (int i = 0; i < candidate_count; i++)
                {
                    if (candidates[i].score > 0.8f) {
                        surrounding_table_found = 1;
                        break;
                    }
                }
            }

            fz_drop_stext_page(ctx, textpage);
            fz_drop_page(ctx, page);
            if (surrounding_table_found) break;
        }

        // --- Step 2: If a surrounding table was found, we can consider the job done.
        if (surrounding_table_found) {
            free(blocks);
            fz_drop_document(ctx, doc);
            doc = NULL; // Prevent double free in fz_always
            fz_drop_context(ctx);
            return 1;
        }

        // --- Step 3: Analyze the target page.
        fz_page* target_page = fz_load_page(ctx, doc, page_number);
        if (target_page)
        {
            fz_stext_options opts = {0};
            fz_stext_page* target_textpage = fz_new_stext_page_from_page(ctx, target_page, &opts);
            if (target_textpage)
            {
                int block_count = 0;
                for (fz_stext_block* block = target_textpage->first_block; block && block_count < MAX_BLOCKS; block = block->next)
                {
                    if (block->type == FZ_STEXT_BLOCK_TEXT) {
                        blocks[block_count++] = block->bbox;
                    }
                }

                if (block_count >= 4)
                {
                    qsort(blocks, block_count, sizeof(fz_rect), compare_rects_by_top_left);
                    table_candidate_t candidates[MAX_CANDIDATES];
                    int candidate_count = find_table_candidates(blocks, block_count, candidates, MAX_CANDIDATES, ctx, target_textpage);

                    // Find the highest score among all candidates on the target page.
                    for (int i = 0; i < candidate_count; i++)
                    {
                        if (candidates[i].score > best_score)
                        {
                            best_score = candidates[i].score;
                        }
                    }
                }
                fz_drop_stext_page(ctx, target_textpage);
            }
            fz_drop_page(ctx, target_page);
        }

        free(blocks);
    }
    fz_always(ctx)
    {
        if (doc) fz_drop_document(ctx, doc);
    }
    fz_catch(ctx)
    {
        // On error, we assume no table is present.
        best_score = 0.0f;
    }

    fz_drop_context(ctx);

    // Final decision: if a surrounding page had a table, return true.
    // Otherwise, base the decision on the best score found on the target page.
    return best_score > 0.35f;
}


/**
 * @brief (Deprecated) Checks if a page likely contains a table using an older, simpler algorithm.
 *
 * This function uses a basic clustering algorithm to find 2x2 groups of text blocks,
 * which is a simple indicator of a table. It is less accurate than the improved `page_has_table`.
 *
 * @param pdf_path The file path to the PDF document.
 * @param page_number The 0-based index of the page to check.
 * @return 1 if a table is likely present, 0 otherwise.
 */
extern int original_page_has_table(const char* pdf_path, int page_number)
{
    fz_context* ctx = NULL;
    fz_document* volatile doc = NULL;
    int volatile has_table = 0;

    ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
    if (!ctx) return 0;

    fz_try(ctx)
    {
        fz_register_document_handlers(ctx);
        doc = fz_open_document(ctx, pdf_path);
        if (!doc) fz_throw(ctx, FZ_ERROR_GENERIC, "Cannot open document");

        int page_count = fz_count_pages(ctx, doc);
        int start = page_number > 0 ? page_number - 1 : 0;
        int end = page_number < page_count - 1 ? page_number + 1 : page_count - 1;

        fz_rect* blocks = malloc(sizeof(fz_rect) * MAX_BLOCKS);
        int block_count = 0;

        // Collect blocks from target and adjacent pages.
        for (int p = start; p <= end; p++)
        {
            fz_page* page = fz_load_page(ctx, doc, p);
            fz_stext_options opts = {0};
            fz_stext_page* textpage = fz_new_stext_page_from_page(ctx, page, &opts);

            // Apply a simple y-offset to simulate a single continuous page.
            float y_offset = (float)(p - page_number) * (fz_bound_page(ctx, page).y1 - fz_bound_page(ctx, page).y0);

            for (fz_stext_block* block = textpage->first_block; block && block_count < MAX_BLOCKS; block = block->next)
            {
                if (block->type != FZ_STEXT_BLOCK_TEXT) continue;
                fz_rect r = block->bbox;
                r.y0 += y_offset;
                r.y1 += y_offset;
                blocks[block_count++] = r;
            }

            fz_drop_stext_page(ctx, textpage);
            fz_drop_page(ctx, page);
        }

        // Simple 2x2 cluster check: if any block has at least one neighbor to its
        // right (same y) and one neighbor below it (same x), it might be a table.
        for (int i = 0; i < block_count && !has_table; i++)
        {
            int row_count = 0, col_count = 0;
            for (int j = 0; j < block_count; j++)
            {
                if (i == j) continue;
                if (fabs(blocks[i].y0 - blocks[j].y0) < EPSILON) row_count++;
                if (fabs(blocks[i].x0 - blocks[j].x0) < EPSILON) col_count++;
            }
            if (row_count >= 1 && col_count >= 1)
            {
                has_table = 1;
                break;
            }
        }

        free(blocks);
    }
    fz_always(ctx)
    {
        if (doc) fz_drop_document(ctx, doc);
    }
    fz_catch(ctx)
    {
        has_table = 0;
    }

    fz_drop_context(ctx);
    return has_table;
}
