#include "mupdf/fitz.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include <ctype.h>

// Constants matching PyMuPDF version
#define DEFAULT_SNAP_TOLERANCE 3
#define DEFAULT_JOIN_TOLERANCE 3
#define DEFAULT_MIN_WORDS_VERTICAL 3
#define DEFAULT_MIN_WORDS_HORIZONTAL 1
#define DEFAULT_X_TOLERANCE 3
#define DEFAULT_Y_TOLERANCE 3
#define DEFAULT_X_DENSITY 7.25
#define DEFAULT_Y_DENSITY 13

#define UNSET_FLOAT -9999.0f
#define MAX_CELLS 10000
#define MAX_EDGES 10000
#define MAX_CHARS 50000
#define MAX_WORDS 10000
#define MAX_TABLES 100
#define MAX_ROWS 1000
#define MAX_COLS 100
#define MAX_INTERSECTIONS 1000
#define MAX_TEXT_LENGTH 10000
#define MAX_SPANS 1000
#define MAX_PATHS 1000

typedef struct {
    float x0, y0, x1, y1;
} bbox_t;

typedef struct {
    float x, y;
} point_t;

typedef struct {
    float a, b, c, d, e, f;
} matrix_t;

typedef struct {
    char text[256];
    bbox_t bbox;
    float doctop;
    char fontname[256];
    float fontsize;
    int upright;
    float width, height;
    float adv;
    matrix_t matrix;
    int page_number;
    float size;
    float color[3];
} char_info_t;

typedef struct {
    char_info_t *chars;
    int count;
    int capacity;
} char_list_t;

typedef struct {
    char text[1024];
    bbox_t bbox;
    float doctop;
    int upright;
    int direction;
    int rotation;
} word_info_t;

typedef struct {
    word_info_t *words;
    int count;
    int capacity;
} word_list_t;

typedef struct {
    bbox_t bbox;
    char orientation; // 'h' or 'v'
    float width, height;
    float top, bottom;
    char object_type[32];
    int page_number;
} edge_info_t;

typedef struct {
    edge_info_t *edges;
    int count;
    int capacity;
} edge_list_t;

typedef struct {
    point_t point;
    edge_list_t v_edges;
    edge_list_t h_edges;
} intersection_t;

typedef struct {
    intersection_t *intersections;
    int count;
    int capacity;
} intersection_list_t;

typedef struct {
    bbox_t *cells;
    int count;
    int capacity;
} cell_list_t;

typedef struct {
    bbox_t bbox;
    bbox_t *cells;
    char **names;
    int col_count;
    bool external;
} table_header_t;

typedef struct {
    bbox_t *cells;
    int count;
} table_row_t;

typedef struct {
    bbox_t bbox;
    cell_list_t cells;
    table_header_t header;
    table_row_t *rows;
    int row_count;
    int col_count;
} table_t;

typedef struct {
    char vertical_strategy[32];
    char horizontal_strategy[32];
    float snap_tolerance;
    float snap_x_tolerance;
    float snap_y_tolerance;
    float join_tolerance;
    float join_x_tolerance;
    float join_y_tolerance;
    float edge_min_length;
    float min_words_vertical;
    float min_words_horizontal;
    float intersection_tolerance;
    float intersection_x_tolerance;
    float intersection_y_tolerance;
    float text_x_tolerance;
    float text_y_tolerance;
} table_settings_t;

typedef struct {
    table_t *tables;
    int count;
    int capacity;
    edge_list_t edges;
    intersection_list_t intersections;
    cell_list_t cells;
} table_finder_t;

// Global variables for character and edge data
static char_list_t g_chars = {0};
static edge_list_t g_edges = {0};
static fz_stext_page *g_textpage = NULL;

// Utility functions
static float min_f(float a, float b) { return a < b ? a : b; }
static float max_f(float a, float b) { return a > b ? a : b; }
static int min_i(int a, int b) { return a < b ? a : b; }
static int max_i(int a, int b) { return a > b ? a : b; }

static bool float_equal(float a, float b, float tolerance) {
    return fabs(a - b) <= tolerance;
}

static bool is_unset(float val) {
    return val == UNSET_FLOAT;
}

// Bbox operations
static bbox_t bbox_union(bbox_t a, bbox_t b) {
    bbox_t result;
    result.x0 = min_f(a.x0, b.x0);
    result.y0 = min_f(a.y0, b.y0);
    result.x1 = max_f(a.x1, b.x1);
    result.y1 = max_f(a.y1, b.y1);
    return result;
}

static bbox_t objects_to_bbox(bbox_t *objects, int count) {
    if (count == 0) return (bbox_t){0, 0, 0, 0};
    
    bbox_t result = objects[0];
    for (int i = 1; i < count; i++) {
        result = bbox_union(result, objects[i]);
    }
    return result;
}

static float bbox_area(bbox_t bbox) {
    return (bbox.x1 - bbox.x0) * (bbox.y1 - bbox.y0);
}

static bbox_t bbox_intersection(bbox_t a, bbox_t b) {
    bbox_t result;
    result.x0 = max_f(a.x0, b.x0);
    result.y0 = max_f(a.y0, b.y0);
    result.x1 = min_f(a.x1, b.x1);
    result.y1 = min_f(a.y1, b.y1);
    
    if (result.x0 >= result.x1 || result.y0 >= result.y1) {
        return (bbox_t){0, 0, 0, 0}; // Empty intersection
    }
    return result;
}

// String utilities
static bool is_whitespace_only(const char *str) {
    while (*str) {
        if (!isspace(*str)) return false;
        str++;
    }
    return true;
}

static void trim_string(char *str) {
    char *start = str;
    char *end;
    
    // Trim leading space
    while (isspace(*start)) start++;
    
    // Trim trailing space
    end = start + strlen(start) - 1;
    while (end > start && isspace(*end)) end--;
    
    // Write new null terminator
    end[1] = '\0';
    
    // Move string if needed
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
}

// Dynamic array utilities
static void init_char_list(char_list_t *list, int capacity) {
    list->chars = malloc(capacity * sizeof(char_info_t));
    list->count = 0;
    list->capacity = capacity;
}

