#include "mupdf/fitz.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MAX_LINES 1024
#define MAX_ROWS 128
#define MAX_COLS 64
#define TOL 1.0f
#define MAX_TEXT_LEN 1024

typedef struct { float x0, y0, x1, y1; } Line;

typedef struct {
    Line horizontal[MAX_LINES];
    int h_count;
    Line vertical[MAX_LINES];
    int v_count;
} LineSet;

typedef struct {
    float x0, y0, x1, y1;
    char content[MAX_TEXT_LEN];
} Cell;

typedef struct {
    Cell cells[MAX_ROWS][MAX_COLS];
    int rows;
    int cols;
} Table;

// Helper functions
int cmpf(const void *a, const void *b) {
    float fa = *(float*)a, fb = *(float*)b;
    return (fa > fb) - (fa < fb);
}

int line_inside_bbox(Line l, fz_rect bbox) {
    return (l.x0 >= bbox.x0 && l.x1 <= bbox.x1 && l.y0 >= bbox.y0 && l.y1 <= bbox.y1);
}

int rects_intersect(fz_rect r1, Cell c) {
    return !(r1.x1 < c.x0 || r1.x0 > c.x1 || r1.y1 < c.y0 || r1.y0 > c.y1);
}

// Median gap for clustering
float compute_median_gap(float coords[], int count) {
    if (count < 2) return 10.0f;
    float gaps[MAX_LINES];
    for (int i = 1; i < count; i++) {
        gaps[i - 1] = coords[i] - coords[i - 1];
    }
    qsort(gaps, count - 1, sizeof(float), cmpf);
    return gaps[(count - 2) / 2];
}

// Cluster coordinates
int cluster_coords(float coords[], int count, float clustered[]) {
    if (count == 0) return 0;
    
    int c_count = 0;
    clustered[0] = coords[0];
    c_count = 1;
    
    for (int i = 1; i < count; i++) {
        float gap = coords[i] - clustered[c_count - 1];
        if (gap > TOL) {  // Only add if gap is significant
            clustered[c_count++] = coords[i];
        }
    }
    return c_count;
}

// Structure to hold path drawing state
typedef struct {
    fz_point current_point;
    LineSet *lines;
    fz_rect bbox;
} path_walker_state;

// Walker function for path items
static void
walk_path(fz_context *ctx, void *arg, fz_path_item cmd, fz_point *pts)
{
    path_walker_state *state = (path_walker_state *)arg;
    
    switch (cmd)
    {
    case FZ_PATH_MOVETO:
        state->current_point = pts[0];
        break;
        
    case FZ_PATH_LINETO:
        {
            Line l = {state->current_point.x, state->current_point.y, pts[0].x, pts[0].y};
            if (line_inside_bbox(l, state->bbox)) {
                // Check if it's horizontal or vertical line
                if (fabs(l.y1 - l.y0) < TOL && state->lines->h_count < MAX_LINES) {
                    state->lines->horizontal[state->lines->h_count++] = l;
                } else if (fabs(l.x1 - l.x0) < TOL && state->lines->v_count < MAX_LINES) {
                    state->lines->vertical[state->lines->v_count++] = l;
                }
            }
            state->current_point = pts[0];
        }
        break;
        
    case FZ_PATH_CURVETO:
        // For curves, we'll use the end point as current point
        state->current_point = pts[2];
        break;
        
    case FZ_PATH_CLOSEPATH:
        // Nothing special needed
        break;
    }
}

// Custom device to extract paths
typedef struct {
    fz_device super;
    LineSet *lines;
    fz_rect bbox;
} extract_device;

static void
extract_fill_path(fz_context *ctx, fz_device *dev, const fz_path *path, int even_odd, fz_matrix ctm,
    fz_colorspace *colorspace, const float *color, float alpha, fz_color_params color_params)
{
    // We don't need fill operations for line extraction
}

