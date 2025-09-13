#include <mupdf/fitz.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <math.h>
#include <stdbool.h>
#include <ctype.h>
#include "improved_table_detection.h"

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
#define MAX_INTERSECTIONS 10000
#define MAX_TEXT_LENGTH 10000
#define MAX_SPANS 1000
#define MAX_PATHS 1000

// Data Structures
typedef struct
{
    float x0, y0, x1, y1;
} bbox_t;

typedef struct
{
    float x, y;
} point_t;

typedef struct
{
    float a, b, c, d, e, f;
} matrix_t;

typedef struct
{

    char text[10];
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
    int color;
    int flags;

} char_info_t;

typedef struct
{
    char_info_t *chars;

    int count;
    int capacity;
} char_info_list_t;

typedef struct
{

    char text[1024];
    bbox_t bbox;
    float doctop;
    int upright;
    int direction;
    int rotation;
} word_info_t;

typedef struct
{
    word_info_t *words;
    int count;
    int capacity;
} word_info_list_t;

typedef struct
{
    bbox_t bbox;
    char orientation; // 'h' or 'v'
    float width, height;

    float top, bottom;
    char object_type[32];
    int page_number;
} edge_info_t;

typedef struct
{
    edge_info_t *edges;
    int count;
    int capacity;
} edge_info_list_t;

typedef struct
{
    point_t point;
    edge_info_list_t v_edges;
    edge_info_list_t h_edges;
} intersection_t;

typedef struct
{
    intersection_t *intersections;
    int count;
    int capacity;
} intersection_list_t;

typedef struct
{
    bbox_t *cells;
    int count;
    int capacity;

} cell_list_t;

typedef struct
{
    bbox_t bbox;
    bbox_t *cells;
    char **names;
    int col_count;
    bool external;
} table_header_t;

typedef struct
{
    bbox_t *cells;
    int count;
} table_row_t;

typedef struct
{

    bbox_t bbox;
    cell_list_t cells;
    table_header_t header;
    table_row_t *rows;
    int row_count;
    int col_count;
} table_t;

typedef struct
{
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
    float *explicit_vertical_lines;
    int num_explicit_vertical_lines;
    float *explicit_horizontal_lines;
    int num_explicit_horizontal_lines;
} table_settings_t;

typedef struct
{
    table_t *tables;
    int count;
    int capacity;
    edge_info_list_t edges;
    intersection_list_t intersections;
    cell_list_t cells;
} table_finder_t;

// Global variables

static char_info_list_t g_chars = {0};
static edge_info_list_t g_edges = {0};
static fz_stext_page *g_textpage = NULL;

// Forward declarations

// Utility functions
static float min_f(float a, float b) { return a < b ? a : b; }
static float max_f(float a, float b) { return a > b ? a : b; }
static bool float_equal(float a, float b, float tolerance) { return fabs(a - b) <= tolerance; }
static bool is_unset(float val) { return val == UNSET_FLOAT; }

// Bbox operations
static bbox_t bbox_union(bbox_t a, bbox_t b)
{
    return (bbox_t){min_f(a.x0, b.x0), min_f(a.y0, b.y0), max_f(a.x1, b.x1), max_f(a.y1, b.y1)};
}

static bbox_t objects_to_bbox(bbox_t *objects, int count)
{
    if (count == 0)
        return (bbox_t){0, 0, 0, 0};
    bbox_t result = objects[0];
    for (int i = 1; i < count; i++)
        result = bbox_union(result, objects[i]);
    return result;
}

// String utilities
static void trim_string(char *str)
{
    if (!str)
        return;
    char *start = str;
    while (isspace(*start))
        start++;
    char *end = start + strlen(start) - 1;
    while (end > start && isspace(*end))
        end--;
    end[1] = '\0';
    if (start != str)
        memmove(str, start, strlen(start) + 1);
}

// Dynamic array utilities

#define FZ_STEXT_STYLE_BOLD 1
#define FZ_STEXT_STYLE_ITALIC 2
#define FZ_STEXT_STYLE_MONOSPACED 4
// Note: MuPDF does not have a standard flag for strikeout in fz_stext_char,
// but PyMuPDF's rawdict extraction provides it. We define it for compatibility.
#define FZ_STEXT_STYLE_STRIKEOUT 8

