#include "table.h"
#include "platform_compat.h"
#include "font_metrics.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>

// ============================================================================
// Text-based 2-column table synthesizer (for tables without line grids)
// ============================================================================

// Helper: compare floats for qsort
static int compare_float_asc(const void* a, const void* b)
{
    float fa = *(const float*)a;
    float fb = *(const float*)b;
    if (fa < fb)
        return -1;
    if (fa > fb)
        return 1;
    return 0;
}

// Helper: compute median in-place (modifies array order)
static float median_inplace(float* values, int count)
{
    if (!values || count <= 0)
        return -1.0f; // Sentinel
    qsort(values, (size_t)count, sizeof(float), compare_float_asc);
    if (count & 1)
        return values[count / 2];
    return 0.5f * (values[count / 2 - 1] + values[count / 2]);
}

// Geometry for a text run within a line
typedef struct
{
    fz_rect bbox;
    int has_visible; // Has printable ASCII chars (33-126)
    int bold_chars;
    int total_chars;
} LineRunGeom;

// Geometry for a single stext line
typedef struct
{
    fz_rect bbox;
    float y0, y1;
    int run_count; // 0, 1, or 2 (>2 means too complex)
    LineRunGeom runs[2];
    float split_x; // Midpoint between runs if run_count==2
} LineGeom;

// Row candidate for synthesized table
typedef struct
{
    fz_rect row_bbox;
    fz_rect left_bbox;
    fz_rect right_bbox;
} SynthRow;

// Compare LineGeom by y0 for sorting
static int compare_linegeom_y0(const void* a, const void* b)
{
    const LineGeom* la = (const LineGeom*)a;
    const LineGeom* lb = (const LineGeom*)b;
    float dy = la->y0 - lb->y0;
    if (fabsf(dy) > 1e-3f)
        return (dy < 0.0f) ? -1 : 1;
    float dx = la->bbox.x0 - lb->bbox.x0;
    if (fabsf(dx) > 1e-3f)
        return (dx < 0.0f) ? -1 : 1;
    return 0;
}

// Build line geometry from stext_line - detect runs separated by large gaps
static int build_line_geom(fz_context* ctx, fz_stext_line* line, LineGeom* out)
{
    if (!line || !out)
        return 0;
    memset(out, 0, sizeof(*out));
    out->bbox = fz_empty_rect;
    out->runs[0].bbox = fz_empty_rect;
    out->runs[1].bbox = fz_empty_rect;
    out->y0 = line->bbox.y0;
    out->y1 = line->bbox.y1;
    out->split_x = -1.0f; // Sentinel for "not set"

    int run_idx = -1;
    float prev_x1 = -1.0f; // Use sentinel instead of NAN

    for (fz_stext_char* ch = line->first_char; ch; ch = ch->next)
    {
        if (ch->c == 0)
            continue;

        // Skip whitespace for run detection
        bool is_whitespace = (ch->c == ' ' || ch->c == '\t' || ch->c == '\r' || ch->c == '\n' || ch->c == 160);
        if (is_whitespace)
            continue;

        fz_rect char_box = fz_rect_from_quad(ch->quad);
        float x0 = char_box.x0;
        float x1 = char_box.x1;

        // Large gap threshold for column split
        float split_gap_threshold = fmaxf(15.0f, ch->size * 2.0f);
        bool start_new_run = false;

        if (prev_x1 < 0.0f)
        {
            start_new_run = true;
        }
        else
        {
            float gap = x0 - prev_x1;
            if (gap > split_gap_threshold)
                start_new_run = true;
        }
        prev_x1 = x1;

        if (start_new_run)
        {
            if (run_idx < 0)
                run_idx = 0;
            else if (run_idx == 0)
                run_idx = 1;
            else
            {
                out->run_count = 3; // Too many runs
                return 0;
            }
        }

        if (run_idx < 0 || run_idx > 1)
        {
            out->run_count = (run_idx < 0) ? 0 : 3;
            return 0;
        }

        // Update bounding boxes
        if (fz_is_empty_rect(out->bbox))
            out->bbox = char_box;
        else
            out->bbox = fz_union_rect(out->bbox, char_box);

        if (fz_is_empty_rect(out->runs[run_idx].bbox))
            out->runs[run_idx].bbox = char_box;
        else
            out->runs[run_idx].bbox = fz_union_rect(out->runs[run_idx].bbox, char_box);

        // Track visible chars and boldness
        out->runs[run_idx].total_chars++;
        if (ch->c >= 33 && ch->c <= 126)
            out->runs[run_idx].has_visible = 1;
        if (ch->font && fz_font_is_bold(ctx, ch->font))
            out->runs[run_idx].bold_chars++;
    }

    if (run_idx < 0)
    {
        out->run_count = 0;
        return 1;
    }

    out->run_count = run_idx + 1;
    if (out->run_count == 2)
    {
        out->split_x = (out->runs[0].bbox.x1 + out->runs[1].bbox.x0) * 0.5f;
    }
    return 1;
}

// Finalize a group of synthesized rows - keep the best contiguous region
static void finalize_synth_group(SynthRow** best_rows, int* best_row_count, SynthRow** current_rows,
                                 int* current_row_count, int* current_row_cap)
{
    if (!best_rows || !best_row_count || !current_rows || !current_row_count || !current_row_cap)
        return;

    if (*current_rows && *current_row_count >= 3)
    {
        // Require at least 2 rows with both columns present
        int both_cols = 0;
        for (int r = 0; r < *current_row_count; r++)
        {
            if (!fz_is_empty_rect((*current_rows)[r].left_bbox) && !fz_is_empty_rect((*current_rows)[r].right_bbox))
                both_cols++;
        }

        if (both_cols >= 2 && *current_row_count > *best_row_count)
        {
            free(*best_rows);
            *best_rows = *current_rows;
            *best_row_count = *current_row_count;
            *current_rows = NULL;
        }
    }

    free(*current_rows);
    *current_rows = NULL;
    *current_row_count = 0;
    *current_row_cap = 0;
}

