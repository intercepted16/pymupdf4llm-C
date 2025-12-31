// Table grid logic - edge-based table detection

#include "table_grid.h"
#include "table.h"
#include "spatial_hash.h"
#include "utils.h"
#include <stdlib.h>
#include <math.h>
#include <stdbool.h>

void init_cell_array(CellArray* arr)
{
    arr->items = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

void free_cell_array(CellArray* arr)
{
    free(arr->items);
    arr->items = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

// Word-cutting validation: Initialize word rect array
void init_word_rect_array(WordRectArray* arr)
{
    arr->items = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

// Word-cutting validation: Free word rect array
void free_word_rect_array(WordRectArray* arr)
{
    free(arr->items);
    arr->items = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

// Word-cutting validation: Extract word bboxes from textpage within bounds
void extract_word_rects(fz_context* ctx, fz_stext_page* textpage, fz_rect bounds, WordRectArray* words)
{
    if (!ctx || !textpage || !words)
        return;

    // Initial capacity
    words->capacity = 128;
    words->items = malloc(words->capacity * sizeof(WordRect));
    words->count = 0;

    if (!words->items)
        return;

    for (fz_stext_block* block = textpage->first_block; block; block = block->next)
    {
        if (block->type != FZ_STEXT_BLOCK_TEXT)
            continue;

        // Skip blocks outside bounds
        if (block->bbox.x1 < bounds.x0 || block->bbox.x0 > bounds.x1 || block->bbox.y1 < bounds.y0 ||
            block->bbox.y0 > bounds.y1)
            continue;

        for (fz_stext_line* line = block->u.t.first_line; line; line = line->next)
        {
            // Skip lines outside bounds
            if (line->bbox.y1 < bounds.y0 || line->bbox.y0 > bounds.y1)
                continue;

            // Build words from characters
            fz_rect word_bbox = fz_empty_rect;
            float prev_x1 = -1000.0f;
            bool in_word = false;

            for (fz_stext_char* ch = line->first_char; ch; ch = ch->next)
            {
                fz_rect char_bbox = fz_rect_from_quad(ch->quad);

                // Skip whitespace
                if (ch->c == ' ' || ch->c == '\t' || ch->c == '\n' || ch->c == '\r')
                {
                    // End current word if any
                    if (in_word && !fz_is_empty_rect(word_bbox))
                    {
                        if (words->count >= words->capacity)
                        {
                            words->capacity *= 2;
                            WordRect* new_items = realloc(words->items, words->capacity * sizeof(WordRect));
                            if (!new_items)
                                return;
                            words->items = new_items;
                        }
                        words->items[words->count++].bbox = word_bbox;
                        word_bbox = fz_empty_rect;
                        in_word = false;
                    }
                    prev_x1 = -1000.0f;
                    continue;
                }

                // Check for word break (large gap)
                float gap = char_bbox.x0 - prev_x1;
                float tolerance = ch->size * 0.5f;
                if (tolerance < 3.0f)
                    tolerance = 3.0f;

                if (in_word && gap > tolerance)
                {
                    // End current word
                    if (!fz_is_empty_rect(word_bbox))
                    {
                        if (words->count >= words->capacity)
                        {
                            words->capacity *= 2;
                            WordRect* new_items = realloc(words->items, words->capacity * sizeof(WordRect));
                            if (!new_items)
                                return;
                            words->items = new_items;
                        }
                        words->items[words->count++].bbox = word_bbox;
                    }
                    word_bbox = char_bbox;
                }
                else
                {
                    // Extend current word
                    if (fz_is_empty_rect(word_bbox))
                        word_bbox = char_bbox;
                    else
                        word_bbox = fz_union_rect(word_bbox, char_bbox);
                }

                in_word = true;
                prev_x1 = char_bbox.x1;
            }

            // End final word in line
            if (in_word && !fz_is_empty_rect(word_bbox))
            {
                if (words->count >= words->capacity)
                {
                    words->capacity *= 2;
                    WordRect* new_items = realloc(words->items, words->capacity * sizeof(WordRect));
                    if (!new_items)
                        return;
                    words->items = new_items;
                }
                words->items[words->count++].bbox = word_bbox;
            }
        }
    }
}

// Word-cutting validation: Check if horizontal line y cuts through any word
bool intersects_words_h(float y, fz_rect table_bbox, const WordRectArray* words)
{
    if (!words || words->count == 0)
        return false;

    for (int i = 0; i < words->count; i++)
    {
        fz_rect wr = words->items[i].bbox;

        // Word must be within table bounds horizontally
        if (wr.x1 < table_bbox.x0 || wr.x0 > table_bbox.x1)
            continue;

        // Check if line cuts through word vertically (with small tolerance)
        float margin = 1.0f;
        if (wr.y0 + margin < y && y < wr.y1 - margin)
            return true;
    }
    return false;
}

// Word-cutting validation: Check if vertical line x cuts through any word
bool intersects_words_v(float x, fz_rect table_bbox, const WordRectArray* words)
{
    if (!words || words->count == 0)
        return false;

    for (int i = 0; i < words->count; i++)
    {
        fz_rect wr = words->items[i].bbox;

        // Word must be within table bounds vertically
        if (wr.y1 < table_bbox.y0 || wr.y0 > table_bbox.y1)
            continue;

        // Check if line cuts through word horizontally (with small tolerance)
        float margin = 1.0f;
        if (wr.x0 + margin < x && x < wr.x1 - margin)
            return true;
    }
    return false;
}

int compare_edges_v(const void* a, const void* b)
{
    const Edge* ea = (Edge*)a;
    const Edge* eb = (Edge*)b;
    const int cmp = CMP_FLOAT(ea->x0, eb->x0);
    return cmp ? cmp : CMP_FLOAT(ea->y0, eb->y0);
}

int compare_edges_h(const void* a, const void* b)
{
    Edge *ea = (Edge*)a, *eb = (Edge*)b;
    int cmp = CMP_FLOAT(ea->y0, eb->y0);
    return cmp ? cmp : CMP_FLOAT(ea->x0, eb->x0);
}

int compare_points(const void* a, const void* b)
{
    Point *pa = (Point*)a, *pb = (Point*)b;
    int cmp = CMP_FLOAT(pa->y, pb->y);
    return cmp ? cmp : CMP_FLOAT(pa->x, pb->x);
}

static int compare_rects_lexicographically(const void* a, const void* b)
{
    const fz_rect* ra = (fz_rect*)a;
    const fz_rect* rb = (fz_rect*)b;
    const int cmp = CMP_FLOAT(ra->y0, rb->y0);
    return cmp ? cmp : CMP_FLOAT(ra->x0, rb->x0);
}

void merge_edges(EdgeArray* edges, double snap_tolerance, double join_tolerance)
{
    if (edges->count == 0)
        return;

    char orientation = edges->items[0].orientation;
    qsort(edges->items, edges->count, sizeof(Edge), orientation == 'h' ? compare_edges_h : compare_edges_v);

    const int snap_tol_int = COORD_TO_INT(snap_tolerance);
    int join_tol_int = COORD_TO_INT(join_tolerance);

    EdgeArray result;
    init_edge_array(&result);

    // Preallocate to avoid reallocation in loop
    result.capacity = edges->count;
    result.items = malloc(result.capacity * sizeof(Edge));

    int i = 0;
    while (i < edges->count)
    {
        const Edge current = edges->items[i];

        // Find cluster of edges to snap together
        int cluster_start = i;
        int pos_sum = orientation == 'h' ? COORD_TO_INT(current.y0) : COORD_TO_INT(current.x0);
        int cluster_count = 1;
        i++;

        while (i < edges->count)
        {
            const Edge next = edges->items[i];
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
        double snapped_pos = INT_TO_COORD(pos_sum / cluster_count); // NOLINT

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
void find_intersections(const EdgeArray* v_edges, const EdgeArray* h_edges, SpatialHash* hash)
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

// FIX: Handle incomplete grids - find cells even without perfect 4 corners
void find_cells(const PointArray* intersections, SpatialHash* hash, CellArray* cells)
{
    if (intersections->count < 4)
        return;

    // Sort points for structured searching
    qsort(intersections->items, intersections->count, sizeof(Point), compare_points);

    cells->capacity = intersections->count * intersections->count;
    cells->items = malloc(cells->capacity * sizeof(fz_rect));
    cells->count = 0;

    // Build index of points by Y coordinate for faster lookup
    int* y_indices = malloc(intersections->count * sizeof(int));
    for (int i = 0; i < intersections->count; i++)
    {
        y_indices[i] = i;
    }

    const double EPSILON = 1e-6;

    for (int i = 0; i < intersections->count; ++i)
    {
        Point p1 = intersections->items[i];

        // Find all points with same Y (on same horizontal line)
        for (int j = i + 1; j < intersections->count; ++j)
        {
            if (fabs(intersections->items[j].y - p1.y) > EPSILON)
                break; // Sorted by Y, stop searching

            Point p2 = intersections->items[j];
            if (p2.x <= p1.x)
                continue; // Need p2 to be to the right

            // Now find points below p1 and p2
            for (int k = j + 1; k < intersections->count; ++k)
            {
                Point p3 = intersections->items[k];
                if (p3.y <= p1.y + EPSILON)
                    continue; // Need p3 below p1

                if (fabs(p3.x - p1.x) < EPSILON)
                {
                    // Found left-bottom corner, now look for right-bottom
                    for (int m = k + 1; m < intersections->count; ++m)
                    {
                        Point p4 = intersections->items[m];

                        // p4 should be at approximately (p2.x, p3.y)
                        if (fabs(p4.x - p2.x) < EPSILON && fabs(p4.y - p3.y) < EPSILON)
                        {
                            // Perfect 4-corner match
                            fz_rect cell_bbox = {p1.x, p1.y, p2.x, p3.y};
                            double width = cell_bbox.x1 - cell_bbox.x0;
                            double height = cell_bbox.y1 - cell_bbox.y0;

                            if (width > 2.0 && height > 0.5)
                            {
                                cells->items[cells->count++] = cell_bbox;
                            }
                            goto next_p2;
                        }
                    }

                    // FALLBACK: Accept cell even if p4 doesn't exist exactly
                    // This handles merged cells and missing borders
                    fz_rect cell_bbox = {p1.x, p1.y, p2.x, p3.y};
                    double width = cell_bbox.x1 - cell_bbox.x0;
                    double height = cell_bbox.y1 - cell_bbox.y0;

                    if (width > 2.0 && height > 0.5)
                    {
                        cells->items[cells->count++] = cell_bbox;
                    }
                    goto next_p2;
                }
            }

        next_p2:;
        }
    }

    free(y_indices);
}

// Helper to initialize a new table in the array
static Table* add_new_table(TableArray* tables)
{
    const int idx = tables->count;
    Table* new_tables = realloc(tables->tables, (tables->count + 1) * sizeof(Table));
    if (!new_tables)
        return NULL;
    tables->tables = new_tables;
    tables->count++;
    Table* table = &tables->tables[idx];
    table->bbox = fz_empty_rect;
    table->count = 0;
    table->rows = malloc(16 * sizeof(TableRow));
    if (!table->rows)
    {
        tables->count--;
        return NULL;
    }
    return table;
}

TableArray* group_cells_into_tables(const CellArray* cells)
{
    if (cells->count == 0)
        return NULL;

    qsort(cells->items, cells->count, sizeof(fz_rect), compare_rects_lexicographically);

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
        const float current_y0 = cells->items[i].y0;
        int j = i;

        // Find all cells in this row (same y0)
        while (j < cells->count && fabsf(cells->items[j].y0 - current_y0) < ROW_Y_TOLERANCE)
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

static bool is_valid_rect(fz_rect r, fz_rect bounds, float margin)
{
    return r.y0 >= bounds.y0 - margin && r.y1 <= bounds.y1 + margin && r.x0 >= bounds.x0 - margin &&
           r.x1 <= bounds.x1 + margin;
}

bool validate_tables(TableArray* tables, fz_rect page_rect)
{
    if (!tables || !tables->count)
        return false;
    for (int t = 0; t < tables->count; t++)
    {
        Table* table = &tables->tables[t];
        if (!is_valid_rect(table->bbox, page_rect, 50))
            continue;

        // First, count structural metrics to assess table quality
        int expected_cols = -1, valid_rows = 0, missing_cell_rows = 0;
        for (int r = 0; r < table->count; r++)
        {
            if (!is_valid_rect(table->rows[r].bbox, page_rect, 10))
                continue;
            if (!table->rows[r].count)
                continue;
            if (expected_cols < 0)
                expected_cols = table->rows[r].count;
            else if (table->rows[r].count != expected_cols)
            {
                if (table->rows[r].count < expected_cols)
                    missing_cell_rows++;
            }
            valid_rows++;
        }

        // GATE 1: Reject tables that are too large - they're layout, not data tables
        // Real data tables rarely cover >60% of page height
        float page_height = page_rect.y1 - page_rect.y0;
        float page_width = page_rect.x1 - page_rect.x0;
        float table_height = table->bbox.y1 - table->bbox.y0;
        float table_width = table->bbox.x1 - table->bbox.x0;
        float height_ratio = table_height / page_height;
        float width_ratio = table_width / page_width;

        // Reject page-spanning tables - they're almost always layout, not data
        if (height_ratio > 0.6f || width_ratio > 0.9f)
        {
            fprintf(stderr, "    Validation: Rejecting table (too large: %.1f%% height, %.1f%% width)\n",
                    height_ratio * 100, width_ratio * 100);
            continue;
        }

        // GATE 2: Reject if too many rows have missing cells (inconsistent structure)
        if (valid_rows > 0 && missing_cell_rows > valid_rows * 0.4f)
        {
            fprintf(stderr, "    Validation: Rejecting table (inconsistent rows: %d/%d have missing cells)\n",
                    missing_cell_rows, valid_rows);
            continue;
        }

        if (valid_rows >= 2 && expected_cols >= 2)
            return true;
    }
    return false;
}