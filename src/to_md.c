/*

 * Standalone PDF to Markdown Converter with low-memory parallel processing
 * Pure C implementation - no Python dependencies
 *
 * Compile with:
 * gcc -O3 -march=native -std=c99 -pthread -o to_md_parallel to_md_parallel.c -lmupdf -lm
 *
 * Usage:
 * ./to_md_parallel input.pdf [output.md]
 */

// Enable POSIX functions - MUST be defined before any includes
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

// PDF to markdown conversion
#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include <ctype.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>
#include <mupdf/fitz.h>
#include "../include/get_raw_lines.h"
#include "../include/multicolumn.h"

// MuPDF style constants (if not defined)
#ifndef FZ_STEXT_STYLE_BOLD
#define FZ_STEXT_STYLE_BOLD 1
#endif
#ifndef FZ_STEXT_STYLE_ITALIC
#define FZ_STEXT_STYLE_ITALIC 2
#endif
#ifndef FZ_STEXT_STYLE_MONOSPACE
#define FZ_STEXT_STYLE_MONOSPACE 8
#endif

// MuPDF font flag constants
#ifndef FZ_FONT_FLAG_BOLD
#define FZ_FONT_FLAG_BOLD 1
#endif
#ifndef FZ_FONT_FLAG_ITALIC
#define FZ_FONT_FLAG_ITALIC 2
#endif
#ifndef FZ_FONT_FLAG_MONOSPACE
#define FZ_FONT_FLAG_MONOSPACE 8
#endif

// MuPDF text extraction flags (matching Python version exactly)
#ifndef FZ_STEXT_CLIP
#define FZ_STEXT_CLIP 1
#endif
#ifndef FZ_STEXT_ACCURATE_BBOXES
#define FZ_STEXT_ACCURATE_BBOXES 2
#endif
#ifndef FZ_STEXT_COLLECT_STYLES
#define FZ_STEXT_COLLECT_STYLES 32768
#endif
#ifndef FZ_STEXT_USE_GID_FOR_UNKNOWN_UNICODE
#define FZ_STEXT_USE_GID_FOR_UNKNOWN_UNICODE 4
#endif
#ifndef FZ_STEXT_PRESERVE_LIGATURES
#define FZ_STEXT_PRESERVE_LIGATURES 8
#endif
#ifndef FZ_STEXT_PRESERVE_WHITESPACE
#define FZ_STEXT_PRESERVE_WHITESPACE 16
#endif

// Constants
#define MAX_FONT_SIZE 200
#define MAX_BUFFER_SIZE (16 * 1024 * 1024)   // 16MB batch buffer
#define BATCH_BUFFER_SIZE (32 * 1024 * 1024) // 32MB batch buffer for I/O
#define INITIAL_BUFFER_SIZE 8192
#define MAX_SPANS_PER_LINE 1000 // Fixed-size reusable span array
#define MAX_LINES_PER_PAGE 10000
#define BATCH_PAGES 10
#define MAX_SPAN_TEXT_SIZE 4096 // Maximum text per span

// Job structure for batch assignment
typedef struct
{
    int start_page;
    int end_page;
    int batch_num;
} Job;

// Worker thread argument structure
typedef struct
{
    const char *pdf_path;
    const char *output_path; // Pass the base output path for temp file naming
    Job *jobs;
    int *next_job_index;
    pthread_mutex_t *job_mutex;
    pthread_mutex_t *table_mutex; // Mutex for table extraction (global state protection)
} WorkerArgs;

// Global mutex for table extraction (protects global variables in table.c)
static pthread_mutex_t g_table_extraction_mutex = PTHREAD_MUTEX_INITIALIZER;

// String builder for efficient text construction
typedef struct
{
    char *data;
    size_t length;
    size_t capacity;
} StringBuilder;

// Batch buffer for memory-efficient I/O
typedef struct
{
    char *data;
    size_t length;
    size_t capacity;
    FILE *output_file;
} BatchBuffer;

// Font analysis structure
typedef struct
{
    int font_counts[MAX_FONT_SIZE];
    double body_limit;
    char header_mapping[MAX_FONT_SIZE][8]; // e.g., "## " for level 2
} FontAnalyzer;

// Text span information
typedef struct
{
    char *text;
    char *font;
    int flags;
    int char_flags;
    double size;
    fz_rect bbox;
    fz_matrix trm;
    int bold;
    int italic;
    int mono;
    int strikeout;
    int block_num;
    int superscript;
} TextSpan;

// Reusable span array for memory efficiency
typedef struct
{
    TextSpan spans[MAX_SPANS_PER_LINE];
    char text_buffers[MAX_SPANS_PER_LINE][MAX_SPAN_TEXT_SIZE];
    int count;
} ReusableSpanArray;

// Text line information
typedef struct
{
    TextSpan *spans;
    int span_count;
    int capacity;
    fz_rect bbox;
} TextLine;

// Text block information
typedef struct
{
    TextLine *lines;
    int line_count;
    int capacity;
    fz_rect bbox;
    int block_type;
} TextBlock;

// Link information
typedef struct
{
    char *uri;
    fz_rect bbox;
} LinkInfo;