static void add_char(char_list_t *list, const char_info_t *ch) {
    if (list->count >= list->capacity) {
        list->capacity *= 2;
        list->chars = realloc(list->chars, list->capacity * sizeof(char_info_t));
    }
    list->chars[list->count++] = *ch;
}

static void init_word_list(word_list_t *list, int capacity) {
    list->words = malloc(capacity * sizeof(word_info_t));
    list->count = 0;
    list->capacity = capacity;
}

static void add_word(word_list_t *list, const word_info_t *word) {
    if (list->count >= list->capacity) {
        list->capacity *= 2;
        list->words = realloc(list->words, list->capacity * sizeof(word_info_t));
    }
    list->words[list->count++] = *word;
}

static void init_edge_list(edge_list_t *list, int capacity) {
    list->edges = malloc(capacity * sizeof(edge_info_t));
    list->count = 0;
    list->capacity = capacity;
}

static void add_edge(edge_list_t *list, const edge_info_t *edge) {
    if (list->count >= list->capacity) {
        list->capacity *= 2;
        list->edges = realloc(list->edges, list->capacity * sizeof(edge_info_t));
    }
    list->edges[list->count++] = *edge;
}

static void init_cell_list(cell_list_t *list, int capacity) {
    list->cells = malloc(capacity * sizeof(bbox_t));
    list->count = 0;
    list->capacity = capacity;
}

static void add_cell(cell_list_t *list, const bbox_t *cell) {
    if (list->count >= list->capacity) {
        list->capacity *= 2;
        list->cells = realloc(list->cells, list->capacity * sizeof(bbox_t));
    }
    list->cells[list->count++] = *cell;
}

// Clustering functions
typedef struct {
    float *values;
    int *cluster_ids;
    int count;
} cluster_result_t;

static cluster_result_t cluster_values(float *values, int count, float tolerance) {
    cluster_result_t result;
    result.values = malloc(count * sizeof(float));
    result.cluster_ids = malloc(count * sizeof(int));
    result.count = count;
    
    // Copy and sort values
    for (int i = 0; i < count; i++) {
        result.values[i] = values[i];
    }
    
    // Simple bubble sort for values
    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - i - 1; j++) {
            if (result.values[j] > result.values[j + 1]) {
                float temp = result.values[j];
                result.values[j] = result.values[j + 1];
                result.values[j + 1] = temp;
            }
        }
    }
    
    // Assign cluster IDs
    int cluster_id = 0;
    for (int i = 0; i < count; i++) {
        if (i == 0 || result.values[i] - result.values[i-1] > tolerance) {
            cluster_id++;
        }
        
        // Find original index
        for (int j = 0; j < count; j++) {
            if (float_equal(values[j], result.values[i], 0.001f)) {
                result.cluster_ids[j] = cluster_id;
                break;
            }
        }
    }
    
    return result;
}

// Text extraction from cells
static char* extract_cell_text(fz_context *ctx, fz_stext_page *page, bbox_t cell, bool markdown) {
    char *text = malloc(MAX_TEXT_LENGTH);
    text[0] = '\0';
    
    fz_stext_block *block;
    for (block = page->first_block; block; block = block->next) {
        if (block->type != FZ_STEXT_BLOCK_TEXT) continue;
        
        fz_rect block_bbox = block->bbox;
        
        // Check if block intersects with cell
        if (block_bbox.x0 > cell.x1 || block_bbox.x1 < cell.x0 ||
            block_bbox.y0 > cell.y1 || block_bbox.y1 < cell.y0) {
            continue;
        }
        
        fz_stext_line *line;
        for (line = block->u.t.first_line; line; line = line->next) {
            fz_rect line_bbox = line->bbox;
            
            // Check if line intersects with cell
            if (line_bbox.x0 > cell.x1 || line_bbox.x1 < cell.x0 ||
                line_bbox.y0 > cell.y1 || line_bbox.y1 < cell.y0) {
                continue;
            }
            
            if (strlen(text) > 0) {
                if (markdown) {
                    strcat(text, "<br>");
                } else {
                    strcat(text, "\n");
                }
            }
            
            fz_stext_char *ch;
            for (ch = line->first_char; ch; ch = ch->next) {
                fz_rect char_bbox = fz_rect_from_quad(ch->quad);
                
                // Check if character has >50% overlap with cell
                fz_rect intersection = fz_intersect_rect(char_bbox, 
                    fz_make_rect(cell.x0, cell.y0, cell.x1, cell.y1));
                float char_area = (char_bbox.x1 - char_bbox.x0) * (char_bbox.y1 - char_bbox.y0);
                float intersect_area = (intersection.x1 - intersection.x0) * 
                                     (intersection.y1 - intersection.y0);
                
                if (intersect_area > 0.5f * char_area) {
                    char utf8[10];
                    int len = fz_runetochar(utf8, ch->c);
                    utf8[len] = '\0';
                    strcat(text, utf8);
                }
            }
        }
    }
    
    // Trim the result
    trim_string(text);
    return text;
}

