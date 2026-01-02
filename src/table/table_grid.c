// Table grid logic - edge-based table detection

#include "table_grid.h"
#include "table.h"
#include "spatial_hash.h"
#include "utils.h"
#include <stdlib.h>
#include <math.h>
#include <stdbool.h>

void init_word_rect_array(WordRectArray* arr)
{
    arr->items = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

void free_word_rect_array(WordRectArray* arr)
{
    free(arr->items);
    arr->items = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

void extract_word_rects(fz_context* ctx, fz_stext_page* textpage, fz_rect bounds, WordRectArray* words)
{
    if (!textpage || !words)
        return;

    init_word_rect_array(words);
    words->capacity = 256;
    words->items = malloc(words->capacity * sizeof(WordRect));

    for (fz_stext_block* block = textpage->first_block; block; block = block->next)
    {
        if (block->type != FZ_STEXT_BLOCK_TEXT)
            continue;

        for (fz_stext_line* line = block->u.t.first_line; line; line = line->next)
        {
            fz_rect word_bbox = fz_empty_rect;
            bool in_word = false;

            for (fz_stext_char* ch = line->first_char; ch; ch = ch->next)
            {
                if (ch->c <= 32 || ch->c == 160) // Whitespace
                {
                    if (in_word && !fz_is_empty_rect(word_bbox))
                    {
                        if (words->count >= words->capacity)
                        {
                            words->capacity *= 2;
                            words->items = realloc(words->items, words->capacity * sizeof(WordRect));
                        }
                        words->items[words->count++].bbox = word_bbox;
                    }
                    word_bbox = fz_empty_rect;
                    in_word = false;
                }
                else
                {
                    fz_rect char_bbox = fz_rect_from_quad(ch->quad);
                    word_bbox = in_word ? fz_union_rect(word_bbox, char_bbox) : char_bbox;
                    in_word = true;
                }
            }

            if (in_word && !fz_is_empty_rect(word_bbox))
            {
                if (words->count >= words->capacity)
                {
                    words->capacity *= 2;
                    words->items = realloc(words->items, words->capacity * sizeof(WordRect));
                }
                words->items[words->count++].bbox = word_bbox;
            }
        }
    }
}

bool intersects_words_h(float y, fz_rect table_bbox, const WordRectArray* words)
{
    if (!words || words->count == 0)
        return false;

    const float tolerance = 2.0f;

    for (int i = 0; i < words->count; i++)
    {
        fz_rect word = words->items[i].bbox;

        if (word.x1 < table_bbox.x0 || word.x0 > table_bbox.x1)
            continue;

        if (y > word.y0 + tolerance && y < word.y1 - tolerance)
            return true;
    }

    return false;
}

bool intersects_words_v(float x, fz_rect table_bbox, const WordRectArray* words)
{
    if (!words || words->count == 0)
        return false;

    const float tolerance = 2.0f;

    for (int i = 0; i < words->count; i++)
    {
        fz_rect word = words->items[i].bbox;

        if (word.y1 < table_bbox.y0 || word.y0 > table_bbox.y1)
            continue;

        if (x > word.x0 + tolerance && x < word.x1 - tolerance)
            return true;
    }

    return false;
}

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

void init_point_array(PointArray* arr)
{
    arr->items = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

void add_to_point_array(PointArray* arr, Point item)
{
    if (arr->count >= arr->capacity)
    {
        arr->capacity = arr->capacity == 0 ? 16 : arr->capacity * 2;
        arr->items = realloc(arr->items, arr->capacity * sizeof(Point));
    }
    arr->items[arr->count++] = item;
}

void free_point_array(PointArray* arr)
{
    free(arr->items);
    arr->items = NULL;
    arr->count = 0;
    arr->capacity = 0;
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

    float dy = ra->y0 - rb->y0;
    if (fabsf(dy) > 5.0f)
    {
        return (dy > 0) ? 1 : -1;
    }

    return (ra->x0 > rb->x0) ? 1 : -1;
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
    const double intersection_tolerance = 1.0;
    int tol_int = COORD_TO_INT(intersection_tolerance);

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
void find_cells(const PointArray* intersections, SpatialHash* hash, CellArray* cells, fz_rect page_rect)
{
    if (intersections->count < 4)
        return;

    const float page_width = page_rect.x1 - page_rect.x0;
    const float page_height = page_rect.y1 - page_rect.y0;
    const float page_diagonal = sqrtf(page_width * page_width + page_height * page_height);
    const float min_cell_size = fminf(page_width, page_height) * MIN_CELL_SIZE_RATIO;
    const float max_cell_width = page_width * MAX_CELL_WIDTH_RATIO;
    const float max_cell_height = page_height * MAX_CELL_HEIGHT_RATIO;
    const float snap_dist = page_width * SNAP_TOLERANCE_RATIO;
    const float epsilon = page_diagonal * INTERSECTION_TOLERANCE_RATIO;

    // Sort points for structured searching
    qsort(intersections->items, intersections->count, sizeof(Point), compare_points);

    PointArray snapped_points;
    init_point_array(&snapped_points);
    snapped_points.capacity = intersections->count;
    snapped_points.items = malloc(snapped_points.capacity * sizeof(Point));

    const float SNAP_DIST = snap_dist;

    for (int i = 0; i < intersections->count; i++)
    {
        Point p = intersections->items[i];
        bool merged = false;

        for (int j = 0; j < snapped_points.count; j++)
        {
            float dx = fabsf(p.x - snapped_points.items[j].x);
            float dy = fabsf(p.y - snapped_points.items[j].y);

            if (dx < SNAP_DIST && dy < SNAP_DIST)
            {
                snapped_points.items[j].x = (snapped_points.items[j].x + p.x) / 2.0f;
                snapped_points.items[j].y = (snapped_points.items[j].y + p.y) / 2.0f;
                merged = true;
                break;
            }
        }

        if (!merged)
        {
            add_to_point_array(&snapped_points, p);
        }
    }

    fprintf(stderr, "  Point snapping: %d original -> %d snapped (removed %d duplicates)\n", intersections->count,
            snapped_points.count, intersections->count - snapped_points.count);

    const PointArray* points_to_use = &snapped_points;

    cells->capacity = points_to_use->count * points_to_use->count;
    cells->items = malloc(cells->capacity * sizeof(fz_rect));
    cells->count = 0;

    // Build index of points by Y coordinate for faster lookup
    int* y_indices = malloc(points_to_use->count * sizeof(int));
    for (int i = 0; i < points_to_use->count; i++)
    {
        y_indices[i] = i;
    }

    const double EPSILON = epsilon;

    for (int i = 0; i < points_to_use->count; ++i)
    {
        Point p1 = points_to_use->items[i];

        // Find all points with same Y (on same horizontal line)
        for (int j = i + 1; j < points_to_use->count; ++j)
        {
            if (fabs(points_to_use->items[j].y - p1.y) > EPSILON)
                break; // Sorted by Y, stop searching

            Point p2 = points_to_use->items[j];
            if (p2.x <= p1.x)
                continue; // Need p2 to be to the right

            // Now find points below p1 and p2
            for (int k = j + 1; k < points_to_use->count; ++k)
            {
                Point p3 = points_to_use->items[k];
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

                            if (width > min_cell_size && width < max_cell_width && height > min_cell_size &&
                                height < max_cell_height)
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

                    if (width > min_cell_size && width < max_cell_width && height > min_cell_size &&
                        height < max_cell_height)
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
    free_point_array(&snapped_points);
}

static float iou_rect(fz_rect a, fz_rect b)
{
    float inter_x0 = fmaxf(a.x0, b.x0);
    float inter_y0 = fmaxf(a.y0, b.y0);
    float inter_x1 = fminf(a.x1, b.x1);
    float inter_y1 = fminf(a.y1, b.y1);

    if (inter_x1 <= inter_x0 || inter_y1 <= inter_y0)
        return 0.0f;

    float inter_area = (inter_x1 - inter_x0) * (inter_y1 - inter_y0);
    float area_a = (a.x1 - a.x0) * (a.y1 - a.y0);
    float area_b = (b.x1 - b.x0) * (b.y1 - b.y0);
    float union_area = area_a + area_b - inter_area;

    return union_area > 0 ? inter_area / union_area : 0.0f;
}

void deduplicate_cells(CellArray* cells)
{
    if (cells->count <= 1)
        return;

    int* keep = calloc(cells->count, sizeof(int));

    for (int i = 0; i < cells->count; i++)
    {
        keep[i] = 1;
    }

    for (int i = 0; i < cells->count; i++)
    {
        if (!keep[i])
            continue;

        fz_rect cell_i = cells->items[i];
        float area_i = (cell_i.x1 - cell_i.x0) * (cell_i.y1 - cell_i.y0);

        for (int j = i + 1; j < cells->count; j++)
        {
            if (!keep[j])
                continue;

            fz_rect cell_j = cells->items[j];
            float area_j = (cell_j.x1 - cell_j.x0) * (cell_j.y1 - cell_j.y0);

            float inter_x0 = fmaxf(cell_i.x0, cell_j.x0);
            float inter_y0 = fmaxf(cell_i.y0, cell_j.y0);
            float inter_x1 = fminf(cell_i.x1, cell_j.x1);
            float inter_y1 = fminf(cell_i.y1, cell_j.y1);

            if (inter_x1 <= inter_x0 || inter_y1 <= inter_y0)
                continue; // No overlap

            float inter_area = (inter_x1 - inter_x0) * (inter_y1 - inter_y0);

            float smaller_area = fminf(area_i, area_j);
            float containment_ratio = inter_area / smaller_area;

            if (containment_ratio > 0.9f)
            {
                if (area_i >= area_j)
                {
                    keep[i] = 0; // Discard larger container cell i
                    break;       // No need to check further for i
                }
                else
                {
                    keep[j] = 0; // Discard larger container cell j
                }
            }
            else
            {
                float iou = inter_area / (area_i + area_j - inter_area);

                if (iou > 0.6f)
                {
                    if (area_i >= area_j)
                        keep[j] = 0;
                    else
                    {
                        keep[i] = 0;
                        break;
                    }
                }
            }
        }
    }

    int write_idx = 0;
    for (int i = 0; i < cells->count; i++)
    {
        if (keep[i])
        {
            cells->items[write_idx++] = cells->items[i];
        }
    }

    int removed = cells->count - write_idx;
    cells->count = write_idx;

    if (removed > 0)
    {
        fprintf(stderr, "  NMS deduplication: Removed %d overlapping cells (containment > 0.9 or IoU > 0.6)\n",
                removed);
    }

    free(keep);
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

TableArray* group_cells_into_tables(const CellArray* cells, fz_rect page_rect)
{
    if (cells->count == 0)
        return NULL;

    const float page_height = page_rect.y1 - page_rect.y0;
    const float table_split_gap_threshold = page_height * TABLE_SPLIT_GAP_RATIO;

    fprintf(stderr, "  Grouping %d cells into tables:\n", cells->count);

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
    int table_start_cell = 0;
    while (i < cells->count)
    {

        const float row_y0 = cells->items[i].y0;
        int j = i + 1;
        const float y_tolerance = (page_rect.y1 - page_rect.y0) * ROW_Y_TOLERANCE_RATIO;

        while (j < cells->count)
        {
            const float next_y0 = cells->items[j].y0;
            float y_diff = fabsf(next_y0 - row_y0);

            if (y_diff > y_tolerance)
                break;

            j++;
        }

        // Check for large gap indicating a new table
        float gap_from_prev = (prev_row_y1 > -500.0f) ? (row_y0 - prev_row_y1) : 0.0f;

        float gap_from_prev_cell = (i > 0) ? (row_y0 - cells->items[i - 1].y1) : 0.0f;
        float actual_gap = fmaxf(gap_from_prev, gap_from_prev_cell);

        if (actual_gap > table_split_gap_threshold)
        {
            int cells_in_table = i - table_start_cell;
            fprintf(stderr,
                    "  Starting new table due to gap of %.1f pixels (threshold=%.1f, prev table had %d cells)\n",
                    actual_gap, table_split_gap_threshold, cells_in_table);

            // Start a new table
            table = add_new_table(tables);
            if (!table)
            {
                free_table_array(tables);
                return NULL;
            }
            row_capacity = 16;
            prev_row_y1 = -1000.0f; // Reset for new table
            table_start_cell = i;
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

        float row_height = row->bbox.y1 - row->bbox.y0;
        if (row_height > 100.0f)
        {
            fprintf(stderr, "  WARNING: Row %d has unusual height %.1f: (%.1f, %.1f, %.1f, %.1f)\n", table->count - 1,
                    row_height, row->bbox.x0, row->bbox.y0, row->bbox.x1, row->bbox.y1);
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

            valid_rows++;

            if (expected_cols < 0)
            {
                expected_cols = table->rows[r].count;
            }
            else if (table->rows[r].count != expected_cols)
            {
                if (table->rows[r].count < expected_cols)
                    missing_cell_rows++;
            }
        }

        // GATE 1: Reject tables that are too large - they're layout, not data tables
        // Real data tables rarely cover >60% of page height
        float page_height = page_rect.y1 - page_rect.y0;
        float page_width = page_rect.x1 - page_rect.x0;
        float table_height = table->bbox.y1 - table->bbox.y0;
        float table_width = table->bbox.x1 - table->bbox.x0;
        float height_ratio = table_height / page_height;
        float width_ratio = table_width / page_width;

        fprintf(stderr, "    Table bbox: (%.1f, %.1f, %.1f, %.1f), page: (%.1f, %.1f, %.1f, %.1f)\n", table->bbox.x0,
                table->bbox.y0, table->bbox.x1, table->bbox.y1, page_rect.x0, page_rect.y0, page_rect.x1, page_rect.y1);

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