// Page processing parameters (simplified - no tables/images)
typedef struct
{
    BatchBuffer *batch_buffer;
    ReusableSpanArray *reusable_spans;
    FontAnalyzer *font_analyzer;
    fz_rect clip;
    fz_stext_page *textpage; // Temporary handle, not owned
    fz_rect *table_rects;
    int table_count;
} PageParams;

// Function declarations
static StringBuilder *sb_create(size_t initial_capacity);
static void sb_destroy(StringBuilder *sb);
static int sb_append(StringBuilder *sb, const char *str);
static int sb_append_char(StringBuilder *sb, char c);
static int sb_append_formatted(StringBuilder *sb, const char *format, ...);
static int sb_ensure_capacity(StringBuilder *sb, size_t needed);

// Batch buffer functions for memory-efficient I/O
static BatchBuffer *batch_buffer_create(FILE *output_file);
static void batch_buffer_destroy(BatchBuffer *buffer);
static int batch_buffer_append(BatchBuffer *buffer, const char *str);
static int batch_buffer_append_formatted(BatchBuffer *buffer, const char *format, ...);
static int batch_buffer_flush(BatchBuffer *buffer);
static void batch_buffer_reset(BatchBuffer *buffer);

// Reusable span array functions
static void reusable_spans_init(ReusableSpanArray *spans);
static void reusable_spans_reset(ReusableSpanArray *spans);
static int append_markdown_for_span(BatchBuffer *buffer, TextSpan *span, FontAnalyzer *analyzer);

// Font analyzer functions
FontAnalyzer *font_analyzer_create(void);
void font_analyzer_destroy(FontAnalyzer *analyzer);
void font_analyzer_build_mappings(FontAnalyzer *analyzer, double body_font_size, int max_header_levels);
const char *get_header_id_from_analyzer(TextSpan *span, void *user_data);

// Page processing functions
static PageParams *create_page_params(void);
static void destroy_page_params(PageParams *params);
static void init_page_params(PageParams *params, fz_page *page, fz_rect clip);
static int page_is_ocr(fz_context *ctx, fz_page *page);
static int extract_annotations(fz_context *ctx, fz_page *page, PageParams *params);
static int find_column_boxes(fz_context *ctx, fz_page *page, PageParams *params);
static void sort_reading_order(fz_rect *rects, int count);
static int intersects_rects(fz_rect rect, fz_rect *rect_list, int count);
static int is_in_rects(fz_rect rect, fz_rect *rect_list, int count);
static void process_text_in_rect(fz_context *ctx, PageParams *params, fz_rect text_rect);

// Forward declarations for helper functions used in wrapper
static int is_bold(int flags, int char_flags);
static int is_italic(int flags);
static int is_mono(int flags);
static int is_strikeout(int char_flags);

// Link processing functions
static int extract_links(fz_context *ctx, fz_page *page, PageParams *params);
static char *resolve_span_link(LinkInfo *links, int link_count, fz_rect span_bbox);

// Advanced text processing
static int process_pdf_page(fz_context *ctx, fz_page *page, PageParams *params);

// NEW: Markdown cleanup functions
static char *str_replace(char *orig, const char *rep, const char *with);
static char *cleanup_markdown(const char *content);

// NEW: Helper function to append a temporary file's content to the final output file

static void append_file(FILE *dest, const char *temp_filename)
{
    FILE *src = fopen(temp_filename, "rb");
    if (!src)
    {
        fprintf(stderr, "Warning: Could not open temporary file %s for reading.\n", temp_filename);
        return;
    }

    fseek(src, 0, SEEK_END);
    long file_size = ftell(src);
    fseek(src, 0, SEEK_SET);

    if (file_size <= 0)
    {
        fclose(src);
        return;
    }

    char *original_content = malloc(file_size + 1);
    if (!original_content)
    {
        fprintf(stderr, "Error: Failed to allocate memory to read temp file.\n");
        fclose(src);
        return;
    }

    if (fread(original_content, 1, file_size, src) != (size_t)file_size)
    {
        fprintf(stderr, "Error: Failed to read temp file %s.\n", temp_filename);
        free(original_content);
        fclose(src);
        return;
    }
    original_content[file_size] = '\0';
    fclose(src);

    char *cleaned_content = cleanup_markdown(original_content);

    if (cleaned_content)
    {
        fwrite(cleaned_content, 1, strlen(cleaned_content), dest);
        free(cleaned_content);
    }
    else
    {
        // if cleanup failed, write original content
        fwrite(original_content, 1, file_size, dest);
    }

    free(original_content);
}

