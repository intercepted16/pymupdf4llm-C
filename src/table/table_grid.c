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

int compare_edges_v(const void* a, const void* b)
{
    const Edge *ea = (Edge*)a;
    const Edge *eb = (Edge*)b;
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
    const fz_rect *ra = (fz_rect*)a;
    const fz_rect *rb = (fz_rect*)b;
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

// Optimized: Use spatial hash for O(1) point lookup instead of O(n) scan
void find_cells(const PointArray* intersections, SpatialHash* hash, CellArray* cells)
{
    // Preallocate cells array
    cells->capacity = intersections->count;
    cells->items = malloc(cells->capacity * sizeof(fz_rect));
    cells->count = 0;

    for (int i = 0; i < intersections->count; ++i)
    {
        Point p1 = intersections->items[i];
        Point p_right = {-1, -1};
        Point down = {-1, -1};

        // Since points are sorted, we can optimize the search
        // Find next point to the right (same y, larger x)
        for (int j = i + 1; j < intersections->count && intersections->items[j].y == p1.y; ++j)
        {
                p_right = intersections->items[j];
                break; // First one found is the closest
        }

        // Find next point below (same x, larger y)
        for (int j = i + 1; j < intersections->count; ++j)
        {
            if (intersections->items[j].x == p1.x)
            {
                    down = intersections->items[j];
                    break; // First one found is the closest
            }
        }

        if (p_right.x != -1 && down.y != -1)
        {
            // Use spatial hash for O(1) lookup of bottom-right corner
            if (find_in_spatial_hash(hash, p_right.x, down.y))
            {
                const fz_rect cell_bbox = {p1.x, p1.y, p_right.x, down.y};
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

// Helper to initialize a new table in the array
static Table* add_new_table(TableArray* tables)
{
    const int idx = tables->count;
    Table *new_tables = realloc(tables->tables, (tables->count + 1) * sizeof(Table));
    if (!new_tables) return NULL;
    tables->tables = new_tables; tables->count++;
    Table* table = &tables->tables[idx];
    table->bbox = fz_empty_rect;
    table->count = 0;
    table->rows = malloc(16 * sizeof(TableRow));
    if (!table->rows) { tables->count--; return NULL; }
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
        int expected_cols = -1, valid_rows = 0;
        for (int r = 0; r < table->count; r++)
        {
            if (!is_valid_rect(table->rows[r].bbox, page_rect, 10))
                continue;
            if (!table->rows[r].count)
                continue;
            if (expected_cols < 0)
                expected_cols = table->rows[r].count;
            else if (table->rows[r].count != expected_cols)
                return false;
            valid_rows++;
        }
        if (valid_rows >= 2 && expected_cols >= 2)
            return true;
    }
    return false;
}
