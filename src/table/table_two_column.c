// Two column table synthesizer logic
#include "table.h"
#include "font_metrics.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include "utils.h"
#include "table_utils.h"

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
} TextRow;

DEFINE_ARRAY_METHODS_PUBLIC(Point, Point, point);

static int compare_linegeom_y0(const void* a, const void* b)
{
    const LineGeom* la = (const LineGeom*)a;
    const LineGeom* lb = (const LineGeom*)b;
    float dy = la->y0 - lb->y0;
    if (fabsf(dy) > 1e-3f)
        return CMP_FLOAT(dy, 0);
    return CMP_FLOAT(la->bbox.x0, lb->bbox.x0);
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
    SET_SENTINEL(out->split_x);

    int run_idx = -1;
    float prev_x1;
    SET_SENTINEL(prev_x1);

    for (fz_stext_char* ch = line->first_char; ch; ch = ch->next)
    {
        if (ch->c == 0)
            continue;

        bool is_whitespace = (ch->c == ' ' || ch->c == '\t' || ch->c == '\r' || ch->c == '\n' || ch->c == 160);
        if (is_whitespace)
            continue;

        fz_rect char_box = fz_rect_from_quad(ch->quad);
        float x0 = char_box.x0;
        float x1 = char_box.x1;

        // Large gap threshold for column split
        float split_gap_threshold = fmaxf(15.0f, ch->size * 2.0f);
        bool start_new_run = false;

        if (IS_SENTINEL(prev_x1))
            start_new_run = true;
        else if (x0 - prev_x1 > split_gap_threshold)
            start_new_run = true;
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
        out->bbox = update_bbox(out->bbox, char_box);
        out->runs[run_idx].bbox = update_bbox(out->runs[run_idx].bbox, char_box);

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
static void validate_syth_group(TextRow** best_rows, int* best_row_count, TextRow** current_rows,
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

    float min_x = -1.0f, max_x = -1.0f;

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
    TextRow* best_rows = NULL;
    int best_row_count = 0;
    TextRow* current_rows = NULL;
    int current_row_count = 0;
    int current_row_cap = 0;

    bool in_group = false;
    float prev_y1 = -1.0f;
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
            TextRow* prev_row = &current_rows[current_row_count - 1];
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
                validate_syth_group(&best_rows, &best_row_count, &current_rows, &current_row_count, &current_row_cap);
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
                TextRow* tmp = (TextRow*)realloc(current_rows, (size_t)new_cap * sizeof(TextRow));
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
            TextRow row;
            row.left_bbox = left_bbox;
            row.right_bbox = right_bbox;
            row.row_bbox = fz_union_rect(left_bbox, right_bbox);
            current_rows[current_row_count++] = row;
        }
        else if (left_continuation && current_row_count > 0)
        {
            // Merge into previous row's left cell
            TextRow* row = &current_rows[current_row_count - 1];
            if (fz_is_empty_rect(row->left_bbox))
                row->left_bbox = lines[left_idx].bbox;
            else
                row->left_bbox = fz_union_rect(row->left_bbox, lines[left_idx].bbox);
            row->row_bbox = fz_union_rect(row->row_bbox, lines[left_idx].bbox);
        }
        else if (right_continuation && current_row_count > 0)
        {
            // Merge into previous row's right cell
            TextRow* row = &current_rows[current_row_count - 1];
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
        validate_syth_group(&best_rows, &best_row_count, &current_rows, &current_row_count, &current_row_cap);
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