#define IMPLEMENT_DYN_ARRAY(type, member)                                               \
    static void init_##type##_list(type##_list_t *list, int capacity)                   \
    {                                                                                   \
        list->member = malloc(capacity * sizeof(type##_t));                             \
        if (!list->member)                                                              \
        {                                                                               \
            fprintf(stderr, "Memory allocation failed for " #type "_list\n");           \
            exit(1);                                                                    \
        }                                                                               \
        list->count = 0;                                                                \
        list->capacity = capacity;                                                      \
    }                                                                                   \
    static void add_##type(type##_list_t *list, const type##_t *item)                   \
    {                                                                                   \
        if (list->count >= list->capacity)                                              \
        {                                                                               \
            int new_capacity = list->capacity == 0 ? 16 : list->capacity * 2;           \
            type##_t *new_ptr = realloc(list->member, new_capacity * sizeof(type##_t)); \
            if (!new_ptr)                                                               \
            {                                                                           \
                fprintf(stderr, "Memory reallocation failed for " #type "_list\n");     \
                exit(1);                                                                \
            }                                                                           \
            list->member = new_ptr;                                                     \
            list->capacity = new_capacity;                                              \
        }                                                                               \
        list->member[list->count++] = *item;                                            \
    }

IMPLEMENT_DYN_ARRAY(char_info, chars)
IMPLEMENT_DYN_ARRAY(word_info, words)
IMPLEMENT_DYN_ARRAY(edge_info, edges)
typedef bbox_t cell_t;
IMPLEMENT_DYN_ARRAY(cell, cells)

// Comparison function for qsort
static int compare_floats(const void *a, const void *b)
{
    float fa = *(const float *)a;
    float fb = *(const float *)b;
    return (fa > fb) - (fa < fb);
}

// Clustering
typedef struct
{
    int *cluster_ids;
    int num_clusters;
} cluster_result_t;

static cluster_result_t cluster_values(float *values, int count, float tolerance)
{
    cluster_result_t result = {0};
    if (count == 0)
        return result;

    float *sorted_values = malloc(count * sizeof(float));
    if (!sorted_values)
    {
        fprintf(stderr, "Memory allocation failed in cluster_values\n");
        return result;
    }
    memcpy(sorted_values, values, count * sizeof(float));
    qsort(sorted_values, count, sizeof(float), compare_floats);

    result.cluster_ids = malloc(count * sizeof(int));
    int *cluster_map = malloc(count * sizeof(int));
    if (!result.cluster_ids || !cluster_map)
    {
        free(sorted_values);
        free(result.cluster_ids);
        free(cluster_map);
        fprintf(stderr, "Memory allocation failed in cluster_values\n");
        memset(&result, 0, sizeof(result));
        return result;
    }

    int cluster_id = 0;
    if (count > 0)
    {
        cluster_map[0] = cluster_id;
        for (int i = 1; i < count; i++)
        {
            if (sorted_values[i] - sorted_values[i - 1] > tolerance)
            {
                cluster_id++;
            }
            cluster_map[i] = cluster_id;
        }
    }
    result.num_clusters = cluster_id + 1;

    for (int i = 0; i < count; i++)
    {
        for (int j = 0; j < count; j++)
        {
            if (float_equal(values[i], sorted_values[j], 1e-6f))
            {
                result.cluster_ids[i] = cluster_map[j];
                break;
            }
        }
    }

    free(sorted_values);
    free(cluster_map);
    return result;
}

// Text extraction
static int compare_char_info(const void *a, const void *b)
{
    const char_info_t *ca = (const char_info_t *)a;
    const char_info_t *cb = (const char_info_t *)b;
    if (!float_equal(ca->bbox.y0, cb->bbox.y0, 1.0f))
    {
        return ca->bbox.y0 < cb->bbox.y0 ? -1 : 1;
    }
    return ca->bbox.x0 < cb->bbox.x0 ? -1 : 1;
}

static void close_markdown_tags(char *dest, int flags)
{
    if (flags & FZ_STEXT_STYLE_MONOSPACED)
        strcat(dest, "`");
    if (flags & FZ_STEXT_STYLE_ITALIC)
        strcat(dest, "_");
    if (flags & FZ_STEXT_STYLE_BOLD)
        strcat(dest, "**");
    if (flags & FZ_STEXT_STYLE_STRIKEOUT)
        strcat(dest, "~~");
}

static void open_markdown_tags(char *dest, int flags)
{
    if (flags & FZ_STEXT_STYLE_STRIKEOUT)
        strcat(dest, "~~");
    if (flags & FZ_STEXT_STYLE_BOLD)
        strcat(dest, "**");
    if (flags & FZ_STEXT_STYLE_ITALIC)
        strcat(dest, "_");
    if (flags & FZ_STEXT_STYLE_MONOSPACED)
        strcat(dest, "`");
}

static char *extract_cell_text(fz_context *ctx, fz_stext_page *page, bbox_t cell, bool markdown)
{
    char *text = calloc(MAX_TEXT_LENGTH, 1);
    if (!text)
    {
        fprintf(stderr, "Memory allocation failed in extract_cell_text\n");
        return NULL;
    }

    char_info_list_t cell_chars;
    init_char_info_list(&cell_chars, 100);

    for (int i = 0; i < g_chars.count; i++)
    {
        char_info_t *c = &g_chars.chars[i];

        // Use character center point for more precise cell membership
        float char_center_x = (c->bbox.x0 + c->bbox.x1) / 2.0f;
        float char_center_y = (c->bbox.y0 + c->bbox.y1) / 2.0f;

        // Check if character center is within cell boundaries with small tolerance
        float tolerance = 1.0f;
        if (char_center_x >= (cell.x0 - tolerance) &&
            char_center_x <= (cell.x1 + tolerance) &&
            char_center_y >= (cell.y0 - tolerance) &&
            char_center_y <= (cell.y1 + tolerance))
        {
            add_char_info(&cell_chars, c);
        }
    }

    if (cell_chars.count == 0)
    {
        free(cell_chars.chars);
        return text; // Return empty string
    }

    qsort(cell_chars.chars, cell_chars.count, sizeof(char_info_t), compare_char_info);

    // Simple text extraction without markdown formatting for table cells
    for (int i = 0; i < cell_chars.count; i++)
    {
        if (strlen(text) + strlen(cell_chars.chars[i].text) < MAX_TEXT_LENGTH - 2)
        {
            // Add space between characters if there's a significant gap
            if (i > 0)
            {
                char_info_t *prev = &cell_chars.chars[i - 1];
                char_info_t *curr = &cell_chars.chars[i];

                // Add space if characters are on different lines or far apart horizontally
                if (!float_equal(prev->bbox.y0, curr->bbox.y0, 2.0f) ||
                    (curr->bbox.x0 - prev->bbox.x1) > 5.0f)
                {
                    strcat(text, " ");
                }
            }
            strcat(text, cell_chars.chars[i].text);
        }
    }

    trim_string(text);
    free(cell_chars.chars);
    return text;
}

// Character and Word Extraction
static void make_chars(fz_context *ctx, fz_page *page, fz_rect clip)
{
    (void)clip; // Suppress unused parameter warning
    g_chars.count = 0;
    if (!g_chars.chars)
        init_char_info_list(&g_chars, MAX_CHARS);

    fz_stext_options opts = {0};
    opts.flags = FZ_STEXT_PRESERVE_SPANS | FZ_STEXT_PRESERVE_WHITESPACE;

    if (g_textpage)
    {
        fz_drop_stext_page(ctx, g_textpage);
        g_textpage = NULL;
    }

    fz_try(ctx)
    {
        g_textpage = fz_new_stext_page_from_page(ctx, page, &opts);
    }
    fz_catch(ctx)
    {
        fprintf(stderr, "Failed to create stext page\n");
        return;
    }

    if (!g_textpage)
        return;

    fz_stext_block *block;
    for (block = g_textpage->first_block; block; block = block->next)
    {
        if (block->type != FZ_STEXT_BLOCK_TEXT)
            continue;

        fz_stext_line *line;
        for (line = block->u.t.first_line; line; line = line->next)
        {
            fz_stext_char *ch;
            for (ch = line->first_char; ch; ch = ch->next)
            {
                char_info_t char_info = {0};
                fz_rect char_rect = fz_rect_from_quad(ch->quad);
                char_info.bbox = (bbox_t){char_rect.x0, char_rect.y0, char_rect.x1, char_rect.y1};
                char_info.doctop = char_rect.y0;
                fz_runetochar(char_info.text, ch->c);
                if (ch->font && ch->font->name)
                {
                    strncpy(char_info.fontname, ch->font->name, sizeof(char_info.fontname) - 1);
                    char_info.fontname[sizeof(char_info.fontname) - 1] = '\0';
                }
                char_info.fontsize = ch->size;
                char_info.size = ch->size;
                char_info.upright = (abs(line->wmode) < 1);
                char_info.width = char_rect.x1 - char_rect.x0;
                char_info.height = char_rect.y1 - char_rect.y0;
                char_info.page_number = 1; // Simplified

                char_info.flags = 0;
                if (ch->font && ch->font->name)
                {
                    if (strstr(ch->font->name, "Bold") || strstr(ch->font->name, "Black") || strstr(ch->font->name, "Heavy"))
                        char_info.flags |= FZ_STEXT_STYLE_BOLD;
                    if (strstr(ch->font->name, "Italic") || strstr(ch->font->name, "Oblique"))
                        char_info.flags |= FZ_STEXT_STYLE_ITALIC;
                    if (strstr(ch->font->name, "Mono") || strstr(ch->font->name, "Courier") || strstr(ch->font->name, "Consolas"))
                        char_info.flags |= FZ_STEXT_STYLE_MONOSPACED;
                }
                // The FZ_STEXT_SPAN_FLAG_STRIKEOUT is not standard in MuPDF's public headers.
                // This is a placeholder for potential future implementation.
                // if (span->flags & FZ_STEXT_SPAN_FLAG_STRIKEOUT)
                //     char_info.flags |= FZ_STEXT_STYLE_STRIKEOUT;

                add_char_info(&g_chars, &char_info);
            }
        }
    }
}

static const char *expand_ligature(const char *text)
{
    if (strcmp(text, "ﬀ") == 0)
        return "ff";
    if (strcmp(text, "ﬃ") == 0)
        return "ffi";
    if (strcmp(text, "ﬄ") == 0)
        return "ffl";
    if (strcmp(text, "ﬁ") == 0)
        return "fi";
    if (strcmp(text, "ﬂ") == 0)
        return "fl";
    if (strcmp(text, "ﬆ") == 0)
        return "st";
    if (strcmp(text, "ﬅ") == 0)
        return "st";
    return text;
}

static void extract_words_from_chars(word_info_list_t *words, const char_info_list_t *chars, float x_tolerance, float y_tolerance)
{
    if (chars->count == 0)
        return;

    words->count = 0;
    init_word_info_list(words, MAX_WORDS);

    char current_word_text[1024] = {0};
    bbox_t current_word_bbox = {0};
    int chars_in_word = 0;

    for (int i = 0; i < chars->count; i++)
    {
        const char_info_t *ch = &chars->chars[i];
        if (isspace(ch->text[0]))
        {
            if (chars_in_word > 0)
            {
                word_info_t word = {0};
                strcpy(word.text, current_word_text);
                word.bbox = current_word_bbox;
                word.upright = chars->chars[i - 1].upright;
                add_word_info(words, &word);
                current_word_text[0] = '\0';
                chars_in_word = 0;
            }
            continue;
        }

        if (chars_in_word > 0)
        {
            const char_info_t *prev_ch = &chars->chars[i - 1];
            bool new_word = false;
            if (ch->upright != prev_ch->upright)
                new_word = true;
            else if (ch->upright)
            { // Horizontal
                if (fabs(ch->bbox.y0 - prev_ch->bbox.y0) > y_tolerance ||
                    ch->bbox.x0 > prev_ch->bbox.x1 + x_tolerance)
                    new_word = true;
            }
            else
            { // Vertical
                if (fabs(ch->bbox.x0 - prev_ch->bbox.x0) > x_tolerance ||
                    ch->bbox.y0 > prev_ch->bbox.y1 + y_tolerance)
                    new_word = true;
            }

            if (new_word)
            {
                word_info_t word = {0};
                strcpy(word.text, current_word_text);
                word.bbox = current_word_bbox;
                word.upright = prev_ch->upright;
                add_word_info(words, &word);
                current_word_text[0] = '\0';
                chars_in_word = 0;
            }
        }

        if (chars_in_word == 0)
        {
            current_word_bbox = ch->bbox;
        }
        else
        {
            current_word_bbox = bbox_union(current_word_bbox, ch->bbox);
        }

        const char *expanded_text = expand_ligature(ch->text);
        size_t current_len = strlen(current_word_text);
        size_t text_len = strlen(expanded_text);
        if (current_len + text_len < sizeof(current_word_text) - 1)
        {
            strcat(current_word_text, expanded_text);
        }
        chars_in_word++;
    }

    if (chars_in_word > 0)
    {
        word_info_t word = {0};
        strcpy(word.text, current_word_text);
        word.bbox = current_word_bbox;
        word.upright = chars->chars[chars->count - 1].upright;
        add_word_info(words, &word);
    }
}

// Edge Creation
static edge_info_t line_to_edge(bbox_t line_bbox, char orientation)
{
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

static void words_to_edges_h(edge_info_list_t *edges, const word_info_list_t *words, int word_threshold)
{
    if (words->count < word_threshold)
        return;

    float *tops = malloc(words->count * sizeof(float));
    if (!tops)
    {
        fprintf(stderr, "Memory allocation failed in words_to_edges_h\n");
        return;
    }

    for (int i = 0; i < words->count; i++)
        tops[i] = words->words[i].bbox.y0;

    cluster_result_t clusters = cluster_values(tops, words->count, 1.0f);
    if (clusters.num_clusters == 0 || !clusters.cluster_ids)
    {
        free(tops);
        return;
    }

    int *cluster_counts = calloc(clusters.num_clusters, sizeof(int));
    if (!cluster_counts)
    {
        free(tops);
        free(clusters.cluster_ids);
        return;
    }

    for (int i = 0; i < words->count; i++)
    {
        if (clusters.cluster_ids[i] >= 0 && clusters.cluster_ids[i] < clusters.num_clusters)
        {
            cluster_counts[clusters.cluster_ids[i]]++;
        }
    }

    for (int cid = 0; cid < clusters.num_clusters; cid++)
    {
        if (cluster_counts[cid] >= word_threshold)
        {
            bbox_t cluster_bbox = {INFINITY, INFINITY, -INFINITY, -INFINITY};
            for (int i = 0; i < words->count; i++)
            {
                if (clusters.cluster_ids[i] == cid)
                {
                    cluster_bbox = bbox_union(cluster_bbox, words->words[i].bbox);
                }
            }
            edge_info_t top_edge = line_to_edge((bbox_t){cluster_bbox.x0, cluster_bbox.y0, cluster_bbox.x1, cluster_bbox.y0}, 'h');
            edge_info_t bottom_edge = line_to_edge((bbox_t){cluster_bbox.x0, cluster_bbox.y1, cluster_bbox.x1, cluster_bbox.y1}, 'h');
            add_edge_info(edges, &top_edge);
            add_edge_info(edges, &bottom_edge);
        }
    }
    free(tops);
    free(clusters.cluster_ids);
    free(cluster_counts);
}

static void words_to_edges_v(edge_info_list_t *edges, const word_info_list_t *words, int word_threshold)
{
    if (words->count < word_threshold)
        return;

    float *coords = malloc(words->count * 3 * sizeof(float));
    if (!coords)
    {
        fprintf(stderr, "Memory allocation failed in words_to_edges_v\n");
        return;
    }

    for (int i = 0; i < words->count; i++)
    {
        coords[i * 3 + 0] = words->words[i].bbox.x0;
        coords[i * 3 + 1] = words->words[i].bbox.x1;
        coords[i * 3 + 2] = (words->words[i].bbox.x0 + words->words[i].bbox.x1) / 2.0f;
    }

    cluster_result_t clusters = cluster_values(coords, words->count * 3, 1.0f);
    if (clusters.num_clusters == 0 || !clusters.cluster_ids)
    {
        free(coords);
        return;
    }

    int *cluster_counts = calloc(clusters.num_clusters, sizeof(int));
    if (!cluster_counts)
    {
        free(coords);
        free(clusters.cluster_ids);
        return;
    }

    for (int i = 0; i < words->count * 3; i++)
    {
        if (clusters.cluster_ids[i] >= 0 && clusters.cluster_ids[i] < clusters.num_clusters)
        {
            cluster_counts[clusters.cluster_ids[i]]++;
        }
    }

    for (int cid = 0; cid < clusters.num_clusters; cid++)
    {
        if (cluster_counts[cid] >= word_threshold)
        {
            float x_coord = 0;
            int count = 0;
            bbox_t cluster_bbox = {INFINITY, INFINITY, -INFINITY, -INFINITY};
            for (int i = 0; i < words->count * 3; i++)
            {
                if (clusters.cluster_ids[i] == cid)
                {
                    x_coord += coords[i];
                    count++;
                    if (i / 3 < words->count)
                    {
                        cluster_bbox = bbox_union(cluster_bbox, words->words[i / 3].bbox);
                    }
                }
            }
            if (count > 0)
            {
                x_coord /= count;
                edge_info_t edge = line_to_edge((bbox_t){x_coord, cluster_bbox.y0, x_coord, cluster_bbox.y1}, 'v');
                add_edge_info(edges, &edge);
            }
        }
    }
    free(coords);
    free(clusters.cluster_ids);
    free(cluster_counts);
}

// Edge Processing
static void snap_edges(edge_info_list_t *edges, float x_tolerance, float y_tolerance)
{
    for (int i = 0; i < edges->count; i++)
    {
        for (int j = i + 1; j < edges->count; j++)
        {
            edge_info_t *e1 = &edges->edges[i];
            edge_info_t *e2 = &edges->edges[j];
            if (e1->orientation != e2->orientation)
                continue;
            if (e1->orientation == 'v' && float_equal(e1->bbox.x0, e2->bbox.x0, x_tolerance))
            {
                float avg_x = (e1->bbox.x0 + e2->bbox.x0) / 2.0f;
                e1->bbox.x0 = e1->bbox.x1 = avg_x;
                e2->bbox.x0 = e2->bbox.x1 = avg_x;
            }
            else if (e1->orientation == 'h' && float_equal(e1->bbox.y0, e2->bbox.y0, y_tolerance))
            {
                float avg_y = (e1->bbox.y0 + e2->bbox.y0) / 2.0f;
                e1->bbox.y0 = e1->bbox.y1 = avg_y;
                e2->bbox.y0 = e2->bbox.y1 = avg_y;
            }
        }
    }
}

static void join_edges(edge_info_list_t *edges, float x_tolerance, float y_tolerance)
{
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (int i = 0; i < edges->count; i++)
        {
            for (int j = i + 1; j < edges->count; j++)
            {
                edge_info_t *e1 = &edges->edges[i];
                edge_info_t *e2 = &edges->edges[j];
                if (e1->orientation != e2->orientation)
                    continue;

                bool can_join = false;
                if (e1->orientation == 'h' && float_equal(e1->bbox.y0, e2->bbox.y0, y_tolerance) &&
                    (max_f(e1->bbox.x0, e2->bbox.x0) - min_f(e1->bbox.x1, e2->bbox.x1)) <= x_tolerance)
                {
                    can_join = true;
                }
                else if (e1->orientation == 'v' && float_equal(e1->bbox.x0, e2->bbox.x0, x_tolerance) &&
                         (max_f(e1->bbox.y0, e2->bbox.y0) - min_f(e1->bbox.y1, e2->bbox.y1)) <= y_tolerance)
                {
                    can_join = true;
                }

                if (can_join)
                {
                    e1->bbox = bbox_union(e1->bbox, e2->bbox);
                    e1->width = e1->bbox.x1 - e1->bbox.x0;
                    e1->height = e1->bbox.y1 - e1->bbox.y0;

                    if (j < edges->count - 1)
                    {
                        edges->edges[j] = edges->edges[edges->count - 1];
                    }
                    edges->count--;
                    j--;
                    changed = true;
                }
            }
        }
    }
}

static void filter_edges(edge_info_list_t *edges, float min_length)
{
    int write_pos = 0;
    for (int i = 0; i < edges->count; i++)
    {
        edge_info_t *edge = &edges->edges[i];
        float length = (edge->orientation == 'h') ? edge->width : edge->height;
        if (length >= min_length)
        {
            if (write_pos != i)
            {
                edges->edges[write_pos] = *edge;
            }
            write_pos++;
        }
    }
    edges->count = write_pos;
}

// Intersection and Cell Finding
static void edges_to_intersections(intersection_list_t *intersections, const edge_info_list_t *edges, float x_tolerance, float y_tolerance)
{
    intersections->count = 0;
    if (!intersections->intersections)
    {
        intersections->intersections = malloc(MAX_INTERSECTIONS * sizeof(intersection_t));
        if (!intersections->intersections)
        {
            fprintf(stderr, "Memory allocation failed in edges_to_intersections\n");
            return;
        }
        intersections->capacity = MAX_INTERSECTIONS;
    }

    for (int i = 0; i < edges->count; i++)
    {
        if (edges->edges[i].orientation != 'v')
            continue;
        for (int j = 0; j < edges->count; j++)
        {
            if (edges->edges[j].orientation != 'h')
                continue;

            const edge_info_t *v_edge = &edges->edges[i];
            const edge_info_t *h_edge = &edges->edges[j];

            if (v_edge->bbox.x0 >= h_edge->bbox.x0 - x_tolerance &&
                v_edge->bbox.x0 <= h_edge->bbox.x1 + x_tolerance &&
                h_edge->bbox.y0 >= v_edge->bbox.y0 - y_tolerance &&
                h_edge->bbox.y0 <= v_edge->bbox.y1 + y_tolerance)
            {

                point_t p = {v_edge->bbox.x0, h_edge->bbox.y0};
                int found_idx = -1;
                for (int k = 0; k < intersections->count; k++)
                {
                    if (float_equal(intersections->intersections[k].point.x, p.x, x_tolerance) &&
                        float_equal(intersections->intersections[k].point.y, p.y, y_tolerance))
                    {
                        found_idx = k;
                        break;
                    }
                }

                if (found_idx != -1)
                {
                    add_edge_info(&intersections->intersections[found_idx].v_edges, v_edge);
                    add_edge_info(&intersections->intersections[found_idx].h_edges, h_edge);
                }
                else
                {
                    if (intersections->count < intersections->capacity)
                    {
                        intersection_t *new_isect = &intersections->intersections[intersections->count];
                        new_isect->point = p;
                        init_edge_info_list(&new_isect->v_edges, 4);
                        init_edge_info_list(&new_isect->h_edges, 4);
                        add_edge_info(&new_isect->v_edges, v_edge);
                        add_edge_info(&new_isect->h_edges, h_edge);
                        intersections->count++;
                    }
                }
            }
        }
    }
}

static int compare_points(const void *a, const void *b)
{
    point_t *p1 = (point_t *)a;
    point_t *p2 = (point_t *)b;
    if (p1->y < p2->y)
        return -1;
    if (p1->y > p2->y)
        return 1;
    if (p1->x < p2->x)
        return -1;

    if (p1->x > p2->x)
        return 1;
    return 0;
}

static bool edge_connects(const intersection_t *i1, const intersection_t *i2)
{
    if (float_equal(i1->point.x, i2->point.x, 0.1f))
    { // Vertical connection
        for (int i = 0; i < i1->v_edges.count; i++)
        {
            for (int j = 0; j < i2->v_edges.count; j++)
            {
                if (float_equal(i1->v_edges.edges[i].bbox.x0, i2->v_edges.edges[j].bbox.x0, 0.1f) &&
                    float_equal(i1->v_edges.edges[i].bbox.x1, i2->v_edges.edges[j].bbox.x1, 0.1f))
                    return true;
            }
        }
    }
    if (float_equal(i1->point.y, i2->point.y, 0.1f))
    { // Horizontal connection
        for (int i = 0; i < i1->h_edges.count; i++)
        {
            for (int j = 0; j < i2->h_edges.count; j++)
            {
                if (float_equal(i1->h_edges.edges[i].bbox.y0, i2->h_edges.edges[j].bbox.y0, 0.1f) &&
                    float_equal(i1->h_edges.edges[i].bbox.y1, i2->h_edges.edges[j].bbox.y1, 0.1f))
                    return true;
            }
        }
    }
    return false;
}

static void intersections_to_cells(cell_list_t *cells, const intersection_list_t *intersections)
{
    cells->count = 0;
    if (!cells->cells)
        init_cell_list(cells, MAX_CELLS);

    for (int i = 0; i < intersections->count; i++)
    {
        const intersection_t *top_left = &intersections->intersections[i];

        for (int j = i + 1; j < intersections->count; j++)
        {
            const intersection_t *right = &intersections->intersections[j];
            if (!float_equal(right->point.y, top_left->point.y, 0.1f) || right->point.x <= top_left->point.x)
                continue;
            if (!edge_connects(top_left, right))
                continue;

            for (int k = i + 1; k < intersections->count; k++)
            {
                const intersection_t *bottom = &intersections->intersections[k];
                if (!float_equal(bottom->point.x, top_left->point.x, 0.1f) || bottom->point.y <= top_left->point.y)
                    continue;
                if (!edge_connects(top_left, bottom))
                    continue;

                point_t bottom_right_p = {right->point.x, bottom->point.y};
                for (int l = k + 1; l < intersections->count; l++)
                {
                    const intersection_t *bottom_right = &intersections->intersections[l];
                    if (float_equal(bottom_right->point.x, bottom_right_p.x, 0.1f) &&
                        float_equal(bottom_right->point.y, bottom_right_p.y, 0.1f))
                    {
                        if (edge_connects(right, bottom_right) && edge_connects(bottom, bottom_right))
                        {
                            bbox_t cell = {top_left->point.x, top_left->point.y, bottom_right_p.x, bottom_right_p.y};
                            add_cell(cells, &cell);
                            goto next_top_left;
                        }
                    }
                }
            }
        }
    next_top_left:;
    }
}

// Table Grouping
static int cells_to_tables(table_t *tables, fz_context *ctx, fz_page *page, const cell_list_t *cells)
{
    if (cells->count == 0)
        return 0;

    bool *used = calloc(cells->count, sizeof(bool));
    if (!used)
    {
        fprintf(stderr, "Memory allocation failed in cells_to_tables\n");
        return 0;
    }

    int table_count = 0;

    for (int i = 0; i < cells->count && table_count < MAX_TABLES; i++)
    {
        if (used[i])
            continue;

        cell_list_t table_cells;
        init_cell_list(&table_cells, 16);
        add_cell(&table_cells, &cells->cells[i]);
        used[i] = true;

        bool new_found = true;
        while (new_found)
        {
            new_found = false;
            for (int j = 0; j < cells->count; j++)
            {
                if (used[j])
                    continue;
                for (int k = 0; k < table_cells.count; k++)
                {
                    bbox_t cell1 = cells->cells[j];
                    bbox_t cell2 = table_cells.cells[k];
                    point_t c1_corners[4] = {{cell1.x0, cell1.y0}, {cell1.x1, cell1.y0}, {cell1.x0, cell1.y1}, {cell1.x1, cell1.y1}};
                    point_t c2_corners[4] = {{cell2.x0, cell2.y0}, {cell2.x1, cell2.y0}, {cell2.x0, cell2.y1}, {cell2.x1, cell2.y1}};
                    bool shares_corner = false;
                    for (int c1 = 0; c1 < 4; c1++)
                    {
                        for (int c2 = 0; c2 < 4; c2++)
                        {
                            if (float_equal(c1_corners[c1].x, c2_corners[c2].x, 0.1f) &&

                                float_equal(c1_corners[c1].y, c2_corners[c2].y, 0.1f))
                            {
                                shares_corner = true;
                                break;
                            }
                        }
                        if (shares_corner)
                            break;
                    }
                    if (shares_corner)
                    {
                        add_cell(&table_cells, &cells->cells[j]);
                        used[j] = true;
                        new_found = true;
                        goto next_cell;
                    }
                }
            next_cell:;
            }
        }

        if (table_cells.count > 0)
        {
            // PyMuPDF modification: Filter out tables with no text or only 1 column.
            bbox_t table_bbox = objects_to_bbox(table_cells.cells, table_cells.count);

            // Check for single column
            float *x0_coords = malloc(table_cells.count * sizeof(float));
            float *x1_coords = malloc(table_cells.count * sizeof(float));
            int x0_count = 0, x1_count = 0;
            if (x0_coords && x1_coords)
            {
                for (int j = 0; j < table_cells.count; j++)
                {
                    bool found = false;
                    for (int k = 0; k < x0_count; k++)
                        if (float_equal(x0_coords[k], table_cells.cells[j].x0, 0.1f))
                            found = true;
                    if (!found)
                        x0_coords[x0_count++] = table_cells.cells[j].x0;

                    found = false;
                    for (int k = 0; k < x1_count; k++)
                        if (float_equal(x1_coords[k], table_cells.cells[j].x1, 0.1f))
                            found = true;
                    if (!found)
                        x1_coords[x1_count++] = table_cells.cells[j].x1;
                }
                free(x0_coords);
                free(x1_coords);
            }

            // Check for text
            char *table_text = extract_cell_text(ctx, g_textpage, table_bbox, false);
            bool has_text = table_text && strlen(table_text) > 0;
            if (table_text)
                free(table_text);

            if ((x0_count < 2 || x1_count < 2) || !has_text)
            {
                free(table_cells.cells);
                continue; // Skip this table
            }

            table_t *table = &tables[table_count++];
            table->cells = table_cells;
            table->bbox = table_bbox;

            // Create rows
            float *y_coords = malloc(table_cells.count * sizeof(float));
            if (!y_coords)
            {

                fprintf(stderr, "Memory allocation failed for y_coords\n");
                continue;
            }

            for (int j = 0; j < table_cells.count; j++)
                y_coords[j] = table_cells.cells[j].y0;

            cluster_result_t y_clusters = cluster_values(y_coords, table_cells.count, 1.0f);

            if (y_clusters.num_clusters == 0 || !y_clusters.cluster_ids)
            {
                free(y_coords);
                continue;
            }

            table->row_count = y_clusters.num_clusters;
            table->rows = malloc(table->row_count * sizeof(table_row_t));
            if (!table->rows)
            {
                free(y_coords);
                free(y_clusters.cluster_ids);
                continue;
            }

            for (int rid = 0; rid < table->row_count; rid++)
            {
                table->rows[rid].count = 0;
                for (int j = 0; j < table_cells.count; j++)
                {
                    if (y_clusters.cluster_ids[j] == rid)
                        table->rows[rid].count++;
                }

                if (table->rows[rid].count > 0)
                {
                    table->rows[rid].cells = malloc(table->rows[rid].count * sizeof(bbox_t));

                    if (!table->rows[rid].cells)
                    {
                        // Clean up partially allocated row
                        for (int cleanup = 0; cleanup < rid; cleanup++)
                        {
                            if (table->rows[cleanup].cells)
                                free(table->rows[cleanup].cells);
                        }
                        free(table->rows);
                        free(y_coords);
                        free(y_clusters.cluster_ids);
                        continue;
                    }

                    int cell_idx = 0;
                    for (int j = 0; j < table_cells.count; j++)
                    {
                        if (y_clusters.cluster_ids[j] == rid)
                        {
                            table->rows[rid].cells[cell_idx++] = table_cells.cells[j];
                        }
                    }
                }
                else
                {
                    table->rows[rid].cells = NULL;
                }
            }

            table->col_count = 0;
            for (int j = 0; j < table->row_count; j++)
            {
                if (table->rows[j].count > table->col_count)
                {
                    table->col_count = table->rows[j].count;
                }
            }

            free(y_coords);
            free(y_clusters.cluster_ids);
        }
        else
        {
            free(table_cells.cells);
        }
    }
    free(used);
    return table_count;
}

// Header Detection
static bool row_has_bold(bbox_t bbox)
{
    for (int i = 0; i < g_chars.count; i++)
    {
        char_info_t *c = &g_chars.chars[i];
        fz_rect char_rect = {c->bbox.x0, c->bbox.y0, c->bbox.x1, c->bbox.y1};
        fz_rect bbox_rect = {bbox.x0, bbox.y0, bbox.x1, bbox.y1};
        if (!fz_is_empty_rect(fz_intersect_rect(char_rect, bbox_rect)))
        {
            if (c->flags & FZ_STEXT_STYLE_BOLD)
                return true;
        }
    }
    return false;
}

static int get_top_color_from_pixmap(fz_context *ctx, fz_pixmap *pix)
{
    (void)ctx; // Suppress unused parameter warning
    if (!pix || !pix->samples)
        return 0;
    int w = pix->w;
    int h = pix->h;
    int n = pix->n;
    if (n < 3)
        return 0; // not RGB/RGBA

    // Use a map for color histogram. Since we can't use std::map, we'll use a simple array of structs.
    // This is a simple implementation and may not be suitable for images with many colors.
    struct color_count
    {
        int color;
        int count;
    };
    struct color_count color_hist[256];
    int num_colors = 0;

    for (int y = 0; y < h; y++)
    {
        unsigned char *p = pix->samples + y * pix->stride;
        for (int x = 0; x < w; x++)
        {
            int color = (p[0] << 16) | (p[1] << 8) | p[2];
            p += n;

            int found = 0;
            for (int i = 0; i < num_colors; i++)
            {
                if (color_hist[i].color == color)
                {
                    color_hist[i].count++;
                    found = 1;
                    break;
                }
            }
            if (!found && num_colors < 256)
            {
                color_hist[num_colors].color = color;
                color_hist[num_colors].count = 1;
                num_colors++;
            }
        }
    }

    if (num_colors == 0)
        return 0;

    int top_color = 0;
    int max_count = 0;
    for (int i = 0; i < num_colors; i++)
    {
        if (color_hist[i].count > max_count)
        {
            max_count = color_hist[i].count;
            top_color = color_hist[i].color;
        }
    }
    return top_color;
}

static fz_pixmap *get_bbox_pixmap(fz_context *ctx, fz_page *page, bbox_t bbox)
{
    fz_rect clip = {bbox.x0, bbox.y0, bbox.x1, bbox.y1};
    fz_irect bbox_irect = fz_round_rect(clip);
    if (fz_is_empty_irect(bbox_irect))
        return NULL;

    fz_pixmap *pix = fz_new_pixmap_with_bbox(ctx, fz_device_rgb(ctx), bbox_irect, NULL, 0);
    fz_clear_pixmap_with_value(ctx, pix, 0xFF);

    fz_device *dev = fz_new_draw_device(ctx, fz_identity, pix);
    fz_run_page(ctx, page, dev, fz_identity, NULL);
    fz_drop_device(ctx, dev);

    return pix;
}

static table_header_t get_table_header(fz_context *ctx, fz_page *page, const table_t *table)
{
    table_header_t header = {0};
    if (table->row_count == 0)
        return header;

    const table_row_t *first_row = &table->rows[0];
    bbox_t first_row_bbox = objects_to_bbox(first_row->cells, first_row->count);

    // Default to first row as header
    header.bbox = first_row_bbox;
    header.col_count = first_row->count;
    header.cells = malloc(header.col_count * sizeof(bbox_t));
    header.names = malloc(header.col_count * sizeof(char *));
    header.external = false;

    if (!header.cells || !header.names)
    {
        if (header.cells)
            free(header.cells);
        if (header.names)
            free(header.names);
        memset(&header, 0, sizeof(header));
        return header; // Early exit on allocation failure
    }
    memcpy(header.cells, first_row->cells, first_row->count * sizeof(bbox_t));

    // --- Heuristics to decide if first row is the header ---

    // 1. Background color check
    float row_height = first_row_bbox.y1 - first_row_bbox.y0;
    if (row_height > 0)
    {
        bbox_t above_bbox = {first_row_bbox.x0, first_row_bbox.y0 - row_height, first_row_bbox.x1, first_row_bbox.y0};
        fz_pixmap *pix0 = get_bbox_pixmap(ctx, page, first_row_bbox);
        fz_pixmap *pixt = get_bbox_pixmap(ctx, page, above_bbox);
        if (pix0 && pixt)
        {
            int color0 = get_top_color_from_pixmap(ctx, pix0);
            int colort = get_top_color_from_pixmap(ctx, pixt);
            if (color0 != colort)
            {
                header.external = false;
                fz_drop_pixmap(ctx, pix0);
                fz_drop_pixmap(ctx, pixt);
                goto finalize_header;
            }
        }
        if (pix0)
            fz_drop_pixmap(ctx, pix0);
        if (pixt)
            fz_drop_pixmap(ctx, pixt);
    }

    // 2. Bold text check
    bool first_row_bold = row_has_bold(first_row_bbox);
    if (table->row_count > 1)
    {
        const table_row_t *second_row = &table->rows[1];
        bbox_t second_row_bbox = objects_to_bbox(second_row->cells, second_row->count);
        if (first_row_bold && !row_has_bold(second_row_bbox))
        {
            header.external = false; // Confirmed internal header
            goto finalize_header;
        }
    }

    // --- Logic to find external header ---
    word_info_list_t words_above;
    init_word_info_list(&words_above, 100);
    char_info_list_t chars_above;
    init_char_info_list(&chars_above, 1000);

    for (int i = 0; i < g_chars.count; i++)
    {
        if (g_chars.chars[i].bbox.y1 < table->bbox.y0)
        {
            add_char_info(&chars_above, &g_chars.chars[i]);
        }
    }
    extract_words_from_chars(&words_above, &chars_above, DEFAULT_X_TOLERANCE, DEFAULT_Y_TOLERANCE);

    if (words_above.count > 0)
    {
        float *col_x = malloc(table->col_count * sizeof(float));
        if (col_x)
        {
            for (int i = 0; i < first_row->count - 1; i++)
                col_x[i] = first_row->cells[i].x1;

            bool crossing_found = false;
            for (int i = 0; i < words_above.count; i++)
            {
                for (int j = 0; j < first_row->count - 1; j++)
                {
                    if (words_above.words[i].bbox.x0 < col_x[j] && words_above.words[i].bbox.x1 > col_x[j])
                    {
                        crossing_found = true;
                        break;
                    }
                }
                if (crossing_found)
                    break;
            }
            free(col_x);

            if (!crossing_found)
            {
                float *y_coords = malloc(words_above.count * sizeof(float));
                for (int i = 0; i < words_above.count; i++)
                    y_coords[i] = words_above.words[i].bbox.y0;
                cluster_result_t y_clusters = cluster_values(y_coords, words_above.count, DEFAULT_Y_TOLERANCE);

                if (y_clusters.num_clusters > 0)
                {
                    bbox_t header_bbox = {table->bbox.x0, INFINITY, table->bbox.x1, -INFINITY};
                    int last_cluster_id = -1;
                    float last_y = table->bbox.y0;

                    for (int i = 0; i < words_above.count; i++)
                    {
                        float max_y0_in_cluster = -1;
                        int word_idx = -1;
                        for (int j = 0; j < words_above.count; j++)
                        {
                            if (y_clusters.cluster_ids[j] == i)
                            {
                                if (words_above.words[j].bbox.y0 > max_y0_in_cluster)
                                {
                                    max_y0_in_cluster = words_above.words[j].bbox.y0;
                                    word_idx = j;
                                }
                            }
                        }
                        if (word_idx == -1)
                            continue;
                        if (y_clusters.cluster_ids[word_idx] == last_cluster_id)
                            continue;
                        last_cluster_id = y_clusters.cluster_ids[word_idx];

                        float line_height = words_above.words[word_idx].bbox.y1 - words_above.words[word_idx].bbox.y0;
                        if (last_y - max_y0_in_cluster > 1.5 * line_height)
                        {
                            break;
                        }
                        header_bbox.y0 = min_f(header_bbox.y0, words_above.words[word_idx].bbox.y0);
                        header_bbox.y1 = max_f(header_bbox.y1, words_above.words[word_idx].bbox.y1);
                        last_y = header_bbox.y0;
                    }

                    if (header_bbox.y0 < INFINITY)
                    {
                        bool header_above_is_bold = row_has_bold(header_bbox);
                        if (!first_row_bold || (first_row_bold && header_above_is_bold))
                        {
                            header.external = true;
                            header.bbox = header_bbox;
                            free(header.cells);
                            header.cells = malloc(header.col_count * sizeof(bbox_t));
                            for (int i = 0; i < header.col_count; i++)
                            {
                                header.cells[i] = (bbox_t){first_row->cells[i].x0, header_bbox.y0, first_row->cells[i].x1, header_bbox.y1};
                            }
                        }
                    }
                }
                if (y_coords)
                    free(y_coords);
                if (y_clusters.cluster_ids)
                    free(y_clusters.cluster_ids);
            }
        }
    }
    free(words_above.words);
    free(chars_above.chars);

finalize_header:
    for (int i = 0; i < header.col_count; i++)
    {
        header.names[i] = extract_cell_text(ctx, g_textpage, header.cells[i], false);
        if (!header.names[i] || strlen(header.names[i]) == 0)
        {
            if (header.names[i])
                free(header.names[i]);
            header.names[i] = malloc(20);
            if (header.names[i])
                snprintf(header.names[i], 20, "Col%d", i + 1);
        }
        else
        {
            trim_string(header.names[i]);
        }
    }

    return header;
}

// Display List Processing for Path Extraction
typedef struct
{
    fz_device super;
    edge_info_list_t *edges;
    fz_rect clip;
} edge_device_t;

// --- Path walking implementation to replace direct fz_path access ---
typedef struct
{
    edge_device_t *edev;
    fz_matrix ctm;
    fz_point last_pos;
} path_walker_state;

static void
path_walker_moveto(fz_context *ctx, void *arg, float x, float y)
{
    (void)ctx; // Suppress unused parameter warning
    path_walker_state *state = (path_walker_state *)arg;
    state->last_pos.x = x;
    state->last_pos.y = y;
}

static void
path_walker_lineto(fz_context *ctx, void *arg, float x, float y)
{
    (void)ctx; // Suppress unused parameter warning
    path_walker_state *state = (path_walker_state *)arg;
    edge_device_t *edev = state->edev;

    fz_point p0 = state->last_pos;
    fz_point p1 = {x, y};

    state->last_pos = p1;

    p0 = fz_transform_point(p0, state->ctm);
    p1 = fz_transform_point(p1, state->ctm);

    bool is_h = float_equal(p0.y, p1.y, 0.1f);
    bool is_v = float_equal(p0.x, p1.x, 0.1f);

    if (!is_h && !is_v)
        return;

    bbox_t line_bbox = {
        min_f(p0.x, p1.x), min_f(p0.y, p1.y),
        max_f(p0.x, p1.x), max_f(p0.y, p1.y)};

    fz_rect bbox_rect = {line_bbox.x0, line_bbox.y0, line_bbox.x1, line_bbox.y1};
    if (fz_is_empty_rect(fz_intersect_rect(bbox_rect, edev->clip)))
    {
        return;
    }

    edge_info_t edge = line_to_edge(line_bbox, is_h ? 'h' : 'v');
    strcpy(edge.object_type, "line");
    add_edge_info(edev->edges, &edge);
}

static void
path_walker_curveto(fz_context *ctx, void *arg, float x1, float y1, float x2, float y2, float x3, float y3)
{
    (void)ctx;
    path_walker_state *state = (path_walker_state *)arg;
    // For simplicity, treat curves as line segments to the end point
    path_walker_lineto(ctx, arg, x3, y3);
}

static void
path_walker_quadto(fz_context *ctx, void *arg, float x1, float y1, float x2, float y2)
{
    (void)ctx;
    path_walker_state *state = (path_walker_state *)arg;
    // For simplicity, treat quad curves as line segments to the end point
    path_walker_lineto(ctx, arg, x2, y2);
}

static void
path_walker_closepath(fz_context *ctx, void *arg)
{
    (void)ctx;
    (void)arg;
    // No action needed for closepath in our edge extraction
}

static const fz_path_walker path_walker = {
    .moveto = path_walker_moveto,
    .lineto = path_walker_lineto,
    .curveto = path_walker_curveto,
    .quadto = path_walker_quadto,
    .closepath = path_walker_closepath};

static void
fz_stroke_path_to_edges(fz_context *ctx, fz_device *dev, const fz_path *path, const fz_stroke_state *stroke, fz_matrix ctm, fz_colorspace *colorspace, const float *color, float alpha, fz_color_params color_params)
{
    (void)stroke;
    (void)colorspace;
    (void)color;
    (void)alpha;
    (void)color_params; // Suppress unused parameter warnings

    path_walker_state state;
    state.edev = (edge_device_t *)dev;
    state.ctm = ctm;
    state.last_pos.x = 0;
    state.last_pos.y = 0;

    fz_walk_path(ctx, path, &path_walker, &state);
}

static void
fz_fill_path_to_edges(fz_context *ctx, fz_device *dev, const fz_path *path, int even_odd, fz_matrix ctm, fz_colorspace *colorspace, const float *color, float alpha, fz_color_params color_params)
{
    (void)even_odd;
    (void)colorspace;
    (void)color;
    (void)alpha;
    (void)color_params; // Suppress unused parameter warnings

    edge_device_t *edev = (edge_device_t *)dev;
    fz_rect bbox = fz_bound_path(ctx, path, NULL, ctm);

    if (fz_is_empty_rect(fz_intersect_rect(bbox, edev->clip)))
    {
        return;
    }

    // Treat thin filled rectangles as lines
    if (bbox.x1 - bbox.x0 < 3)
    { // thin vertical rect
        bbox_t line_bbox = {bbox.x0, bbox.y0, bbox.x0, bbox.y1};
        edge_info_t edge = line_to_edge(line_bbox, 'v');
        strcpy(edge.object_type, "rect");
        add_edge_info(edev->edges, &edge);
    }
    if (bbox.y1 - bbox.y0 < 3)
    { // thin horizontal rect
        bbox_t line_bbox = {bbox.x0, bbox.y0, bbox.x1, bbox.y0};
        edge_info_t edge = line_to_edge(line_bbox, 'h');
        strcpy(edge.object_type, "rect");
        add_edge_info(edev->edges, &edge);
    }
}

static fz_device *new_edge_device(fz_context *ctx, edge_info_list_t *edges, fz_rect clip)
{
    edge_device_t *dev = fz_new_derived_device(ctx, edge_device_t);
    dev->super.stroke_path = fz_stroke_path_to_edges;
    dev->super.fill_path = fz_fill_path_to_edges;
    dev->edges = edges;
    dev->clip = clip;
    return (fz_device *)dev;
}

// Main Table Finding Logic
static void make_edges_from_paths(fz_context *ctx, fz_page *page, edge_info_list_t *edges, const table_settings_t *settings, fz_rect clip)
{
    if (strcmp(settings->vertical_strategy, "explicit") == 0)
    {
        for (int i = 0; i < settings->num_explicit_vertical_lines; i++)
        {
            float x = settings->explicit_vertical_lines[i];
            bbox_t line_bbox = {x, clip.y0, x, clip.y1};
            edge_info_t edge = line_to_edge(line_bbox, 'v');
            add_edge_info(edges, &edge);
        }
    }
    if (strcmp(settings->horizontal_strategy, "explicit") == 0)
    {
        for (int i = 0; i < settings->num_explicit_horizontal_lines; i++)
        {
            float y = settings->explicit_horizontal_lines[i];
            bbox_t line_bbox = {clip.x0, y, clip.x1, y};
            edge_info_t edge = line_to_edge(line_bbox, 'h');
            add_edge_info(edges, &edge);
        }
    }

    if (strcmp(settings->vertical_strategy, "lines") == 0 || strcmp(settings->horizontal_strategy, "lines") == 0 ||
        strcmp(settings->vertical_strategy, "lines_strict") == 0 || strcmp(settings->horizontal_strategy, "lines_strict") == 0)
    {
        fz_device *dev = new_edge_device(ctx, edges, clip);
        fz_run_page(ctx, page, dev, fz_identity, NULL);
        fz_drop_device(ctx, dev);
    }

    // Text-based strategy
    if (strcmp(settings->horizontal_strategy, "text") == 0 || strcmp(settings->vertical_strategy, "text") == 0)
    {
        word_info_list_t words = {0};
        extract_words_from_chars(&words, &g_chars, settings->text_x_tolerance, settings->text_y_tolerance);

        if (strcmp(settings->horizontal_strategy, "text") == 0)
        {
            words_to_edges_h(edges, &words, (int)settings->min_words_horizontal);
        }
        if (strcmp(settings->vertical_strategy, "text") == 0)
        {
            words_to_edges_v(edges, &words, (int)settings->min_words_vertical);
        }

        if (words.words)
            free(words.words);
    }
}

static table_settings_t init_table_settings(void)
{
    table_settings_t s = {0};
    strcpy(s.vertical_strategy, "lines");
    strcpy(s.horizontal_strategy, "lines");
    s.snap_tolerance = DEFAULT_SNAP_TOLERANCE;
    s.snap_x_tolerance = UNSET_FLOAT;
    s.snap_y_tolerance = UNSET_FLOAT;
    s.join_tolerance = DEFAULT_JOIN_TOLERANCE;
    s.join_x_tolerance = UNSET_FLOAT;
    s.join_y_tolerance = UNSET_FLOAT;
    s.edge_min_length = 3.0f;
    s.min_words_vertical = DEFAULT_MIN_WORDS_VERTICAL;
    s.min_words_horizontal = DEFAULT_MIN_WORDS_HORIZONTAL;
    s.intersection_tolerance = 3.0f;
    s.intersection_x_tolerance = UNSET_FLOAT;
    s.intersection_y_tolerance = UNSET_FLOAT;
    s.text_x_tolerance = DEFAULT_X_TOLERANCE;
    s.text_y_tolerance = DEFAULT_Y_TOLERANCE;
    s.explicit_vertical_lines = NULL;
    s.num_explicit_vertical_lines = 0;
    s.explicit_horizontal_lines = NULL;
    s.num_explicit_horizontal_lines = 0;
    return s;
}

static void resolve_table_settings(table_settings_t *s)
{
    if (is_unset(s->snap_x_tolerance))
        s->snap_x_tolerance = s->snap_tolerance;
    if (is_unset(s->snap_y_tolerance))
        s->snap_y_tolerance = s->snap_tolerance;
    if (is_unset(s->join_x_tolerance))
        s->join_x_tolerance = s->join_tolerance;
    if (is_unset(s->join_y_tolerance))
        s->join_y_tolerance = s->join_tolerance;
    if (is_unset(s->intersection_x_tolerance))
        s->intersection_x_tolerance = s->intersection_tolerance;
    if (is_unset(s->intersection_y_tolerance))
        s->intersection_y_tolerance = s->intersection_tolerance;
}

static table_finder_t *find_tables(fz_context *ctx, fz_page *page, fz_rect clip, const table_settings_t *user_settings)
{
    table_finder_t *finder = malloc(sizeof(table_finder_t));
    if (!finder)
    {
        fprintf(stderr, "Memory allocation failed for table_finder_t\n");
        return NULL;
    }

    memset(finder, 0, sizeof(table_finder_t));
    finder->tables = malloc(MAX_TABLES * sizeof(table_t));
    if (!finder->tables)
    {
        free(finder);
        return NULL;
    }
    finder->capacity = MAX_TABLES;

    table_settings_t settings = user_settings ? *user_settings : init_table_settings();
    resolve_table_settings(&settings);

    make_chars(ctx, page, clip);

    init_edge_info_list(&finder->edges, MAX_EDGES);
    make_edges_from_paths(ctx, page, &finder->edges, &settings, clip);

    snap_edges(&finder->edges, settings.snap_x_tolerance, settings.snap_y_tolerance);
    join_edges(&finder->edges, settings.join_x_tolerance, settings.join_y_tolerance);

    filter_edges(&finder->edges, settings.edge_min_length);

    finder->intersections.intersections = malloc(MAX_INTERSECTIONS * sizeof(intersection_t));
    if (finder->intersections.intersections)
    {
        finder->intersections.capacity = MAX_INTERSECTIONS;
        edges_to_intersections(&finder->intersections, &finder->edges,
                               settings.intersection_x_tolerance, settings.intersection_y_tolerance);
    }

    init_cell_list(&finder->cells, MAX_CELLS);
    intersections_to_cells(&finder->cells, &finder->intersections);

    finder->count = cells_to_tables(finder->tables, ctx, page, &finder->cells);

    for (int i = 0; i < finder->count; i++)
    {
        finder->tables[i].header = get_table_header(ctx, page, &finder->tables[i]);
    }

    return finder;
}

// Export and Cleanup
static char ***extract_table_to_array(fz_context *ctx, const table_t *table)
{
    if (table->row_count == 0 || table->col_count == 0)
        return NULL;

    char ***result = malloc(table->row_count * sizeof(char **));
    if (!result)
        return NULL;

    for (int i = 0; i < table->row_count; i++)
    {
        result[i] = malloc(table->col_count * sizeof(char *));
        if (!result[i])
        {
            // Clean up on failure
            for (int cleanup = 0; cleanup < i; cleanup++)
            {
                for (int j = 0; j < table->col_count; j++)
                {
                    if (result[cleanup][j])
                        free(result[cleanup][j]);
                }
                free(result[cleanup]);
            }
            free(result);
            return NULL;
        }

        const table_row_t *row = &table->rows[i];
        for (int j = 0; j < table->col_count; j++)
        {
            if (j < row->count)
            {
                result[i][j] = extract_cell_text(ctx, g_textpage, row->cells[j], false);
                if (!result[i][j])
                {
                    result[i][j] = strdup("");
                }
            }
            else
            {
                result[i][j] = strdup("");
            }
        }
    }
    return result;
}

static void escape_markdown(char *dest, const char *src, size_t dest_size)
{
    char *d = dest;
    const char *s = src;
    while (*s && (d - dest) < (ptrdiff_t)(dest_size - 1))
    {
        // Only escape pipe characters for table cells
        if (*s == '|')
        {
            if ((d - dest) < (ptrdiff_t)(dest_size - 3))
            {
                strcpy(d, "\\|");
                d += 2;
            }
            else
            {
                break;
            }
        }
        else
        {
            *d++ = *s;
        }
        s++;
    }
    *d = '\0';
}

static char *table_to_markdown(fz_context *ctx, const table_t *table, bool clean, bool fill_empty)
{
    if (table->row_count == 0 || table->col_count == 0)
        return NULL;

    char *markdown = malloc(50000);
    if (!markdown)
        return NULL;

    markdown[0] = '\0';

    // Use a temporary buffer for header names to handle line breaks
    char temp_header_name[MAX_TEXT_LENGTH];

    // Get cell data with markdown formatting for the whole table
    char ***table_data_md = malloc(table->row_count * sizeof(char **));
    if (!table_data_md)
    {
        free(markdown);
        return NULL;
    }

    for (int i = 0; i < table->row_count; i++)
    {
        table_data_md[i] = malloc(table->col_count * sizeof(char *));
        if (!table_data_md[i])
        {
            // Cleanup
            for (int k = 0; k < i; k++)
            {
                for (int l = 0; l < table->col_count; l++)
                    free(table_data_md[k][l]);
                free(table_data_md[k]);
            }
            free(table_data_md);
            free(markdown);
            return NULL;
        }
        const table_row_t *row = &table->rows[i];
        for (int j = 0; j < table->col_count; j++)
        {
            if (j < row->count)
            {
                table_data_md[i][j] = extract_cell_text(ctx, g_textpage, row->cells[j], false);
                if (!table_data_md[i][j])
                {
                    table_data_md[i][j] = strdup("");
                }
                else
                {
                    // Clean up the text - remove extra whitespace and newlines
                    char *clean_text = table_data_md[i][j];
                    char *src = clean_text;
                    char *dst = clean_text;
                    bool prev_space = false;

                    while (*src)
                    {
                        if (*src == '\n' || *src == '\r' || *src == '\t')
                        {
                            if (!prev_space)
                            {
                                *dst++ = ' ';
                                prev_space = true;
                            }
                        }
                        else if (*src == ' ')
                        {
                            if (!prev_space)
                            {
                                *dst++ = ' ';
                                prev_space = true;
                            }
                        }
                        else
                        {
                            *dst++ = *src;
                            prev_space = false;
                        }
                        src++;
                    }
                    *dst = '\0';

                    // Trim trailing whitespace
                    dst--;
                    while (dst >= clean_text && *dst == ' ')
                    {
                        *dst = '\0';
                        dst--;
                    }
                }
            }
            else
            {
                table_data_md[i][j] = strdup("");
            }
        }
    }

    if (fill_empty)
    {
        // Horizontal fill
        for (int i = 0; i < table->row_count; i++)
        {
            for (int j = 1; j < table->col_count; j++)
            {
                if (strlen(table_data_md[i][j]) == 0 && strlen(table_data_md[i][j - 1]) > 0)
                {
                    free(table_data_md[i][j]);
                    table_data_md[i][j] = strdup(table_data_md[i][j - 1]);
                }
            }
        }
        // Vertical fill
        for (int j = 0; j < table->col_count; j++)
        {
            for (int i = 1; i < table->row_count; i++)
            {
                if (strlen(table_data_md[i][j]) == 0 && strlen(table_data_md[i - 1][j]) > 0)
                {
                    free(table_data_md[i][j]);
                    table_data_md[i][j] = strdup(table_data_md[i - 1][j]);
                }
            }
        }
    }

    // Build markdown header
    if (strlen(markdown) < 49900)
        strcat(markdown, "|");
    for (int j = 0; j < table->col_count && strlen(markdown) < 49900; j++)
    {
        const char *name = (j < table->header.col_count && table->header.names[j]) ? table->header.names[j] : "";

        // Clean up whitespace and newlines in header names
        char *dest = temp_header_name;
        const char *src = name;
        bool prev_space = false;

        while (*src && (dest - temp_header_name) < sizeof(temp_header_name) - 1)
        {
            if (*src == '\n' || *src == '\r' || *src == '\t')
            {
                if (!prev_space)
                {
                    *dest++ = ' ';
                    prev_space = true;
                }
            }
            else if (*src == ' ')
            {
                if (!prev_space)
                {
                    *dest++ = ' ';
                    prev_space = true;
                }
            }
            else
            {
                *dest++ = *src;
                prev_space = false;
            }
            src++;
        }
        *dest = '\0';

        // Trim trailing whitespace
        dest--;
        while (dest >= temp_header_name && *dest == ' ')
        {
            *dest = '\0';
            dest--;
        }

        if (clean)
        {
            char clean_name[MAX_TEXT_LENGTH];
            escape_markdown(clean_name, temp_header_name, sizeof(clean_name));
            strncat(markdown, clean_name, 49900 - strlen(markdown) - 1);
        }
        else
        {
            strncat(markdown, temp_header_name, 49900 - strlen(markdown) - 1);
        }
        if (strlen(markdown) < 49900)
            strcat(markdown, "|");
    }
    if (strlen(markdown) < 49900)
        strcat(markdown, "\n|");

    for (int j = 0; j < table->col_count && strlen(markdown) < 49900; j++)
    {
        strncat(markdown, "---", 49900 - strlen(markdown) - 1);
        if (strlen(markdown) < 49900)
            strcat(markdown, "|");
    }
    if (strlen(markdown) < 49900)
        strcat(markdown, "\n");

    // Build table rows
    int start_row = table->header.external ? 0 : 1;
    for (int i = start_row; i < table->row_count && strlen(markdown) < 49900; i++)
    {
        if (strlen(markdown) < 49900)
            strcat(markdown, "|");
        for (int j = 0; j < table->col_count && strlen(markdown) < 49900; j++)
        {
            if (table_data_md[i][j])
            {
                if (clean)
                {
                    char clean_text[MAX_TEXT_LENGTH];
                    escape_markdown(clean_text, table_data_md[i][j], sizeof(clean_text));
                    strncat(markdown, clean_text, 49900 - strlen(markdown) - 1);
                }
                else
                {
                    strncat(markdown, table_data_md[i][j], 49900 - strlen(markdown) - 1);
                }
            }
            if (strlen(markdown) < 49900)
                strcat(markdown, "|");
        }
        if (strlen(markdown) < 49900)
            strcat(markdown, "\n");
    }

    // Clean up table_data_md
    for (int i = 0; i < table->row_count; i++)
    {
        for (int j = 0; j < table->col_count; j++)
        {
            if (table_data_md[i][j])
                free(table_data_md[i][j]);
        }
        free(table_data_md[i]);
    }
    free(table_data_md);

    return markdown;
}

static void free_table_finder(table_finder_t *finder)
{
    if (!finder)
        return;

    for (int i = 0; i < finder->count; i++)
    {
        table_t *table = &finder->tables[i];
        if (table->header.cells)
        {
            free(table->header.cells);
            table->header.cells = NULL;
        }
        if (table->header.names)
        {
            for (int j = 0; j < table->header.col_count; j++)
            {
                if (table->header.names[j])
                    free(table->header.names[j]);
            }

            free(table->header.names);
            table->header.names = NULL;
        }
        if (table->rows)
        {
            for (int j = 0; j < table->row_count; j++)
            {
                if (table->rows[j].cells)
                    free(table->rows[j].cells);
            }
            free(table->rows);
            table->rows = NULL;
        }
        if (table->cells.cells)
        {
            free(table->cells.cells);
            table->cells.cells = NULL;
        }
    }

    if (finder->tables)
    {
        free(finder->tables);
        finder->tables = NULL;
    }
    if (finder->edges.edges)
    {
        free(finder->edges.edges);
        finder->edges.edges = NULL;
    }

    if (finder->intersections.intersections)
    {
        for (int i = 0; i < finder->intersections.count; i++)
        {
            if (finder->intersections.intersections[i].v_edges.edges)
            {
                free(finder->intersections.intersections[i].v_edges.edges);
                finder->intersections.intersections[i].v_edges.edges = NULL;
            }
            if (finder->intersections.intersections[i].h_edges.edges)
            {
                free(finder->intersections.intersections[i].h_edges.edges);
                finder->intersections.intersections[i].h_edges.edges = NULL;
            }
        }
        free(finder->intersections.intersections);
        finder->intersections.intersections = NULL;
    }

    if (finder->cells.cells)
    {
        free(finder->cells.cells);
        finder->cells.cells = NULL;
    }
    free(finder);
}

static void cleanup_globals(fz_context *ctx)
{
    if (g_chars.chars)
    {
        free(g_chars.chars);
        g_chars.chars = NULL;
        g_chars.count = 0;
        g_chars.capacity = 0;
    }
    if (g_edges.edges)
    {
        free(g_edges.edges);
        g_edges.edges = NULL;
        g_edges.count = 0;
        g_edges.capacity = 0;
    }
    if (g_textpage)
    {
        if (ctx)
        {
            fz_drop_stext_page(ctx, g_textpage);
        }
        g_textpage = NULL;
    }
}

// Public function to extract tables as markdown
char **extract_tables_as_markdown(fz_context *ctx, fz_document *doc, int page_number, int *count)
{
    *count = 0;
    if (!ctx || !doc)
        return NULL;

    fz_page *volatile page = NULL;
    table_finder_t *volatile finder = NULL;
    char **volatile result = NULL;

    fz_try(ctx)
    {
        page = fz_load_page(ctx, doc, page_number);
        if (!page)
            fz_throw(ctx, FZ_ERROR_GENERIC, "Cannot load page");

        fz_rect page_bounds = fz_bound_page(ctx, page);
        table_settings_t settings = init_table_settings();
        resolve_table_settings(&settings);

        // Find tables on the page
        finder = find_tables(ctx, page, page_bounds, &settings);
        if (!finder || finder->count == 0)
        {
            *count = 0;
            if (page)
                fz_drop_page(ctx, page);
            return NULL;
        }

        // Allocate result array
        result = malloc(finder->count * sizeof(char *));
        if (!result)
            fz_throw(ctx, FZ_ERROR_GENERIC, "Cannot allocate result array");

        // Convert each table to markdown
        for (int i = 0; i < finder->count; i++)
        {
            result[i] = table_to_markdown(ctx, &finder->tables[i], true, true);
            if (result[i])
                (*count)++;
        }

        // If no tables were successfully converted, free and return NULL
        if (*count == 0)
        {
            free(result);
            result = NULL;
        }
    }
    fz_always(ctx)
    {
        if (finder)
            free_table_finder(finder);
        if (page)
            fz_drop_page(ctx, page);
        cleanup_globals(ctx);
    }
    fz_catch(ctx)
    {
        if (result)
        {
            for (int i = 0; i < *count; i++)
            {
                if (result[i])
                    free(result[i]);
            }
            free(result);
            result = NULL;
        }
        *count = 0;
    }

    return result;
}

// Free the result returned by extract_tables_as_markdown
void free_table_markdown_array(char **table_markdowns, int count)
{
    if (!table_markdowns)
        return;

    for (int i = 0; i < count; i++)
    {
        if (table_markdowns[i])
            free(table_markdowns[i]);
    }
    free(table_markdowns);
}