// Character extraction from page
static void make_chars(fz_context *ctx, fz_page *page, fz_rect clip) {
    g_chars.count = 0;
    if (!g_chars.chars) {
        init_char_list(&g_chars, MAX_CHARS);
    }
    
    fz_stext_options opts = {0};
    opts.flags = FZ_STEXT_PRESERVE_SPANS | FZ_STEXT_PRESERVE_WHITESPACE;
    
    g_textpage = fz_new_stext_page_from_page(ctx, page, &opts);
    
    fz_rect page_rect = fz_bound_page(ctx, page);
    float page_height = page_rect.y1 - page_rect.y0;
    int page_number = 1; // Simplified for this example
    
    fz_stext_block *block;
    for (block = g_textpage->first_block; block; block = block->next) {
        if (block->type != FZ_STEXT_BLOCK_TEXT) continue;
        
        fz_stext_line *line;
        for (line = block->u.t.first_line; line; line = line->next) {
            fz_stext_char *ch;
            for (ch = line->first_char; ch; ch = ch->next) {
                char_info_t char_info = {0};
                
                fz_rect char_rect = fz_rect_from_quad(ch->quad);
                char_info.bbox = (bbox_t){char_rect.x0, char_rect.y0, 
                                         char_rect.x1, char_rect.y1};
                
                char_info.doctop = char_rect.y0;
                
                // Convert Unicode to UTF-8
                int len = fz_runetochar(char_info.text, ch->c);
                char_info.text[len] = '\0';
                
                // Font information
                if (ch->font && ch->font->name) {
                    strncpy(char_info.fontname, ch->font->name, sizeof(char_info.fontname) - 1);
                }
                char_info.fontsize = ch->size;
                char_info.size = ch->size;
                
                // Calculate upright property
                char_info.upright = (fabs(line->wmode) < 0.1f);
                
                char_info.width = char_rect.x1 - char_rect.x0;
                char_info.height = char_rect.y1 - char_rect.y0;
                char_info.page_number = page_number;
                
                // Color (simplified)
                char_info.color[0] = 0; // RGB values would go here
                char_info.color[1] = 0;
                char_info.color[2] = 0;
                
                add_char(&g_chars, &char_info);
            }
        }
    }
}

// Word extraction
static void extract_words_from_chars(word_list_t *words, const char_list_t *chars, 
                                    float x_tolerance, float y_tolerance) {
    if (chars->count == 0) return;
    
    words->count = 0;
    if (!words->words) {
        init_word_list(words, MAX_WORDS);
    }
    
    char current_word[1024] = {0};
    bbox_t word_bbox = {0};
    float word_doctop = 0;
    int word_upright = 1;
    int chars_in_word = 0;
    
    for (int i = 0; i < chars->count; i++) {
        const char_info_t *ch = &chars->chars[i];
        
        bool start_new_word = false;
        
        if (chars_in_word == 0) {
            start_new_word = false; // First character
        } else {
            const char_info_t *prev_ch = &chars->chars[i - 1];
            
            // Check if we should start a new word
            if (ch->upright) {
                // Horizontal text
                float intra_line_dist = ch->bbox.x0 - prev_ch->bbox.x1;
                float inter_line_dist = fabs(ch->bbox.y0 - prev_ch->bbox.y0);
                
                if (intra_line_dist > x_tolerance || inter_line_dist > y_tolerance) {
                    start_new_word = true;
                }
            } else {
                // Vertical text
                float intra_line_dist = ch->bbox.y0 - prev_ch->bbox.y1;
                float inter_line_dist = fabs(ch->bbox.x0 - prev_ch->bbox.x0);
                
                if (intra_line_dist > y_tolerance || inter_line_dist > x_tolerance) {
                    start_new_word = true;
                }
            }
            
            // Check for whitespace
            if (is_whitespace_only(ch->text)) {
                start_new_word = true;
            }
        }
        
        if (start_new_word && chars_in_word > 0) {
            // Finish current word
            word_info_t word = {0};
            strncpy(word.text, current_word, sizeof(word.text) - 1);
            word.bbox = word_bbox;
            word.doctop = word_doctop;
            word.upright = word_upright;
            word.direction = 1; // Simplified
            word.rotation = 0; // Simplified
            
            add_word(words, &word);
            
            // Reset for new word
            current_word[0] = '\0';
            chars_in_word = 0;
        }
        
        if (!is_whitespace_only(ch->text)) {
            // Add character to current word
            if (chars_in_word == 0) {
                word_bbox = ch->bbox;
                word_doctop = ch->doctop;
                word_upright = ch->upright;
            } else {
                word_bbox = bbox_union(word_bbox, ch->bbox);
            }
            
            strcat(current_word, ch->text);
            chars_in_word++;
        }
    }
    
    // Add final word if exists
    if (chars_in_word > 0) {
        word_info_t word = {0};
        strncpy(word.text, current_word, sizeof(word.text) - 1);
        word.bbox = word_bbox;
        word.doctop = word_doctop;
        word.upright = word_upright;
        word.direction = 1;
        word.rotation = 0;
        
        add_word(words, &word);
    }
}

// Edge creation from lines
static edge_info_t line_to_edge(bbox_t line_bbox, char orientation) {
    edge_info_t edge = {0};
    edge.bbox = line_bbox;
    edge.orientation = orientation;
    edge.width = line_bbox.x1 - line_bbox.x0;
    edge.height = line_bbox.y1 - line_bbox.y0;
    edge.top = line_bbox.y0;
    edge.bottom = line_bbox.y1;
    strcpy(edge.object_type, "line");
    edge.page_number = 1;
    return edge;
}

// Words to horizontal edges
static void words_to_edges_h(edge_list_t *edges, const word_list_t *words, int word_threshold) {
    if (words->count < word_threshold) return;
    
    // Cluster words by top coordinate
    float *tops = malloc(words->count * sizeof(float));
    for (int i = 0; i < words->count; i++) {
        tops[i] = words->words[i].bbox.y0;
    }
    
    cluster_result_t clusters = cluster_values(tops, words->count, 1.0f);
    
    // Find large clusters
    int *cluster_counts = calloc(words->count + 1, sizeof(int));
    for (int i = 0; i < words->count; i++) {
        cluster_counts[clusters.cluster_ids[i]]++;
    }
    
    // Process large clusters
    for (int cluster_id = 1; cluster_id <= words->count; cluster_id++) {
        if (cluster_counts[cluster_id] >= word_threshold) {
            // Find words in this cluster
            bbox_t cluster_bbox = {INFINITY, INFINITY, -INFINITY, -INFINITY};
            for (int i = 0; i < words->count; i++) {
                if (clusters.cluster_ids[i] == cluster_id) {
                    cluster_bbox = bbox_union(cluster_bbox, words->words[i].bbox);
                }
            }
            
            // Create horizontal edges
            edge_info_t top_edge = line_to_edge(
                (bbox_t){cluster_bbox.x0, cluster_bbox.y0, cluster_bbox.x1, cluster_bbox.y0}, 'h');
            edge_info_t bottom_edge = line_to_edge(
                (bbox_t){cluster_bbox.x0, cluster_bbox.y1, cluster_bbox.x1, cluster_bbox.y1}, 'h');
            
            add_edge(edges, &top_edge);
            add_edge(edges, &bottom_edge);
        }
    }
    
    free(tops);
    free(clusters.values);
    free(clusters.cluster_ids);
    free(cluster_counts);
}