static void
extract_stroke_path(fz_context *ctx, fz_device *dev, const fz_path *path, const fz_stroke_state *stroke,
    fz_matrix ctm, fz_colorspace *colorspace, const float *color, float alpha, fz_color_params color_params)
{
    extract_device *edev = (extract_device *)dev;
    path_walker_state state;
    state.lines = edev->lines;
    state.bbox = edev->bbox;
    state.current_point = fz_make_point(0, 0);
    
    // Walk through the path and extract line segments
    fz_walk_path(ctx, path, walk_path, &state);
}

static fz_device *
new_extract_device(fz_context *ctx, LineSet *lines, fz_rect bbox)
{
    extract_device *dev = fz_new_derived_device(ctx, extract_device);
    
    dev->super.fill_path = extract_fill_path;
    dev->super.stroke_path = extract_stroke_path;
    dev->lines = lines;
    dev->bbox = bbox;
    
    return (fz_device *)dev;
}

// Extract text content and assign to cells
void extract_text_to_cells(fz_context *ctx, fz_page *page, Table *table) {
    fz_stext_page *text = fz_new_stext_page_from_page(ctx, page, NULL);
    
    // Initialize cell content
    for (int r = 0; r < table->rows - 1; r++) {
        for (int c = 0; c < table->cols - 1; c++) {
            table->cells[r][c].content[0] = '\0';
        }
    }
    
    // Walk through text blocks
    for (fz_stext_block *block = text->first_block; block; block = block->next) {
        if (block->type != FZ_STEXT_BLOCK_TEXT) continue;
        
        for (fz_stext_line *line = block->u.t.first_line; line; line = line->next) {
            for (fz_stext_char *ch = line->first_char; ch; ch = ch->next) {
                fz_rect char_bbox = fz_rect_from_quad(ch->quad);
                
                // Find which cell this character belongs to
                for (int r = 0; r < table->rows - 1; r++) {
                    for (int c = 0; c < table->cols - 1; c++) {
                        Cell *cell = &table->cells[r][c];
                        fz_rect cell_rect = fz_make_rect(cell->x0, cell->y0, cell->x1, cell->y1);
                        
                        // Expand cell slightly for tolerance
                        cell_rect = fz_expand_rect(cell_rect, TOL);
                        
                        if (fz_contains_rect(cell_rect, char_bbox)) {
                            // Add character to cell content
                            int len = strlen(cell->content);
                            if (len < MAX_TEXT_LEN - 2) {
                                char utf8[8];
                                int n = fz_runetochar(utf8, ch->c);
                                utf8[n] = '\0';
                                strcat(cell->content, utf8);
                            }
                        }
                    }
                }
            }
        }
    }
    
    fz_drop_stext_page(ctx, text);
}

// Extract table using custom device
Table extract_table(fz_context *ctx, fz_page *page, fz_rect bbox) {
    Table table = {0};
    LineSet lines = {0};

    // Extract lines using custom device
    fz_device *dev = new_extract_device(ctx, &lines, bbox);
    fz_run_page(page, dev, fz_identity, NULL);
    fz_drop_device(ctx, dev);

    // Add table boundary lines if not present
    if (lines.h_count == 0) {
        lines.horizontal[0] = (Line){bbox.x0, bbox.y0, bbox.x1, bbox.y0};
        lines.horizontal[1] = (Line){bbox.x0, bbox.y1, bbox.x1, bbox.y1};
        lines.h_count = 2;
    }
    if (lines.v_count == 0) {
        lines.vertical[0] = (Line){bbox.x0, bbox.y0, bbox.x0, bbox.y1};
        lines.vertical[1] = (Line){bbox.x1, bbox.y0, bbox.x1, bbox.y1};
        lines.v_count = 2;
    }

    // Sort and cluster coordinates
    float h_coords[MAX_LINES], v_coords[MAX_LINES];
    for (int i = 0; i < lines.h_count; i++) {
        h_coords[i] = lines.horizontal[i].y0;
    }
    for (int i = 0; i < lines.v_count; i++) {
        v_coords[i] = lines.vertical[i].x0;
    }
    
    qsort(h_coords, lines.h_count, sizeof(float), cmpf);
    qsort(v_coords, lines.v_count, sizeof(float), cmpf);

    // Remove duplicates and cluster
    table.rows = cluster_coords(h_coords, lines.h_count, h_coords);
    table.cols = cluster_coords(v_coords, lines.v_count, v_coords);

    // Build cell grid
    for (int r = 0; r < table.rows - 1; r++) {
        for (int c = 0; c < table.cols - 1; c++) {
            table.cells[r][c].x0 = v_coords[c];
            table.cells[r][c].x1 = v_coords[c + 1];
            table.cells[r][c].y0 = h_coords[r];
            table.cells[r][c].y1 = h_coords[r + 1];
            table.cells[r][c].content[0] = '\0';  // Initialize empty
        }
    }

    // Extract and assign text to cells
    extract_text_to_cells(ctx, page, &table);

    return table;
}