// NEW: In-place string replacement (from https://stackoverflow.com/a/3241374)
// Returns a new string with all replacements.
static char *str_replace(char *orig, const char *rep, const char *with)
{
    char *result;
    char *ins;
    char *tmp;
    int len_rep;
    int len_with;
    int len_front;
    int count;

    if (!orig || !rep)
        return NULL;
    len_rep = strlen(rep);
    if (len_rep == 0)
        return NULL; // cannot replace an empty string
    if (!with)
        with = "";
    len_with = strlen(with);

    ins = orig;
    for (count = 0; (tmp = strstr(ins, rep)); ++count)
    {
        ins = tmp + len_rep;
    }

    tmp = result = malloc(strlen(orig) + (len_with - len_rep) * count + 1);

    if (!result)
        return NULL;

    char *current_pos = tmp;
    while (count--)
    {
        ins = strstr(orig, rep);
        len_front = ins - orig;
        memcpy(current_pos, orig, len_front);
        current_pos += len_front;
        memcpy(current_pos, with, len_with);
        current_pos += len_with;
        orig += len_front + len_rep;
    }
    strcpy(current_pos, orig);
    return result;
}

// NEW: Cleanup function for generated markdown
static char *cleanup_markdown(const char *content)
{
    if (!content)
        return NULL;

    char *temp1 = str_replace((char *)content, "<br>", "\n");
    if (!temp1)
        return NULL;
    char *temp2 = str_replace(temp1, "<br/>", "\n");
    free(temp1);
    if (!temp2)
        return NULL;

    // Remove Unicode replacement character (U+FFFD)
    char *temp3 = str_replace(temp2, "\xEF\xBF\xBD", "");
    free(temp2);
    if (!temp3)
        return NULL;

    // Normalize newlines (3 or more to 2)
    char *temp4 = str_replace(temp3, "\n\n\n", "\n\n");
    free(temp3);
    if (!temp4)
        return NULL;

    // Run again for cases like \n\n\n\n -> \n\n\n
    char *final_content = str_replace(temp4, "\n\n\n", "\n\n");
    free(temp4);

    return final_content;
}

// Worker thread function declarations

void *worker_thread(void *arg);

extern EXPORT int to_markdown(const char *pdf_path, const char *output_path);

// Implementation begins here

// StringBuilder implementation

static StringBuilder *sb_create(size_t initial_capacity)
{

    StringBuilder *sb = malloc(sizeof(StringBuilder));

    if (!sb)
        return NULL;

    sb->capacity = initial_capacity < INITIAL_BUFFER_SIZE ? INITIAL_BUFFER_SIZE : initial_capacity;

    sb->data = malloc(sb->capacity);

    if (!sb->data)
    {

        free(sb);

        return NULL;
    }

    sb->length = 0;

    sb->data[0] = '\0';

    return sb;
}

static void sb_destroy(StringBuilder *sb)
{

    if (sb)
    {

        free(sb->data);

        free(sb);
    }
}

static int sb_ensure_capacity(StringBuilder *sb, size_t needed)
{

    if (!sb)
        return -1;

    if (needed > MAX_BUFFER_SIZE)
    {

        // Prevent runaway allocations

        return -1;
    }

    if (sb->capacity <= needed)
    {

        size_t new_capacity = sb->capacity;

        while (new_capacity <= needed)
        {

            new_capacity *= 2;

            if (new_capacity > MAX_BUFFER_SIZE)
            {

                new_capacity = MAX_BUFFER_SIZE;

                break;
            }
        }

        char *new_data = realloc(sb->data, new_capacity);

        if (!new_data)
            return -1;

        sb->data = new_data;

        sb->capacity = new_capacity;
    }

    return 0;
}

static int sb_append(StringBuilder *sb, const char *str)
{

    if (!sb || !str)
        return -1;

    size_t str_len = strlen(str);

    if (str_len == 0)
        return 0;

    if (sb_ensure_capacity(sb, sb->length + str_len + 1) != 0)
    {

        return -1;
    }

    // Use memcpy instead of strcpy to avoid buffer overruns

    memcpy(sb->data + sb->length, str, str_len);

    sb->length += str_len;

    sb->data[sb->length] = '\0';

    return 0;
}

static int sb_append_char(StringBuilder *sb, char c)
{

    if (!sb)
        return -1;

    if (sb_ensure_capacity(sb, sb->length + 2) != 0)
    {

        return -1;
    }

    sb->data[sb->length] = c;

    sb->length++;

    sb->data[sb->length] = '\0';

    return 0;
}

static int sb_append_formatted(StringBuilder *sb, const char *format, ...)
{

    if (!sb || !format)
        return -1;

    va_list args;

    va_start(args, format);

    // Get required length

    va_list args_copy;

    va_copy(args_copy, args);

    int len = vsnprintf(NULL, 0, format, args_copy);

    va_end(args_copy);

    if (len < 0)
    {

        va_end(args);

        return -1;
    }

    // Ensure capacity

    if (sb_ensure_capacity(sb, sb->length + len + 1) != 0)
    {

        va_end(args);

        return -1;
    }

    // Format the string

    vsnprintf(sb->data + sb->length, len + 1, format, args);

    sb->length += len;

    va_end(args);

    return 0;
}

// Batch buffer implementation for memory-efficient I/O

static BatchBuffer *batch_buffer_create(FILE *output_file)
{

    BatchBuffer *buffer = malloc(sizeof(BatchBuffer));

    if (!buffer)
        return NULL;

    buffer->capacity = BATCH_BUFFER_SIZE;

    buffer->data = malloc(buffer->capacity);

    if (!buffer->data)
    {

        free(buffer);

        return NULL;
    }

    buffer->length = 0;

    buffer->output_file = output_file;

    return buffer;
}