// Main synthesizer: detect 2-column tables from text layout
TableArray* synthesize_text_table_two_col(fz_context* ctx, fz_stext_page* textpage, const PageMetrics* metrics)
{
    if (!ctx || !textpage || !metrics)
        return NULL;

    LineGeom* lines = NULL;
    int line_count = 0;
    int line_cap = 0;

    float min_x = -1.0f, max_x = -1.0f; // Use sentinel instead of NAN

    // First pass: collect line geometry
    for (fz_stext_block* block = textpage->first_block; block; block = block->next)
    {
        if (block->type != FZ_STEXT_BLOCK_TEXT)
            continue;
        for (fz_stext_line* line = block->u.t.first_line; line; line = line->next)
        {
            LineGeom geom;
            if (!build_line_geom(ctx, line, &geom))
                continue;
            if (geom.run_count <= 0 || geom.run_count > 2)
                continue;
            if (fz_is_empty_rect(geom.bbox))
                continue;

            if (line_count == line_cap)
            {
                int new_cap = (line_cap == 0) ? 64 : (line_cap * 2);
                LineGeom* tmp = (LineGeom*)realloc(lines, (size_t)new_cap * sizeof(LineGeom));
                if (!tmp)
                {
                    free(lines);
                    return NULL;
                }
                lines = tmp;
                line_cap = new_cap;
            }
            lines[line_count++] = geom;

            if (min_x < 0 || geom.bbox.x0 < min_x)
                min_x = geom.bbox.x0;
            if (max_x < 0 || geom.bbox.x1 > max_x)
                max_x = geom.bbox.x1;
        }
    }

    if (line_count < 3 || min_x < 0 || max_x < 0 || max_x <= min_x)
    {
        free(lines);
        return NULL;
    }

    float content_width = max_x - min_x;
    float split_tol = fmaxf(12.0f, content_width * 0.03f);

    // Sort by vertical position
    qsort(lines, (size_t)line_count, sizeof(LineGeom), compare_linegeom_y0);

    float band_y_tol = fmaxf(4.0f, metrics->body_font_size * 0.8f);
    float min_sep = fmaxf(50.0f, content_width * 0.15f); // Minimum column separation

    // Collect split candidates
    size_t split_cap = (size_t)line_count * 2;
    float* split_values = (float*)malloc(split_cap * sizeof(float));
    float* right_x0_values = (float*)malloc(split_cap * sizeof(float));
    if (!split_values || !right_x0_values)
    {
        free(right_x0_values);
        free(split_values);
        free(lines);
        return NULL;
    }
    int split_count = 0;

    // (1) Within-line split candidates
    for (int i = 0; i < line_count; i++)
    {
        LineGeom* lg = &lines[i];
        if (lg->run_count != 2)
            continue;
        if (!lg->runs[0].has_visible || !lg->runs[1].has_visible)
            continue;
        if (lg->split_x < 0)
            continue;
        if ((size_t)split_count >= split_cap)
            break;
        split_values[split_count] = lg->split_x;
        right_x0_values[split_count] = lg->runs[1].bbox.x0;
        split_count++;
    }

    // (2) Row-band split candidates (left/right as separate stext lines at same y)
    for (int i = 0; i < line_count;)
    {
        float y_ref = lines[i].y0;
        int band_start = i;
        int band_end = i + 1;
        while (band_end < line_count && fabsf(lines[band_end].y0 - y_ref) < band_y_tol)
            band_end++;

        int left_idx = -1, right_idx = -1;
        for (int j = band_start; j < band_end; j++)
        {
            LineGeom* lg = &lines[j];
            if (lg->run_count != 1 || !lg->runs[0].has_visible)
                continue;
            if (left_idx < 0 || lg->bbox.x0 < lines[left_idx].bbox.x0)
                left_idx = j;
            if (right_idx < 0 || lg->bbox.x0 > lines[right_idx].bbox.x0)
                right_idx = j;
        }

        if (left_idx >= 0 && right_idx >= 0 && left_idx != right_idx)
        {
            float sep = lines[right_idx].bbox.x0 - lines[left_idx].bbox.x0;
            if (sep >= min_sep)
            {
                float mid = (lines[left_idx].bbox.x1 + lines[right_idx].bbox.x0) * 0.5f;
                if ((size_t)split_count < split_cap)
                {
                    split_values[split_count] = mid;
                    right_x0_values[split_count] = lines[right_idx].bbox.x0;
                    split_count++;
                }
            }
        }
        i = band_end;
    }

    if (split_count < 3)
    {
        free(right_x0_values);
        free(split_values);
        free(lines);
        return NULL;
    }

    float median_split = median_inplace(split_values, split_count);
    float right_col_x0 = median_inplace(right_x0_values, split_count);
    free(right_x0_values);
    free(split_values);

    if (median_split < 0 || right_col_x0 < 0)
    {
        free(lines);
        return NULL;
    }

    // Group candidate runs into contiguous 2-column regions
    SynthRow* best_rows = NULL;
    int best_row_count = 0;
    SynthRow* current_rows = NULL;
    int current_row_count = 0;
    int current_row_cap = 0;

    bool in_group = false;
    float prev_y1 = -1.0f; // Sentinel
    float max_gap = fmaxf(25.0f, metrics->body_font_size * 3.0f);

    for (int i = 0; i < line_count;)
    {
        float y_ref = lines[i].y0;
        int band_start = i;
        int band_end = i + 1;
        while (band_end < line_count && fabsf(lines[band_end].y0 - y_ref) < band_y_tol)
            band_end++;

        float band_y1 = lines[band_start].y1;
        for (int j = band_start + 1; j < band_end; j++)
        {
            if (lines[j].y1 > band_y1)
                band_y1 = lines[j].y1;
        }

        // Identify band-level left/right candidates
        int left_idx = -1, right_idx = -1, aligned_line_idx = -1;
        for (int j = band_start; j < band_end; j++)
        {
            LineGeom* lg = &lines[j];
            // Check for within-line 2-column alignment
            if (lg->run_count == 2 && lg->runs[0].has_visible && lg->runs[1].has_visible && lg->split_x >= 0 &&
                fabsf(lg->split_x - median_split) <= split_tol)
            {
                aligned_line_idx = j;
                break;
            }

            int visible = 0;
            if (lg->run_count == 2)
                visible = lg->runs[0].has_visible || lg->runs[1].has_visible;
            else
                visible = lg->runs[0].has_visible;
            if (!visible)
                continue;

            if (left_idx < 0 || lg->bbox.x0 < lines[left_idx].bbox.x0)
                left_idx = j;
            if (right_idx < 0 || lg->bbox.x0 > lines[right_idx].bbox.x0)
                right_idx = j;
        }

        bool row_added = false;
        bool left_continuation = false;
        bool right_continuation = false;
        fz_rect left_bbox = fz_empty_rect, right_bbox = fz_empty_rect;

        // Try to create a new row first
        if (aligned_line_idx >= 0)
        {
            left_bbox = lines[aligned_line_idx].runs[0].bbox;
            right_bbox = lines[aligned_line_idx].runs[1].bbox;
            row_added = true;
        }
        else if (left_idx >= 0 && right_idx >= 0 && right_idx != left_idx)
        {
            float sep = lines[right_idx].bbox.x0 - lines[left_idx].bbox.x0;
            if (sep >= min_sep)
            {
                // Ensure candidates straddle the inferred split
                if (lines[left_idx].bbox.x1 < median_split - split_tol * 0.10f &&
                    lines[right_idx].bbox.x0 > median_split + split_tol * 0.10f)
                {
                    left_bbox = lines[left_idx].bbox;
                    right_bbox = lines[right_idx].bbox;
                    row_added = true;
                }
            }
        }

        // If no row added and we have existing rows, check for continuations
        if (!row_added && current_row_count > 0)
        {
            SynthRow* prev_row = &current_rows[current_row_count - 1];
            float y_cont_tol = metrics->body_font_size * 1.5f; // Allow some vertical distance for continuations

            // Check for left-column continuation (multi-line left cell)
            if (left_idx >= 0)
            {
                float x0 = lines[left_idx].bbox.x0;
                float y0 = lines[left_idx].bbox.y0;
                // It's a left continuation if: in left column region AND close to previous row vertically
                if (x0 < median_split - split_tol * 0.10f && fabsf(y0 - prev_row->row_bbox.y1) < y_cont_tol)
                {
                    left_continuation = true;
                }
            }

            // Check for right-column continuation (multi-line right cell)
            if (right_idx >= 0)
            {
                float x0 = lines[right_idx].bbox.x0;
                float y0 = lines[right_idx].bbox.y0;
                // It's a right continuation if: aligns with right column AND close vertically
                if (x0 >= median_split + split_tol * 0.25f && fabsf(x0 - right_col_x0) <= split_tol &&
                    fabsf(y0 - prev_row->row_bbox.y1) < y_cont_tol)
                {
                    right_continuation = true;
                }
            }
        }

        if (!row_added && !left_continuation && !right_continuation)
        {
            i = band_end;
            continue;
        }

        // Group management
        if (!in_group)
        {
            in_group = true;
            prev_y1 = band_y1;
        }
        else if (prev_y1 >= 0)
        {
            float gap = y_ref - prev_y1;
            if (gap > max_gap)
            {
                finalize_synth_group(&best_rows, &best_row_count, &current_rows, &current_row_count, &current_row_cap);
                prev_y1 = band_y1;
            }
            else
            {
                prev_y1 = fmaxf(prev_y1, band_y1);
            }
        }

        if (row_added)
        {
            if (current_row_count == current_row_cap)
            {
                int new_cap = (current_row_cap == 0) ? 16 : (current_row_cap * 2);
                SynthRow* tmp = (SynthRow*)realloc(current_rows, (size_t)new_cap * sizeof(SynthRow));
                if (!tmp)
                {
                    free(current_rows);
                    free(best_rows);
                    free(lines);
                    return NULL;
                }
                current_rows = tmp;
                current_row_cap = new_cap;
            }
            SynthRow row;
            row.left_bbox = left_bbox;
            row.right_bbox = right_bbox;
            row.row_bbox = fz_union_rect(left_bbox, right_bbox);
            current_rows[current_row_count++] = row;
        }
        else if (left_continuation && current_row_count > 0)
        {
            // Merge into previous row's left cell
            SynthRow* row = &current_rows[current_row_count - 1];
            if (fz_is_empty_rect(row->left_bbox))
                row->left_bbox = lines[left_idx].bbox;
            else
                row->left_bbox = fz_union_rect(row->left_bbox, lines[left_idx].bbox);
            row->row_bbox = fz_union_rect(row->row_bbox, lines[left_idx].bbox);
        }
        else if (right_continuation && current_row_count > 0)
        {
            // Merge into previous row's right cell
            SynthRow* row = &current_rows[current_row_count - 1];
            if (fz_is_empty_rect(row->right_bbox))
                row->right_bbox = lines[right_idx].bbox;
            else
                row->right_bbox = fz_union_rect(row->right_bbox, lines[right_idx].bbox);
            row->row_bbox = fz_union_rect(row->row_bbox, lines[right_idx].bbox);
        }

        i = band_end;
    }

    if (in_group)
    {
        finalize_synth_group(&best_rows, &best_row_count, &current_rows, &current_row_count, &current_row_cap);
    }

    free(lines);

    if (!best_rows || best_row_count < 3)
    {
        free(best_rows);
        return NULL;
    }

    // Sanity check: verify content density (right column should be denser)
    float left_char_total = 0, right_char_total = 0;
    for (int r = 0; r < best_row_count; r++)
    {
        float lw = best_rows[r].left_bbox.x1 - best_rows[r].left_bbox.x0;
        float rw = best_rows[r].right_bbox.x1 - best_rows[r].right_bbox.x0;
        if (!fz_is_empty_rect(best_rows[r].left_bbox))
            left_char_total += lw;
        if (!fz_is_empty_rect(best_rows[r].right_bbox))
            right_char_total += rw;
    }
    // Right column should be notably wider overall (typical for key-value tables)
    if (right_char_total < left_char_total * 1.2f)
    {
        free(best_rows);
        return NULL;
    }

    // Build TableArray with synthesized 2-column table
    TableArray* tables = (TableArray*)calloc(1, sizeof(TableArray));
    if (!tables)
    {
        free(best_rows);
        return NULL;
    }
    tables->tables = (Table*)calloc(1, sizeof(Table));
    if (!tables->tables)
    {
        free(tables);
        free(best_rows);
        return NULL;
    }
    tables->count = 1;

    Table* table = &tables->tables[0];
    table->rows = (TableRow*)calloc((size_t)best_row_count, sizeof(TableRow));
    if (!table->rows)
    {
        free(tables->tables);
        free(tables);
        free(best_rows);
        return NULL;
    }
    table->count = best_row_count;
    table->bbox = fz_empty_rect;

    for (int r = 0; r < best_row_count; r++)
    {
        TableRow* row = &table->rows[r];
        row->count = 2;
        row->cells = (TableCell*)calloc(2, sizeof(TableCell));
        if (!row->cells)
        {
            for (int k = 0; k < r; k++)
                free(table->rows[k].cells);
            free(table->rows);
            free(tables->tables);
            free(tables);
            free(best_rows);
            return NULL;
        }

        row->cells[0].bbox = best_rows[r].left_bbox;
        row->cells[0].text = NULL;
        row->cells[1].bbox = best_rows[r].right_bbox;
        row->cells[1].text = NULL;
        row->bbox = best_rows[r].row_bbox;

        if (fz_is_empty_rect(table->bbox))
            table->bbox = row->bbox;
        else
            table->bbox = fz_union_rect(table->bbox, row->bbox);
    }

    free(best_rows);
    return tables;
}