// Words to vertical edges
static void words_to_edges_v(edge_list_t *edges, const word_list_t *words, int word_threshold) {
    if (words->count < word_threshold) return;
    
    // Cluster words by left, right, and center coordinates
    float *coords = malloc(words->count * 3 * sizeof(float));
    int coord_count = 0;
    
    for (int i = 0; i < words->count; i++) {
        coords[coord_count++] = words->words[i].bbox.x0; // left
        coords[coord_count++] = words->words[i].bbox.x1; // right
        coords[coord_count++] = (words->words[i].bbox.x0 + words->words[i].bbox.x1) / 2.0f; // center
    }
    
    cluster_result_t clusters = cluster_values(coords, coord_count, 1.0f);
    
    // Find large clusters
    int *cluster_counts = calloc(coord_count + 1, sizeof(int));
    for (int i = 0; i < coord_count; i++) {
        cluster_counts[clusters.cluster_ids[i]]++;
    }
    
    // Process large clusters
    for (int cluster_id = 1; cluster_id <= coord_count; cluster_id++) {
        if (cluster_counts[cluster_id] >= word_threshold) {
            // Find the x-coordinate for this cluster
            float x_coord = 0;
            int count = 0;
            for (int i = 0; i < coord_count; i++) {
                if (clusters.cluster_ids[i] == cluster_id) {
                    x_coord += coords[i];
                    count++;
                }
            }
            x_coord /= count;
            
            // Find bbox of all words that contribute to this cluster
            bbox_t cluster_bbox = {INFINITY, INFINITY, -INFINITY, -INFINITY};
            for (int i = 0; i < words->count; i++) {
                bool word_matches = false;
                float word_coords[3] = {
                    words->words[i].bbox.x0,
                    words->words[i].bbox.x1,
                    (words->words[i].bbox.x0 + words->words[i].bbox.x1) / 2.0f
                };
                
                for (int j = 0; j < 3; j++) {
                    if (float_equal(word_coords[j], x_coord, 2.0f)) {
                        word_matches = true;
                        break;
                    }
                }
                
                if (word_matches) {
                    cluster_bbox = bbox_union(cluster_bbox, words->words[i].bbox);
                }
            }
            
            // Create vertical edge
            edge_info_t edge = line_to_edge(
                (bbox_t){x_coord, cluster_bbox.y0, x_coord, cluster_bbox.y1}, 'v');
            add_edge(edges, &edge);
        }
    }
    
    free(coords);
    free(clusters.values);
    free(clusters.cluster_ids);
    free(cluster_counts);
}

// Edge processing
static void snap_edges(edge_list_t *edges, float x_tolerance, float y_tolerance) {
    // Group vertical edges by x-coordinate and horizontal edges by y-coordinate
    for (int i = 0; i < edges->count; i++) {
        for (int j = i + 1; j < edges->count; j++) {
            edge_info_t *e1 = &edges->edges[i];
            edge_info_t *e2 = &edges->edges[j];
            
            if (e1->orientation == e2->orientation) {
                if (e1->orientation == 'v' && float_equal(e1->bbox.x0, e2->bbox.x0, x_tolerance)) {
                    // Snap vertical edges
                    float avg_x = (e1->bbox.x0 + e2->bbox.x0) / 2.0f;
                    e1->bbox.x0 = e1->bbox.x1 = avg_x;
                    e2->bbox.x0 = e2->bbox.x1 = avg_x;
                } else if (e1->orientation == 'h' && float_equal(e1->bbox.y0, e2->bbox.y0, y_tolerance)) {
                    // Snap horizontal edges
                    float avg_y = (e1->bbox.y0 + e2->bbox.y0) / 2.0f;
                    e1->bbox.y0 = e1->bbox.y1 = avg_y;
                    e2->bbox.y0 = e2->bbox.y1 = avg_y;
                }
            }
        }
    }
}

static void join_edges(edge_list_t *edges, float x_tolerance, float y_tolerance) {
    // Join edges that are close together along the same line
    for (int i = edges->count - 1; i >= 0; i--) {
        for (int j = i - 1; j >= 0; j--) {
            edge_info_t *e1 = &edges->edges[i];
            edge_info_t *e2 = &edges->edges[j];
            
            if (e1->orientation != e2->orientation) continue;
            
            bool can_join = false;
            if (e1->orientation == 'h') {
                // Horizontal edges on same line
                if (float_equal(e1->bbox.y0, e2->bbox.y0, y_tolerance)) {
                    float gap = max_f(e1->bbox.x0, e2->bbox.x0) - min_f(e1->bbox.x1, e2->bbox.x1);
                    if (gap <= x_tolerance) {
                        can_join = true;
                    }
                }
            } else {
                // Vertical edges on same line
                if (float_equal(e1->bbox.x0, e2->bbox.x0, x_tolerance)) {
                    float gap = max_f(e1->bbox.y0, e2->bbox.y0) - min_f(e1->bbox.y1, e2->bbox.y1);
                    if (gap <= y_tolerance) {
                        can_join = true;
                    }
                }
            }
            
            if (can_join) {
                // Extend e2 to include e1
                e2->bbox.x0 = min_f(e1->bbox.x0, e2->bbox.x0);
                e2->bbox.y0 = min_f(e1->bbox.y0, e2->bbox.y0);
                e2->bbox.x1 = max_f(e1->bbox.x1, e2->bbox.x1);
                e2->bbox.y1 = max_f(e1->bbox.y1, e2->bbox.y1);
                e2->width = e2->bbox.x1 - e2->bbox.x0;
                e2->height = e2->bbox.y1 - e2->bbox.y0;
                
                // Remove e1
                for (int k = i; k < edges->count - 1; k++) {
                    edges->edges[k] = edges->edges[k + 1];
                }
                edges->count--;
                break;
            }
        }
    }
}

