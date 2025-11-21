#include "table.h"
#include "platform_compat.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#define EDGE_MIN_LENGTH 3.0
#define SNAP_TOLERANCE 3.0
#define JOIN_TOLERANCE 3.0
#define INTERSECTION_TOLERANCE 1.0

// --- Helper functions for dynamic arrays ---

static void init_edge_array(EdgeArray* arr) {
    arr->items = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

static void add_to_edge_array(EdgeArray* arr, Edge item) {
    if (arr->count >= arr->capacity) {
        arr->capacity = arr->capacity == 0 ? 16 : arr->capacity * 2;
        arr->items = realloc(arr->items, arr->capacity * sizeof(Edge));
    }
    arr->items[arr->count++] = item;
}

static void free_edge_array(EdgeArray* arr) {
    free(arr->items);
}

static void init_point_array(PointArray* arr) {
    arr->items = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

static void add_to_point_array(PointArray* arr, Point item) {
    if (arr->count >= arr->capacity) {
        arr->capacity = arr->capacity == 0 ? 16 : arr->capacity * 2;
        arr->items = realloc(arr->items, arr->capacity * sizeof(Point));
    }
    arr->items[arr->count++] = item;
}

static void free_point_array(PointArray* arr) {
    free(arr->items);
}

static void init_cell_array(CellArray* arr) {
    arr->items = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

static void add_to_cell_array(CellArray* arr, fz_rect item) {
    if (arr->count >= arr->capacity) {
        arr->capacity = arr->capacity == 0 ? 16 : arr->capacity * 2;
        arr->items = realloc(arr->items, arr->capacity * sizeof(fz_rect));
    }
    arr->items[arr->count++] = item;
}

static void free_cell_array(CellArray* arr) {
    free(arr->items);
}


// --- MuPDF device to capture lines ---

typedef struct {
    fz_device super;
    EdgeArray edges;
} CaptureDevice;

static void capture_stroke_path(fz_context* ctx, fz_device* dev_, const fz_path* path, const fz_stroke_state* stroke, fz_matrix ctm,
                                fz_colorspace* colorspace, const float* color, float alpha, fz_color_params color_params) {
    (void)colorspace;
    (void)color;
    (void)alpha;
    (void)color_params;

    CaptureDevice* dev = (CaptureDevice*)dev_;
    fz_rect bbox = fz_bound_path(ctx, path, stroke, ctm);
    
    double width = bbox.x1 - bbox.x0;
    double height = bbox.y1 - bbox.y0;

    if (height <= 1.0 && width >= EDGE_MIN_LENGTH) { // Horizontal line
        add_to_edge_array(&dev->edges, (Edge){bbox.x0, bbox.y0, bbox.x1, bbox.y0, 'h'});
    } else if (width <= 1.0 && height >= EDGE_MIN_LENGTH) { // Vertical line
        add_to_edge_array(&dev->edges, (Edge){bbox.x0, bbox.y0, bbox.x0, bbox.y1, 'v'});
    }
}

static void capture_fill_path(fz_context* ctx, fz_device* dev_, const fz_path* path, int even_odd, fz_matrix ctm,
                              fz_colorspace* colorspace, const float* color, float alpha, fz_color_params color_params) {
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
    if (width > 0 && height > 0) {
        add_to_edge_array(&dev->edges, (Edge){bbox.x0, bbox.y0, bbox.x1, bbox.y0, 'h'}); // Top
        add_to_edge_array(&dev->edges, (Edge){bbox.x0, bbox.y1, bbox.x1, bbox.y1, 'h'}); // Bottom
        add_to_edge_array(&dev->edges, (Edge){bbox.x0, bbox.y0, bbox.x0, bbox.y1, 'v'}); // Left
        add_to_edge_array(&dev->edges, (Edge){bbox.x1, bbox.y0, bbox.x1, bbox.y1, 'v'}); // Right
    }
}

static void capture_close_device(fz_context *ctx, fz_device *dev_) {
    (void)ctx;
    (void)dev_;
	// Nothing to do
}

static void capture_drop_device(fz_context *ctx, fz_device *dev_) {
    (void)ctx;
	CaptureDevice *dev = (CaptureDevice*)dev_;
    free_edge_array(&dev->edges);
}

static fz_device* new_capture_device(fz_context* ctx) {
    CaptureDevice* dev = fz_new_derived_device(ctx, CaptureDevice);
    dev->super.close_device = capture_close_device;
    dev->super.drop_device = capture_drop_device;
    dev->super.stroke_path = capture_stroke_path;
    dev->super.fill_path = capture_fill_path;
    init_edge_array(&dev->edges);
    return (fz_device*)dev;
}

// --- Comparison functions for sorting ---

static int compare_edges_v(const void* a, const void* b) {
    Edge* edge_a = (Edge*)a;
    Edge* edge_b = (Edge*)b;
    if (edge_a->x0 < edge_b->x0) return -1;
    if (edge_a->x0 > edge_b->x0) return 1;
    if (edge_a->y0 < edge_b->y0) return -1;
    if (edge_a->y0 > edge_b->y0) return 1;
    return 0;
}

static int compare_edges_h(const void* a, const void* b) {
    Edge* edge_a = (Edge*)a;
    Edge* edge_b = (Edge*)b;
    if (edge_a->y0 < edge_b->y0) return -1;
    if (edge_a->y0 > edge_b->y0) return 1;
    if (edge_a->x0 < edge_b->x0) return -1;
    if (edge_a->x0 > edge_b->x0) return 1;
    return 0;
}

static int compare_points(const void* a, const void* b) {
    Point* p_a = (Point*)a;
    Point* p_b = (Point*)b;
    if (p_a->y < p_b->y) return -1;
    if (p_a->y > p_b->y) return 1;
    if (p_a->x < p_b->x) return -1;
    if (p_a->x > p_b->x) return 1;
    return 0;
}

static int compare_rects_lexicographically(const void* a, const void* b) {
    fz_rect* rect_a = (fz_rect*)a;
    fz_rect* rect_b = (fz_rect*)b;
    if (rect_a->y0 < rect_b->y0) return -1;
    if (rect_a->y0 > rect_b->y0) return 1;
    if (rect_a->x0 < rect_b->x0) return -1;
    if (rect_a->x0 > rect_b->x0) return 1;
    return 0;
}

// --- Core table finding logic ---

static void merge_edges(EdgeArray* edges, double snap_tolerance, double join_tolerance) {
    if (edges->count == 0) return;

    char orientation = edges->items[0].orientation;
    qsort(edges->items, edges->count, sizeof(Edge), orientation == 'h' ? compare_edges_h : compare_edges_v);

    // Snap edges
    for (int i = 0; i < edges->count; ++i) {
        double cluster_sum = 0;
        int cluster_count = 0;
        double current_pos = orientation == 'h' ? edges->items[i].y0 : edges->items[i].x0;
        
        int j = i;
        while (j < edges->count) {
            double pos = orientation == 'h' ? edges->items[j].y0 : edges->items[j].x0;
            if (fabs(pos - current_pos) <= snap_tolerance) {
                cluster_sum += pos;
                cluster_count++;
                j++;
            } else {
                break;
            }
        }

        if (cluster_count > 0) {
            double avg_pos = cluster_sum / cluster_count;
            for (int k = i; k < j; ++k) {
                if (orientation == 'h') {
                    edges->items[k].y0 = avg_pos;
                    edges->items[k].y1 = avg_pos;
                } else {
                    edges->items[k].x0 = avg_pos;
                    edges->items[k].x1 = avg_pos;
                }
            }
            i = j - 1;
        }
    }

    // Join edges
    EdgeArray joined_edges;
    init_edge_array(&joined_edges);

    if (edges->count > 0) {
        add_to_edge_array(&joined_edges, edges->items[0]);
    }

    for (int i = 1; i < edges->count; ++i) {
        Edge* last = &joined_edges.items[joined_edges.count - 1];
        Edge* current = &edges->items[i];

        if (orientation == 'h') {
            if (fabs(current->y0 - last->y0) < 0.1 && current->x0 <= (last->x1 + join_tolerance)) {
                last->x1 = fmax(last->x1, current->x1);
            } else {
                add_to_edge_array(&joined_edges, *current);
            }
        } else { // 'v'
            if (fabs(current->x0 - last->x0) < 0.1 && current->y0 <= (last->y1 + join_tolerance)) {
                last->y1 = fmax(last->y1, current->y1);
            } else {
                add_to_edge_array(&joined_edges, *current);
            }
        }
    }

    free_edge_array(edges);
    *edges = joined_edges;
}


static void find_intersections(EdgeArray* v_edges, EdgeArray* h_edges, PointArray* intersections) {
    for (int i = 0; i < v_edges->count; ++i) {
        for (int j = 0; j < h_edges->count; ++j) {
            Edge* v = &v_edges->items[i];
            Edge* h = &h_edges->items[j];
            if (h->x0 - INTERSECTION_TOLERANCE <= v->x0 && h->x1 + INTERSECTION_TOLERANCE >= v->x0 &&
                v->y0 - INTERSECTION_TOLERANCE <= h->y0 && v->y1 + INTERSECTION_TOLERANCE >= h->y0) {
                add_to_point_array(intersections, (Point){v->x0, h->y0});
            }
        }
    }
    if (intersections->count > 0) {
        qsort(intersections->items, intersections->count, sizeof(Point), compare_points);
    }
}

static void find_cells(PointArray* intersections, CellArray* cells) {
    for (int i = 0; i < intersections->count; ++i) {
        Point p1 = intersections->items[i];
        Point best_p_right = { -1, -1 };
        Point best_p_down = { -1, -1 };

        for (int j = i + 1; j < intersections->count; ++j) {
            Point p2 = intersections->items[j];
            if (p2.y == p1.y) { // Point is to the right
                if (best_p_right.x == -1 || p2.x < best_p_right.x) {
                    best_p_right = p2;
                }
            }
        }

        for (int j = i + 1; j < intersections->count; ++j) {
            Point p2 = intersections->items[j];
            if (p2.x == p1.x) { // Point is below
                if (best_p_down.y == -1 || p2.y < best_p_down.y) {
                    best_p_down = p2;
                }
            }
        }

        if (best_p_right.x != -1 && best_p_down.y != -1) {
            Point p4 = { best_p_right.x, best_p_down.y };
            int found_p4 = 0;
            for (int k = 0; k < intersections->count; ++k) {
                if (fabs(intersections->items[k].x - p4.x) < 0.1 && fabs(intersections->items[k].y - p4.y) < 0.1) {
                    found_p4 = 1;
                    break;
                }
            }
            if (found_p4) {
                fz_rect cell_bbox = { p1.x, p1.y, p4.x, p4.y };
                if (cell_bbox.x1 > cell_bbox.x0 && cell_bbox.y1 > cell_bbox.y0) {
                    add_to_cell_array(cells, cell_bbox);
                }
            }
        }
    }
}

TableArray* find_tables_with_mupdf_native(const char* pdf_path, int page_number) {
    fz_context* ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
    if (!ctx) {
        fprintf(stderr, "Failed to create MuPDF context.\n");
        return NULL;
    }

    fz_document* doc = NULL;
    fz_page* page = NULL;
    CaptureDevice* capture_dev = NULL;
    TableArray* tables = NULL;
    
    fz_try(ctx) {
        fz_register_document_handlers(ctx);
        doc = fz_open_document(ctx, pdf_path);
        page = fz_load_page(ctx, doc, page_number);
        
        // Use edge-based detection (matches table_ex.py approach)
        capture_dev = (CaptureDevice*)new_capture_device(ctx);
        fz_run_page(ctx, page, (fz_device*)capture_dev, fz_identity, NULL);
        fz_close_device(ctx, (fz_device*)capture_dev);
        
        // Separate edges by orientation
        EdgeArray h_edges, v_edges;
        init_edge_array(&h_edges);
        init_edge_array(&v_edges);
        
        for (int i = 0; i < capture_dev->edges.count; ++i) {
            if (capture_dev->edges.items[i].orientation == 'h') {
                add_to_edge_array(&h_edges, capture_dev->edges.items[i]);
            } else {
                add_to_edge_array(&v_edges, capture_dev->edges.items[i]);
            }
        }

        // Merge close edges
        merge_edges(&h_edges, SNAP_TOLERANCE, JOIN_TOLERANCE);
        merge_edges(&v_edges, SNAP_TOLERANCE, JOIN_TOLERANCE);

        // Find intersections
        PointArray intersections;
        init_point_array(&intersections);
        find_intersections(&v_edges, &h_edges, &intersections);

        // Find cells from intersections
        CellArray cells;
        init_cell_array(&cells);
        find_cells(&intersections, &cells);

        free_edge_array(&h_edges);
        free_edge_array(&v_edges);
        free_point_array(&intersections);

        // Group cells into tables
        tables = malloc(sizeof(TableArray));
        tables->count = 0;
        tables->tables = NULL;

        if (cells.count > 0) {
            tables->count = 1;
            tables->tables = malloc(sizeof(Table));
            Table* table = &tables->tables[0];
            table->bbox = fz_empty_rect;
            
            // Group cells into rows
            qsort(cells.items, cells.count, sizeof(fz_rect), compare_rects_lexicographically);

            int row_capacity = 16;
            table->rows = malloc(row_capacity * sizeof(TableRow));
            table->count = 0;

            int i = 0;
            while (i < cells.count) {
                double current_y0 = cells.items[i].y0;
                int j = i;
                while (j < cells.count && fabs(cells.items[j].y0 - current_y0) < 1.0) {
                    j++;
                }
                
                if (table->count >= row_capacity) {
                    row_capacity *= 2;
                    table->rows = realloc(table->rows, row_capacity * sizeof(TableRow));
                }

                TableRow* row = &table->rows[table->count++];
                row->count = j - i;
                row->cells = malloc(row->count * sizeof(TableCell));
                row->bbox = fz_empty_rect;

                for (int k = 0; k < row->count; ++k) {
                    row->cells[k].bbox = cells.items[i + k];
                    row->bbox = fz_union_rect(row->bbox, cells.items[i + k]);
                }
                table->bbox = fz_union_rect(table->bbox, row->bbox);
                i = j;
            }
        }

        free_cell_array(&cells);
    }
    fz_always(ctx) {
        if (capture_dev) fz_drop_device(ctx, (fz_device*)capture_dev);
        fz_drop_page(ctx, page);
        fz_drop_document(ctx, doc);
    }
    fz_catch(ctx) {
        fprintf(stderr, "Failed to detect tables: %s\n", fz_caught_message(ctx));
        if (tables) {
            free_table_array(tables);
            tables = NULL;
        }
    }
    
    fz_drop_context(ctx);
    return tables;
}

TableArray* find_tables_on_page(fz_context* ctx, fz_document* doc, int page_number, BlockArray* blocks) {
    (void)ctx;
    (void)blocks;
    
    // For now, we'll use a simple edge-based detection approach
    // Similar to find_tables_with_mupdf_native but within the existing context
    fz_page* page = NULL;
    CaptureDevice* capture_dev = NULL;
    TableArray* tables = NULL;
    
    fz_try(ctx) {
        page = fz_load_page(ctx, doc, page_number);
        
        // Use edge-based detection
        capture_dev = (CaptureDevice*)new_capture_device(ctx);
        fz_run_page(ctx, page, (fz_device*)capture_dev, fz_identity, NULL);
        fz_close_device(ctx, (fz_device*)capture_dev);
        
        // Separate edges by orientation
        EdgeArray h_edges, v_edges;
        init_edge_array(&h_edges);
        init_edge_array(&v_edges);
        
        for (int i = 0; i < capture_dev->edges.count; ++i) {
            if (capture_dev->edges.items[i].orientation == 'h') {
                add_to_edge_array(&h_edges, capture_dev->edges.items[i]);
            } else {
                add_to_edge_array(&v_edges, capture_dev->edges.items[i]);
            }
        }

        // Merge close edges
        merge_edges(&h_edges, SNAP_TOLERANCE, JOIN_TOLERANCE);
        merge_edges(&v_edges, SNAP_TOLERANCE, JOIN_TOLERANCE);

        // Find intersections
        PointArray intersections;
        init_point_array(&intersections);
        find_intersections(&v_edges, &h_edges, &intersections);

        // Find cells from intersections
        CellArray cells;
        init_cell_array(&cells);
        find_cells(&intersections, &cells);

        free_edge_array(&h_edges);
        free_edge_array(&v_edges);
        free_point_array(&intersections);

        // Group cells into tables
        tables = malloc(sizeof(TableArray));
        tables->count = 0;
        tables->tables = NULL;

        if (cells.count > 0) {
            tables->count = 1;
            tables->tables = malloc(sizeof(Table));
            Table* table = &tables->tables[0];
            table->bbox = fz_empty_rect;
            
            // Group cells into rows
            qsort(cells.items, cells.count, sizeof(fz_rect), compare_rects_lexicographically);

            int row_capacity = 16;
            table->rows = malloc(row_capacity * sizeof(TableRow));
            table->count = 0;

            int i = 0;
            while (i < cells.count) {
                double current_y0 = cells.items[i].y0;
                int j = i;
                while (j < cells.count && fabs(cells.items[j].y0 - current_y0) < 1.0) {
                    j++;
                }
                
                if (table->count >= row_capacity) {
                    row_capacity *= 2;
                    table->rows = realloc(table->rows, row_capacity * sizeof(TableRow));
                }

                TableRow* row = &table->rows[table->count++];
                row->count = j - i;
                row->cells = malloc(row->count * sizeof(TableCell));
                row->bbox = fz_empty_rect;

                for (int k = 0; k < row->count; ++k) {
                    row->cells[k].bbox = cells.items[i + k];
                    row->cells[k].text = NULL;  // Will be filled during extraction
                    row->bbox = fz_union_rect(row->bbox, cells.items[i + k]);
                }
                table->bbox = fz_union_rect(table->bbox, row->bbox);
                i = j;
            }
        }

        free_cell_array(&cells);
    }
    fz_always(ctx) {
        if (capture_dev) fz_drop_device(ctx, (fz_device*)capture_dev);
        if (page) fz_drop_page(ctx, page);
    }
    fz_catch(ctx) {
        if (tables) {
            free_table_array(tables);
            tables = NULL;
        }
    }
    
    return tables;
}

void free_table_array(TableArray* tables) {
    if (!tables) return;
    for (int i = 0; i < tables->count; ++i) {
        Table* table = &tables->tables[i];
        if (table->rows) {
            for (int j = 0; j < table->count; ++j) {
                if (table->rows[j].cells) {
                    for (int k = 0; k < table->rows[j].count; ++k) {
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