// ============================================================================
// End of text-based table synthesizer
// ============================================================================

#define EDGE_MIN_LENGTH 3.0
#define SNAP_TOLERANCE 3.0
#define JOIN_TOLERANCE 3.0
#define INTERSECTION_TOLERANCE 1.0
#define ROW_Y_TOLERANCE 2.0
#define COL_X_TOLERANCE 2.0

// Optimization: scale factor for integer coordinates (1000x precision)
#define COORD_SCALE 1000
#define COORD_TO_INT(x) ((int)((x) * COORD_SCALE + 0.5))
#define INT_TO_COORD(i) ((double)(i) / COORD_SCALE)

// Spatial hash grid for fast intersection lookup
#define GRID_CELL_SIZE 2.0
#define HASH_SIZE 4096

typedef struct PointNode
{
    Point point;
    struct PointNode* next;
} PointNode;

typedef struct
{
    PointNode* buckets[HASH_SIZE];
    PointNode* node_pool;
    int pool_size;
    int pool_capacity;
} SpatialHash;

// Memory pool for table structures
typedef struct
{
    TableCell* cell_buffer;
    int cell_capacity;
    int cell_used;
    TableRow* row_buffer;
    int row_capacity;
    int row_used;
} MemoryPool;

// --- Helper functions for dynamic arrays ---