static void filter_edges(edge_list_t *edges, float min_length) {
    int write_pos = 0;
    for (int i = 0; i < edges->count; i++) {
        edge_info_t *edge = &edges->edges[i];
        float length = (edge->orientation == 'h') ? edge->width : edge->height;
        if (length >= min_length) {
            if (write_pos != i) {
                edges->edges[write_pos] = edges->edges[i];
            }
            write_pos++;
        }
    }
    edges->count = write_pos;
}

// Intersection finding
static void edges_to_intersections(intersection_list_t *intersections, const edge_list_t *edges,
                                  float x_tolerance, float y_tolerance) {
    intersections->count = 0;
    if (!intersections->intersections) {
        intersections->intersections = malloc(MAX_INTERSECTIONS * sizeof(intersection_t));
        intersections->capacity = MAX_INTERSECTIONS;
    }
    
    // Separate vertical and horizontal edges
    edge_list_t v_edges = {0}, h_edges = {0};
    init_edge_list(&v_edges, edges->count);
    init_edge_list(&h_edges, edges->count);
    
    for (int i = 0; i < edges->count; i++) {
        if (edges->edges[i].orientation == 'v') {
            add_edge(&v_edges, &edges->edges[i]);
        } else {
            add_edge(&h_edges, &edges->edges[i]);
        }
    }
    
    // Find intersections
    for (int v = 0; v < v_edges.count; v++) {
        for (int h = 0; h < h_edges.count; h++) {
            edge_info_t *v_edge = &v_edges.edges[v];
            edge_info_t *h_edge = &h_edges.edges[h];
            
            // Check if they intersect
            if ((v_edge->top <= (h_edge->top + y_tolerance)) &&
                (v_edge->bottom >= (h_edge->top - y_tolerance)) &&
                (v_edge->bbox.x0 >= (h_edge->bbox.x0 - x_tolerance)) &&
                (v_edge->bbox.x0 <= (h_edge->bbox.x1 + x_tolerance))) {
                
                intersection_t intersection = {0};
                intersection.point = (point_t){v_edge->bbox.x0, h_edge->top};
                
                // Initialize edge lists for intersection
                init_edge_list(&intersection.v_edges, 10);
                init_edge_list(&intersection.h_edges, 10);
                add_edge(&intersection.v_edges, v_edge);
                add_edge(&intersection.h_edges, h_edge);
                
                // Check if this intersection already exists
                bool exists = false;
                for (int i = 0; i < intersections->count; i++) {
                    if (float_equal(intersections->intersections[i].point.x, intersection.point.x, 0.1f) &&
                        float_equal(intersections->intersections[i].point.y, intersection.point.y, 0.1f)) {
                        // Add edges to existing intersection
                        add_edge(&intersections->intersections[i].v_edges, v_edge);
                        add_edge(&intersections->intersections[i].h_edges, h_edge);
                        exists = true;
                        break;
                    }
                }
                
                if (!exists && intersections->count < intersections->capacity) {
                    intersections->intersections[intersections->count++] = intersection;
                }
            }
        }
    }
    
    free(v_edges.edges);
    free(h_edges.edges);
}

// Cell finding from intersections
static void intersections_to_cells(cell_list_t *cells, const intersection_list_t *intersections) {
    cells->count = 0;
    if (!cells->cells) {
        init_cell_list(cells, MAX_CELLS);
    }
    
    // Create array of intersection points for easier processing
    point_t *points = malloc(intersections->count * sizeof(point_t));
    for (int i = 0; i < intersections->count; i++) {
        points[i] = intersections->intersections[i].point;
    }
    
    // Sort points by y, then x
    for (int i = 0; i < intersections->count - 1; i++) {
        for (int j = 0; j < intersections->count - i - 1; j++) {
            if (points[j].y > points[j + 1].y || 
                (float_equal(points[j].y, points[j + 1].y, 0.1f) && points[j].x > points[j + 1].x)) {
                point_t temp = points[j];
                points[j] = points[j + 1];
                points[j + 1] = temp;
            }
        }
    }
    
    // Find rectangular cells
    for (int i = 0; i < intersections->count; i++) {
        point_t top_left = points[i];
        
        // Find points directly below and directly right
        for (int j = i + 1; j < intersections->count; j++) {
            point_t below_pt = points[j];
            if (!float_equal(below_pt.x, top_left.x, 0.1f)) continue;
            
            for (int k = i + 1; k < intersections->count; k++) {
                point_t right_pt = points[k];
                if (!float_equal(right_pt.y, top_left.y, 0.1f)) continue;
                
                // Look for bottom-right corner
                point_t bottom_right = {right_pt.x, below_pt.y};
                bool found_corner = false;
                
                for (int l = 0; l < intersections->count; l++) {
                    if (float_equal(points[l].x, bottom_right.x, 0.1f) &&
                        float_equal(points[l].y, bottom_right.y, 0.1f)) {
                        found_corner = true;
                        break;
                    }
                }
                
                if (found_corner) {
                    bbox_t cell = {top_left.x, top_left.y, bottom_right.x, bottom_right.y};
                    add_cell(cells, &cell);
                }
            }
        }
    }
    
    free(points);
}