static void batch_buffer_destroy(BatchBuffer *buffer)
{

    if (buffer)
    {

        // Flush any remaining data before destroying

        if (buffer->length > 0)
        {

            batch_buffer_flush(buffer);
        }

        free(buffer->data);

        free(buffer);
    }
}

static int batch_buffer_append(BatchBuffer *buffer, const char *str)
{

    if (!buffer || !str)
        return -1;

    size_t str_len = strlen(str);

    if (str_len == 0)
        return 0;

    // If adding this string would exceed capacity, flush first

    if (buffer->length + str_len >= buffer->capacity)
    {

        if (batch_buffer_flush(buffer) != 0)
        {

            return -1;
        }

        // If string is still too large after flush, it's too big for our buffer

        if (str_len >= buffer->capacity)
        {

            fprintf(stderr, "Warning: String too large for batch buffer, writing directly\n");

            return fwrite(str, 1, str_len, buffer->output_file) == str_len ? 0 : -1;
        }
    }

    // Append to buffer

    memcpy(buffer->data + buffer->length, str, str_len);

    buffer->length += str_len;

    return 0;
}

static int batch_buffer_append_formatted(BatchBuffer *buffer, const char *format, ...)
{

    if (!buffer || !format)
        return -1;

    va_list args;

    va_start(args, format);

    // Get required length

    va_list args_copy;

    va_copy(args_copy, args);

    int len = vsnprintf(NULL, 0, format, args_copy);

    va_end(args_copy);

    if (len < 0)
    {

        va_end(args);

        return -1;
    }

    // If formatted string would exceed capacity, flush first

    if (buffer->length + len >= buffer->capacity)
    {

        if (batch_buffer_flush(buffer) != 0)
        {

            va_end(args);

            return -1;
        }
    }

    // If still too large, write directly

    if (len >= buffer->capacity)
    {

        fprintf(stderr, "Warning: Formatted string too large for batch buffer\n");

        int result = vfprintf(buffer->output_file, format, args);

        va_end(args);

        return result >= 0 ? 0 : -1;
    }

    // Format into buffer

    vsnprintf(buffer->data + buffer->length, len + 1, format, args);

    buffer->length += len;

    va_end(args);

    return 0;
}

static int batch_buffer_flush(BatchBuffer *buffer)
{

    if (!buffer || buffer->length == 0)
        return 0;

    size_t written = fwrite(buffer->data, 1, buffer->length, buffer->output_file);

    if (written != buffer->length)
    {

        return -1;
    }

    buffer->length = 0;

    return 0;
}

static void batch_buffer_reset(BatchBuffer *buffer)
{

    if (buffer)
    {

        buffer->length = 0;
    }
}

// Reusable span array functions

static void reusable_spans_init(ReusableSpanArray *spans)
{

    if (!spans)
        return;

    spans->count = 0;

    memset(spans->text_buffers, 0, sizeof(spans->text_buffers));
}

static void reusable_spans_reset(ReusableSpanArray *spans)
{

    if (!spans)
        return;

    spans->count = 0;

    // Clear text buffers for reuse

    for (int i = 0; i < MAX_SPANS_PER_LINE; i++)
    {

        spans->text_buffers[i][0] = '\0';
    }
}

// Generate markdown for a single span directly into batch buffer

static int append_markdown_for_span(BatchBuffer *buffer, TextSpan *span, FontAnalyzer *analyzer)
{

    if (!buffer || !span || !span->text)
        return -1;

    // Get header prefix if this is a header

    const char *header_prefix = "";

    if (analyzer)
    {

        int font_size = (int)round(span->size);

        if (font_size >= 0 && font_size < MAX_FONT_SIZE && analyzer->header_mapping[font_size][0] != '\0')
        {

            header_prefix = analyzer->header_mapping[font_size];
        }
    }

    // Apply markdown formatting

    int bold = span->bold;

    int italic = span->italic;

    int mono = span->mono;

    // Build the formatted text

    if (strlen(header_prefix) > 0)
    {

        batch_buffer_append(buffer, header_prefix);
    }

    if (mono)
    {

        batch_buffer_append(buffer, "`");
    }
    else
    {

        if (bold && italic)
        {

            batch_buffer_append(buffer, "***");
        }
        else if (bold)
        {

            batch_buffer_append(buffer, "**");
        }
        else if (italic)
        {

            batch_buffer_append(buffer, "*");
        }
    }

    // Add the actual text

    batch_buffer_append(buffer, span->text);

    // Close formatting tags (in reverse order)

    if (mono)
    {

        batch_buffer_append(buffer, "`");
    }
    else
    {

        if (bold && italic)
        {

            batch_buffer_append(buffer, "***");
        }
        else if (bold)
        {

            batch_buffer_append(buffer, "**");
        }
        else if (italic)
        {

            batch_buffer_append(buffer, "*");
        }
    }

    return 0;
}

// Font analyzer functions