static void init_edge_array(EdgeArray* arr)
{
    arr->items = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

static void add_to_edge_array(EdgeArray* arr, Edge item)
{
    if (arr->count >= arr->capacity)
    {
        arr->capacity = arr->capacity == 0 ? 16 : arr->capacity * 2;
        arr->items = realloc(arr->items, arr->capacity * sizeof(Edge));
    }
    arr->items[arr->count++] = item;
}

static void free_edge_array(EdgeArray* arr)
{
    free(arr->items);
}

static void init_point_array(PointArray* arr)
{
    arr->items = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

static void add_to_point_array(PointArray* arr, Point item)
{
    if (arr->count >= arr->capacity)
    {
        arr->capacity = arr->capacity == 0 ? 16 : arr->capacity * 2;
        arr->items = realloc(arr->items, arr->capacity * sizeof(Point));
    }
    arr->items[arr->count++] = item;
}

static void free_point_array(PointArray* arr)
{
    free(arr->items);
}

static void init_cell_array(CellArray* arr)
{
    arr->items = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

static void free_cell_array(CellArray* arr)
{
    free(arr->items);
}

// --- Spatial hash functions ---

static int hash_point(double x, double y)
{
    int ix = (int)(x / GRID_CELL_SIZE);
    int iy = (int)(y / GRID_CELL_SIZE);
    return ((ix * 73856093) ^ (iy * 19349663)) & (HASH_SIZE - 1);
}

static void init_spatial_hash(SpatialHash* hash)
{
    memset(hash->buckets, 0, sizeof(hash->buckets));
    hash->pool_capacity = 256;
    hash->node_pool = malloc(hash->pool_capacity * sizeof(PointNode));
    hash->pool_size = 0;
}

static void add_to_spatial_hash(SpatialHash* hash, Point p)
{
    int h = hash_point(p.x, p.y);

    // Check if point already exists (deduplication)
    for (PointNode* node = hash->buckets[h]; node; node = node->next)
    {
        if (fabs(node->point.x - p.x) < 0.1 && fabs(node->point.y - p.y) < 0.1)
        {
            return; // Already exists
        }
    }

    // Expand pool if needed
    if (hash->pool_size >= hash->pool_capacity)
    {
        PointNode* old_pool = hash->node_pool;
        hash->pool_capacity *= 2;
        hash->node_pool = realloc(hash->node_pool, hash->pool_capacity * sizeof(PointNode));

        // If realloc moved the memory, update all bucket pointers
        if (hash->node_pool != old_pool)
        {
            // Update all bucket head pointers
            for (int i = 0; i < HASH_SIZE; i++)
            {
                if (hash->buckets[i])
                {
                    // Calculate offset from old base and apply to new base
                    ptrdiff_t offset = hash->buckets[i] - old_pool;
                    hash->buckets[i] = hash->node_pool + offset;
                }
            }
            // Update all next pointers within nodes
            for (int i = 0; i < hash->pool_size; i++)
            {
                if (hash->node_pool[i].next)
                {
                    ptrdiff_t offset = hash->node_pool[i].next - old_pool;
                    hash->node_pool[i].next = hash->node_pool + offset;
                }
            }
        }
    }

    PointNode* node = &hash->node_pool[hash->pool_size++];
    node->point = p;
    node->next = hash->buckets[h];
    hash->buckets[h] = node;
}

static int find_in_spatial_hash(SpatialHash* hash, double x, double y)
{
    int h = hash_point(x, y);
    for (PointNode* node = hash->buckets[h]; node; node = node->next)
    {
        if (fabs(node->point.x - x) < 0.1 && fabs(node->point.y - y) < 0.1)
        {
            return 1;
        }
    }
    return 0;
}

static void collect_points_from_hash(SpatialHash* hash, PointArray* arr)
{
    for (int i = 0; i < hash->pool_size; i++)
    {
        add_to_point_array(arr, hash->node_pool[i].point);
    }
}

static void free_spatial_hash(SpatialHash* hash)
{
    free(hash->node_pool);
}

// --- Memory pool functions ---

static void init_memory_pool(MemoryPool* pool)
{
    pool->cell_capacity = 1024;
    pool->cell_buffer = malloc(pool->cell_capacity * sizeof(TableCell));
    pool->cell_used = 0;
    pool->row_capacity = 128;
    pool->row_buffer = malloc(pool->row_capacity * sizeof(TableRow));
    pool->row_used = 0;
}

static void free_memory_pool(MemoryPool* pool)
{
    // Note: individual cells/rows point into these buffers, so don't free them individually
    free(pool->cell_buffer);
    free(pool->row_buffer);
}

// --- MuPDF device to capture lines ---

typedef struct
{
    fz_device super;
    EdgeArray edges;
} CaptureDevice;

static void capture_stroke_path(fz_context* ctx, fz_device* dev_, const fz_path* path, const fz_stroke_state* stroke,
                                fz_matrix ctm, fz_colorspace* colorspace, const float* color, float alpha,
                                fz_color_params color_params)
{
    (void)colorspace;
    (void)color;
    (void)alpha;
    (void)color_params;

    CaptureDevice* dev = (CaptureDevice*)dev_;
    fz_rect bbox = fz_bound_path(ctx, path, stroke, ctm);

    double width = bbox.x1 - bbox.x0;
    double height = bbox.y1 - bbox.y0;

    if (height <= 1.0 && width >= EDGE_MIN_LENGTH)
    { // Horizontal line
        add_to_edge_array(&dev->edges, (Edge){bbox.x0, bbox.y0, bbox.x1, bbox.y0, 'h'});
    }
    else if (width <= 1.0 && height >= EDGE_MIN_LENGTH)
    { // Vertical line
        add_to_edge_array(&dev->edges, (Edge){bbox.x0, bbox.y0, bbox.x0, bbox.y1, 'v'});
    }
}

static void capture_fill_path(fz_context* ctx, fz_device* dev_, const fz_path* path, int even_odd, fz_matrix ctm,
                              fz_colorspace* colorspace, const float* color, float alpha, fz_color_params color_params)
{
    (void)ctx;
    (void)path;
    (void)even_odd;
    (void)ctm;
    (void)colorspace;
    (void)color;
    (void)alpha;
    (void)color_params;

    CaptureDevice* dev = (CaptureDevice*)dev_;
    fz_rect bbox = fz_bound_path(ctx, path, NULL, ctm);

    double width = bbox.x1 - bbox.x0;
    double height = bbox.y1 - bbox.y0;

    // Capture filled rectangles as edges
    if (width > 0 && height > 0)
    {
        add_to_edge_array(&dev->edges, (Edge){bbox.x0, bbox.y0, bbox.x1, bbox.y0, 'h'}); // Top
        add_to_edge_array(&dev->edges, (Edge){bbox.x0, bbox.y1, bbox.x1, bbox.y1, 'h'}); // Bottom
        add_to_edge_array(&dev->edges, (Edge){bbox.x0, bbox.y0, bbox.x0, bbox.y1, 'v'}); // Left
        add_to_edge_array(&dev->edges, (Edge){bbox.x1, bbox.y0, bbox.x1, bbox.y1, 'v'}); // Right
    }
}

static void capture_close_device(fz_context* ctx, fz_device* dev_)
{
    (void)ctx;
    (void)dev_;
}

static void capture_drop_device(fz_context* ctx, fz_device* dev_)
{
    (void)ctx;
    CaptureDevice* dev = (CaptureDevice*)dev_;
    free_edge_array(&dev->edges);
}

static fz_device* new_capture_device(fz_context* ctx)
{
    CaptureDevice* dev = fz_new_derived_device(ctx, CaptureDevice);
    dev->super.close_device = capture_close_device;
    dev->super.drop_device = capture_drop_device;
    dev->super.stroke_path = capture_stroke_path;
    dev->super.fill_path = capture_fill_path;
    init_edge_array(&dev->edges);
    return (fz_device*)dev;
}

// --- Comparison functions for sorting ---

static int compare_edges_v(const void* a, const void* b)
{
    Edge* edge_a = (Edge*)a;
    Edge* edge_b = (Edge*)b;
    if (edge_a->x0 < edge_b->x0)
        return -1;
    if (edge_a->x0 > edge_b->x0)
        return 1;
    if (edge_a->y0 < edge_b->y0)
        return -1;
    if (edge_a->y0 > edge_b->y0)
        return 1;
    return 0;
}

static int compare_edges_h(const void* a, const void* b)
{
    Edge* edge_a = (Edge*)a;
    Edge* edge_b = (Edge*)b;
    if (edge_a->y0 < edge_b->y0)
        return -1;
    if (edge_a->y0 > edge_b->y0)
        return 1;
    if (edge_a->x0 < edge_b->x0)
        return -1;
    if (edge_a->x0 > edge_b->x0)
        return 1;
    return 0;
}

static int compare_points(const void* a, const void* b)
{
    Point* p_a = (Point*)a;
    Point* p_b = (Point*)b;
    if (p_a->y < p_b->y)
        return -1;
    if (p_a->y > p_b->y)
        return 1;
    if (p_a->x < p_b->x)
        return -1;
    if (p_a->x > p_b->x)
        return 1;
    return 0;
}

static int compare_rects_lexicographically(const void* a, const void* b)
{
    fz_rect* rect_a = (fz_rect*)a;
    fz_rect* rect_b = (fz_rect*)b;
    if (rect_a->y0 < rect_b->y0)
        return -1;
    if (rect_a->y0 > rect_b->y0)
        return 1;
    if (rect_a->x0 < rect_b->x0)
        return -1;
    if (rect_a->x0 > rect_b->x0)
        return 1;
    return 0;
}

// --- Core table finding logic (optimized) ---

// Optimized: Combined snap and merge in one pass using integer coordinates
static void merge_edges(EdgeArray* edges, double snap_tolerance, double join_tolerance)
{
    if (edges->count == 0)
        return;

    char orientation = edges->items[0].orientation;
    qsort(edges->items, edges->count, sizeof(Edge), orientation == 'h' ? compare_edges_h : compare_edges_v);

    int snap_tol_int = COORD_TO_INT(snap_tolerance);
    int join_tol_int = COORD_TO_INT(join_tolerance);

    EdgeArray result;
    init_edge_array(&result);

    // Preallocate to avoid reallocation in loop
    result.capacity = edges->count;
    result.items = malloc(result.capacity * sizeof(Edge));

    int i = 0;
    while (i < edges->count)
    {
        Edge current = edges->items[i];

        // Find cluster of edges to snap together
        int cluster_start = i;
        int pos_sum = orientation == 'h' ? COORD_TO_INT(current.y0) : COORD_TO_INT(current.x0);
        int cluster_count = 1;
        i++;

        while (i < edges->count)
        {
            Edge next = edges->items[i];
            int next_pos = orientation == 'h' ? COORD_TO_INT(next.y0) : COORD_TO_INT(next.x0);
            if (abs(next_pos - pos_sum / cluster_count) <= snap_tol_int)
            {
                pos_sum += next_pos;
                cluster_count++;
                i++;
            }
            else
            {
                break;
            }
        }

        // Apply snapped position to all edges in cluster
        double snapped_pos = INT_TO_COORD(pos_sum / cluster_count);

        // Join edges within this cluster
        Edge joined = edges->items[cluster_start];
        if (orientation == 'h')
        {
            joined.y0 = joined.y1 = snapped_pos;
        }
        else
        {
            joined.x0 = joined.x1 = snapped_pos;
        }

        for (int j = cluster_start + 1; j < cluster_start + cluster_count; j++)
        {
            Edge next = edges->items[j];
            if (orientation == 'h')
            {
                next.y0 = next.y1 = snapped_pos;
                int join_test = COORD_TO_INT(next.x0) - COORD_TO_INT(joined.x1);
                if (join_test <= join_tol_int)
                {
                    joined.x1 = fmax(joined.x1, next.x1);
                }
                else
                {
                    result.items[result.count++] = joined;
                    joined = next;
                }
            }
            else
            {
                next.x0 = next.x1 = snapped_pos;
                int join_test = COORD_TO_INT(next.y0) - COORD_TO_INT(joined.y1);
                if (join_test <= join_tol_int)
                {
                    joined.y1 = fmax(joined.y1, next.y1);
                }
                else
                {
                    result.items[result.count++] = joined;
                    joined = next;
                }
            }
        }
        result.items[result.count++] = joined;
    }

    free_edge_array(edges);
    *edges = result;
}

// Optimized: Use spatial hash to eliminate O(n*m) nested loop
static void find_intersections(EdgeArray* v_edges, EdgeArray* h_edges, SpatialHash* hash)
{
    int tol_int = COORD_TO_INT(INTERSECTION_TOLERANCE);

    // For each vertical edge, find horizontal edges it could intersect
    for (int i = 0; i < v_edges->count; ++i)
    {
        Edge* v = &v_edges->items[i];
        int v_x_int = COORD_TO_INT(v->x0);
        int v_y0_int = COORD_TO_INT(v->y0);
        int v_y1_int = COORD_TO_INT(v->y1);

        for (int j = 0; j < h_edges->count; ++j)
        {
            Edge* h = &h_edges->items[j];
            int h_y_int = COORD_TO_INT(h->y0);

            // Quick integer-based bounds check
            if (h_y_int < v_y0_int - tol_int || h_y_int > v_y1_int + tol_int)
            {
                continue;
            }

            int h_x0_int = COORD_TO_INT(h->x0);
            int h_x1_int = COORD_TO_INT(h->x1);

            if (h_x0_int - tol_int <= v_x_int && h_x1_int + tol_int >= v_x_int)
            {
                add_to_spatial_hash(hash, (Point){v->x0, h->y0});
            }
        }
    }
}

// Optimized: Use spatial hash for O(1) point lookup instead of O(n) scan
static void find_cells(PointArray* intersections, SpatialHash* hash, CellArray* cells)
{
    // Preallocate cells array
    cells->capacity = intersections->count;
    cells->items = malloc(cells->capacity * sizeof(fz_rect));
    cells->count = 0;

    for (int i = 0; i < intersections->count; ++i)
    {
        Point p1 = intersections->items[i];
        Point best_p_right = {-1, -1};
        Point best_p_down = {-1, -1};

        // Since points are sorted, we can optimize the search
        // Find next point to the right (same y, larger x)
        for (int j = i + 1; j < intersections->count && intersections->items[j].y == p1.y; ++j)
        {
            if (best_p_right.x == -1 || intersections->items[j].x < best_p_right.x)
            {
                best_p_right = intersections->items[j];
                break; // First one found is the closest
            }
        }

        // Find next point below (same x, larger y)
        for (int j = i + 1; j < intersections->count; ++j)
        {
            if (intersections->items[j].x == p1.x)
            {
                if (best_p_down.y == -1 || intersections->items[j].y < best_p_down.y)
                {
                    best_p_down = intersections->items[j];
                    break; // First one found is the closest
                }
            }
        }

        if (best_p_right.x != -1 && best_p_down.y != -1)
        {
            // Use spatial hash for O(1) lookup of bottom-right corner
            if (find_in_spatial_hash(hash, best_p_right.x, best_p_down.y))
            {
                fz_rect cell_bbox = {p1.x, p1.y, best_p_right.x, best_p_down.y};
                double width = cell_bbox.x1 - cell_bbox.x0;
                double height = cell_bbox.y1 - cell_bbox.y0;

                if (width > 5.0 && height > 0)
                {
                    cells->items[cells->count++] = cell_bbox;
                }
            }
        }
    }
}

// Threshold for splitting tables: large gaps indicate separate tables
#define TABLE_SPLIT_GAP_THRESHOLD 50.0

// Helper to initialize a new table in the array
static Table* add_new_table(TableArray* tables)
{
    int new_idx = tables->count;
    tables->count++;
    tables->tables = realloc(tables->tables, tables->count * sizeof(Table));
    if (!tables->tables)
        return NULL;
    Table* table = &tables->tables[new_idx];
    table->bbox = fz_empty_rect;
    table->count = 0;
    table->rows = malloc(16 * sizeof(TableRow));
    return table;
}

// Optimized: Use precomputed column boundaries and better allocation strategy
// Now also detects large gaps between rows to split into multiple tables
static TableArray* group_cells_into_tables(CellArray* cells)
{
    if (cells->count == 0)
        return NULL;

    // Use insertion sort for small arrays, qsort for large
    if (cells->count < 20)
    {
        for (int i = 1; i < cells->count; i++)
        {
            fz_rect key = cells->items[i];
            int j = i - 1;
            while (j >= 0 && compare_rects_lexicographically(&cells->items[j], &key) > 0)
            {
                cells->items[j + 1] = cells->items[j];
                j--;
            }
            cells->items[j + 1] = key;
        }
    }
    else
    {
        qsort(cells->items, cells->count, sizeof(fz_rect), compare_rects_lexicographically);
    }

    TableArray* tables = malloc(sizeof(TableArray));
    if (!tables)
        return NULL;

    tables->count = 0;
    tables->tables = NULL;

    // Add first table
    Table* table = add_new_table(tables);
    if (!table)
    {
        free(tables);
        return NULL;
    }
    int row_capacity = 16;

    float prev_row_y1 = -1000.0f;

    int i = 0;
    while (i < cells->count)
    {
        float current_y0 = cells->items[i].y0;
        int j = i;

        // Find all cells in this row (same y0)
        while (j < cells->count && fabs(cells->items[j].y0 - current_y0) < ROW_Y_TOLERANCE)
        {
            j++;
        }

        // Check for large gap indicating a new table
        if (prev_row_y1 > -500.0f && (current_y0 - prev_row_y1) > TABLE_SPLIT_GAP_THRESHOLD)
        {
            // Start a new table
            table = add_new_table(tables);
            if (!table)
            {
                free_table_array(tables);
                return NULL;
            }
            row_capacity = 16;
        }

        if (table->count >= row_capacity)
        {
            row_capacity *= 2;
            table->rows = realloc(table->rows, row_capacity * sizeof(TableRow));
        }

        TableRow* row = &table->rows[table->count++];
        row->count = j - i;
        row->cells = malloc(row->count * sizeof(TableCell));
        row->bbox = fz_empty_rect;

        // Store cells in this row (sorted by x position)
        for (int k = 0; k < row->count; k++)
        {
            row->cells[k].bbox = cells->items[i + k];
            row->cells[k].text = NULL;
            row->bbox = fz_union_rect(row->bbox, cells->items[i + k]);
        }

        table->bbox = fz_union_rect(table->bbox, row->bbox);
        prev_row_y1 = row->bbox.y1; // Track for gap detection
        i = j;
    }

    // Process each table: normalize columns and clean up
    for (int t = 0; t < tables->count; t++)
    {
        table = &tables->tables[t];
        if (table->count == 0)
            continue;

        // Find the row with the most cells (likely a complete data row, not header)
        int max_cells = 0;
        int max_row_idx = 0;
        for (int r = 0; r < table->count; r++)
        {
            if (table->rows[r].count > max_cells)
            {
                max_cells = table->rows[r].count;
                max_row_idx = r;
            }
        }

        if (max_cells == 0)
            continue;

        // Use the reference row to establish column positions
        float* col_x_positions = malloc(max_cells * sizeof(float));
        for (int c = 0; c < max_cells; c++)
        {
            col_x_positions[c] = table->rows[max_row_idx].cells[c].bbox.x0;
        }
        int col_count = max_cells;

        // Now normalize all rows to have the same column structure
        for (int r = 0; r < table->count; r++)
        {
            TableRow* row = &table->rows[r];
            TableCell* old_cells = row->cells;
            int old_count = row->count;

            // Create new cell array with proper column count
            TableCell* new_cells = malloc(col_count * sizeof(TableCell));
            row->cells = new_cells;
            row->count = col_count;

            // Initialize all cells as empty first
            for (int c = 0; c < col_count; c++)
            {
                new_cells[c].bbox = fz_empty_rect;
                new_cells[c].text = NULL;
            }

            // Map old cells to new column positions
            for (int old_c = 0; old_c < old_count; old_c++)
            {
                fz_rect old_bbox = old_cells[old_c].bbox;

                // Find best matching column based on x position
                int best_col = -1;
                float best_dist = 1000000.0f;

                for (int new_c = 0; new_c < col_count; new_c++)
                {
                    float dist = fabsf(old_bbox.x0 - col_x_positions[new_c]);
                    if (dist < best_dist)
                    {
                        best_dist = dist;
                        best_col = new_c;
                    }
                }

                if (best_col >= 0)
                {
                    new_cells[best_col].bbox = old_bbox;
                }
            }

            free(old_cells);
        }

        free(col_x_positions);

        // Remove completely empty columns
        if (table->count > 0 && table->rows[0].count > 0)
        {
            col_count = table->rows[0].count;
            int* keep_cols = calloc(col_count, sizeof(int));
            int new_col_count = 0;

            // Mark columns that have at least one non-empty cell
            for (int c = 0; c < col_count; c++)
            {
                for (int r = 0; r < table->count; r++)
                {
                    if (!fz_is_empty_rect(table->rows[r].cells[c].bbox))
                    {
                        keep_cols[c] = 1;
                        break;
                    }
                }
                if (keep_cols[c])
                {
                    new_col_count++;
                }
            }

            // If we're removing columns, rebuild all rows
            if (new_col_count < col_count && new_col_count > 0)
            {
                for (int r = 0; r < table->count; r++)
                {
                    TableRow* row = &table->rows[r];
                    TableCell* old_cells = row->cells;
                    TableCell* new_cells = malloc(new_col_count * sizeof(TableCell));
                    row->cells = new_cells;

                    int new_c = 0;
                    for (int c = 0; c < col_count; c++)
                    {
                        if (keep_cols[c])
                        {
                            new_cells[new_c] = old_cells[c];
                            new_c++;
                        }
                    }
                    row->count = new_col_count;
                    free(old_cells);
                }
            }

            free(keep_cols);
        }

        // Remove completely empty rows
        int write_idx = 0;
        for (int r = 0; r < table->count; r++)
        {
            TableRow* row = &table->rows[r];
            int has_content = 0;

            for (int c = 0; c < row->count; c++)
            {
                if (!fz_is_empty_rect(row->cells[c].bbox))
                {
                    has_content = 1;
                    break;
                }
            }

            if (has_content)
            {
                if (write_idx != r)
                {
                    table->rows[write_idx] = table->rows[r];
                }
                write_idx++;
            }
            else
            {
                // Free empty row cells
                free(row->cells);
            }
        }
        table->count = write_idx;
    }

    // Remove empty tables
    int table_write_idx = 0;
    for (int t = 0; t < tables->count; t++)
    {
        if (tables->tables[t].count >= 2 && tables->tables[t].rows[0].count >= 2)
        {
            if (table_write_idx != t)
            {
                tables->tables[table_write_idx] = tables->tables[t];
            }
            table_write_idx++;
        }
        else
        {
            // Free invalid table
            for (int r = 0; r < tables->tables[t].count; r++)
            {
                free(tables->tables[t].rows[r].cells);
            }
            free(tables->tables[t].rows);
        }
    }
    tables->count = table_write_idx;

    if (tables->count == 0)
    {
        free(tables->tables);
        free(tables);
        return NULL;
    }

    return tables;
}

TableArray* find_tables_with_mupdf_native(const char* pdf_path, int page_number)
{
    fz_context* ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
    if (!ctx)
    {
        fprintf(stderr, "Failed to create MuPDF context.\n");
        return NULL;
    }

    fz_document* doc = NULL;
    fz_page* page = NULL;
    CaptureDevice* capture_dev = NULL;
    TableArray* tables = NULL;
    MemoryPool pool;
    SpatialHash hash;

    init_memory_pool(&pool);
    init_spatial_hash(&hash);

    fz_try(ctx)
    {
        fz_register_document_handlers(ctx);
        doc = fz_open_document(ctx, pdf_path);
        page = fz_load_page(ctx, doc, page_number);

        // Use edge-based detection
        capture_dev = (CaptureDevice*)new_capture_device(ctx);
        fz_run_page(ctx, page, (fz_device*)capture_dev, fz_identity, NULL);
        fz_close_device(ctx, (fz_device*)capture_dev);

        // Separate edges by orientation with preallocation
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

        // Merge close edges (combined snap and join)
        merge_edges(&h_edges, SNAP_TOLERANCE, JOIN_TOLERANCE);
        merge_edges(&v_edges, SNAP_TOLERANCE, JOIN_TOLERANCE);

        // Find intersections using spatial hash
        find_intersections(&v_edges, &h_edges, &hash);

        // Convert hash to sorted array
        PointArray intersections;
        init_point_array(&intersections);
        collect_points_from_hash(&hash, &intersections);
        if (intersections.count > 0)
        {
            qsort(intersections.items, intersections.count, sizeof(Point), compare_points);
        }

        // Find cells from intersections using spatial hash
        CellArray cells;
        init_cell_array(&cells);
        find_cells(&intersections, &hash, &cells);

        free_edge_array(&h_edges);
        free_edge_array(&v_edges);
        free_point_array(&intersections);

        // Group cells into structured tables
        tables = group_cells_into_tables(&cells);
        free_cell_array(&cells);
    }
    fz_always(ctx)
    {
        if (capture_dev)
            fz_drop_device(ctx, (fz_device*)capture_dev);
        fz_drop_page(ctx, page);
        fz_drop_document(ctx, doc);
        free_spatial_hash(&hash);
        free_memory_pool(&pool);
    }
    fz_catch(ctx)
    {
        fprintf(stderr, "Failed to detect tables: %s\n", fz_caught_message(ctx));
        if (tables)
        {
            free_table_array(tables);
            tables = NULL;
        }
    }

    fz_drop_context(ctx);
    return tables;
}

TableArray* find_tables_on_page(fz_context* ctx, fz_document* doc, int page_number, BlockArray* blocks)
{
    (void)blocks;

    fz_page* page = NULL;
    CaptureDevice* capture_dev = NULL;
    TableArray* tables = NULL;
    MemoryPool pool;
    SpatialHash hash;

    init_memory_pool(&pool);
    init_spatial_hash(&hash);

    fz_try(ctx)
    {
        page = fz_load_page(ctx, doc, page_number);

        // Use edge-based detection
        capture_dev = (CaptureDevice*)new_capture_device(ctx);
        fz_run_page(ctx, page, (fz_device*)capture_dev, fz_identity, NULL);
        fz_close_device(ctx, (fz_device*)capture_dev);

        // Separate edges by orientation with preallocation
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

        // Merge close edges (combined snap and join)
        merge_edges(&h_edges, SNAP_TOLERANCE, JOIN_TOLERANCE);
        merge_edges(&v_edges, SNAP_TOLERANCE, JOIN_TOLERANCE);

        // Find intersections using spatial hash
        find_intersections(&v_edges, &h_edges, &hash);

        // Convert hash to sorted array
        PointArray intersections;
        init_point_array(&intersections);
        collect_points_from_hash(&hash, &intersections);
        if (intersections.count > 0)
        {
            qsort(intersections.items, intersections.count, sizeof(Point), compare_points);
        }

        // Find cells from intersections using spatial hash
        CellArray cells;
        init_cell_array(&cells);
        find_cells(&intersections, &hash, &cells);

        free_edge_array(&h_edges);
        free_edge_array(&v_edges);
        free_point_array(&intersections);

        // Group cells into structured tables
        tables = group_cells_into_tables(&cells);
        free_cell_array(&cells);
    }
    fz_always(ctx)
    {
        if (capture_dev)
            fz_drop_device(ctx, (fz_device*)capture_dev);
        if (page)
            fz_drop_page(ctx, page);
        free_spatial_hash(&hash);
        free_memory_pool(&pool);
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