// Table grouping from cells
static int cells_to_tables(table_t *tables, fz_context *ctx, fz_page *page, const cell_list_t *cells) {
    if (cells->count == 0) return 0;
    
    bool *used = calloc(cells->count, sizeof(bool));
    int table_count = 0;
    
    for (int i = 0; i < cells->count && table_count < MAX_TABLES; i++) {
        if (used[i]) continue;
        
        // Start a new table
        cell_list_t table_cells = {0};
        init_cell_list(&table_cells, cells->count);
        add_cell(&table_cells, &cells->cells[i]);
        used[i] = true;
        
        // Find connected cells
        bool found_new = true;
        while (found_new) {
            found_new = false;
            for (int j = 0; j < cells->count; j++) {
                if (used[j]) continue;
                
                // Check if this cell shares corners with any cell in current table
                bool shares_corner = false;
                for (int k = 0; k < table_cells.count; k++) {
                    bbox_t cell1 = cells->cells[j];
                    bbox_t cell2 = table_cells.cells[k];
                    
                    // Check if they share any corner points
                    point_t corners1[4] = {
                        {cell1.x0, cell1.y0}, {cell1.x1, cell1.y0},
                        {cell1.x0, cell1.y1}, {cell1.x1, cell1.y1}
                    };
                    point_t corners2[4] = {
                        {cell2.x0, cell2.y0}, {cell2.x1, cell2.y0},
                        {cell2.x0, cell2.y1}, {cell2.x1, cell2.y1}
                    };
                    
                    for (int c1 = 0; c1 < 4 && !shares_corner; c1++) {
                        for (int c2 = 0; c2 < 4 && !shares_corner; c2++) {
                            if (float_equal(corners1[c1].x, corners2[c2].x, 0.1f) &&
                                float_equal(corners1[c1].y, corners2[c2].y, 0.1f)) {
                                shares_corner = true;
                            }
                        }
                    }
                }
                
                if (shares_corner) {
                    add_cell(&table_cells, &cells->cells[j]);
                    used[j] = true;
                    found_new = true;
                }
            }
        }
        
        // Create table if it has sufficient cells
        if (table_cells.count > 1) {
            table_t *table = &tables[table_count++];
            table->cells = table_cells;
            table->bbox = objects_to_bbox(table_cells.cells, table_cells.count);
            
            // Create rows by clustering cells by y-coordinate
            float *y_coords = malloc(table_cells.count * sizeof(float));
            for (int j = 0; j < table_cells.count; j++) {
                y_coords[j] = table_cells.cells[j].y0;
            }
            
            cluster_result_t y_clusters = cluster_values(y_coords, table_cells.count, 1.0f);
            
            // Count unique y-clusters
            int max_cluster = 0;
            for (int j = 0; j < table_cells.count; j++) {
                if (y_clusters.cluster_ids[j] > max_cluster) {
                    max_cluster = y_clusters.cluster_ids[j];
                }
            }
            
            table->row_count = max_cluster;
            table->rows = malloc(table->row_count * sizeof(table_row_t));
            
            // Fill rows
            for (int row_id = 1; row_id <= max_cluster; row_id++) {
                table_row_t *row = &table->rows[row_id - 1];
                row->count = 0;
                
                // Count cells in this row
                for (int j = 0; j < table_cells.count; j++) {
                    if (y_clusters.cluster_ids[j] == row_id) {
                        row->count++;
                    }
                }
                
                row->cells = malloc(row->count * sizeof(bbox_t));
                int cell_idx = 0;
                for (int j = 0; j < table_cells.count; j++) {
                    if (y_clusters.cluster_ids[j] == row_id) {
                        row->cells[cell_idx++] = table_cells.cells[j];
                    }
                }
                
                // Sort cells in row by x-coordinate
                for (int a = 0; a < row->count - 1; a++) {
                    for (int b = 0; b < row->count - a - 1; b++) {
                        if (row->cells[b].x0 > row->cells[b + 1].x0) {
                            bbox_t temp = row->cells[b];
                            row->cells[b] = row->cells[b + 1];
                            row->cells[b + 1] = temp;
                        }
                    }
                }
            }
            
            // Calculate column count
            table->col_count = 0;
            for (int j = 0; j < table->row_count; j++) {
                if (table->rows[j].count > table->col_count) {
                    table->col_count = table->rows[j].count;
                }
            }
            
            free(y_coords);
            free(y_clusters.values);
            free(y_clusters.cluster_ids);
        } else {
            free(table_cells.cells);
        }
    }
    
    free(used);
    return table_count;
}

// Header detection
static table_header_t get_table_header(fz_context *ctx, fz_page *page, const table_t *table) {
    table_header_t header = {0};
    
    if (table->row_count == 0) return header;
    
    // For simplicity, assume first row is header
    const table_row_t *first_row = &table->rows[0];
    
    header.bbox = objects_to_bbox(first_row->cells, first_row->count);
    header.col_count = first_row->count;
    header.cells = malloc(header.col_count * sizeof(bbox_t));
    header.names = malloc(header.col_count * sizeof(char*));
    header.external = false; // Simplified - assume header is internal
    
    for (int i = 0; i < header.col_count; i++) {
        header.cells[i] = first_row->cells[i];
        header.names[i] = extract_cell_text(ctx, g_textpage, first_row->cells[i], false);
        
        // Clean up the header name
        if (header.names[i]) {
            trim_string(header.names[i]);
            if (strlen(header.names[i]) == 0) {
                free(header.names[i]);
                header.names[i] = malloc(20);
                snprintf(header.names[i], 20, "Col%d", i + 1);
            }
        } else {
            header.names[i] = malloc(20);
            snprintf(header.names[i], 20, "Col%d", i + 1);
        }
    }
    
    return header;
}

// Edge creation from vector graphics - simplified text-based approach
static void make_edges_from_paths(fz_context *ctx, fz_page *page, edge_list_t *edges,
                                 const table_settings_t *settings, fz_rect clip) {
    // For now, focus on text-based edge extraction since vector graphics API is complex
    // This should work for most tables that have clear text alignment
    
    if (strcmp(settings->horizontal_strategy, "text") == 0 || 
        strcmp(settings->vertical_strategy, "text") == 0) {
        word_list_t words = {0};
        extract_words_from_chars(&words, &g_chars, settings->text_x_tolerance, settings->text_y_tolerance);
        
        if (strcmp(settings->horizontal_strategy, "text") == 0) {
            words_to_edges_h(edges, &words, (int)settings->min_words_horizontal);
        }
        if (strcmp(settings->vertical_strategy, "text") == 0) {
            words_to_edges_v(edges, &words, (int)settings->min_words_vertical);
        }
        
        free(words.words);
    }
    
    // Also try default "lines" strategy using text when no vector graphics available
    if (strcmp(settings->horizontal_strategy, "lines") == 0 || 
        strcmp(settings->vertical_strategy, "lines") == 0) {
        // Fallback: treat "lines" strategy as "text" strategy for now
        // In a more complete implementation, you would extract actual vector lines
        word_list_t words = {0};
        extract_words_from_chars(&words, &g_chars, settings->text_x_tolerance, settings->text_y_tolerance);
        
        if (strcmp(settings->horizontal_strategy, "lines") == 0) {
            words_to_edges_h(edges, &words, (int)settings->min_words_horizontal);
        }
        if (strcmp(settings->vertical_strategy, "lines") == 0) {
            words_to_edges_v(edges, &words, (int)settings->min_words_vertical);
        }
        
        free(words.words);
    }
}

