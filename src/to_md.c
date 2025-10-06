/*

 * Standalone PDF to Markdown Converter with low-memory parallel processing
 * Pure C implementation
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

#include <ctype.h>
#include <math.h>
#include <mupdf/fitz.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../include/multicolumn.h"
#include "../include/table.h"

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
#define MAX_PATTERNS 50
#define MAX_BUFFER_SIZE (16 * 1024 * 1024)   // 16MB batch buffer
#define BATCH_BUFFER_SIZE (32 * 1024 * 1024) // 32MB batch buffer for I/O
#define INITIAL_BUFFER_SIZE 8192
#define MAX_SPANS_PER_LINE 1000 // Fixed-size reusable span array
#define MAX_LINES_PER_PAGE 10000
#define BATCH_PAGES 10
#define MAX_SPAN_TEXT_SIZE 4096 // Maximum text per span

// =============================================================================
// Struct Definitions
// =============================================================================

// Defines a unit of work for a worker thread, specifying a range of pages.
typedef struct
{
    int start_page; // Starting page number of the batch.
    int end_page;   // Ending page number of the batch (exclusive).
    int batch_num;  // Unique identifier for the batch.
} Job;

// Arguments passed to each worker thread.
typedef struct
{
    const char* pdf_path;         // Path to the source PDF file.
    const char* output_path;      // Base path for temporary output files.
    Job* jobs;                    // Pointer to the array of jobs.
    int* next_job_index;          // Pointer to the index of the next available job.
    pthread_mutex_t* job_mutex;   // Mutex to protect access to the next_job_index.
    pthread_mutex_t* table_mutex; // Mutex to protect table extraction logic (if it involves global state).
} WorkerArgs;

// Global mutex for thread-safe table extraction.
static pthread_mutex_t g_table_extraction_mutex __attribute__((unused)) = PTHREAD_MUTEX_INITIALIZER;

// Data structures for decoupled table processing
typedef struct {
    int page_number;
    fz_rect bbox;
    int batch_num;
    char placeholder_id[64];
} DetectedTable;

typedef struct {
    DetectedTable* tables;
    int count;
    int capacity;
    pthread_mutex_t mutex;
} TableRegistry;

static TableRegistry g_table_registry = {0};

// A dynamic string builder for efficient string concatenation.
typedef struct
{
    char* data;       // Buffer to store the string.
    size_t length;    // Current length of the string.
    size_t capacity;  // Allocated capacity of the buffer.
} StringBuilder;

// A buffer for batching writes to a file, reducing I/O operations.
typedef struct
{
    char* data;          // Buffer to store data before writing.
    size_t length;       // Current amount of data in the buffer.
    size_t capacity;     // Allocated capacity of the buffer.
    FILE* output_file;   // The file to write to.
} BatchBuffer;

// Holds data for font analysis to identify headers and body text.
typedef struct
{
    int font_counts[MAX_FONT_SIZE];        // Frequency of each font size.
    double body_limit;                     // The font size considered as body text.
    char header_mapping[MAX_FONT_SIZE][8]; // Maps font sizes to Markdown header tags (e.g., "## ").
} FontAnalyzer;

// Represents a text span with detailed properties.
typedef struct
{
    char* text;       // The text content of the span.
    char* font;       // The font name.
    int flags;        // Style flags: bit 1=italic, bit 3=mono, bit 4=bold (matching Python)
    int char_flags;   // Character-specific flags: bit 0=strikeout, bit 3=bold (matching Python)
    double size;      // Font size.
    fz_rect bbox;     // Bounding box of the span.
    fz_matrix trm;    // Transformation matrix of the text.
    int bold;         // Boolean flag for bold style.
    int italic;       // Boolean flag for italic style.
    int mono;         // Boolean flag for monospace style.
    int strikeout;    // Boolean flag for strikeout style.
    int block_num;    // The block number this span belongs to.
    int superscript;  // Boolean flag for superscript style (derived from flags & 1).
} TextSpan;

// A memory-efficient, reusable array for storing spans of a single line.
typedef struct
{
    TextSpan spans[MAX_SPANS_PER_LINE];                  // Fixed-size array of spans.
    char text_buffers[MAX_SPANS_PER_LINE][MAX_SPAN_TEXT_SIZE]; // Pre-allocated text buffers for each span.
    int count;                                           // The number of spans currently in use.
} ReusableSpanArray;

// Represents a line of text, composed of multiple spans.
typedef struct
{
    TextSpan* spans;     // Dynamic array of spans in the line.
    int span_count;      // Number of spans.
    int capacity;        // Allocated capacity of the spans array.
    fz_rect bbox;        // Bounding box of the entire line.
} TextLine;

// Represents a block of text, composed of multiple lines.
typedef struct
{
    TextLine* lines;    // Dynamic array of lines in the block.
    int line_count;     // Number of lines.
    int capacity;       // Allocated capacity of the lines array.
    fz_rect bbox;       // Bounding box of the entire block.
    int block_type;     // Type of block (e.g., text, image).
} TextBlock;

// Represents a hyperlink.
typedef struct
{
    char* uri;      // The target URI of the link.
    fz_rect bbox;   // The bounding box of the link's "hotspot".
} LinkInfo;

// Parameters for processing a single page.
typedef struct
{
    BatchBuffer* batch_buffer;           // Buffer for writing output.
    ReusableSpanArray* reusable_spans;   // Reusable array for spans.
    FontAnalyzer* font_analyzer;         // Font analysis data for the page or batch.
    fz_rect clip;                        // The clipping rectangle for the page.
    fz_stext_page* textpage;             // The structured text page (temporary handle).
    fz_rect* table_rects;                // Array of rectangles identified as tables.
    int table_count;                     // The number of table rectangles.
} PageParams;

// Function declarations
static StringBuilder* sb_create(size_t initial_capacity);
static void sb_destroy(StringBuilder* sb);
static int sb_append(StringBuilder* sb, const char* str);
static int sb_append_char(StringBuilder* sb, char c);
static int sb_append_formatted(StringBuilder* sb, const char* format, ...);
static int sb_ensure_capacity(StringBuilder* sb, size_t needed);

// Table registry functions
static int table_registry_init(void);
static void table_registry_destroy(void);
static int table_registry_add(int page_number, fz_rect bbox, int batch_num, const char* placeholder_id);
static int table_registry_get_count(void);
static DetectedTable* table_registry_get_tables(void);

// Batch buffer functions for memory-efficient I/O
static BatchBuffer* batch_buffer_create(FILE* output_file);
static void batch_buffer_destroy(BatchBuffer* buffer);
static int batch_buffer_append(BatchBuffer* buffer, const char* str);
static int batch_buffer_append_formatted(BatchBuffer* buffer, const char* format, ...);
static int batch_buffer_flush(BatchBuffer* buffer);
static void batch_buffer_reset(BatchBuffer* buffer);

// Reusable span array functions
static void reusable_spans_init(ReusableSpanArray* spans);
static void reusable_spans_reset(ReusableSpanArray* spans);
static int append_markdown_for_span(BatchBuffer* buffer, TextSpan* span, FontAnalyzer* analyzer);
static int append_markdown_for_span_no_header(BatchBuffer* buffer, TextSpan* span);

// Font analyzer functions
FontAnalyzer* font_analyzer_create(void);
void font_analyzer_destroy(FontAnalyzer* analyzer);
int font_analyzer_analyze_document(FontAnalyzer* analyzer, fz_context* ctx, fz_document* doc, int* pages, int page_count, double body_limit);
void font_analyzer_build_mappings(FontAnalyzer* analyzer, double body_font_size,
                                  int max_header_levels);
const char* get_header_id_from_analyzer(TextSpan* span, void* user_data);

// Page processing functions
static PageParams* create_page_params(void);
static void destroy_page_params(PageParams* params);
static void init_page_params(PageParams* params, fz_page* page, fz_rect clip);
static int page_is_ocr(fz_context* ctx, fz_page* page);
static int extract_annotations(fz_context* ctx, fz_page* page, PageParams* params);
static int find_column_boxes(fz_context* ctx, fz_page* page, PageParams* params);
static void sort_reading_order(fz_rect* rects, int count);
static int intersects_rects(fz_rect rect, fz_rect* rect_list, int count);
static int is_in_rects(fz_rect rect, fz_rect* rect_list, int count);
static void process_text_in_rect(fz_context* ctx, PageParams* params, fz_rect text_rect);

// Forward declarations for helper functions used in wrapper
static int is_bold(int flags, int char_flags);
static int is_italic(int flags);
static int is_mono(int flags);
static int is_strikeout(int char_flags);

// Link processing functions
static int extract_links(fz_context* ctx, fz_page* page, PageParams* params);
static char* resolve_span_link(LinkInfo* links, int link_count, fz_rect span_bbox);

// Advanced text processing
static int process_pdf_page(fz_context* ctx, fz_page* page, PageParams* params);

// NEW: Text processing improvements (fixes 1-4)
static char* merge_consecutive_bold_spans(const char* content);
static char* merge_short_lines(const char* content);
static char* convert_single_lines_to_bullets(const char* content);
static char* remove_standalone_horizontal_rules(const char* content);
static char* normalize_excessive_newlines(const char* content);
static char* normalize_unicode_gremlins(const char* content);
static char* advanced_cleanup_markdown(const char* content);

// NEW: Additional cleanup functions for the requested features
static char* remove_error_references(const char* content);
static char* remove_repeating_headers_footers(const char* content, int total_pages);
static char* normalize_italic_bold_fragments(const char* content);
static char* handle_footnotes_inline(const char* content);
static char* is_repeating_header_footer(const char* line, char** known_patterns, int* pattern_counts, int max_patterns);

// NEW: LLM quality improvement functions
static char* remove_all_caps_headers(const char* content);
static char* remove_page_numbers(const char* content);
static char* remove_inline_references(const char* content);
static char* normalize_bullet_lists(const char* content);
static char* fix_table_multiline_cells(const char* content);
static char* remove_figure_stubs(const char* content);
static char* remove_chapter_page_sequences(const char* content);

// NEW: Markdown cleanup functions
static char* str_replace(char* orig, const char* rep, const char* with);
static char* cleanup_markdown(const char* content);
static char* python_table_cleanup_markdown(const char* content);

// NEW: Helper function to append a temporary file's content to the final output file

static void append_file(FILE* dest, const char* temp_filename);

// Table registry implementation
static int table_registry_init(void)
{
    pthread_mutex_init(&g_table_registry.mutex, NULL);
    g_table_registry.capacity = 100;
    g_table_registry.tables = malloc(g_table_registry.capacity * sizeof(DetectedTable));
    if (!g_table_registry.tables) return -1;
    g_table_registry.count = 0;
    return 0;
}

static void table_registry_destroy(void)
{
    pthread_mutex_destroy(&g_table_registry.mutex);
    free(g_table_registry.tables);
    g_table_registry.tables = NULL;
    g_table_registry.count = 0;
    g_table_registry.capacity = 0;
}

static int table_registry_add(int page_number, fz_rect bbox, int batch_num, const char* placeholder_id)
{
    pthread_mutex_lock(&g_table_registry.mutex);

    if (g_table_registry.count >= g_table_registry.capacity) {
        size_t new_capacity = g_table_registry.capacity * 2;
        DetectedTable* new_tables = realloc(g_table_registry.tables, new_capacity * sizeof(DetectedTable));
        if (!new_tables) {
            pthread_mutex_unlock(&g_table_registry.mutex);
            return -1;
        }
        g_table_registry.tables = new_tables;
        g_table_registry.capacity = new_capacity;
    }

    DetectedTable* table = &g_table_registry.tables[g_table_registry.count++];
    table->page_number = page_number;
    table->bbox = bbox;
    table->batch_num = batch_num;
    strncpy(table->placeholder_id, placeholder_id, sizeof(table->placeholder_id) - 1);
    table->placeholder_id[sizeof(table->placeholder_id) - 1] = '\0';

    pthread_mutex_unlock(&g_table_registry.mutex);
    return 0;
}

static int table_registry_get_count(void)
{
    pthread_mutex_lock(&g_table_registry.mutex);
    int count = g_table_registry.count;
    pthread_mutex_unlock(&g_table_registry.mutex);
    return count;
}

static DetectedTable* table_registry_get_tables(void)
{
    pthread_mutex_lock(&g_table_registry.mutex);
    DetectedTable* tables = g_table_registry.tables;
    pthread_mutex_unlock(&g_table_registry.mutex);
    return tables;
}

// Reassemble final output by replacing table placeholders with actual markdown
static int reassemble_with_tables(const char* output_path, DetectedTable* tables, char** table_results, int table_count)
{
    // Read the current output file
    FILE* original = fopen(output_path, "rb");
    if (!original) {
        fprintf(stderr, "Error: Could not open output file for reassembly\n");
        return -1;
    }

    fseek(original, 0, SEEK_END);
    long file_size = ftell(original);
    fseek(original, 0, SEEK_SET);

    if (file_size <= 0) {
        fclose(original);
        return 0;
    }

    char* content = malloc(file_size + 1);
    if (!content) {
        fclose(original);
        return -1;
    }

    if (fread(content, 1, file_size, original) != (size_t)file_size) {
        free(content);
        fclose(original);
        return -1;
    }
    content[file_size] = '\0';
    fclose(original);

    // Build new content with replacements
    StringBuilder* sb = sb_create(file_size * 2);  // Allocate extra for table content
    if (!sb) {
        free(content);
        return -1;
    }

    char* pos = content;
    for (int i = 0; i < table_count; i++) {
        // Build the full placeholder string with HTML comment markers
        char full_placeholder[128];
        snprintf(full_placeholder, sizeof(full_placeholder), "<!-- %s -->", tables[i].placeholder_id);
        
        // Find the placeholder
        char* found = strstr(pos, full_placeholder);
        if (found) {
            
            // Append everything before the placeholder
            size_t prefix_len = found - pos;
            if (prefix_len > 0) {
                char* prefix = malloc(prefix_len + 1);
                if (prefix) {
                    memcpy(prefix, pos, prefix_len);
                    prefix[prefix_len] = '\0';
                    sb_append(sb, prefix);
                    free(prefix);
                }
            }
            
            // Append the table markdown with minimal cleanup for Python tables
            if (table_results[i] && strlen(table_results[i]) > 0) {
                // Apply minimal cleanup (newlines only) to Python table results
                char* cleaned_table = python_table_cleanup_markdown(table_results[i]);
                sb_append(sb, "\n");
                if (cleaned_table) {
                    sb_append(sb, cleaned_table);
                    free(cleaned_table);
                } else {
                    sb_append(sb, table_results[i]);
                }
                sb_append(sb, "\n");
            }
            
            // Move position past the placeholder
            pos = found + strlen(full_placeholder);
        }
    }
    
    // Append any remaining content
    sb_append(sb, pos);

    // Apply advanced cleanup to the final reassembled content
    char* cleaned_final = advanced_cleanup_markdown(sb->data);
    
    // Write the cleaned reassembled content back
    FILE* output = fopen(output_path, "wb");
    if (!output) {
        if (cleaned_final) free(cleaned_final);
        sb_destroy(sb);
        free(content);
        return -1;
    }

    if (cleaned_final) {
        fwrite(cleaned_final, 1, strlen(cleaned_final), output);
        free(cleaned_final);
    } else {
        // Fallback to uncleaned if cleanup failed
        fwrite(sb->data, 1, sb->length, output);
    }
    
    fclose(output);

    sb_destroy(sb);
    free(content);
    return 0;
}

__attribute__((unused))
static void append_file(FILE* dest, const char* temp_filename)
{
    FILE* src = fopen(temp_filename, "rb");
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

    char* original_content = malloc(file_size + 1);
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

    char* cleaned_content = advanced_cleanup_markdown(original_content);

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
static char* str_replace(char* orig, const char* rep, const char* with)
{
    char* result;
    char* ins;
    char* tmp;
    int len_rep;
    int len_with;
    int len_front;
    int count;

    if (!orig || !rep) return NULL;
    len_rep = strlen(rep);
    if (len_rep == 0) return NULL; // cannot replace an empty string
    if (!with) with = "";
    len_with = strlen(with);

    ins = orig;
    for (count = 0; (tmp = strstr(ins, rep)); ++count)
    {
        ins = tmp + len_rep;
    }

    tmp = result = malloc(strlen(orig) + (len_with - len_rep) * count + 1);

    if (!result) return NULL;

    char* current_pos = tmp;
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

static char* render_page_markdown(fz_context* ctx, fz_document* doc, int page_number)
{
    if (!ctx || !doc || page_number < 0)
    {
        return NULL;
    }

    char* output = NULL;
    char* buffer_ptr = NULL;
    size_t buffer_size = 0;
    FILE* mem_fp = NULL;
    BatchBuffer* batch_buffer = NULL;
    FontAnalyzer* analyzer = NULL;

    fz_try(ctx)
    {
        analyzer = font_analyzer_create();
        if (!analyzer)
        {
            fz_throw(ctx, FZ_ERROR_GENERIC, "failed to allocate font analyzer");
        }

        fz_page* page = NULL;
        fz_stext_page* textpage = NULL;
        fz_try(ctx)
        {
            page = fz_load_page(ctx, doc, page_number);
            fz_stext_options opts = {
                .flags = FZ_STEXT_CLIP | FZ_STEXT_ACCURATE_BBOXES | FZ_STEXT_COLLECT_STYLES
            };
            textpage = fz_new_stext_page_from_page(ctx, page, &opts);
            if (textpage)
            {
                for (fz_stext_block* block = textpage->first_block; block; block = block->next)
                {
                    if (block->type != FZ_STEXT_BLOCK_TEXT) continue;
                    for (fz_stext_line* line = block->u.t.first_line; line; line = line->next)
                    {
                        for (fz_stext_char* ch = line->first_char; ch; ch = ch->next)
                        {
                            if (ch->c <= 32 || ch->c == 160) continue;
                            int font_size = (int)round(ch->size);
                            if (font_size >= 0 && font_size < MAX_FONT_SIZE)
                            {
                                analyzer->font_counts[font_size]++;
                            }
                        }
                    }
                }
            }
        }
        fz_always(ctx)
        {
            if (textpage) fz_drop_stext_page(ctx, textpage);
            if (page) fz_drop_page(ctx, page);
        }
        fz_catch(ctx)
        {
            fprintf(stderr, "Warning: Failed to analyze fonts on page %d.\n", page_number + 1);
        }

        font_analyzer_build_mappings(analyzer, 12.0, 6);

        mem_fp = open_memstream(&buffer_ptr, &buffer_size);
        if (!mem_fp)
        {
            fz_throw(ctx, FZ_ERROR_GENERIC, "failed to create memory stream");
        }

        batch_buffer = batch_buffer_create(mem_fp);
        if (!batch_buffer)
        {
            fz_throw(ctx, FZ_ERROR_GENERIC, "failed to create batch buffer");
        }

        ReusableSpanArray reusable_spans;
        reusable_spans_init(&reusable_spans);

        page = fz_load_page(ctx, doc, page_number);
        PageParams params = {0};
        params.batch_buffer = batch_buffer;
        params.reusable_spans = &reusable_spans;
        params.font_analyzer = analyzer;
        params.table_rects = NULL;
        params.table_count = 0;

        if (process_pdf_page(ctx, page, &params) != 0)
        {
            fz_drop_page(ctx, page);
            fz_throw(ctx, FZ_ERROR_GENERIC, "failed to process page");
        }
        fz_drop_page(ctx, page);

        batch_buffer_destroy(batch_buffer);
        batch_buffer = NULL;

        fclose(mem_fp);
        mem_fp = NULL;

        if (buffer_ptr)
        {
            output = buffer_ptr;
            buffer_ptr = NULL;
        }
    }
    fz_always(ctx)
    {
        if (batch_buffer) batch_buffer_destroy(batch_buffer);
        if (mem_fp) fclose(mem_fp);
        if (analyzer) font_analyzer_destroy(analyzer);
    }
    fz_catch(ctx)
    {
        if (buffer_ptr)
        {
            free(buffer_ptr);
            buffer_ptr = NULL;
        }
        output = NULL;
    }

    return output;
}
// NEW: Cleanup function for generated markdown
static char* cleanup_markdown(const char* content)
{
    if (!content) return NULL;

    char* temp1 = str_replace((char*)content, "<br>", "\n");
    if (!temp1) return NULL;
    char* temp2 = str_replace(temp1, "<br/>", "\n");
    free(temp1);
    if (!temp2) return NULL;

    // Remove Unicode replacement character (U+FFFD)
    char* temp3 = str_replace(temp2, "\xEF\xBF\xBD", "");
    free(temp2);
    if (!temp3) return NULL;

    // Clean up excessive headers - remove standalone ###### that contain only whitespace
    char* temp4 = str_replace(temp3, "###### \n", "\n");
    free(temp3);
    if (!temp4) return NULL;

    // Clean up headers with only spaces/tabs
    char* temp5 = str_replace(temp4, "######                                                      \n", "\n");
    free(temp4);
    if (!temp5) return NULL;

    // Clean up empty headers
    char* temp6 = str_replace(temp5, "######\n", "\n");
    free(temp5);
    if (!temp6) return NULL;

    // Clean up excessive bold formatting - remove ** ** (bold with only spaces)
    char* temp7 = str_replace(temp6, "** **", " ");
    free(temp6);
    if (!temp7) return NULL;

    // Clean up multiple consecutive headers
    char* temp8 = str_replace(temp7, "###### ###### ", "");
    free(temp7);
    if (!temp8) return NULL;

    // Normalize newlines (3 or more to 2)
    char* temp9 = str_replace(temp8, "\n\n\n", "\n\n");
    free(temp8);
    if (!temp9) return NULL;

    // Run again for cases like \n\n\n\n -> \n\n\n
    char* final_content = str_replace(temp9, "\n\n\n", "\n\n");
    free(temp9);

    return final_content;
}

// NEW: Implementation of additional cleanup features

// Feature 1: Remove "Error! Reference source not found." text + Unicode cleanup
static char* normalize_unicode_gremlins(const char* content)
{
    if (!content) return NULL;

    char* result = strdup(content);
    if (!result) return NULL;

    const struct {
        const char* needle;
        const char* replacement;
    } mappings[] = {
        {"\xC2\xA0", " "},      // U+00A0 NO-BREAK SPACE -> space
        {"\xE2\x80\x93", "-"},  // U+2013 EN DASH -> hyphen
        {"\xE2\x80\x94", "-"},  // U+2014 EM DASH -> hyphen
        {"\xE2\x80\x99", "'"},  // U+2019 RIGHT SINGLE QUOTATION MARK -> apostrophe
        {"\xE2\x80\x9C", "\""}, // U+201C LEFT DOUBLE QUOTATION MARK -> quote
        {"\xE2\x80\x9D", "\""}, // U+201D RIGHT DOUBLE QUOTATION MARK -> quote
        {"\xE2\x80\xA2", "- "}, // U+2022 BULLET -> dash space
        {"\xE2\x80\xA6", "..."},// U+2026 HORIZONTAL ELLIPSIS -> three dots
        {"\xEF\xBF\xBD", ""}    // U+FFFD REPLACEMENT CHARACTER -> remove
    };

    size_t mapping_count = sizeof(mappings) / sizeof(mappings[0]);

    for (size_t i = 0; i < mapping_count; i++) {
        char* replaced = str_replace(result, mappings[i].needle, mappings[i].replacement);
        if (!replaced) {
            free(result);
            return NULL;
        }
        free(result);
        result = replaced;
    }

    return result;
}

static char* remove_error_references(const char* content)
{
    if (!content) return NULL;
    
    // Remove various forms of this error message
    char* temp1 = str_replace((char*)content, "Error! Reference source not found.", "");
    if (!temp1) return NULL;
    
    char* temp2 = str_replace(temp1, "Error! Reference source not found", "");
    free(temp1);
    if (!temp2) return NULL;
    
    // Also remove common variations
    char* temp3 = str_replace(temp2, "Error! Bookmark not defined.", "");
    free(temp2);
    if (!temp3) return NULL;
    
    char* temp4 = str_replace(temp3, "Error! Bookmark not defined", "");
    free(temp3);
    if (!temp4) return NULL;
    
    // Remove Unicode replacement character (U+FFFD) - appears as the replacement glyph
    char* temp5 = str_replace(temp4, "\xEF\xBF\xBD", "");
    free(temp4);
    if (!temp5) return NULL;
    
    // Convert standalone 'o' characters to bullet points (-)
    // Be careful not to replace 'o' in words - only standalone ones
    char* temp6 = str_replace(temp5, " o ", " - ");
    free(temp5);
    if (!temp6) return NULL;
    
    char* temp7 = str_replace(temp6, "\no ", "\n- ");
    free(temp6);
    if (!temp7) return NULL;
    
    char* result = str_replace(temp7, " o\n", " -\n");
    free(temp7);

    return result;
}

// Feature 2: Detect and remove repeating headers/footers that appear on every page
static char* is_repeating_header_footer(const char* line, char** known_patterns, int* pattern_counts, int max_patterns)
{
    if (!line || !known_patterns || !pattern_counts) return NULL;
    
    // Skip empty lines and very short lines
    size_t len = strlen(line);
    if (len < 5) return NULL;
    
    // Create a normalized version of the line (trim whitespace, remove page numbers)
    char* normalized = malloc(len + 1);
    if (!normalized) return NULL;
    
    // Copy and normalize: remove leading/trailing whitespace, normalize spaces
    const char* src = line;
    char* dst = normalized;
    
    // Skip leading whitespace
    while (*src && (*src == ' ' || *src == '\t' || *src == '\n' || *src == '\r')) src++;
    
    // Copy content, normalizing spaces and removing trailing digits/page numbers
    while (*src) {
        if (*src == ' ' || *src == '\t') {
            if (dst > normalized && *(dst-1) != ' ') {
                *dst++ = ' ';
            }
        } else if (*src != '\n' && *src != '\r') {
            *dst++ = *src;
        }
        src++;
    }
    
    // Remove trailing whitespace and page numbers
    while (dst > normalized && (*(dst-1) == ' ' || *(dst-1) == '\t' || 
           (*(dst-1) >= '0' && *(dst-1) <= '9'))) {
        dst--;
    }
    *dst = '\0';
    
    // If normalized line is too short, ignore it
    if (strlen(normalized) < 5) {
        free(normalized);
        return NULL;
    }
    
    // Check if this pattern already exists
    for (int i = 0; i < max_patterns; i++) {
        if (known_patterns[i] && strcmp(known_patterns[i], normalized) == 0) {
            pattern_counts[i]++;
            free(normalized);
            return known_patterns[i]; // Return existing pattern
        }
    }
    
    // Add new pattern if we have space
    for (int i = 0; i < max_patterns; i++) {
        if (!known_patterns[i]) {
            known_patterns[i] = normalized;
            pattern_counts[i] = 1;
            return known_patterns[i];
        }
    }
    
    free(normalized);
    return NULL;
}

__attribute__((unused))
static char* remove_repeating_headers_footers(const char* content, int total_pages)
{
    if (!content || total_pages < 3) return strdup(content); // Need at least 3 pages to detect patterns
    
    char* known_patterns[MAX_PATTERNS] = {0};
    int pattern_counts[MAX_PATTERNS] = {0};
    
    // First pass: identify potential repeating patterns
    char* temp_content = strdup(content);
    char* line = strtok(temp_content, "\n");
    
    while (line) {
        // Check if this line could be a header/footer
        size_t line_len = strlen(line);
        if (line_len > 5 && line_len < 200) { // Reasonable header/footer length
            is_repeating_header_footer(line, known_patterns, pattern_counts, MAX_PATTERNS);
        }
        line = strtok(NULL, "\n");
    }
    free(temp_content);
    
    // Second pass: remove patterns that appear frequently (likely headers/footers)
    char* result = strdup(content);
    int min_occurrences = (total_pages >= 10) ? (total_pages / 3) : 2; // Appear on at least 1/3 of pages
    
    for (int i = 0; i < MAX_PATTERNS; i++) {
        if (known_patterns[i] && pattern_counts[i] >= min_occurrences) {
            // Remove this repeating pattern
            char* new_result = str_replace(result, known_patterns[i], "");
            if (new_result) {
                free(result);
                result = new_result;
            }
        }
    }
    
    // Cleanup allocated patterns
    for (int i = 0; i < MAX_PATTERNS; i++) {
        if (known_patterns[i]) {
            free(known_patterns[i]);
        }
    }
    
    return result;
}

// Feature 3: Normalize italic/bold fragments that got split
static char* normalize_italic_bold_fragments(const char* content)
{
    if (!content) return NULL;
    
    char* result = strdup(content);
    if (!result) return NULL;
    
    // Fix bold fragments: **text** **more** -> **text more**
    char* temp1 = str_replace(result, "** **", " ");
    free(result);
    if (!temp1) return NULL;
    
    // Fix bold with minimal spacing
    char* temp2 = str_replace(temp1, "****", "");  // Remove empty bold
    free(temp1);
    if (!temp2) return NULL;
    
    // Fix italic fragments: _text_ _more_ -> _text more_
    char* temp3 = str_replace(temp2, "_ _", " ");
    free(temp2);
    if (!temp3) return NULL;
    
    // Fix italic with minimal spacing  
    char* temp4 = str_replace(temp3, "__", "");  // Remove empty italic
    free(temp3);
    if (!temp4) return NULL;
    
    // Fix mixed bold-italic issues
    char* temp5 = str_replace(temp4, "**_ _**", " ");  // Bold around separated italic
    free(temp4);
    if (!temp5) return NULL;
    
    char* temp6 = str_replace(temp5, "_** **_", " ");  // Italic around separated bold
    free(temp5);
    if (!temp6) return NULL;
    
    // Handle more complex cases with regex-like patterns
    // **word** followed by space and **word** -> **word word**
    // This is a simplified approach - in practice you'd want more sophisticated logic
    
    return temp6;
}

// Feature 4: Handle footnotes by converting to inline text (safe approach)
static char* handle_footnotes_inline(const char* content)
{
    if (!content) return NULL;
    
    char* result = strdup(content);
    if (!result) return NULL;
    
    // Convert common footnote markers to inline text
    // Replace superscript numbers with (footnote N)
    for (int i = 1; i <= 99; i++) {
        char footnote_marker[16];
        char inline_replacement[32];
        
        // Handle various footnote formats
        snprintf(footnote_marker, sizeof(footnote_marker), "^%d", i);
        snprintf(inline_replacement, sizeof(inline_replacement), " (footnote %d)", i);
        
        char* temp = str_replace(result, footnote_marker, inline_replacement);
        if (temp) {
            free(result);
            result = temp;
        }
        
        // Handle footnote references like [1], [2], etc.
        snprintf(footnote_marker, sizeof(footnote_marker), "[%d]", i);
        snprintf(inline_replacement, sizeof(inline_replacement), " (ref %d)", i);
        
        temp = str_replace(result, footnote_marker, inline_replacement);
        if (temp) {
            free(result);
            result = temp;
        }
    }
    
    // Remove footnote separator lines
    char* temp1 = str_replace(result, "\n---\nFootnotes:", "\n\nFootnotes:");
    if (temp1) {
        free(result);
        result = temp1;
    }
    
    char* temp2 = str_replace(result, "\n___\n", "\n");
    if (temp2) {
        free(result);
        result = temp2;
    }
    
    return result;
}

// NEW: LLM quality improvement functions implementation

// Remove all-caps headers with spaces (e.g., "C ONTINGENCY P LANNING G UIDE")
static char* remove_all_caps_headers(const char* content)
{
    if (!content) return NULL;
    
    char* result = strdup(content);
    if (!result) return NULL;
    
    // Split into lines and process each one
    char* lines = strdup(content);
    if (!lines) {
        free(result);
        return NULL;
    }
    
    StringBuilder* sb = sb_create(strlen(content));
    if (!sb) {
        free(result);
        free(lines);
        return NULL;
    }
    
    char* line = strtok(lines, "\n");
    while (line) {
        // Check if line is all caps with spaces between letters
        int is_spaced_caps = 1;
        int letter_count = 0;
        int space_count = 0;
        
        for (char* p = line; *p; p++) {
            if (*p >= 'A' && *p <= 'Z') {
                letter_count++;
            } else if (*p == ' ') {
                space_count++;
            } else if (*p >= 'a' && *p <= 'z') {
                is_spaced_caps = 0;
                break;
            }
        }
        
        // If it's mostly caps with many spaces, likely a header artifact
        if (is_spaced_caps && letter_count > 8 && space_count > letter_count / 2) {
            // Skip this line - it's a header artifact
        } else {
            sb_append(sb, line);
            sb_append(sb, "\n");
        }
        
        line = strtok(NULL, "\n");
    }
    
    free(result);
    free(lines);
    result = strdup(sb->data);
    sb_destroy(sb);
    
    return result;
}

// Remove chapter and page number sequences (e.g., "CHAPTER 3 13")
static char* remove_chapter_page_sequences(const char* content)
{
    if (!content) return NULL;
    
    char* result = strdup(content);
    if (!result) return NULL;
    
    // Remove patterns like "CHAPTER \d+ \d+"
    char* temp1 = result;
    for (int chapter = 1; chapter <= 20; chapter++) {
        for (int page = 1; page <= 999; page++) {
            char pattern[64];
            snprintf(pattern, sizeof(pattern), "CHAPTER %d %d", chapter, page);
            
            char* temp = str_replace(temp1, pattern, "");
            if (temp) {
                if (temp1 != result) free(temp1);
                temp1 = temp;
            }
            
            // Also try lowercase
            snprintf(pattern, sizeof(pattern), "Chapter %d %d", chapter, page);
            temp = str_replace(temp1, pattern, "");
            if (temp) {
                if (temp1 != result) free(temp1);
                temp1 = temp;
            }
        }
    }
    
    return temp1;
}

// Remove standalone page numbers
static char* remove_page_numbers(const char* content)
{
    if (!content) return NULL;
    
    char* result = strdup(content);
    if (!result) return NULL;
    
    // Remove standalone numbers on their own lines (likely page numbers)
    char* lines = strdup(content);
    if (!lines) {
        free(result);
        return NULL;
    }
    
    StringBuilder* sb = sb_create(strlen(content));
    if (!sb) {
        free(result);
        free(lines);
        return NULL;
    }
    
    char* line = strtok(lines, "\n");
    while (line) {
        // Trim whitespace
        while (*line == ' ' || *line == '\t') line++;
        char* end = line + strlen(line) - 1;
        while (end > line && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
            *end = '\0';
            end--;
        }
        
        // Check if line is just a number (page number)
        int is_just_number = 1;
        if (strlen(line) == 0) {
            is_just_number = 0;
        } else {
            for (char* p = line; *p; p++) {
                if (*p < '0' || *p > '9') {
                    is_just_number = 0;
                    break;
                }
            }
        }
        
        // Skip if it's just a page number, otherwise keep
        if (!is_just_number || strlen(line) > 3) { // Allow numbers > 999 as they're likely content
            sb_append(sb, line);
            sb_append(sb, "\n");
        }
        
        line = strtok(NULL, "\n");
    }
    
    free(result);
    free(lines);
    result = strdup(sb->data);
    sb_destroy(sb);
    
    return result;
}

// Remove inline references like (ref 13), [14], etc.
static char* remove_inline_references(const char* content)
{
    if (!content) return NULL;
    
    char* result = strdup(content);
    if (!result) return NULL;
    
    // Remove (ref \d+) patterns
    for (int i = 1; i <= 999; i++) {
        char pattern[32];
        snprintf(pattern, sizeof(pattern), "(ref %d)", i);
        
        char* temp = str_replace(result, pattern, "");
        if (temp) {
            free(result);
            result = temp;
        }
    }
    
    // Remove [digit] patterns (footnote markers)
    for (int i = 1; i <= 99; i++) {
        char pattern[16];
        snprintf(pattern, sizeof(pattern), "[%d]", i);
        
        char* temp = str_replace(result, pattern, "");
        if (temp) {
            free(result);
            result = temp;
        }
        
        // Also try with spaces
        snprintf(pattern, sizeof(pattern), "[%d ]", i);
        temp = str_replace(result, pattern, "");
        if (temp) {
            free(result);
            result = temp;
        }
        
        snprintf(pattern, sizeof(pattern), "[ %d]", i);
        temp = str_replace(result, pattern, "");
        if (temp) {
            free(result);
            result = temp;
        }
    }
    
    return result;
}

// Normalize bullet lists (replace `o` with -)
static char* normalize_bullet_lists(const char* content)
{
    if (!content) return NULL;
    
    char* result = strdup(content);
    if (!result) return NULL;
    
    // Replace `o` at start of lines with -
    char* temp1 = str_replace(result, "\n`o`", "\n-");
    if (temp1) {
        free(result);
        result = temp1;
    }
    
    // Replace `o` at start of content
    if (result[0] == '`' && result[1] == 'o' && result[2] == '`') {
        char* temp = malloc(strlen(result) + 1);
        if (temp) {
            temp[0] = '-';
            strcpy(temp + 1, result + 3);
            free(result);
            result = temp;
        }
    }
    
    // Also handle ` o ` (space o space) patterns
    char* temp2 = str_replace(result, "`o `", "- ");
    if (temp2) {
        free(result);
        result = temp2;
    }
    
    char* temp3 = str_replace(result, "` o`", "- ");
    if (temp3) {
        free(result);
        result = temp3;
    }
    
    return result;
}

// Fix table multiline cells by collapsing line breaks inside cells
static char* fix_table_multiline_cells(const char* content)
{
    if (!content) return NULL;
    
    char* result = strdup(content);
    if (!result) return NULL;
    
    // Process line by line to fix table formatting
    char* lines = strdup(content);
    if (!lines) {
        free(result);
        return NULL;
    }
    
    StringBuilder* sb = sb_create(strlen(content) * 2);
    if (!sb) {
        free(result);
        free(lines);
        return NULL;
    }
    
    char* line = strtok(lines, "\n");
    char* prev_line = NULL;
    
    while (line) {
        // Check if current line looks like a broken table cell
        // (starts with text but no |, and prev line was a table row)
        int current_is_table = (strchr(line, '|') != NULL);
        int prev_was_table = (prev_line && strchr(prev_line, '|') != NULL);
        
        if (prev_was_table && !current_is_table && strlen(line) < 50) {
            // This might be a continuation of the previous table cell
            // Remove the last newline from sb and append this as part of previous line
            if (sb->length > 0 && sb->data[sb->length - 1] == '\n') {
                sb->length--;
                sb->data[sb->length] = '\0';
            }
            sb_append(sb, "; ");
            sb_append(sb, line);
            sb_append(sb, "\n");
        } else {
            sb_append(sb, line);
            sb_append(sb, "\n");
        }
        
        prev_line = line;
        line = strtok(NULL, "\n");
    }
    
    free(result);
    free(lines);
    result = strdup(sb->data);
    sb_destroy(sb);
    
    return result;
}

// Remove figure stubs (lines like "**Figure 3-1: ...**")
static char* remove_figure_stubs(const char* content)
{
    if (!content) return NULL;
    
    char* result = strdup(content);
    if (!result) return NULL;
    
    // Remove figure captions that start with **Figure
    char* lines = strdup(content);
    if (!lines) {
        free(result);
        return NULL;
    }
    
    StringBuilder* sb = sb_create(strlen(content));
    if (!sb) {
        free(result);
        free(lines);
        return NULL;
    }
    
    char* line = strtok(lines, "\n");
    while (line) {
        // Trim whitespace
        while (*line == ' ' || *line == '\t') line++;
        
        // Check if line starts with **Figure or **Table
        int is_figure_stub = 0;
        if (strncmp(line, "**Figure", 8) == 0 || 
            strncmp(line, "**Table", 7) == 0 ||
            strncmp(line, "Figure", 6) == 0 ||
            strncmp(line, "Table", 5) == 0) {
            is_figure_stub = 1;
        }
        
        if (!is_figure_stub) {
            sb_append(sb, line);
            sb_append(sb, "\n");
        }
        
        line = strtok(NULL, "\n");
    }
    
    free(result);
    free(lines);
    result = strdup(sb->data);
    sb_destroy(sb);
    
    return result;
}

// NEW: Implementation of text processing improvements (fixes 1-4)

// Fix #3: Remove standalone horizontal rules (--- not in tables)
static char* remove_standalone_horizontal_rules(const char* content)
{
    if (!content) return NULL;
    
    // Replace standalone "---" lines that aren't part of tables
    char* temp1 = str_replace((char*)content, "\n---\n", "\n");
    if (!temp1) return NULL;
    
    char* temp2 = str_replace(temp1, "\n--- \n", "\n");
    free(temp1);
    if (!temp2) return NULL;
    
    // Also handle start/end cases
    char* temp3 = str_replace(temp2, "---\n", "");
    free(temp2);
    if (!temp3) return NULL;
    
    char* result = str_replace(temp3, "\n---", "");
    free(temp3);
    
    return result;
}

// Fix #4: Normalize excessive newlines (3+ becomes 2)
static char* normalize_excessive_newlines(const char* content)
{
    if (!content) return NULL;
    
    char* temp1 = str_replace((char*)content, "\n\n\n\n", "\n\n");
    if (!temp1) return NULL;
    
    char* temp2 = str_replace(temp1, "\n\n\n", "\n\n");
    free(temp1);
    if (!temp2) return NULL;
    
    // Run again to catch any remaining cases
    char* result = str_replace(temp2, "\n\n\n", "\n\n");
    free(temp2);
    
    return result;
}

// Fix #2: Bold text consolidation (**text** **more** -> **text more**)
static char* merge_consecutive_bold_spans(const char* content)
{
    if (!content) return NULL;
    
    // Merge bold spans separated by single space
    char* temp1 = str_replace((char*)content, "** **", " ");
    if (!temp1) return NULL;
    
    // Merge bold spans separated by multiple spaces
    char* temp2 = str_replace(temp1, "**  **", " ");
    free(temp1);
    if (!temp2) return NULL;
    
    // Merge bold spans with tab
    char* temp3 = str_replace(temp2, "**\t**", " ");
    free(temp2);
    if (!temp3) return NULL;
    
    // Handle cases like **word** **word**
    char* temp4 = str_replace(temp3, "** **", " ");
    free(temp3);
    
    return temp4;
}

// Fix #1: Intelligent line merging with structure preservation and minimum words per line
static char* merge_short_lines(const char* content)
{
    if (!content) return NULL;
    
    size_t len = strlen(content);
    char* result = malloc(len + 1);
    if (!result) return NULL;
    
    const char* src = content;
    char* dst = result;
    
    while (*src) {
        if (*src == '\n') {
            // Get current line info
            char* current_line_start = dst;
            while (current_line_start > result && *(current_line_start-1) != '\n') {
                current_line_start--;
            }
            
            // Get next line info
            const char* next_line_start = src + 1;
            const char* next_line_end = strchr(next_line_start, '\n');
            if (!next_line_end) next_line_end = next_line_start + strlen(next_line_start);
            
            // Skip if next line is empty
            if (next_line_end == next_line_start) {
                *dst++ = *src++;
                continue;
            }
            
            // Check if next line should NOT be merged (preserve structure)
            int preserve_next = 0;
            
            // 1. Headers
            if (*next_line_start == '#') preserve_next = 1;
            
            // 2. Lists/bullets
            int is_list_candidate = 0;
            if (*next_line_start == '-' || *next_line_start == '*' || *next_line_start == '+') {
                char next_char = (next_line_start + 1 < next_line_end) ? next_line_start[1] : '\0';
                if (next_char == ' ' || next_char == '\t') {
                    is_list_candidate = 1;
                }
            }
            if (!is_list_candidate && (*next_line_start >= '1' && *next_line_start <= '9')) {
                const char* marker = next_line_start + 1;
                if (*marker == '.' || *marker == ')' ) {
                    is_list_candidate = 1;
                }
            }
            if (is_list_candidate) {
                preserve_next = 1;
            }
            
            // 3. Tables
            if (*next_line_start == '|') preserve_next = 1;
            
            // 4. Code blocks
            if (strncmp(next_line_start, "```", 3) == 0) preserve_next = 1;
            
            // 5. Blockquotes
            if (*next_line_start == '>') preserve_next = 1;
            
            // 6. Sentences that end with punctuation (preserve paragraph breaks)
            if (dst > current_line_start) {
                char* line_end = dst - 1;
                while (line_end > current_line_start && (*line_end == ' ' || *line_end == '\t')) line_end--;
                if (*line_end == '.' || *line_end == '!' || *line_end == '?' || *line_end == ':') {
                    preserve_next = 1;
                }
            }
            
            // 7. Line starting with uppercase (likely new sentence/paragraph)
            if (*next_line_start >= 'A' && *next_line_start <= 'Z') {
                // Check if current line ends with sentence punctuation
                if (dst > current_line_start) {
                    char* line_end = dst - 1;
                    while (line_end > current_line_start && (*line_end == ' ' || *line_end == '\t')) line_end--;
                    if (*line_end == '.' || *line_end == '!' || *line_end == '?') {
                        preserve_next = 1;
                    }
                }
            }
            
            // 8. Special case: Headers with same level should be merged if they're short names
            // Check if current and next lines are both headers with same level
            int current_header_level = 0, next_header_level = 0;
            
            // Count # in current line
            char* p = current_line_start;
            while (p < dst && *p == '#') {
                current_header_level++;
                p++;
            }
            
            // Count # in next line  
            p = (char*)next_line_start;
            while (p < next_line_end && *p == '#') {
                next_header_level++;
                p++;
            }
            
            // If both are headers with same level and both are short (like names), allow merging
            if (current_header_level > 0 && current_header_level == next_header_level) {
                // Count actual content words (excluding #)
                int current_content_words = 0, next_content_words = 0;
                
                // Current line content words (skip #'s and spaces)
                p = current_line_start;
                while (p < dst && (*p == '#' || *p == ' ')) p++;
                int in_word = 0;
                while (p < dst) {
                    if (*p == ' ' || *p == '\t') {
                        in_word = 0;
                    } else if (!in_word) {
                        current_content_words++;
                        in_word = 1;
                    }
                    p++;
                }
                
                // Next line content words (skip #'s and spaces)
                p = (char*)next_line_start;
                while (p < next_line_end && (*p == '#' || *p == ' ')) p++;
                in_word = 0;
                while (p < next_line_end) {
                    if (*p == ' ' || *p == '\t') {
                        in_word = 0;
                    } else if (!in_word) {
                        next_content_words++;
                        in_word = 1;
                    }
                    p++;
                }
                
                // If both headers have <= 3 content words (like names), don't preserve - allow merging
                if (current_content_words <= 3 && next_content_words <= 3) {
                    preserve_next = 0; // Override previous preservation decision
                }
            }
            
            // If structure should be preserved, keep the newline
            if (preserve_next) {
                *dst++ = *src++;
                continue;
            }
            
            // Count words in current and next lines
            int current_words = 0, next_words = 0;
            
            // Count words in current line
            char* word_p = current_line_start;
            int in_word = 0;
            while (word_p < dst) {
                if (*word_p == ' ' || *word_p == '\t') {
                    in_word = 0;
                } else if (!in_word) {
                    current_words++;
                    in_word = 1;
                }
                word_p++;
            }
            
            // Count words in next line
            word_p = (char*)next_line_start;
            in_word = 0;
            while (word_p < next_line_end) {
                if (*word_p == ' ' || *word_p == '\t') {
                    in_word = 0;
                } else if (!in_word) {
                    next_words++;
                    in_word = 1;
                }
                word_p++;
            }
            
            // Merge if either line has < 6 words AND combined would be < 20 words
            if ((current_words < 6 || next_words < 6) && (current_words + next_words < 20)) {
                // Check that we're not breaking mid-sentence
                int safe_to_merge = 1;
                
                // Don't merge if current line ends with sentence punctuation
                if (dst > current_line_start) {
                    char* line_end = dst - 1;
                    while (line_end > current_line_start && (*line_end == ' ' || *line_end == '\t')) line_end--;
                    if (*line_end == '.' || *line_end == '!' || *line_end == '?') {
                        safe_to_merge = 0;
                    }
                }
                
                if (safe_to_merge) {
                    *dst++ = ' '; // Replace newline with space
                    src++; // Skip the newline
                    continue;
                }
            }
        }
        
        *dst++ = *src++;
    }
    *dst = '\0';
    
    return result;
}

static int is_single_line_bullet_candidate(const char* start, size_t len, size_t* trim_start, size_t* trim_len)
{
    if (!start || len == 0) {
        if (trim_start) *trim_start = 0;
        if (trim_len) *trim_len = 0;
        return 0;
    }

    size_t leading = 0;
    while (leading < len && start[leading] != '\n' && start[leading] != '\r' && isspace((unsigned char)start[leading])) {
        leading++;
    }

    size_t trailing = len;
    while (trailing > leading && start[trailing - 1] != '\n' && start[trailing - 1] != '\r' && isspace((unsigned char)start[trailing - 1])) {
        trailing--;
    }

    size_t trimmed_len = trailing > leading ? (trailing - leading) : 0;

    if (trim_start) *trim_start = leading;
    if (trim_len) *trim_len = trimmed_len;

    if (trimmed_len == 0 || trimmed_len > 80) {
        return 0;
    }

    for (size_t i = leading; i < trailing; i++) {
        if (start[i] == '\n' || start[i] == '\r') {
            return 0;
        }
    }

    char first = start[leading];
    if (first == '-' || first == '*' || first == '+' || first == '#' || first == '>' || first == '|') {
        return 0;
    }

    if (isdigit((unsigned char)first)) {
        size_t idx = leading;
        while (idx < trailing && isdigit((unsigned char)start[idx])) {
            idx++;
        }
        if (idx < trailing && (start[idx] == '.' || start[idx] == ')')) {
            return 0;
        }
    }

    char last = start[trailing - 1];
    if (last == '.' || last == ':' || last == ';' || last == '?' || last == '!') {
        return 0;
    }

    int words = 0;
    int in_word = 0;
    for (size_t i = leading; i < trailing; i++) {
        if (isspace((unsigned char)start[i])) {
            in_word = 0;
        } else if (!in_word) {
            words++;
            in_word = 1;
        }
    }

    if (words == 0 || words > 8) {
        return 0;
    }

    return 1;
}

static char* convert_single_lines_to_bullets(const char* content)
{
    if (!content) return NULL;

    size_t len = strlen(content);
    if (len == 0) return strdup("");

    typedef struct {
        const char* start;
        size_t len;
        size_t delim_len;
        size_t trim_start;
        size_t trim_len;
        int is_candidate;
    } BulletSegment;

    size_t capacity = 64;
    BulletSegment* segments = malloc(capacity * sizeof(BulletSegment));
    if (!segments) return NULL;

    size_t count = 0;
    const char* cursor = content;
    const char* end = content + len;

    while (cursor < end) {
        const char* delim = strstr(cursor, "\n\n");
        size_t text_len;
        size_t delim_len = 0;

        if (!delim) {
            text_len = (size_t)(end - cursor);
            delim_len = 0;
        } else {
            const char* delim_end = delim;
            while (delim_end < end && (*delim_end == '\n' || *delim_end == '\r')) {
                delim_end++;
            }
            text_len = (size_t)(delim - cursor);
            delim_len = (size_t)(delim_end - delim);
        }

        if (count >= capacity) {
            size_t new_capacity = capacity * 2;
            BulletSegment* new_segments = realloc(segments, new_capacity * sizeof(BulletSegment));
            if (!new_segments) {
                free(segments);
                return NULL;
            }
            segments = new_segments;
            capacity = new_capacity;
        }

        BulletSegment* seg = &segments[count++];
        seg->start = cursor;
        seg->len = text_len;
        seg->delim_len = delim_len;
        seg->trim_start = 0;
        seg->trim_len = 0;
        seg->is_candidate = is_single_line_bullet_candidate(cursor, text_len, &seg->trim_start, &seg->trim_len);

        if (!delim) {
            cursor = end;
        } else {
            cursor = delim + delim_len;
        }
    }

    StringBuilder* sb = sb_create(len + 64);
    if (!sb) {
        free(segments);
        return NULL;
    }

    size_t i = 0;
    while (i < count) {
        BulletSegment* seg = &segments[i];

        if (!seg->is_candidate) {
            if (seg->len > 0) {
                sb_append_formatted(sb, "%.*s", (int)seg->len, seg->start);
            }
            if (seg->delim_len > 0) {
                sb_append_formatted(sb, "%.*s", (int)seg->delim_len, seg->start + seg->len);
            }
            i++;
            continue;
        }

        size_t j = i;
        while (j < count && segments[j].is_candidate) {
            if (j > i && segments[j - 1].delim_len < 2) {
                break;
            }
            j++;
        }

        size_t seq_len = j - i;

        if (seq_len >= 2) {
            for (size_t k = i; k < j; k++) {
                BulletSegment* item = &segments[k];
                sb_append(sb, "- ");
                sb_append_formatted(sb, "%.*s", (int)item->trim_len, item->start + item->trim_start);
                sb_append_char(sb, '\n');
            }

            size_t tail_newlines = segments[j - 1].delim_len;
            if (tail_newlines > 0) {
                for (size_t n = 1; n < tail_newlines; n++) {
                    sb_append_char(sb, '\n');
                }
            }

            i = j;
            continue;
        }

        if (seg->len > 0) {
            sb_append_formatted(sb, "%.*s", (int)seg->len, seg->start);
        }
        if (seg->delim_len > 0) {
            sb_append_formatted(sb, "%.*s", (int)seg->delim_len, seg->start + seg->len);
        }
        i++;
    }

    char* output = strdup(sb->data);
    sb_destroy(sb);
    free(segments);

    return output;
}

static char* python_table_cleanup_markdown(const char* content)
{
    if (!content) return NULL;

    size_t len = strlen(content);
    char* result = malloc(len * 2); // generous buffer
    if (!result) return NULL;
    result[0] = '\0';

    const char* cursor = content;

    while (*cursor) {
        // Check if this line looks like a table row
        if (*cursor == '|') {
            // Table block start
            const char* table_start = cursor;

            // Find where table block ends (first double newline or non-table line)
            const char* table_end = strstr(table_start, "\n\n");
            if (!table_end) {
                table_end = content + len; // until EOF
            }

            // Copy table block untouched
            strncat(result, table_start, (size_t)(table_end - table_start));

            // Move cursor
            cursor = table_end;
        } else {
            // Non-table chunk -> cleanup
            const char* next_table = strchr(cursor, '|');
            const char* chunk_end = next_table ? next_table : content + len;

            size_t chunk_len = (size_t)(chunk_end - cursor);
            char* chunk = strndup(cursor, chunk_len);

            if (chunk) {
                char* norm = normalize_excessive_newlines(chunk);
                if (norm) {
                    char* clean = advanced_cleanup_markdown(norm);
                    if (clean) {
                        strcat(result, clean);
                        free(clean);
                    }
                    free(norm);
                }
                free(chunk);
            }

            cursor = chunk_end;
        }
    }

    return result;
}


// Main advanced cleanup function that applies all improvements
static char* advanced_cleanup_markdown(const char* content)
{
    if (!content) return NULL;
    
    // Step 1: Normalize common Unicode punctuation and whitespace gremlins
    char* temp1 = normalize_unicode_gremlins(content);
    if (!temp1) return NULL;

    // Step 2: Remove all-caps headers with spaces (PDF layout artifacts)
    char* temp2 = remove_all_caps_headers(temp1);
    free(temp1);
    if (!temp2) return NULL;

    // Step 3: Remove chapter and page number sequences
    char* temp3 = remove_chapter_page_sequences(temp2);
    free(temp2);
    if (!temp3) return NULL;

    // Step 4: Remove standalone page numbers
    char* temp4 = remove_page_numbers(temp3);
    free(temp3);
    if (!temp4) return NULL;

    // Step 5: Remove inline references (ref 13), [14], etc.
    char* temp5 = remove_inline_references(temp4);
    free(temp4);
    if (!temp5) return NULL;

    // Step 6: Remove figure stubs
    char* temp6 = remove_figure_stubs(temp5);
    free(temp5);
    if (!temp6) return NULL;

    // Step 7: Normalize bullet lists (`o` -> -)
    char* temp7 = normalize_bullet_lists(temp6);
    free(temp6);
    if (!temp7) return NULL;

    // Step 8: Fix table multiline cells
    char* temp8 = fix_table_multiline_cells(temp7);
    free(temp7);
    if (!temp8) return NULL;

    // Step 9: Remove error references and other noisy artifacts
    char* temp9 = remove_error_references(temp8);
    free(temp8);
    if (!temp9) return NULL;

    // Step 10: Handle footnotes inline (before other text processing)
    char* temp10 = handle_footnotes_inline(temp9);
    free(temp9);
    if (!temp10) return NULL;

    // Step 11: Normalize italic/bold fragments
    char* temp11 = normalize_italic_bold_fragments(temp10);
    free(temp10);
    if (!temp11) return NULL;

    // Step 12: Apply structural improvements in sequence
    char* temp12 = remove_standalone_horizontal_rules(temp11);
    free(temp11);
    if (!temp12) return NULL;

    char* temp13 = merge_consecutive_bold_spans(temp12);
    free(temp12);
    if (!temp13) return NULL;

    char* temp14 = normalize_excessive_newlines(temp13);
    free(temp13);
    if (!temp14) return NULL;

    char* temp15 = merge_short_lines(temp14);
    free(temp14);
    if (!temp15) return NULL;

    char* temp16 = convert_single_lines_to_bullets(temp15);
    free(temp15);
    if (!temp16) return NULL;

    // Step 17: Run the existing cleanup as well
    char* temp17 = cleanup_markdown(temp16);
    if (!temp17) {
        free(temp16);
        return NULL;
    }
    free(temp16);
    
    char* result = temp17;

    return result;
}

// Worker thread function declarations

void* worker_thread(void* arg);

extern EXPORT char* page_to_markdown(const char* pdf_path, int page_number);
extern EXPORT void free_markdown(char* content);
extern EXPORT void register_table_callback(const char* (*callback)(const char*, int));
extern EXPORT void register_batch_table_callback(char* (*callback)(const char*, int*, double*, int*, int));
extern EXPORT int to_markdown(const char* pdf_path, const char* output_path);

// Batch callback signature: 
// char* callback(const char* pdf_path, int* page_numbers, double* bboxes, int* bbox_counts, int table_count)
// Returns buffer containing all markdown strings separated by null bytes
typedef char* (*python_batch_table_callback_buffer_t)(const char*, int*, double*, int*, int);

// Legacy single-table callback (deprecated)
typedef const char* (*python_table_callback_t)(const char*, int);

static python_table_callback_t g_python_table_callback = NULL;
static python_batch_table_callback_buffer_t g_python_batch_table_callback_buffer = NULL;

extern EXPORT void register_table_callback(const char* (*callback)(const char*, int))
{
    g_python_table_callback = callback;
}

extern EXPORT void register_batch_table_callback(char* (*callback)(const char*, int*, double*, int*, int))
{
    g_python_batch_table_callback_buffer = callback;
}

// Implementation begins here

// StringBuilder implementation

static StringBuilder* sb_create(size_t initial_capacity)
{
    StringBuilder* sb = malloc(sizeof(StringBuilder));

    if (!sb) return NULL;

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

static void sb_destroy(StringBuilder* sb)
{
    if (sb)
    {
        free(sb->data);

        free(sb);
    }
}

static int sb_ensure_capacity(StringBuilder* sb, size_t needed)
{
    if (!sb) return -1;

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

        char* new_data = realloc(sb->data, new_capacity);

        if (!new_data) return -1;

        sb->data = new_data;

        sb->capacity = new_capacity;
    }

    return 0;
}

static int sb_append(StringBuilder* sb, const char* str)
{
    if (!sb || !str) return -1;

    size_t str_len = strlen(str);

    if (str_len == 0) return 0;

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

static int sb_append_char(StringBuilder* sb, char c)
{
    if (!sb) return -1;

    if (sb_ensure_capacity(sb, sb->length + 2) != 0)
    {
        return -1;
    }

    sb->data[sb->length] = c;

    sb->length++;

    sb->data[sb->length] = '\0';

    return 0;
}

static int sb_append_formatted(StringBuilder* sb, const char* format, ...)
{
    if (!sb || !format) return -1;

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

static BatchBuffer* batch_buffer_create(FILE* output_file)
{
    BatchBuffer* buffer = malloc(sizeof(BatchBuffer));

    if (!buffer) return NULL;

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

static void batch_buffer_destroy(BatchBuffer* buffer)
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

static int batch_buffer_append(BatchBuffer* buffer, const char* str)
{
    if (!buffer || !str) return -1;

    size_t str_len = strlen(str);

    if (str_len == 0) return 0;

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

static int batch_buffer_append_formatted(BatchBuffer* buffer, const char* format, ...)
{
    if (!buffer || !format) return -1;

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

    if (buffer->length + (size_t)len >= buffer->capacity)
    {
        if (batch_buffer_flush(buffer) != 0)
        {
            va_end(args);

            return -1;
        }
    }

    // If still too large, write directly

    if ((size_t)len >= buffer->capacity)
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

static int batch_buffer_flush(BatchBuffer* buffer)
{
    if (!buffer || buffer->length == 0) return 0;

    size_t written = fwrite(buffer->data, 1, buffer->length, buffer->output_file);

    if (written != buffer->length)
    {
        return -1;
    }

    buffer->length = 0;

    return 0;
}

static void batch_buffer_reset(BatchBuffer* buffer)
{
    if (buffer)
    {
        buffer->length = 0;
    }
}

// Reusable span array functions

static void reusable_spans_init(ReusableSpanArray* spans)
{
    if (!spans) return;

    spans->count = 0;

    memset(spans->text_buffers, 0, sizeof(spans->text_buffers));
}

static void reusable_spans_reset(ReusableSpanArray* spans)
{
    if (!spans) return;

    spans->count = 0;

    // Clear text buffers for reuse

    for (int i = 0; i < MAX_SPANS_PER_LINE; i++)
    {
        spans->text_buffers[i][0] = '\0';
    }
}

// Generate markdown for a single span directly into batch buffer

// New function for span-level formatting without header logic
static int append_markdown_for_span_no_header(BatchBuffer* buffer, TextSpan* span)
{
    if (!buffer || !span || !span->text) return -1;

    // For non-header lines: apply span-level formatting
    // Python order (lines 707-714): mono, bold, italic, strikeout
    char prefix[32] = "";
    char suffix[32] = "";

    // Build prefix in Python order: mono, bold, italic, strikeout
    if (span->mono)
    {
        strcat(prefix, "`");
    }
    if (span->bold)
    {
        strcat(prefix, "**");
    }
    if (span->italic)
    {
        strcat(prefix, "_");
    }
    if (span->strikeout)
    {
        strcat(prefix, "~~");
    }

    // Suffix is in reverse order: strikeout, italic, bold, mono
    if (span->strikeout)
    {
        strcat(suffix, "~~");
    }
    if (span->italic)
    {
        strcat(suffix, "_");
    }
    if (span->bold)
    {
        strcat(suffix, "**");
    }
    if (span->mono)
    {
        strcat(suffix, "`");
    }

    // Apply formatting
    if (strlen(prefix) > 0)
    {
        if (batch_buffer_append(buffer, prefix) != 0) return -1;
    }

    // Strip trailing whitespace from span text (matching Python: s['text'].strip())
    char* text = span->text;
    while (*text == ' ' || *text == '\t') text++;
    size_t len = strlen(text);
    while (len > 0 && (text[len-1] == ' ' || text[len-1] == '\t' || text[len-1] == '\n'))
    {
        len--;
    }
    char temp_text[MAX_SPAN_TEXT_SIZE];
    strncpy(temp_text, text, len);
    temp_text[len] = '\0';

    if (batch_buffer_append(buffer, temp_text) != 0) return -1;

    if (strlen(suffix) > 0)
    {
        if (batch_buffer_append(buffer, suffix) != 0) return -1;
    }

    // Add space after span (Python adds space in line 719)
    if (batch_buffer_append(buffer, " ") != 0) return -1;

    return 0;
}

// Keep the old function for compatibility (now unused)
static int append_markdown_for_span(BatchBuffer* buffer, TextSpan* span, FontAnalyzer* analyzer)
{
    if (!buffer || !span || !span->text) return -1;

    // Get header prefix if this is a header
    const char* header_prefix = "";
    int is_likely_header = 0;

    if (analyzer)
    {
        int font_size = (int)round(span->size);

        if (font_size >= 0 && font_size < MAX_FONT_SIZE &&
            analyzer->header_mapping[font_size][0] != '\0')
        {
            // Only apply header formatting if this looks like an actual header:
            // - Text is relatively short (likely a title/heading)
            // - Text doesn't end with lowercase or punctuation (not mid-sentence)
            // - Text has reasonable length for a header
            size_t text_len = strlen(span->text);
            
            if (text_len > 0 && text_len < 200) // Headers shouldn't be too long
            {
                char last_char = span->text[text_len - 1];
                // Check if it looks like a header (doesn't end with sentence punctuation)
                if (last_char != '.' && last_char != ',' && last_char != ';' && 
                    last_char != ':' && last_char != '?' && last_char != '!')
                {
                    // Also check if it's not all whitespace or mostly numbers
                    int has_alpha = 0;
                    for (size_t i = 0; i < text_len; i++)
                    {
                        if (isalpha(span->text[i]))
                        {
                            has_alpha = 1;
                            break;
                        }
                    }
                    
                    if (has_alpha)
                    {
                        header_prefix = analyzer->header_mapping[font_size];
                        is_likely_header = 1;
                    }
                }
            }
        }
    }

    // Apply markdown formatting
    int bold = span->bold;
    int italic = span->italic;
    int mono = span->mono;

    // Build the formatted text
    if (is_likely_header && strlen(header_prefix) > 0)
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

FontAnalyzer* font_analyzer_create(void)
{
    FontAnalyzer* analyzer = malloc(sizeof(FontAnalyzer));

    if (!analyzer) return NULL;

    memset(analyzer->font_counts, 0, sizeof(analyzer->font_counts));

    analyzer->body_limit = 12.0;

    memset(analyzer->header_mapping, 0, sizeof(analyzer->header_mapping));

    return analyzer;
}

void font_analyzer_destroy(FontAnalyzer* analyzer)
{
    if (analyzer)
    {
        free(analyzer);
    }
}

// NEW: Implement document-wide font analysis like Python's IdentifyHeaders.__init__()
int font_analyzer_analyze_document(FontAnalyzer* analyzer, fz_context* ctx, fz_document* doc, int* pages, int page_count, double body_limit)
{
    if (!analyzer || !ctx || !doc) return -1;
    
    // Clear existing counts
    memset(analyzer->font_counts, 0, sizeof(analyzer->font_counts));
    
    // Python: fontsizes = defaultdict(int)
    // Analyze all pages to count font sizes - exactly like Python lines 108-120
    for (int pno = 0; pno < page_count; pno++)
    {
        fz_page* page = NULL;
        fz_stext_page* textpage = NULL;
        
        fz_try(ctx)
        {
            page = fz_load_page(ctx, doc, pages ? pages[pno] : pno);
            fz_stext_options opts = {
                .flags = FZ_STEXT_CLIP | FZ_STEXT_ACCURATE_BBOXES | FZ_STEXT_COLLECT_STYLES
            };
            textpage = fz_new_stext_page_from_page(ctx, page, &opts);
            
            // Extract all text spans and count font sizes
            for (fz_stext_block* block = textpage->first_block; block; block = block->next)
            {
                if (block->type != FZ_STEXT_BLOCK_TEXT) continue;
                
                for (fz_stext_line* line = block->u.t.first_line; line; line = line->next)
                {
                    for (fz_stext_char* ch = line->first_char; ch; ch = ch->next)
                    {
                        // Skip whitespace like Python: if not is_white(s["text"])
                        if (ch->c <= 32 || ch->c == 160) continue; // space, tab, newline, nbsp
                        
                        // Python: fontsz = round(span["size"])
                        int fontsz = (int)round(ch->size);
                        
                        if (fontsz >= 0 && fontsz < MAX_FONT_SIZE)
                        {
                            // Python: fontsizes[fontsz] += len(span["text"].strip())
                            analyzer->font_counts[fontsz]++;
                        }
                    }
                }
            }
        }
        fz_always(ctx)
        {
            if (textpage) fz_drop_stext_page(ctx, textpage);
            if (page) fz_drop_page(ctx, page);
        }
        fz_catch(ctx)
        {
            // Continue processing other pages even if one fails
            fprintf(stderr, "Warning: Failed to analyze fonts on page %d\n", pno + 1);
        }
    }
    
    // Now build the header mappings
    font_analyzer_build_mappings(analyzer, body_limit, 6);
    return 0;
}

void font_analyzer_build_mappings(FontAnalyzer* analyzer, double body_font_size,
                                  int max_header_levels)
{
    if (!analyzer) return;

    // Find the most common font size (body text) - exactly like Python
    // Python lines 132-137: temp = sorted([(k, v) for k, v in fontsizes.items()], key=lambda i: (i[1], i[0]))
    int max_count = 0;
    int body_font_index = (int)round(body_font_size);

    // Find actual body font size from font counts
    for (int i = 0; i < MAX_FONT_SIZE; i++)
    {
        if (analyzer->font_counts[i] > max_count)
        {
            max_count = analyzer->font_counts[i];
            body_font_index = i;
        }
    }

    // Python line 137: self.body_limit = max(body_limit, temp[-1][0])
    if (max_count > 0)
    {
        analyzer->body_limit = fmax(body_font_size, (double)body_font_index);
    }
    else
    {
        analyzer->body_limit = body_font_size;
    }

    // Python lines 141-145: identify up to max_levels font sizes as header candidates
    int sizes[MAX_FONT_SIZE];
    int size_count = 0;
    
    // sizes = sorted([f for f in fontsizes.keys() if f > self.body_limit], reverse=True)[:max_levels]
    for (int f = 0; f < MAX_FONT_SIZE; f++)
    {
        if (analyzer->font_counts[f] > 0 && f > analyzer->body_limit)
        {
            sizes[size_count++] = f;
        }
    }
    
    // Sort descending (reverse=True in Python)
    for (int i = 0; i < size_count - 1; i++)
    {
        for (int j = i + 1; j < size_count; j++)
        {
            if (sizes[i] < sizes[j])
            {
                int temp = sizes[i];
                sizes[i] = sizes[j];
                sizes[j] = temp;
            }
        }
    }
    
    // Take only up to max_header_levels (Python [:max_levels])
    if (size_count > max_header_levels)
        size_count = max_header_levels;

    // Python lines 147-149: make the header tag dictionary
    // for i, size in enumerate(sizes, start=1):
    //     self.header_id[size] = "#" * i + " "
    for (int i = 0; i < size_count; i++)
    {
        int size = sizes[i];
        int level = i + 1; // start=1 in Python enumerate
        
        // Build header string: "#" * level + " "
        for (int h = 0; h < level && h < 6; h++)
        {
            analyzer->header_mapping[size][h] = '#';
        }
        analyzer->header_mapping[size][level] = ' ';
        analyzer->header_mapping[size][level + 1] = '\0';
    }
    
    // Python lines 150-151: if self.header_id.keys():
    //     self.body_limit = min(self.header_id.keys()) - 1
    if (size_count > 0)
    {
        analyzer->body_limit = sizes[size_count - 1] - 1; // min of sorted descending list is last element
    }
}

const char* get_header_id_from_analyzer(TextSpan* span, void* user_data)
{
    FontAnalyzer* analyzer = (FontAnalyzer*)user_data;

    if (!analyzer || !span) return "";

    int font_size = (int)round(span->size);

    if (font_size >= 0 && font_size < MAX_FONT_SIZE &&
        analyzer->header_mapping[font_size][0] != '\0')
    {
        return analyzer->header_mapping[font_size];
    }

    return "";
}

// Page parameters functions

static PageParams* create_page_params(void)
{
    PageParams* params = malloc(sizeof(PageParams));

    if (!params) return NULL;

    memset(params, 0, sizeof(PageParams));

    return params;
}

static void destroy_page_params(PageParams* params)
{
    if (!params) return;

    free(params);
}

static void init_page_params(PageParams* params, fz_page* page __attribute__((unused)), fz_rect clip)
{
    if (!params) return;

    params->clip = clip;

    params->textpage = NULL;
}

// OCR page detection (matching Python exactly)

static int page_is_ocr(fz_context* ctx, fz_page* page)
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
    // Python: (s["flags"] & 16) or (s["char_flags"] & 8)
    return (flags & 16) || (char_flags & 8);
}

static int is_italic(int flags)
{
    // Python: s["flags"] & 2
    return flags & 2;
}

static int is_mono(int flags)
{
    // Python: s["flags"] & 8
    return flags & 8;
}

static int is_strikeout(int char_flags)
{
    // Python: s["char_flags"] & 1
    return char_flags & 1;
}

// Stub implementations for removed functions

static int extract_annotations(fz_context* ctx, fz_page* page, PageParams* params)
{
    (void)ctx;
    (void)page;
    (void)params;

    return 0;
}

static int extract_links(fz_context* ctx, fz_page* page, PageParams* params)
{
    (void)ctx;
    (void)page;
    (void)params;

    return 0;
}

static char* resolve_span_link(LinkInfo* links, int link_count, fz_rect span_bbox)
{
    (void)links;
    (void)link_count;
    (void)span_bbox;

    return NULL;
}

static int find_column_boxes(fz_context* ctx, fz_page* page, PageParams* params)
{
    (void)ctx;
    (void)page;
    (void)params;

    return 0;
}

static void sort_reading_order(fz_rect* rects, int count)
{
    (void)rects;
    (void)count;
}

static int intersects_rects(fz_rect rect, fz_rect* rect_list, int count)
{
    (void)rect;
    (void)rect_list;
    (void)count;

    return 0;
}

static int is_in_rects(fz_rect rect, fz_rect* rect_list, int count)
{
    (void)rect;
    (void)rect_list;
    (void)count;

    return 0;
}

static void process_text_in_rect(fz_context* ctx, PageParams* params, fz_rect text_rect)
{
    (void)ctx;
    (void)params;
    (void)text_rect;
}

int get_header_level_from_font(TextSpan* span, FontAnalyzer* analyzer)
{
    if (!span || !analyzer) return 0;
    int font_size = (int)round(span->size);
    if (font_size <= 0 || font_size >= MAX_FONT_SIZE) return 0;

    const char* hdr_str = analyzer->header_mapping[font_size];
    if (!hdr_str || hdr_str[0] == '\0') return 0;

    // Count '#' characters to get level
    int level = 0;
    for (int i = 0; hdr_str[i] && level < 6; i++)
        if (hdr_str[i] == '#') level++;
    return level;
}

// Main page processing function (memory-efficient version)

static int process_pdf_page(fz_context* ctx, fz_page* page, PageParams* params)
{
    if (!ctx || !page || !params || !params->batch_buffer || !params->reusable_spans) return -1;

    fz_stext_page* textpage = NULL;

    fz_try(ctx)
    {
        fz_stext_options opts = {.flags = FZ_STEXT_CLIP | FZ_STEXT_ACCURATE_BBOXES |
                                          FZ_STEXT_COLLECT_STYLES};

        textpage = fz_new_stext_page_from_page(ctx, page, &opts);

        if (!textpage)
        {
            fz_rethrow(ctx);
        }

        for (fz_stext_block* block = textpage->first_block; block; block = block->next)
        {
            if (block->type != FZ_STEXT_BLOCK_TEXT) continue;

            // Check if block intersects tables (skip if >90% overlap)
            int is_in_table = 0;
            if (params->table_rects && params->table_count > 0)
            {
                for (int i = 0; i < params->table_count; i++)
                {
                    fz_rect* table = &params->table_rects[i];
                    float x0 = fmaxf(block->bbox.x0, table->x0);
                    float y0 = fmaxf(block->bbox.y0, table->y0);
                    float x1 = fminf(block->bbox.x1, table->x1);
                    float y1 = fminf(block->bbox.y1, table->y1);
                    float width = fmaxf(0.0f, x1 - x0);
                    float height = fmaxf(0.0f, y1 - y0);
                    float area_intersect = width * height;
                    float block_area = (block->bbox.x1 - block->bbox.x0) * (block->bbox.y1 - block->bbox.y0);

                    if (block_area > 0 && (area_intersect / block_area) > 0.90f)
                    {
                        is_in_table = 1;
                        break;
                    }
                }
            }

            if (is_in_table) continue;

            for (fz_stext_line* line = block->u.t.first_line; line; line = line->next)
            {
                reusable_spans_reset(params->reusable_spans);

                TextSpan* current_span = NULL;

                // Build spans for this line
                for (fz_stext_char* ch = line->first_char; ch; ch = ch->next)
                {
                    if (ch->c < 32 && ch->c != '\t') continue;

                    const char* font_name = ch->font ? ch->font->name : "unknown";
                    
                    // Map MuPDF flags to Python-style flags
                    int flags = 0;
                    int char_flags = 0;
                    
                    // Use MuPDF's font flag checking functions/macros
                    // Python expects: flags & 16 for bold, flags & 2 for italic, flags & 8 for mono
                    if (ch->font) {
                        if (fz_font_is_bold(ctx, ch->font))
                            flags |= 16;
                        if (fz_font_is_italic(ctx, ch->font))
                            flags |= 2;
                        if (fz_font_is_monospaced(ctx, ch->font))
                            flags |= 8;
                    }
                    
                    // Use the flag-based detection functions
                    int bold = is_bold(flags, char_flags);
                    int italic = is_italic(flags);
                    int mono = is_mono(flags);
                    int strikeout = is_strikeout(char_flags);

                    // Start new span if style changed
                    if (current_span == NULL ||
                        fabs(ch->size - current_span->size) > 0.01 ||
                        bold != current_span->bold ||
                        italic != current_span->italic ||
                        mono != current_span->mono ||
                        strikeout != current_span->strikeout ||
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
                        current_span->font = (char*)font_name;
                        current_span->size = ch->size;
                        current_span->bbox = fz_make_rect(ch->quad.ul.x, ch->quad.ul.y, ch->quad.lr.x, ch->quad.lr.y);
                        current_span->flags = flags;
                        current_span->char_flags = char_flags;
                        current_span->bold = bold;
                        current_span->italic = italic;
                        current_span->mono = mono;
                        current_span->strikeout = strikeout;
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

                // Now process the complete line - EXACTLY like Python
                
                // Skip lines with no spans
                if (params->reusable_spans->count == 0) {
                    continue;
                }
                
                // Python line 632: Build full line text FIRST
                char line_text[MAX_SPAN_TEXT_SIZE * MAX_SPANS_PER_LINE] = "";
                for (int i = 0; i < params->reusable_spans->count; i++)
                {
                    if (strlen(line_text) > 0)
                        strcat(line_text, " ");
                    strcat(line_text, params->reusable_spans->spans[i].text);
                }
                
                // Strip whitespace (Python: text.strip()) - Python line 621
                char* start = line_text;
                while (*start == ' ' || *start == '\t') start++;
                char* end = start + strlen(start) - 1;
                while (end > start && (*end == ' ' || *end == '\t' || *end == '\n')) end--;
                *(end + 1) = '\0';

                // Skip completely empty lines
                if (strlen(start) == 0) {
                    continue;
                }
                
                // Python line 620: hdr_string = max_header_id(spans, page=parms.page)
                // Find the minimum header level among all spans (Python logic)
                int min_hdr_level = 0;
                for (int i = 0; i < params->reusable_spans->count; i++)
                {
                    TextSpan* span = &params->reusable_spans->spans[i];
                    int span_hdr_level = get_header_level_from_font(span, params->font_analyzer);
                    if (span_hdr_level > 0 && (min_hdr_level == 0 || span_hdr_level < min_hdr_level))
                    {
                        min_hdr_level = span_hdr_level;
                    }
                }
                
                // Build header string like Python: "#" * level + " "
                char hdr_string[8] = "";
                if (min_hdr_level > 0 && min_hdr_level <= 6)
                {
                    for (int i = 0; i < min_hdr_level; i++)
                    {
                        hdr_string[i] = '#';
                    }
                    hdr_string[min_hdr_level] = ' ';
                    hdr_string[min_hdr_level + 1] = '\0';
                }

                // Check if ALL spans have each formatting property (Python lines 627-630)
                int all_strikeout = 1, all_italic = 1, all_bold = 1, all_mono = 1;
                for (int i = 0; i < params->reusable_spans->count; i++)
                {
                    if (!params->reusable_spans->spans[i].strikeout) all_strikeout = 0;
                    if (!params->reusable_spans->spans[i].italic) all_italic = 0;
                    if (!params->reusable_spans->spans[i].bold) all_bold = 0;
                    if (!params->reusable_spans->spans[i].mono) all_mono = 0;
                }

                // IS THIS A HEADER LINE? (Python line 633)
                if (hdr_string[0] != '\0')
                {
                    // YES - Header line: apply line-level formatting (Python lines 638-652)
                    batch_buffer_append(params->batch_buffer, hdr_string);
                    
                    // Apply formatting in order: mono, italic, bold, strikeout (Python lines 641-648)
                    if (all_mono)
                        batch_buffer_append(params->batch_buffer, "`");
                    if (all_italic)
                        batch_buffer_append(params->batch_buffer, "_");
                    if (all_bold)
                        batch_buffer_append(params->batch_buffer, "**");
                    if (all_strikeout)
                        batch_buffer_append(params->batch_buffer, "~~");
                    
                    batch_buffer_append(params->batch_buffer, start);
                    
                    // Close in reverse order (Python lines 649-652)
                    if (all_strikeout)
                        batch_buffer_append(params->batch_buffer, "~~");
                    if (all_bold)
                        batch_buffer_append(params->batch_buffer, "**");
                    if (all_italic)
                        batch_buffer_append(params->batch_buffer, "_");
                    if (all_mono)
                        batch_buffer_append(params->batch_buffer, "`");
                }
                else
                {
                    // NO - Regular line: apply span-level formatting (Python lines 707-719)
                    for (int i = 0; i < params->reusable_spans->count; i++)
                    {
                        TextSpan* s = &params->reusable_spans->spans[i];
                        
                        // Build prefix/suffix (Python order: mono, bold, italic, strikeout)
                        char prefix[32] = "";
                        char suffix[32] = "";
                        
                        if (s->mono)
                            strcat(prefix, "`");
                        if (s->bold)
                            strcat(prefix, "**");
                        if (s->italic)
                            strcat(prefix, "_");
                        if (s->strikeout)
                            strcat(prefix, "~~");
                        
                        // Suffix is reverse
                        if (s->strikeout)
                            strcat(suffix, "~~");
                        if (s->italic)
                            strcat(suffix, "_");
                        if (s->bold)
                            strcat(suffix, "**");
                        if (s->mono)
                            strcat(suffix, "`");
                        
                        // Strip span text
                        char* span_start = s->text;
                        while (*span_start == ' ' || *span_start == '\t') span_start++;
                        size_t span_len = strlen(span_start);
                        while (span_len > 0 && (span_start[span_len-1] == ' ' || span_start[span_len-1] == '\t'))
                            span_len--;
                        
                        char span_stripped[MAX_SPAN_TEXT_SIZE];
                        strncpy(span_stripped, span_start, span_len);
                        span_stripped[span_len] = '\0';
                        
                        // Output: prefix + text + suffix + space (Python line 719)
                        if (strlen(prefix) > 0)
                            batch_buffer_append(params->batch_buffer, prefix);
                        batch_buffer_append(params->batch_buffer, span_stripped);
                        if (strlen(suffix) > 0)
                            batch_buffer_append(params->batch_buffer, suffix);
                        batch_buffer_append(params->batch_buffer, " ");
                    }
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
        return -1;
    }

    return 0;
}

// =============================================================================
// Worker Thread and Parallel Processing
// =============================================================================

/**
 * @brief The main function for each worker thread.
 *
 * Each worker thread pulls jobs (page batches) from a shared queue, processes them
 * in isolation, and writes the output to a unique temporary file. This approach
 * minimizes memory usage and allows for parallel processing of the PDF.
 *
 * @param arg A void pointer to a `WorkerArgs` struct containing thread parameters.
 * @return NULL.
 */