FontAnalyzer *font_analyzer_create(void)
{

    FontAnalyzer *analyzer = malloc(sizeof(FontAnalyzer));

    if (!analyzer)
        return NULL;

    memset(analyzer->font_counts, 0, sizeof(analyzer->font_counts));

    analyzer->body_limit = 12.0;

    memset(analyzer->header_mapping, 0, sizeof(analyzer->header_mapping));

    return analyzer;
}

void font_analyzer_destroy(FontAnalyzer *analyzer)
{

    if (analyzer)
    {

        free(analyzer);
    }
}

void font_analyzer_build_mappings(FontAnalyzer *analyzer, double body_font_size, int max_header_levels)
{

    if (!analyzer)
        return;

    // Find the most common font size (body text)

    int max_count = 0;

    int body_font_index = (int)round(body_font_size);

    // Find actual body font size from font counts

    for (int i = 8; i < MAX_FONT_SIZE && i < 20; i++)
    {

        if (analyzer->font_counts[i] > max_count)
        {

            max_count = analyzer->font_counts[i];

            body_font_index = i;
        }
    }

    analyzer->body_limit = (double)body_font_index;

    // Build header mappings for fonts larger than body text

    int header_level = 1;

    for (int size = MAX_FONT_SIZE - 1; size > body_font_index && header_level <= max_header_levels; size--)
    {

        if (analyzer->font_counts[size] > 0)
        {

            // Create header markdown prefix

            for (int h = 0; h < header_level && h < 6; h++)
            {

                analyzer->header_mapping[size][h] = '#';
            }

            analyzer->header_mapping[size][header_level] = ' ';

            analyzer->header_mapping[size][header_level + 1] = '\0';

            header_level++;
        }
    }
}

const char *get_header_id_from_analyzer(TextSpan *span, void *user_data)
{

    FontAnalyzer *analyzer = (FontAnalyzer *)user_data;

    if (!analyzer || !span)
        return "";

    int font_size = (int)round(span->size);

    if (font_size >= 0 && font_size < MAX_FONT_SIZE && analyzer->header_mapping[font_size][0] != '\0')
    {

        return analyzer->header_mapping[font_size];
    }

    return "";
}

// Page parameters functions

static PageParams *create_page_params(void)
{

    PageParams *params = malloc(sizeof(PageParams));

    if (!params)
        return NULL;

    memset(params, 0, sizeof(PageParams));

    return params;
}

static void destroy_page_params(PageParams *params)
{

    if (!params)
        return;

    free(params);
}

static void init_page_params(PageParams *params, fz_page *page, fz_rect clip)
{

    if (!params)
        return;

    params->clip = clip;

    params->textpage = NULL;
}

// OCR page detection (matching Python exactly)

static int page_is_ocr(fz_context *ctx, fz_page *page)
{

    // Check if page exclusively contains OCR text (ignore-text)

    // For simplicity, we'll return 0 for now but this should be enhanced

    (void)ctx; // Suppress unused parameter warning

    (void)page; // Suppress unused parameter warning

    return 0;
}

// Text formatting utilities

static int is_bold(int flags, int char_flags)
{

    return (flags & FZ_STEXT_STYLE_BOLD) || (char_flags & 8);
}

static int is_italic(int flags)
{

    return flags & FZ_STEXT_STYLE_ITALIC;
}

static int is_mono(int flags)
{

    return flags & FZ_STEXT_STYLE_MONOSPACE;
}

static int is_strikeout(int char_flags)
{

    return char_flags & 1;
}

// Stub implementations for removed functions

static int extract_annotations(fz_context *ctx, fz_page *page, PageParams *params)
{

    (void)ctx;
    (void)page;
    (void)params;

    return 0;
}

static int extract_links(fz_context *ctx, fz_page *page, PageParams *params)
{

    (void)ctx;
    (void)page;
    (void)params;

    return 0;
}

static char *resolve_span_link(LinkInfo *links, int link_count, fz_rect span_bbox)
{

    (void)links;
    (void)link_count;
    (void)span_bbox;

    return NULL;
}

static int find_column_boxes(fz_context *ctx, fz_page *page, PageParams *params)
{

    (void)ctx;
    (void)page;
    (void)params;

    return 0;
}

static void sort_reading_order(fz_rect *rects, int count)
{

    (void)rects;
    (void)count;
}

static int intersects_rects(fz_rect rect, fz_rect *rect_list, int count)
{

    (void)rect;
    (void)rect_list;
    (void)count;

    return 0;
}

static int is_in_rects(fz_rect rect, fz_rect *rect_list, int count)
{

    (void)rect;
    (void)rect_list;
    (void)count;

    return 0;
}

static void process_text_in_rect(fz_context *ctx, PageParams *params, fz_rect text_rect)
{

    (void)ctx;
    (void)params;
    (void)text_rect;
}

// Main page processing function (memory-efficient version)