// Table settings initialization
static table_settings_t init_table_settings(void) {
    table_settings_t settings = {0};
    
    strcpy(settings.vertical_strategy, "lines");
    strcpy(settings.horizontal_strategy, "lines");
    settings.snap_tolerance = DEFAULT_SNAP_TOLERANCE;
    settings.snap_x_tolerance = UNSET_FLOAT;
    settings.snap_y_tolerance = UNSET_FLOAT;
    settings.join_tolerance = DEFAULT_JOIN_TOLERANCE;
    settings.join_x_tolerance = UNSET_FLOAT;
    settings.join_y_tolerance = UNSET_FLOAT;
    settings.edge_min_length = 3.0f;
    settings.min_words_vertical = DEFAULT_MIN_WORDS_VERTICAL;
    settings.min_words_horizontal = DEFAULT_MIN_WORDS_HORIZONTAL;
    settings.intersection_tolerance = 3.0f;
    settings.intersection_x_tolerance = UNSET_FLOAT;
    settings.intersection_y_tolerance = UNSET_FLOAT;
    settings.text_x_tolerance = DEFAULT_X_TOLERANCE;
    settings.text_y_tolerance = DEFAULT_Y_TOLERANCE;
    
    return settings;
}

static void resolve_table_settings(table_settings_t *settings) {
    // Set unset values to their fallbacks
    if (is_unset(settings->snap_x_tolerance)) {
        settings->snap_x_tolerance = settings->snap_tolerance;
    }
    if (is_unset(settings->snap_y_tolerance)) {
        settings->snap_y_tolerance = settings->snap_tolerance;
    }
    if (is_unset(settings->join_x_tolerance)) {
        settings->join_x_tolerance = settings->join_tolerance;
    }
    if (is_unset(settings->join_y_tolerance)) {
        settings->join_y_tolerance = settings->join_tolerance;
    }
    if (is_unset(settings->intersection_x_tolerance)) {
        settings->intersection_x_tolerance = settings->intersection_tolerance;
    }
    if (is_unset(settings->intersection_y_tolerance)) {
        settings->intersection_y_tolerance = settings->intersection_tolerance;
    }
}

// Main table extraction function
static table_finder_t* find_tables(fz_context *ctx, fz_page *page, fz_rect clip, 
                                  const table_settings_t *user_settings) {
    printf("Allocating table finder...\n");
    table_finder_t *finder = malloc(sizeof(table_finder_t));
    memset(finder, 0, sizeof(table_finder_t));
    
    finder->tables = malloc(MAX_TABLES * sizeof(table_t));
    finder->capacity = MAX_TABLES;
    
    // Initialize settings
    printf("Initializing settings...\n");
    table_settings_t settings = user_settings ? *user_settings : init_table_settings();
    resolve_table_settings(&settings);
    
    // Extract characters
    printf("Extracting characters...\n");
    make_chars(ctx, page, clip);
    printf("Extracted %d characters\n", g_chars.count);
    
    // Initialize edge list
    printf("Initializing edge list...\n");
    init_edge_list(&finder->edges, MAX_EDGES);
    
    // Create edges from vector graphics and text
    printf("Creating edges from paths...\n");
    make_edges_from_paths(ctx, page, &finder->edges, &settings, clip);
    printf("Created %d edges\n", finder->edges.count);
    
    // Process edges
    printf("Processing edges...\n");
    snap_edges(&finder->edges, settings.snap_x_tolerance, settings.snap_y_tolerance);
    join_edges(&finder->edges, settings.join_x_tolerance, settings.join_y_tolerance);
    filter_edges(&finder->edges, settings.edge_min_length);
    printf("After processing: %d edges\n", finder->edges.count);
    
    // Find intersections
    printf("Finding intersections...\n");
    finder->intersections.intersections = malloc(MAX_INTERSECTIONS * sizeof(intersection_t));
    finder->intersections.capacity = MAX_INTERSECTIONS;
    edges_to_intersections(&finder->intersections, &finder->edges,
                          settings.intersection_x_tolerance, 
                          settings.intersection_y_tolerance);
    printf("Found %d intersections\n", finder->intersections.count);
    
    // Find cells
    printf("Finding cells...\n");
    init_cell_list(&finder->cells, MAX_CELLS);
    intersections_to_cells(&finder->cells, &finder->intersections);
    printf("Found %d cells\n", finder->cells.count);
    
    // Create tables
    printf("Creating tables...\n");
    finder->count = cells_to_tables(finder->tables, ctx, page, &finder->cells);
    printf("Created %d tables\n", finder->count);
    
    // Add headers to tables
    printf("Adding headers to tables...\n");
    for (int i = 0; i < finder->count; i++) {
        finder->tables[i].header = get_table_header(ctx, page, &finder->tables[i]);
    }
    printf("Headers added\n");
    
    return finder;
}

// Table extraction to text arrays
static char*** extract_table_to_array(fz_context *ctx, const table_t *table) {
    if (table->row_count == 0 || table->col_count == 0) return NULL;
    
    char ***result = malloc(table->row_count * sizeof(char**));
    
    for (int i = 0; i < table->row_count; i++) {
        result[i] = malloc(table->col_count * sizeof(char*));
        const table_row_t *row = &table->rows[i];
        
        for (int j = 0; j < table->col_count; j++) {
            if (j < row->count) {
                result[i][j] = extract_cell_text(ctx, g_textpage, row->cells[j], false);
            } else {
                result[i][j] = malloc(1);
                result[i][j][0] = '\0'; // Empty string
            }
        }
    }
    
    return result;
}