// Markdown output
void print_table_md(Table *table) {
    if (table->rows <= 1 || table->cols <= 1) {
        printf("Empty or invalid table\n");
        return;
    }
    
    // Header row
    printf("|");
    for (int c = 0; c < table->cols - 1; c++) {
        printf(" Col%d |", c + 1);
    }
    printf("\n");
    
    // Separator
    printf("|");
    for (int c = 0; c < table->cols - 1; c++) {
        printf(" --- |");
    }
    printf("\n");
    
    // Data rows
    for (int r = 0; r < table->rows - 1; r++) {
        printf("|");
        for (int c = 0; c < table->cols - 1; c++) {
            printf(" %s |", table->cells[r][c].content);
        }
        printf("\n");
    }
    printf("\n");
}

// Free table resources
void free_table(Table *table) {
    // In this version, we're using static arrays, so no dynamic freeing needed
    // But this function is here for future extensibility
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: %s file.pdf [page_num]\n", argv[0]);
        printf("  page_num: optional page number (0-based, default: process all pages)\n");
        return 1;
    }

    fz_context *ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
    if (!ctx) {
        fprintf(stderr, "Cannot create MuPDF context\n");
        return 1;
    }

    fz_try(ctx) {
        fz_register_document_handlers(ctx);
        fz_document *doc = fz_open_document(ctx, argv[1]);
        int page_count = fz_count_pages(ctx, doc);
        
        printf("Document: %s (%d pages)\n\n", argv[1], page_count);

        int start_page = 0, end_page = page_count;
        if (argc >= 3) {
            int target_page = atoi(argv[2]);
            if (target_page >= 0 && target_page < page_count) {
                start_page = target_page;
                end_page = target_page + 1;
            }
        }

        for (int p = start_page; p < end_page; p++) {
            printf("=== Page %d ===\n", p + 1);
            fz_page *page = fz_load_page(ctx, doc, p);
            fz_rect page_bounds = fz_bound_page(ctx, page);
            
            // Example: Extract table from entire page
            // In practice, you would provide specific table bounding boxes
            fz_rect table_bbox = page_bounds;
            
            printf("Extracting table from bbox: (%.1f, %.1f, %.1f, %.1f)\n", 
                   table_bbox.x0, table_bbox.y0, table_bbox.x1, table_bbox.y1);
            
            Table table = extract_table(ctx, page, table_bbox);
            
            if (table.rows > 1 && table.cols > 1) {
                printf("Found table: %d rows x %d columns\n\n", table.rows - 1, table.cols - 1);
                print_table_md(&table);
            } else {
                printf("No valid table found on this page\n\n");
            }
            
            free_table(&table);
            fz_drop_page(ctx, page);
        }

        fz_drop_document(ctx, doc);
    }
    fz_catch(ctx) {
        fprintf(stderr, "Error: %s\n", fz_caught_message(ctx));
        fz_drop_context(ctx);
        return 1;
    }

    fz_drop_context(ctx);
    return 0;
}