static int process_pdf_page(fz_context *ctx, fz_page *page, PageParams *params)
{

    if (!ctx || !page || !params || !params->batch_buffer || !params->reusable_spans)
        return -1;

    fz_stext_page *textpage = NULL;

    fz_try(ctx)
    {

        fz_stext_options opts = {.flags = FZ_STEXT_CLIP | FZ_STEXT_ACCURATE_BBOXES | FZ_STEXT_COLLECT_STYLES};

        textpage = fz_new_stext_page_from_page(ctx, page, &opts);

        if (!textpage)
        {

            fz_rethrow(ctx);
        }

        for (fz_stext_block *block = textpage->first_block; block; block = block->next)
        {
            if (block->type != FZ_STEXT_BLOCK_TEXT)
                continue;

            // Check if the block is inside any table rectangle

            int is_in_table = 0;

            if (params->table_rects && params->table_count > 0)
            {

                for (int i = 0; i < params->table_count; i++)
                {

                    // Check for intersection. An empty intersection is not an overlap.

                    if (!fz_is_empty_rect(fz_intersect_rect(block->bbox, params->table_rects[i])))
                    {

                        is_in_table = 1;

                        break;
                    }
                }
            }

            if (is_in_table)
            {

                continue; // Skip text blocks that are part of a table
            }

            for (fz_stext_line *line = block->u.t.first_line; line; line = line->next)
            {

                reusable_spans_reset(params->reusable_spans);

                TextSpan *current_span = NULL;

                for (fz_stext_char *ch = line->first_char; ch; ch = ch->next)
                {

                    if (ch->c < 32 && ch->c != '\t')
                        continue;

                    const char *font_name = ch->font ? ch->font->name : "unknown";

                    int bold = ch->font && (strstr(font_name, "Bold") || strstr(font_name, "Black"));

                    int italic = ch->font && strstr(font_name, "Italic");

                    int mono = ch->font && (strstr(font_name, "Mono") || strstr(font_name, "Courier"));

                    if (current_span == NULL ||

                        fabs(ch->size - current_span->size) > 0.01 ||

                        bold != current_span->bold ||

                        italic != current_span->italic ||

                        mono != current_span->mono ||

                        strcmp(font_name, current_span->font) != 0)

                    {

                        if (params->reusable_spans->count >= MAX_SPANS_PER_LINE)
                        {

                            fprintf(stderr, "Warning: Line has too many spans, truncating\n");

                            break;
                        }

                        current_span = &params->reusable_spans->spans[params->reusable_spans->count++];

                        current_span->text = params->reusable_spans->text_buffers[params->reusable_spans->count - 1];

                        current_span->text[0] = '\0';

                        current_span->font = (char *)font_name;

                        current_span->size = ch->size;

                        current_span->bbox = fz_make_rect(ch->quad.ul.x, ch->quad.ul.y, ch->quad.lr.x, ch->quad.lr.y);

                        current_span->bold = bold;

                        current_span->italic = italic;

                        current_span->mono = mono;
                    }
                    else
                    {

                        fz_rect char_bbox = fz_make_rect(ch->quad.ul.x, ch->quad.ul.y, ch->quad.lr.x, ch->quad.lr.y);

                        current_span->bbox = fz_union_rect(current_span->bbox, char_bbox);
                    }

                    char utf8_char[8];

                    int len = fz_runetochar(utf8_char, ch->c);

                    utf8_char[len] = '\0';

                    if (strlen(current_span->text) + len < MAX_SPAN_TEXT_SIZE)
                    {

                        strcat(current_span->text, utf8_char);
                    }
                }

                for (int i = 0; i < params->reusable_spans->count; i++)
                {

                    append_markdown_for_span(params->batch_buffer, &params->reusable_spans->spans[i], params->font_analyzer);
                }

                batch_buffer_append(params->batch_buffer, "\n");
            }

            batch_buffer_append(params->batch_buffer, "\n");
        }
    }

    fz_always(ctx)
    {

        if (textpage)
        {

            fz_drop_stext_page(ctx, textpage);

            textpage = NULL;
        }
    }

    fz_catch(ctx)
    {

        return -1; // The fz_always block handles cleanup
    }

    return 0;
}

// NEW: The worker thread function (Temporary File Version)