// Markdown export
static char* table_to_markdown(fz_context *ctx, const table_t *table, bool clean, bool fill_empty) {
    if (table->row_count == 0 || table->col_count == 0) return NULL;
    
    char *markdown = malloc(50000); // Large buffer for markdown
    markdown[0] = '\0';
    
    // Extract table data
    char ***table_data = extract_table_to_array(ctx, table);
    if (!table_data) return NULL;
    
    // Header row
    strcat(markdown, "|");
    for (int j = 0; j < table->col_count; j++) {
        const char *name = (j < table->header.col_count) ? table->header.names[j] : "";
        if (!name || strlen(name) == 0) {
            char col_name[20];
            snprintf(col_name, sizeof(col_name), "Col%d", j + 1);
            strcat(markdown, col_name);
        } else {
            if (clean) {
                // Simple HTML escaping for clean mode
                // In a full implementation, you'd want proper HTML escaping
                strcat(markdown, name);
            } else {
                strcat(markdown, name);
            }
        }
        strcat(markdown, "|");
    }
    strcat(markdown, "\n");
    
    // Header separator
    strcat(markdown, "|");
    for (int j = 0; j < table->col_count; j++) {
        strcat(markdown, "---|");
    }
    strcat(markdown, "\n");
    
    // Data rows (skip first row if header is internal)
    int start_row = table->header.external ? 0 : 1;
    for (int i = start_row; i < table->row_count; i++) {
        strcat(markdown, "|");
        for (int j = 0; j < table->col_count; j++) {
            const char *cell_text = table_data[i][j];
            if (!cell_text) cell_text = "";
            
            if (clean) {
                // Simple HTML escaping
                strcat(markdown, cell_text);
            } else {
                strcat(markdown, cell_text);
            }
            strcat(markdown, "|");
        }
        strcat(markdown, "\n");
    }
    
    // Free table data
    for (int i = 0; i < table->row_count; i++) {
        for (int j = 0; j < table->col_count; j++) {
            free(table_data[i][j]);
        }
        free(table_data[i]);
    }
    free(table_data);
    
    return markdown;
}

// Cleanup functions
static void free_table_finder(table_finder_t *finder) {
    if (!finder) return;
    
    // Free tables
    for (int i = 0; i < finder->count; i++) {
        table_t *table = &finder->tables[i];
        
        // Free header
        if (table->header.cells) {
            free(table->header.cells);
        }
        if (table->header.names) {
            for (int j = 0; j < table->header.col_count; j++) {
                free(table->header.names[j]);
            }
            free(table->header.names);
        }
        
        // Free rows
        if (table->rows) {
            for (int j = 0; j < table->row_count; j++) {
                free(table->rows[j].cells);
            }
            free(table->rows);
        }
        
        // Free cells
        if (table->cells.cells) {
            free(table->cells.cells);
        }
    }
    
    free(finder->tables);
    
    // Free edges
    if (finder->edges.edges) {
        free(finder->edges.edges);
    }
    
    // Free intersections
    if (finder->intersections.intersections) {
        for (int i = 0; i < finder->intersections.count; i++) {
            free(finder->intersections.intersections[i].v_edges.edges);
            free(finder->intersections.intersections[i].h_edges.edges);
        }
        free(finder->intersections.intersections);
    }
    
    // Free cells
    if (finder->cells.cells) {
        free(finder->cells.cells);
    }
    
    free(finder);
}

static void cleanup_globals(fz_context *ctx) {
    if (g_chars.chars) {
        free(g_chars.chars);
        g_chars.chars = NULL;
        g_chars.count = 0;
    }
    
    if (g_edges.edges) {
        free(g_edges.edges);
        g_edges.edges = NULL;
        g_edges.count = 0;
    }
    
    if (g_textpage) {
        fz_drop_stext_page(ctx, g_textpage); // Note: should pass proper context
        g_textpage = NULL;
    }
}

void main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file_path>\n", argv[0]);
        return;
    }

    const char* file_path = argv[1];
    printf("Starting PDF processing for: %s\n", file_path);
    
    fz_context *ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
    if (!ctx) {
        fprintf(stderr, "Cannot create MuPDF context\n");
        return;
    }
    printf("Created MuPDF context\n");

    if (&file_path == NULL) {
        fprintf(stderr, "No file path provided\n");
        fz_drop_context(ctx);
        return;
    }

    if (strlen(file_path) == 0) {
        fprintf(stderr, "Empty file path\n");
        fz_drop_context(ctx);
        return;
    }
    
    fz_register_document_handlers(ctx);
    printf("Registered document handlers\n");
    
    fz_try(ctx) {
        printf("Opening document...\n");
        fz_document *doc = fz_open_document(ctx, file_path);
        printf("Document opened, loading page 0...\n");
        fz_page *page = fz_load_page(ctx, doc, 0);
        printf("Page loaded\n");
        
        // Set up clip rectangle (entire page)
        fz_rect clip = fz_bound_page(ctx, page);
        printf("Page bounds: %.2f,%.2f - %.2f,%.2f\n", clip.x0, clip.y0, clip.x1, clip.y1);
        
        // Find tables with default settings
        printf("Finding tables...\n");
        table_finder_t *finder = find_tables(ctx, page, clip, NULL);
        
        printf("Found %d tables\n", finder->count);
        
        // Process each table
        for (int i = 0; i < finder->count; i++) {
            table_t *table = &finder->tables[i];
            printf("Table %d: %d rows x %d columns\n", 
                   i + 1, table->row_count, table->col_count);
            
            // Export to markdown
            char *markdown = table_to_markdown(ctx, table, false, true);
            if (markdown) {
                printf("Markdown:\n%s\n", markdown);
                free(markdown);
            }
        }
        
        // Cleanup
        free_table_finder(finder);
        fz_drop_page(ctx, page);
        fz_drop_document(ctx, doc);
    }
    fz_catch(ctx) {
        fprintf(stderr, "Error processing document: %s\n", fz_caught_message(ctx));
    }

    cleanup_globals(ctx);
    fz_drop_context(ctx);
    printf("Program completed\n");
}