void* worker_thread(void* arg)
{
    WorkerArgs* worker_args = (WorkerArgs*)arg;

    // Allocate a reusable span array for this thread to minimize memory allocations.
    ReusableSpanArray* reusable_spans = malloc(sizeof(ReusableSpanArray));
    if (!reusable_spans)
    {
        fprintf(stderr, "Error: Thread failed to allocate reusable spans buffer\n");
        return NULL;
    }
    reusable_spans_init(reusable_spans);

    // Main worker loop: continue processing jobs until a sentinel is found.
    while (1)
    {
        // Atomically get the next job from the queue.
        pthread_mutex_lock(worker_args->job_mutex);
        int current_job_index = (*worker_args->next_job_index)++;
        pthread_mutex_unlock(worker_args->job_mutex);

        Job current_job = worker_args->jobs[current_job_index];

        // A start_page of -1 is the sentinel indicating no more jobs.
        if (current_job.start_page == -1)
        {
            break;
        }

        // --- Step 1: Create a unique temporary filename for this batch's output.
        char temp_filename[256];
        snprintf(temp_filename, sizeof(temp_filename), "%s.batch_%d.tmp", worker_args->output_path, current_job.batch_num);
        FILE* temp_file = fopen(temp_filename, "w");
        if (!temp_file)
        {
            fprintf(stderr, "Error: Thread failed to create temporary file: %s\n", temp_filename);
            continue;
        }

        // --- Step 2: Process the batch in an isolated MuPDF context.
        fz_context* ctx = fz_new_context(NULL, NULL, 256 * 1024 * 1024); // 256MB store per thread
        if (!ctx)
        {
            fprintf(stderr, "Error: Thread failed to create MuPDF context.\n");
            fclose(temp_file);
            continue;
        }

        fz_document* doc = NULL;
        FontAnalyzer* batch_analyzer = NULL;
        BatchBuffer* batch_buffer = batch_buffer_create(temp_file);

        fz_try(ctx)
        {
            fz_register_document_handlers(ctx);
            doc = fz_open_document(ctx, worker_args->pdf_path);
            if (!doc) fz_rethrow(ctx);

            // --- First Pass: Analyze fonts for the current batch to identify headers.
            batch_analyzer = font_analyzer_create();
            if (!batch_analyzer) fz_rethrow(ctx);

            for (int p = current_job.start_page; p < current_job.end_page; p++)
            {
                fz_page* page = fz_load_page(ctx, doc, p);
                fz_stext_page* textpage = NULL;
                fz_try(ctx)
                {
                    fz_stext_options opts = {.flags = FZ_STEXT_CLIP | FZ_STEXT_ACCURATE_BBOXES | FZ_STEXT_COLLECT_STYLES};
                    textpage = fz_new_stext_page_from_page(ctx, page, &opts);
                    if (textpage)
                    {
                        for (fz_stext_block* block = textpage->first_block; block; block = block->next)
                        {
                            if (block->type != FZ_STEXT_BLOCK_TEXT) continue;
                            for (fz_stext_line* line = block->u.t.first_line; line; line = line->next)
                            {
                                for (fz_stext_char* ch = line->first_char; ch; ch = ch->next)
                                {
                                    if (ch->c <= 32 || ch->c == 160) continue;
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
                    if (textpage) fz_drop_stext_page(ctx, textpage);
                    if (page) fz_drop_page(ctx, page);
                }
                fz_catch(ctx)
                {
                    fprintf(stderr, "\nWarning: Failed to analyze fonts on page %d. Skipping.\n", p + 1);
                }
            }
            font_analyzer_build_mappings(batch_analyzer, 12.0, 6);

            // --- Second Pass: Generate markdown for each page in the batch.
            for (int p = current_job.start_page; p < current_job.end_page; p++)
            {
                fz_page* page = fz_load_page(ctx, doc, p);

                // Check if page contains tables
                int has_table = page_has_table(worker_args->pdf_path, p);
                
                fz_rect* table_rects = NULL;
                int table_count = 0;
                
                if (has_table) {
                    printf("  [DEBUG] Page %d: Table detected\n", p + 1);
                    // Lock mutex for table detection (thread-safe)
                    pthread_mutex_lock(worker_args->table_mutex);
                    
                    // Get page bounds for table detection
                    fz_rect page_bounds = {0};
                    fz_try(ctx)
                    {
                        page_bounds = fz_bound_page(ctx, page);
                    }
                    fz_catch(ctx)
                    {
                        // If bounds calculation fails, use default bounds
                        page_bounds = fz_make_rect(0, 0, 612, 792); // Default letter size
                    }
                    
                    // For now, treat entire page as potential table region
                    // In production, you'd use more sophisticated table detection
                    table_count = 1;
                    table_rects = malloc(sizeof(fz_rect));
                    if (table_rects) {
                        table_rects[0] = page_bounds;
                        
                        // Register table in global registry
                        char placeholder_id[64];
                        snprintf(placeholder_id, sizeof(placeholder_id), "TABLE_PAGE_%d_RECT_0", p);
                        table_registry_add(p, table_rects[0], current_job.batch_num, placeholder_id);
                        printf("  [DEBUG] Registered table: %s\n", placeholder_id);
                        
                        // Insert placeholder in markdown output
                        batch_buffer_append_formatted(batch_buffer, "\n<!-- %s -->\n\n", placeholder_id);
                    }
                    
                    pthread_mutex_unlock(worker_args->table_mutex);
                }

                // Set up parameters for page processing.
                PageParams params = {
                    .batch_buffer = batch_buffer,
                    .reusable_spans = reusable_spans,
                    .font_analyzer = batch_analyzer,
                    .table_rects = table_rects,
                    .table_count = table_count
                };

                process_pdf_page(ctx, page, &params);

                // Clean up page-specific resources.
                if (table_rects) free(table_rects);
                fz_drop_page(ctx, page);
            }
        }
        fz_always(ctx)
        {
            // Clean up all resources for this batch.
            batch_buffer_destroy(batch_buffer); // Flushes any remaining data to the temp file.
            fclose(temp_file);
            font_analyzer_destroy(batch_analyzer);
            if (doc) fz_drop_document(ctx, doc);
            fz_drop_context(ctx); // Frees all memory associated with this context.
        }
        fz_catch(ctx)
        {
            fprintf(stderr, "Error processing batch starting at page %d.\n", current_job.start_page + 1);
        }
    }

    free(reusable_spans);
    return NULL;
}

extern EXPORT char* page_to_markdown(const char* pdf_path, int page_number)
{
    if (!pdf_path || page_number < 0)
    {
        return NULL;
    }

    fz_context* ctx = fz_new_context(NULL, NULL, 256 * 1024 * 1024);
    if (!ctx)
    {
        fprintf(stderr, "Error: Failed to create MuPDF context for single page conversion.\n");
        return NULL;
    }

    char* output = NULL;
    fz_document* doc = NULL;

    fz_try(ctx)
    {
        fz_register_document_handlers(ctx);
        doc = fz_open_document(ctx, pdf_path);
        if (!doc) fz_rethrow(ctx);

        int page_count = fz_count_pages(ctx, doc);
        if (page_number >= page_count)
        {
            fz_throw(ctx, FZ_ERROR_GENERIC, "page out of range");
        }

        output = render_page_markdown(ctx, doc, page_number);
    }
    fz_always(ctx)
    {
        if (doc) fz_drop_document(ctx, doc);
    }
    fz_catch(ctx)
    {
        if (output)
        {
            free(output);
            output = NULL;
        }
    }

    fz_drop_context(ctx);

    if (output)
    {
        char* cleaned = advanced_cleanup_markdown(output);
        if (cleaned)
        {
            free(output);
            output = cleaned;
        }
    }

    return output;
}

extern EXPORT void free_markdown(char* content)
{
    if (content)
    {
        free(content);
    }
}

/**
 * @brief Main orchestration function for converting a PDF to Markdown.
 *
 * This function sets up the parallel processing environment. It divides the document
 * into batches, creates worker threads to process them, and then stitches the
 * resulting temporary files together into the final output file.
 *
 * @param pdf_path Path to the input PDF file.
 * @param output_path Path to the output Markdown file.
 * @return 0 on success, -1 on failure.
 */
extern EXPORT int to_markdown(const char* pdf_path, const char* output_path)
{
    if (!pdf_path || !output_path)
    {
        return -1;
    }

    // Initialize table registry
    if (table_registry_init() != 0) {
        fprintf(stderr, "Error: Failed to initialize table registry\n");
        return -1;
    }

    fz_context* ctx = fz_new_context(NULL, NULL, 256 * 1024 * 1024);
    if (!ctx)
    {
        fprintf(stderr, "Error: Failed to create MuPDF context for conversion.\n");
        table_registry_destroy();
        return -1;
    }

    fz_document* doc = NULL;
    FILE* out_file = NULL;
    int result = -1;

    fz_try(ctx)
    {
        fz_register_document_handlers(ctx);
        doc = fz_open_document(ctx, pdf_path);
        if (!doc) fz_rethrow(ctx);

        int page_count = fz_count_pages(ctx, doc);
        printf("Processing %d pages (C main path with Python fallback)...\n", page_count);

        out_file = fopen(output_path, "wb");
        if (!out_file)
        {
            fz_throw(ctx, FZ_ERROR_GENERIC, "failed to open output file");
        }

        // Create a batch buffer for sequential processing
        BatchBuffer* batch_buffer = batch_buffer_create(out_file);
        if (!batch_buffer) {
            fz_throw(ctx, FZ_ERROR_GENERIC, "failed to create batch buffer");
        }

        // PHASE 1: Document-wide font analysis (exactly like Python's IdentifyHeaders.__init__)
        printf("[info] Analyzing document fonts for proper header detection...\n");
        FontAnalyzer* analyzer = font_analyzer_create();
        if (!analyzer) {
            batch_buffer_destroy(batch_buffer);
            fz_throw(ctx, FZ_ERROR_GENERIC, "failed to create font analyzer");
        }

        // Use the proper document-wide font analysis
        if (font_analyzer_analyze_document(analyzer, ctx, doc, NULL, page_count, 12.0) != 0) {
            font_analyzer_destroy(analyzer);
            batch_buffer_destroy(batch_buffer);
            fz_throw(ctx, FZ_ERROR_GENERIC, "failed to analyze document fonts");
        }
        
        printf("[info] Font analysis complete - body font: %.1f, header mappings built\n", analyzer->body_limit);

        // Reusable span array
        ReusableSpanArray* reusable_spans = malloc(sizeof(ReusableSpanArray));
        if (!reusable_spans) {
            font_analyzer_destroy(analyzer);
            batch_buffer_destroy(batch_buffer);
            fz_throw(ctx, FZ_ERROR_GENERIC, "failed to allocate reusable spans");
        }
        reusable_spans_init(reusable_spans);

        // Process each page
        for (int pno = 0; pno < page_count; ++pno)
        {
            fz_page* page = fz_load_page(ctx, doc, pno);
            fz_rect page_bounds = {0};
            fz_try(ctx)
            {
                page_bounds = fz_bound_page(ctx, page);
            }
            fz_catch(ctx)
            {
                // If bounds calculation fails, use default bounds
                page_bounds = fz_make_rect(0, 0, 612, 792); // Default letter size
            }

            // Check for tables using page_has_table
            fz_rect* table_rects = NULL;
            int table_count = 0;

            int has_table = page_has_table(pdf_path, pno);
            if (has_table) {
                // Register table and create placeholder
                table_count = 1;
                table_rects = malloc(sizeof(fz_rect));
                if (table_rects) {
                    table_rects[0] = page_bounds;
                    
                    char placeholder_id[64];
                    snprintf(placeholder_id, sizeof(placeholder_id), "TABLE_PAGE_%d_RECT_0", pno);
                    table_registry_add(pno, table_rects[0], 0, placeholder_id);
                    
                    // Insert placeholder in output
                    batch_buffer_append_formatted(batch_buffer, "\n<!-- %s -->\n\n", placeholder_id);
                }
            }

            // Set up parameters for page processing
            PageParams params = {
                .batch_buffer = batch_buffer,
                .reusable_spans = reusable_spans,
                .font_analyzer = analyzer,
                .table_rects = table_rects,
                .table_count = table_count,
                .clip = page_bounds,
                .textpage = NULL
            };

            // Process the page (will skip table regions if table_rects is set)
            process_pdf_page(ctx, page, &params);

            // Clean up
            if (table_rects) free(table_rects);
            fz_drop_page(ctx, page);
        }

        // Flush and clean up processing resources
        batch_buffer_flush(batch_buffer);
        batch_buffer_destroy(batch_buffer);
        font_analyzer_destroy(analyzer);
        free(reusable_spans);

        // Close output file before table processing
        fclose(out_file);
        out_file = NULL;

        // Batch process all detected tables after C pipeline completes
        int table_count = table_registry_get_count();
        if (table_count > 0 && g_python_batch_table_callback_buffer) {
            printf("\n[info] Batch processing %d tables with Python (multicore)...\n", table_count);
            DetectedTable* tables = table_registry_get_tables();

            // Prepare batch data for Python callback
            int* page_numbers = malloc(table_count * sizeof(int));
            double* bboxes = malloc(table_count * 4 * sizeof(double));  // 4 coords per bbox
            int* bbox_counts = malloc(table_count * sizeof(int));

            if (!page_numbers || !bboxes || !bbox_counts) {
                fprintf(stderr, "Error: Failed to allocate memory for batch table processing\n");
                free(page_numbers);
                free(bboxes);
                free(bbox_counts);
                result = -1;
            } else {
                // Pack data for Python
                for (int i = 0; i < table_count; i++) {
                    page_numbers[i] = tables[i].page_number;
                    bboxes[i * 4 + 0] = tables[i].bbox.x0;
                    bboxes[i * 4 + 1] = tables[i].bbox.y0;
                    bboxes[i * 4 + 2] = tables[i].bbox.x1;
                    bboxes[i * 4 + 3] = tables[i].bbox.y1;
                    bbox_counts[i] = 4;  // Always 4 coordinates
                }

                // Call Python batch callback - it handles multiprocessing internally
                char* buffer = g_python_batch_table_callback_buffer(
                    pdf_path,
                    page_numbers,
                    bboxes,
                    bbox_counts,
                    table_count
                );

                if (buffer) {
                    // Split buffer into array of strings using null byte delimiter
                    char** table_results = malloc(table_count * sizeof(char*));
                    if (!table_results) {
                        fprintf(stderr, "Error: Failed to allocate table_results array\n");
                        // NOTE: Do NOT free(buffer) - it's managed by Python
                        result = -1;
                    } else {
                        char* ptr = buffer;
                        for (int i = 0; i < table_count; i++) {
                            table_results[i] = ptr;
                            while (*ptr) ptr++;
                            ptr++; // Skip null byte
                        }
                        printf("[info] Tables processed, reassembling output...\n");
                        result = reassemble_with_tables(output_path, tables, table_results, table_count);
                        // NOTE: Do NOT free(buffer) - it's managed by Python's _callback_buffers list
                        free(table_results);
                    }
                } else {
                    fprintf(stderr, "Warning: Batch table processing failed\n");
                    result = 0;  // Continue with text-only output
                }

                free(page_numbers);
                free(bboxes);
                free(bbox_counts);
            }
        } else if (table_count > 0 && g_python_table_callback) {
            // Fallback to legacy single-table callback
            printf("\n[warn] Using legacy single-table callback (slower)...\n");
            DetectedTable* tables = table_registry_get_tables();
            char** table_results = malloc(table_count * sizeof(char*));
            
            if (table_results) {
                for (int i = 0; i < table_count; i++) {
                    const char* python_md = g_python_table_callback(pdf_path, tables[i].page_number);
                    table_results[i] = python_md ? strdup(python_md) : strdup("");
                }

                result = reassemble_with_tables(output_path, tables, table_results, table_count);

                for (int i = 0; i < table_count; i++) {
                    free(table_results[i]);
                }
                free(table_results);
            }
        } else {
            // No tables - still need to apply cleanup to the output file
            printf("[info] No tables detected, applying final cleanup...\n");
            
            FILE* orig = fopen(output_path, "rb");
            if (orig) {
                fseek(orig, 0, SEEK_END);
                long size = ftell(orig);
                fseek(orig, 0, SEEK_SET);
                
                if (size > 0) {
                    char* raw_content = malloc(size + 1);
                    if (raw_content && fread(raw_content, 1, size, orig) == (size_t)size) {
                        raw_content[size] = '\0';
                        fclose(orig);
                        
                        char* cleaned = advanced_cleanup_markdown(raw_content);
                        if (cleaned) {
                            FILE* out = fopen(output_path, "wb");
                            if (out) {
                                fwrite(cleaned, 1, strlen(cleaned), out);
                                fclose(out);
                                result = 0;
                            }
                            free(cleaned);
                        }
                        free(raw_content);
                    } else {
                        fclose(orig);
                    }
                } else {
                    fclose(orig);
                }
            }
            
            if (result != 0) {
                result = 0;  // Don't fail the whole conversion if cleanup fails
            }
        }
    }
    fz_always(ctx)
    {
        if (out_file) fclose(out_file);
        if (doc) fz_drop_document(ctx, doc);
    }
    fz_catch(ctx)
    {
        result = -1;
    }

    fz_drop_context(ctx);
    table_registry_destroy();
    return result;
}

#ifndef NOLIB_MAIN
int main(int argc, char* argv[])
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

    const char* input_path = argv[1];

    char* output_path = NULL;

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

        char* ext = strrchr(output_path, '.');

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
        printf("\n[ok] Conversion completed successfully!\n");

        return 0;
    }
    else
    {
        printf("\n[error] Conversion failed!\n");

        return 1;
    }
}
#endif /* NOLIB_MAIN */