void *worker_thread(void *arg)
{

    WorkerArgs *worker_args = (WorkerArgs *)arg;

    ReusableSpanArray *reusable_spans = NULL;

    reusable_spans = malloc(sizeof(ReusableSpanArray));

    if (!reusable_spans)
    {

        fprintf(stderr, "Error: Thread failed to allocate reusable spans buffer\n");

        return NULL;
    }

    reusable_spans_init(reusable_spans);

    while (1)
    {

        pthread_mutex_lock(worker_args->job_mutex);

        int current_job_index = (*worker_args->next_job_index)++;

        pthread_mutex_unlock(worker_args->job_mutex);

        if (worker_args->jobs[current_job_index].start_page == -1)
        {

            break; // No more jobs
        }

        Job current_job = worker_args->jobs[current_job_index];

        // --- 1. Create a unique temporary filename ---

        char temp_filename[256];

        snprintf(temp_filename, sizeof(temp_filename), "%s.batch_%d.tmp", worker_args->output_path, current_job.batch_num);

        FILE *temp_file = fopen(temp_filename, "w");

        if (!temp_file)
        {

            fprintf(stderr, "Error: Thread failed to create temporary file: %s\n", temp_filename);

            continue;
        }

        // --- 2. Process the batch in isolation, writing to the temp file ---

        fz_context *ctx = fz_new_context(NULL, NULL, 256 * 1024 * 1024);

        if (!ctx)
        {

            fprintf(stderr, "Error: Thread failed to create MuPDF context.\n");

            fclose(temp_file);

            continue;
        }

        fz_document *doc = NULL;

        FontAnalyzer *batch_analyzer = NULL;

        // The BatchBuffer now writes directly to our temporary file

        BatchBuffer *batch_buffer = batch_buffer_create(temp_file);

        fz_try(ctx)
        {

            fz_register_document_handlers(ctx);

            doc = fz_open_document(ctx, worker_args->pdf_path);

            if (!doc)
                fz_rethrow(ctx);

            // 1. Analyze fonts for the current batch

            batch_analyzer = font_analyzer_create();

            if (!batch_analyzer)
                fz_rethrow(ctx);

            for (int p = current_job.start_page; p < current_job.end_page; p++)
            {

                fz_page *page = fz_load_page(ctx, doc, p);

                fz_stext_page *textpage = NULL;

                fz_try(ctx)
                {

                    fz_stext_options opts = {.flags = FZ_STEXT_CLIP | FZ_STEXT_ACCURATE_BBOXES | FZ_STEXT_COLLECT_STYLES};

                    textpage = fz_new_stext_page_from_page(ctx, page, &opts);

                    if (textpage)
                    {

                        for (fz_stext_block *block = textpage->first_block; block; block = block->next)
                        {

                            if (block->type != FZ_STEXT_BLOCK_TEXT)
                                continue;

                            for (fz_stext_line *line = block->u.t.first_line; line; line = line->next)
                            {

                                for (fz_stext_char *ch = line->first_char; ch; ch = ch->next)
                                {

                                    if (ch->c <= 32 || ch->c == 160)
                                        continue;

                                    int font_size = (int)round(ch->size);

                                    if (font_size >= 0 && font_size < MAX_FONT_SIZE)
                                    {

                                        batch_analyzer->font_counts[font_size]++;
                                    }
                                }
                            }
                        }
                    }
                }

                fz_always(ctx)
                {

                    if (textpage)
                        fz_drop_stext_page(ctx, textpage);

                    if (page)
                        fz_drop_page(ctx, page);
                }

                fz_catch(ctx)
                {

                    fprintf(stderr, "\nWarning: Failed to analyze fonts on page %d. Skipping.\n", p + 1);
                }
            }

            font_analyzer_build_mappings(batch_analyzer, 12.0, 6);

            // Second pass: generate markdown

            for (int p = current_job.start_page; p < current_job.end_page; p++)
            {

                fz_page *page = fz_load_page(ctx, doc, p);

                // Find tables on this page to exclude their text
                // Use mutex to protect global state in table detection functions
                pthread_mutex_lock(worker_args->table_mutex);
                int table_count = 0;
                // ColumnBoxesOptions opts = {
                //     .footer_margin = 0.0f,
                //     .header_margin = 0.0f,
                //     .no_image_text = 1,
                //     .textpage_param = NULL,
                //     .paths = NULL,
                //     .path_count = 0,
                //     .avoid = NULL,
                //     .avoid_count = 0,
                //     .ignore_images = 1,
                //     .result_count = NULL,
                //     .preset = 1 // Use preset 1 (ignore images)
                // };
                fz_rect *table_rects = NULL; // temporarily disable table detection
                pthread_mutex_unlock(worker_args->table_mutex);

                PageParams params = {

                    .batch_buffer = batch_buffer,

                    .reusable_spans = reusable_spans,

                    .font_analyzer = batch_analyzer,

                    .table_rects = table_rects,

                    .table_count = table_count

                };

                process_pdf_page(ctx, page, &params);

                // Cleanup

                if (table_rects)
                    free(table_rects);

                fz_drop_page(ctx, page);
            }
        }

        fz_always(ctx)
        {

            // Cleanup for this batch

            batch_buffer_destroy(batch_buffer); // This will flush the final data to the temp file

            fclose(temp_file);

            font_analyzer_destroy(batch_analyzer);

            if (doc)
                fz_drop_document(ctx, doc);

            fz_drop_context(ctx); // Frees all memory for this batch
        }

        fz_catch(ctx)
        {

            fprintf(stderr, "Error processing batch starting at page %d.\n", current_job.start_page + 1);
        }
    }

    free(reusable_spans);

    return NULL;
}

// NEW: The revised to_markdown orchestrator function

extern EXPORT int to_markdown(const char *pdf_path, const char *output_path)
{

    // --- 1. Setup (Get page count) ---

    int page_count;

    fz_context *count_ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);

    if (!count_ctx)
    {

        fprintf(stderr, "Error: Failed to create MuPDF context for page count\n");

        return -1;
    }

    fz_document *temp_doc = NULL;

    fz_try(count_ctx)
    {

        fz_register_document_handlers(count_ctx);

        temp_doc = fz_open_document(count_ctx, pdf_path);

        page_count = fz_count_pages(count_ctx, temp_doc);
    }

    fz_always(count_ctx)
    {

        if (temp_doc)
            fz_drop_document(count_ctx, temp_doc);

        fz_drop_context(count_ctx);
    }

    fz_catch(count_ctx)
    {

        fprintf(stderr, "Error: Could not open PDF to get page count.\n");

        return -1;
    }

    int num_threads = sysconf(_SC_NPROCESSORS_ONLN);

    if (num_threads <= 0)
        num_threads = 4;

    printf("Processing %d pages using up to %d threads (low memory mode)...\n", page_count, num_threads);

    // --- 2. Create Jobs ---

    int num_batches = (page_count + BATCH_PAGES - 1) / BATCH_PAGES;

    Job *jobs = malloc(sizeof(Job) * (num_batches + num_threads));

    if (!jobs)
    {

        fprintf(stderr, "Error: malloc failed for jobs array\n");

        return -1;
    }

    for (int i = 0; i < num_batches; i++)
    {

        jobs[i].start_page = i * BATCH_PAGES;

        jobs[i].end_page = (jobs[i].start_page + BATCH_PAGES < page_count) ? jobs[i].start_page + BATCH_PAGES : page_count;

        jobs[i].batch_num = i;
    }

    // Add sentinel jobs to stop threads

    for (int i = num_batches; i < num_batches + num_threads; i++)
    {

        jobs[i].start_page = -1;
    }

    // --- 3. Launch Worker Threads ---

    pthread_t *threads = malloc(sizeof(pthread_t) * num_threads);

    if (!threads)
    {

        fprintf(stderr, "Error: malloc failed for threads array\n");

        free(jobs);

        return -1;
    }

    int next_job_index = 0;

    pthread_mutex_t job_mutex = PTHREAD_MUTEX_INITIALIZER;

    WorkerArgs worker_args = {

        .pdf_path = pdf_path,

        .output_path = output_path,

        .jobs = jobs,

        .next_job_index = &next_job_index,

        .job_mutex = &job_mutex,

        .table_mutex = &g_table_extraction_mutex

    };

    for (int i = 0; i < num_threads; ++i)
    {

        pthread_create(&threads[i], NULL, worker_thread, &worker_args);
    }

    // --- 4. Wait for threads to finish ---

    for (int i = 0; i < num_threads; ++i)
    {

        pthread_join(threads[i], NULL);
    }

    printf("\nAll batches processed. Assembling final markdown file from temp files...\n");

    // --- 5. Stitch temporary files together in order ---

    FILE *out_file = fopen(output_path, "wb");

    if (!out_file)
    {

        fprintf(stderr, "Error: Failed to open final output file: %s\n", output_path);

        free(threads);

        free(jobs);

        return -1;
    }

    for (int i = 0; i < num_batches; ++i)
    {

        char temp_filename[256];

        snprintf(temp_filename, sizeof(temp_filename), "%s.batch_%d.tmp", output_path, i);

        append_file(out_file, temp_filename);

        remove(temp_filename); // Delete the temporary file after use
    }

    fclose(out_file);

    // --- 6. Final Cleanup ---

    free(threads);

    free(jobs);

    pthread_mutex_destroy(&job_mutex);

    printf("\n✓ Conversion completed successfully!\n");

    return 0;
}

#ifndef NOLIB_MAIN
int main(int argc, char *argv[])
{

    printf("Standalone PDF to Markdown Converter v2.0 (Parallel Low-Memory Mode)\n");

    printf("Ultra-optimized pure C implementation with temp file batching\n\n");

    if (argc < 2 || argc > 3)
    {

        printf("Usage: %s <input.pdf> [output.md]\n", argv[0]);

        printf("  input.pdf  - PDF file to convert\n");

        printf("  output.md  - Output markdown file (optional, default: input.md)\n");

        return 1;
    }

    const char *input_path = argv[1];

    char *output_path = NULL;

    // Generate output filename if not provided

    if (argc == 3)
    {

        size_t len = strlen(argv[2]);

        output_path = malloc(len + 1);

        strcpy(output_path, argv[2]);
    }
    else
    {

        size_t len = strlen(input_path);

        output_path = malloc(len + 4); // .md + null terminator

        strcpy(output_path, input_path);

        // Replace extension

        char *ext = strrchr(output_path, '.');

        if (ext && strcmp(ext, ".pdf") == 0)
        {

            strcpy(ext, ".md");
        }
        else
        {

            strcat(output_path, ".md");
        }
    }

    // Check if input file exists

    struct stat st;

    if (stat(input_path, &st) != 0)
    {

        fprintf(stderr, "Error: Input file does not exist: %s\n", input_path);

        free(output_path);

        return 1;
    }

    printf("Input:  %s\n", input_path);

    printf("Output: %s\n\n", output_path);

    // Convert PDF to Markdown

    int result = to_markdown(input_path, output_path);

    free(output_path);

    if (result == 0)
    {

        printf("\n✓ Conversion completed successfully!\n");

        return 0;
    }
    else
    {

        printf("\n❌ Conversion failed!\n");

        return 1;
    }
}
#endif /* NOLIB_MAIN */
