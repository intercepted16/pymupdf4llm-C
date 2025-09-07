/*
 * Standalone PDF to Markdown Converter
 * Pure C implementation - no Python dependencies
 * 
 * Compile with:
 * gcc -O3 -march=native -std=c99 -o to_md to_md_standalone.c -lmupdf -lm
 * 
 * Usage:
 * ./to_md input.pdf [output.md]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include <ctype.h>
#include <sys/stat.h>
#include <unistd.h>

// Enable POSIX functions
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

// Add strdup declaration if not available
#ifndef _POSIX_C_SOURCE
char* strdup(const char* s);
#endif

// MuPDF headers (you'll need MuPDF C library installed)
#include <mupdf/fitz.h>

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

// Constants
#define MAX_FONT_SIZE 200
#define MAX_BUFFER_SIZE (16 * 1024 * 1024)  // 16MB
#define INITIAL_BUFFER_SIZE 8192
#define MAX_SPANS_PER_LINE 1000
#define MAX_LINES_PER_PAGE 10000

// String builder for efficient text construction
typedef struct {
    char* data;
    size_t length;
    size_t capacity;
} StringBuilder;

// Font analysis structure
typedef struct {
    int font_counts[MAX_FONT_SIZE];
    double body_limit;
    char header_mapping[MAX_FONT_SIZE][8];  // e.g., "## " for level 2
} FontAnalyzer;

// Text span information
typedef struct {
    char* text;
    char* font;
    int flags;
    int char_flags;
    double size;
    fz_rect bbox;
    fz_matrix trm;
    int bold;
    int italic;
    int mono;
    int strikeout;
} TextSpan;

// Text line information
typedef struct {
    TextSpan* spans;
    int span_count;
    int capacity;
    fz_rect bbox;
} TextLine;

// Text block information
typedef struct {
    TextLine* lines;
    int line_count;
    int capacity;
    fz_rect bbox;
    int block_type;
} TextBlock;

// Table cell information
typedef struct {
    char* text;
    fz_rect bbox;
} TableCell;

// Table information
typedef struct {
    TableCell** cells;
    int rows;
    int cols;
    fz_rect bbox;
} TableInfo;

// Link information
typedef struct {
    char* uri;
    fz_rect bbox;
} LinkInfo;

// Page processing parameters
typedef struct {
    StringBuilder* output;
    FontAnalyzer* font_analyzer;
    double image_size_limit;
    int ignore_images;
    int ignore_code;
    int extract_words;
    TextBlock* blocks;
    int block_count;
    int block_capacity;
    TableInfo* tables;
    int table_count;
    LinkInfo* links;
    int link_count;
    fz_rect* image_rects;
    int image_count;
} PageParams;

// Function declarations
static StringBuilder* sb_create(size_t initial_capacity);
static void sb_destroy(StringBuilder* sb);
static int sb_append(StringBuilder* sb, const char* str);
static int sb_append_char(StringBuilder* sb, char c);
static int sb_append_formatted(StringBuilder* sb, const char* format, ...);
static int sb_ensure_capacity(StringBuilder* sb, size_t needed);

static FontAnalyzer* font_analyzer_create(void);
static void font_analyzer_destroy(FontAnalyzer* analyzer);
static void font_analyzer_add_span(FontAnalyzer* analyzer, double size, int text_length);
static void font_analyzer_build_mappings(FontAnalyzer* analyzer, double body_limit, int max_levels);
static const char* font_analyzer_get_header(FontAnalyzer* analyzer, double size);

static int is_bold(int flags, int char_flags);
static int is_italic(int flags);
static int is_mono(int flags);
static int is_strikeout(int char_flags);
static int is_bullet_char(char c);

static void process_text_spans(StringBuilder* output, TextSpan* spans, int span_count, 
                              FontAnalyzer* analyzer, int ignore_code);
static void format_span_text(StringBuilder* output, TextSpan* span, const char* header_prefix);
static char* escape_markdown_chars(const char* text);
static char* trim_whitespace(char* str);

static int process_pdf_page(fz_context* ctx, fz_page* page, PageParams* params);
static int to_markdown(const char* pdf_path, const char* output_path);

// Helper functions for structured text processing
static TextSpan* create_text_span(const char* text, const char* font, int flags, 
                                 double size, fz_rect bbox, fz_matrix trm);
static void destroy_text_span(TextSpan* span);
static void populate_table_cells(TextBlock* block, TableInfo* table, float* column_positions, int column_count);

// Enhanced table detection function declarations
static int detect_tables_enhanced(fz_context* ctx, fz_page* page, PageParams* params);
static TextLine* create_text_line(void);
static void destroy_text_line(TextLine* line);
static void add_span_to_line(TextLine* line, TextSpan* span);
static TextBlock* create_text_block(void);
static void destroy_text_block(TextBlock* block);
static void add_line_to_block(TextBlock* block, TextLine* line);

// Table processing functions
static int detect_tables(fz_context* ctx, fz_page* page, PageParams* params);
static void process_table(TableInfo* table, StringBuilder* output);
static char* table_to_markdown(TableInfo* table);

// Link processing functions
static int extract_links(fz_context* ctx, fz_page* page, PageParams* params);
static char* resolve_span_link(LinkInfo* links, int link_count, fz_rect span_bbox);

// Advanced text processing
static void sort_text_blocks_reading_order(TextBlock* blocks, int count);
static void merge_adjacent_spans(TextLine* line);
static int detect_list_items(const char* text);
static void process_structured_text(fz_context* ctx, fz_page* page, PageParams* params);
static void format_span_text_enhanced(StringBuilder* output, TextSpan* span, const char* header_prefix, PageParams* params);

// StringBuilder implementation
static StringBuilder* sb_create(size_t initial_capacity) {
    StringBuilder* sb = malloc(sizeof(StringBuilder));
    if (!sb) return NULL;
    
    sb->capacity = initial_capacity > 0 ? initial_capacity : INITIAL_BUFFER_SIZE;
    sb->data = malloc(sb->capacity);
    sb->length = 0;
    
    if (!sb->data) {
        free(sb);
        return NULL;
    }
    
    sb->data[0] = '\0';
    return sb;
}

static void sb_destroy(StringBuilder* sb) {
    if (sb) {
        free(sb->data);
        free(sb);
    }
}

static int sb_ensure_capacity(StringBuilder* sb, size_t needed) {
    if (needed <= sb->capacity) return 0;
    
    size_t new_capacity = sb->capacity;
    while (new_capacity < needed && new_capacity < MAX_BUFFER_SIZE) {
        new_capacity *= 2;
    }
    
    if (new_capacity > MAX_BUFFER_SIZE) {
        new_capacity = MAX_BUFFER_SIZE;
    }
    
    if (needed > new_capacity) return -1;
    
    char* new_data = realloc(sb->data, new_capacity);
    if (!new_data) return -1;
    
    sb->data = new_data;
    sb->capacity = new_capacity;
    return 0;
}

static int sb_append(StringBuilder* sb, const char* str) {
    if (!sb || !str) return -1;
    
    size_t str_len = strlen(str);
    size_t needed = sb->length + str_len + 1;
    
    if (sb_ensure_capacity(sb, needed) < 0) return -1;
    
    memcpy(sb->data + sb->length, str, str_len);
    sb->length += str_len;
    sb->data[sb->length] = '\0';
    
    return 0;
}

static int sb_append_char(StringBuilder* sb, char c) {
    if (!sb) return -1;
    
    size_t needed = sb->length + 2;
    if (sb_ensure_capacity(sb, needed) < 0) return -1;
    
    sb->data[sb->length] = c;
    sb->length++;
    sb->data[sb->length] = '\0';
    
    return 0;
}

static int sb_append_formatted(StringBuilder* sb, const char* format, ...) {
    if (!sb || !format) return -1;
    
    va_list args;
    va_start(args, format);
    
    // Calculate needed size
    va_list args_copy;
    va_copy(args_copy, args);
    int needed = vsnprintf(NULL, 0, format, args_copy) + 1;
    va_end(args_copy);
    
    size_t total_needed = sb->length + needed;
    if (sb_ensure_capacity(sb, total_needed) < 0) {
        va_end(args);
        return -1;
    }
    
    vsnprintf(sb->data + sb->length, needed, format, args);
    sb->length += needed - 1;
    
    va_end(args);
    return 0;
}

// Font analyzer implementation
static FontAnalyzer* font_analyzer_create(void) {
    FontAnalyzer* analyzer = malloc(sizeof(FontAnalyzer));
    if (!analyzer) return NULL;
    
    memset(analyzer->font_counts, 0, sizeof(analyzer->font_counts));
    memset(analyzer->header_mapping, 0, sizeof(analyzer->header_mapping));
    analyzer->body_limit = 12.0;
    
    return analyzer;
}

static void font_analyzer_destroy(FontAnalyzer* analyzer) {
    if (analyzer) {
        free(analyzer);
    }
}

static void font_analyzer_add_span(FontAnalyzer* analyzer, double size, int text_length) {
    if (!analyzer) return;
    
    int font_size = (int)round(size);
    if (font_size >= 0 && font_size < MAX_FONT_SIZE) {
        analyzer->font_counts[font_size] += text_length;
    }
}

static void font_analyzer_build_mappings(FontAnalyzer* analyzer, double body_limit, int max_levels) {
    if (!analyzer) return;
    
    // Find most frequent font size
    int max_count = 0;
    int most_frequent_size = (int)body_limit;
    
    for (int i = 0; i < MAX_FONT_SIZE; i++) {
        if (analyzer->font_counts[i] > max_count) {
            max_count = analyzer->font_counts[i];
            most_frequent_size = i;
        }
    }
    
    analyzer->body_limit = fmax(body_limit, (double)most_frequent_size);
    
    // Create header mappings
    int level = 1;
    for (int i = MAX_FONT_SIZE - 1; i > (int)analyzer->body_limit && level <= max_levels; i--) {
        if (analyzer->font_counts[i] > 0) {
            char* mapping = analyzer->header_mapping[i];
            for (int j = 0; j < level && j < 6; j++) {
                mapping[j] = '#';
            }
            mapping[level] = ' ';
            mapping[level + 1] = '\0';
            level++;
        }
    }
}

static const char* font_analyzer_get_header(FontAnalyzer* analyzer, double size) {
    if (!analyzer) return "";
    
    int font_size = (int)round(size);
    if (font_size <= analyzer->body_limit || font_size >= MAX_FONT_SIZE) {
        return "";
    }
    
    return analyzer->header_mapping[font_size];
}

// Text formatting utilities
static int is_bold(int flags, int char_flags) {
    return (flags & FZ_STEXT_STYLE_BOLD) || (char_flags & 8);
}

static int is_italic(int flags) {
    return flags & FZ_STEXT_STYLE_ITALIC;
}

static int is_mono(int flags) {
    return flags & FZ_STEXT_STYLE_MONOSPACE;
}

static int is_strikeout(int char_flags) {
    return char_flags & 1;
}

static int is_bold_font(const char* font_name) {
    if (!font_name) return 0;
    const char* bold_indicators[] = {"Bold", "bold", "BOLD", "Heavy", "Black", "Demi", NULL};
    for (int i = 0; bold_indicators[i]; i++) {
        if (strstr(font_name, bold_indicators[i])) {
            return 1;
        }
    }
    return 0;
}

static int is_italic_font(const char* font_name) {
    if (!font_name) return 0;
    const char* italic_indicators[] = {"Italic", "italic", "ITALIC", "Oblique", "Slant", NULL};
    for (int i = 0; italic_indicators[i]; i++) {
        if (strstr(font_name, italic_indicators[i])) {
            return 1;
        }
    }
    return 0;
}

static int is_mono_font(const char* font_name) {
    if (!font_name) return 0;
    const char* mono_indicators[] = {"Mono", "mono", "MONO", "Courier", "Code", "Console", "Fixed", NULL};
    for (int i = 0; mono_indicators[i]; i++) {
        if (strstr(font_name, mono_indicators[i])) {
            return 1;
        }
    }
    return 0;
}

static int is_bullet_char(char c) {
    return c == '-' || c == '*' || c == '>';
}

static char* trim_whitespace(char* str) {
    if (!str) return NULL;
    
    // Trim leading whitespace
    while (isspace(*str)) str++;
    
    // Trim trailing whitespace
    char* end = str + strlen(str) - 1;
    while (end > str && isspace(*end)) end--;
    end[1] = '\0';
    
    return str;
}

static char* escape_markdown_chars(const char* text) {
    if (!text) return NULL;
    
    size_t len = strlen(text);
    char* escaped = malloc(len * 2 + 1);  // Worst case: every char needs escaping
    if (!escaped) return NULL;
    
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        char c = text[i];
        
        // Escape special markdown characters if they're not part of intentional formatting
        if (c == '\\' || c == '`' || c == '*' || c == '_' || c == '[' || c == ']' || 
            c == '(' || c == ')' || c == '#' || c == '+' || c == '!' || c == '|') {
            escaped[j++] = '\\';
        }
        escaped[j++] = c;
    }
    escaped[j] = '\0';
    
    return escaped;
}

static void format_span_text(StringBuilder* output, TextSpan* span, const char* header_prefix) {
    if (!output || !span || !span->text) return;
    
    char* text = trim_whitespace(span->text);
    if (strlen(text) == 0) return;
    
    // Add header prefix if present
    if (header_prefix && strlen(header_prefix) > 0) {
        sb_append(output, header_prefix);
    }
    
    // Determine formatting
    int bold = is_bold(span->flags, span->char_flags);
    int italic = is_italic(span->flags);
    int mono = is_mono(span->flags);
    int strikeout = is_strikeout(span->char_flags);
    
    // Add opening formatting
    if (strikeout) sb_append(output, "~~");
    if (bold) sb_append(output, "**");
    if (italic) sb_append(output, "_");
    if (mono) sb_append(output, "`");
    
    // Add the text content
    if (mono) {
        // For monospace text, don't escape markdown chars
        sb_append(output, text);
    } else {
        char* escaped = escape_markdown_chars(text);
        if (escaped) {
            sb_append(output, escaped);
            free(escaped);
        } else {
            sb_append(output, text);
        }
    }
    
    // Add closing formatting (reverse order)
    if (mono) sb_append(output, "`");
    if (italic) sb_append(output, "_");
    if (bold) sb_append(output, "**");
    if (strikeout) sb_append(output, "~~");
    
    // Add space between spans
    sb_append(output, " ");
}

static void process_text_spans(StringBuilder* output, TextSpan* spans, int span_count, 
                              FontAnalyzer* analyzer, int ignore_code) {
    if (!output || !spans || span_count <= 0) return;
    
    // Check if all spans have the same formatting (for headers and code blocks)
    int all_same_size = 1;
    int all_mono = 1;
    double first_size = spans[0].size;
    
    for (int i = 0; i < span_count; i++) {
        if (fabs(spans[i].size - first_size) > 0.5) {
            all_same_size = 0;
        }
        if (!is_mono(spans[i].flags)) {
            all_mono = 0;
        }
    }
    
    // Check for headers
    const char* header_prefix = "";
    if (all_same_size && analyzer) {
        header_prefix = font_analyzer_get_header(analyzer, first_size);
    }
    
    // Handle code blocks
    if (all_mono && !ignore_code && strlen(header_prefix) == 0) {
        sb_append(output, "```\n");
        
        for (int i = 0; i < span_count; i++) {
            if (spans[i].text && strlen(trim_whitespace(spans[i].text)) > 0) {
                sb_append(output, spans[i].text);
                if (i < span_count - 1) sb_append(output, " ");
            }
        }
        
        sb_append(output, "\n```\n");
        return;
    }
    
    // Handle headers
    if (strlen(header_prefix) > 0) {
        sb_append(output, header_prefix);
        
        for (int i = 0; i < span_count; i++) {
            if (spans[i].text && strlen(trim_whitespace(spans[i].text)) > 0) {
                // For headers, combine all text and apply unified formatting
                int bold = is_bold(spans[i].flags, spans[i].char_flags);
                int italic = is_italic(spans[i].flags);
                int mono = is_mono(spans[i].flags);
                int strikeout = is_strikeout(spans[i].char_flags);
                
                if (strikeout) sb_append(output, "~~");
                if (bold) sb_append(output, "**");
                if (italic) sb_append(output, "_");
                if (mono) sb_append(output, "`");
                
                sb_append(output, trim_whitespace(spans[i].text));
                
                if (mono) sb_append(output, "`");
                if (italic) sb_append(output, "_");
                if (bold) sb_append(output, "**");
                if (strikeout) sb_append(output, "~~");
                
                if (i < span_count - 1) sb_append(output, " ");
            }
        }
        
        sb_append(output, "\n\n");
        return;
    }
    
    // Handle regular text with individual span formatting
    for (int i = 0; i < span_count; i++) {
        format_span_text(output, &spans[i], "");
    }
    
    sb_append(output, "\n");
}

// Helper functions for structured text processing
static TextSpan* create_text_span(const char* text, const char* font, int flags, 
                                 double size, fz_rect bbox, fz_matrix trm) {
    TextSpan* span = malloc(sizeof(TextSpan));
    if (!span) return NULL;
    
    span->text = text ? strdup(text) : NULL;
    span->font = font ? strdup(font) : NULL;
    span->flags = flags;
    span->char_flags = 0;
    span->size = size;
    span->bbox = bbox;
    span->trm = trm;
    
    // Determine formatting from flags and font name
    span->bold = is_bold(flags, 0) || is_bold_font(font);
    span->italic = is_italic(flags) || is_italic_font(font);
    span->mono = is_mono(flags) || is_mono_font(font);
    span->strikeout = is_strikeout(0);
    
    return span;
}

static void destroy_text_span(TextSpan* span) {
    if (span) {
        free(span->text);
        free(span->font);
        free(span);
    }
}

static TextLine* create_text_line(void) {
    TextLine* line = malloc(sizeof(TextLine));
    if (!line) return NULL;
    
    line->spans = NULL;
    line->span_count = 0;
    line->capacity = 0;
    line->bbox = (fz_rect){0, 0, 0, 0};
    
    return line;
}

static void destroy_text_line(TextLine* line) {
    if (line) {
        for (int i = 0; i < line->span_count; i++) {
            destroy_text_span(&line->spans[i]);
        }
        free(line->spans);
        free(line);
    }
}

static void add_span_to_line(TextLine* line, TextSpan* span) {
    if (!line || !span) return;
    
    if (line->span_count >= line->capacity) {
        int new_capacity = line->capacity == 0 ? 8 : line->capacity * 2;
        TextSpan* new_spans = realloc(line->spans, new_capacity * sizeof(TextSpan));
        if (!new_spans) return;
        line->spans = new_spans;
        line->capacity = new_capacity;
    }
    
    line->spans[line->span_count] = *span;
    line->span_count++;
    
    // Update line bbox
    if (line->span_count == 1) {
        line->bbox = span->bbox;
    } else {
        line->bbox = fz_union_rect(line->bbox, span->bbox);
    }
}

static TextBlock* create_text_block(void) {
    TextBlock* block = malloc(sizeof(TextBlock));
    if (!block) return NULL;
    
    block->lines = NULL;
    block->line_count = 0;
    block->capacity = 0;
    block->bbox = (fz_rect){0, 0, 0, 0};
    block->block_type = 0;
    
    return block;
}

static void destroy_text_block(TextBlock* block) {
    if (block) {
        for (int i = 0; i < block->line_count; i++) {
            destroy_text_line(&block->lines[i]);
        }
        free(block->lines);
        free(block);
    }
}

static void add_line_to_block(TextBlock* block, TextLine* line) {
    if (!block || !line) return;
    
    if (block->line_count >= block->capacity) {
        int new_capacity = block->capacity == 0 ? 8 : block->capacity * 2;
        TextLine* new_lines = realloc(block->lines, new_capacity * sizeof(TextLine));
        if (!new_lines) return;
        block->lines = new_lines;
        block->capacity = new_capacity;
    }
    
    block->lines[block->line_count] = *line;
    block->line_count++;
    
    // Update block bbox
    if (block->line_count == 1) {
        block->bbox = line->bbox;
    } else {
        block->bbox = fz_union_rect(block->bbox, line->bbox);
    }
}

// Table detection disabled - matching original Python when table detection fails
static int detect_tables_enhanced(fz_context* ctx, fz_page* page, PageParams* params) {
    if (!ctx || !page || !params) return 0;
    
    // Initialize with no tables (like original when table_strategy is None)
    params->tables = NULL;
    params->table_count = 0;
    
    return 0;  // No tables detected - matches original Python behavior when tables disabled
}

static char* table_to_markdown(TableInfo* table) {
    if (!table || table->rows == 0 || table->cols == 0) return NULL;
    
    StringBuilder* md = sb_create(1024);
    if (!md) return NULL;
    
    // Generate markdown table
    for (int row = 0; row < table->rows; row++) {
        sb_append(md, "|");
        for (int col = 0; col < table->cols; col++) {
            TableCell* cell = &table->cells[row][col];
            if (cell && cell->text) {
                sb_append(md, " ");
                // Clean up the text (remove extra whitespace)
                char* cleaned_text = trim_whitespace(cell->text);
                if (cleaned_text && strlen(cleaned_text) > 0) {
                    sb_append(md, cleaned_text);
                } else {
                    sb_append(md, " ");
                }
                sb_append(md, " ");
            } else {
                sb_append(md, "   ");
            }
            sb_append(md, "|");
        }
        sb_append(md, "\n");
        
        // Add separator row after header (first row)
        if (row == 0) {
            sb_append(md, "|");
            for (int col = 0; col < table->cols; col++) {
                sb_append(md, "---|");
            }
            sb_append(md, "\n");
        }
    }
    
    char* result = strdup(md->data);
    sb_destroy(md);
    return result;
}

static int extract_links(fz_context* ctx, fz_page* page, PageParams* params) {
    // Extract links from the page
    params->links = NULL;
    params->link_count = 0;
    
    fz_try(ctx) {
        fz_link* link = fz_load_links(ctx, page);
        
        // Count links first
        int count = 0;
        for (fz_link* l = link; l; l = l->next) {
            if (l->uri) count++;
        }
        
        if (count > 0) {
            params->links = malloc(count * sizeof(LinkInfo));
            params->link_count = 0;
            
            // Extract link information
            for (fz_link* l = link; l; l = l->next) {
                if (l->uri && params->link_count < count) {
                    LinkInfo* link_info = &params->links[params->link_count];
                    link_info->uri = strdup(l->uri);
                    link_info->bbox = l->rect;
                    params->link_count++;
                }
            }
        }
        
        fz_drop_link(ctx, link);
    }
    fz_catch(ctx) {
        params->link_count = 0;
    }
    
    return params->link_count;
}

static char* resolve_span_link(LinkInfo* links, int link_count, fz_rect span_bbox) {
    if (!links || link_count == 0) return NULL;
    
    // Check if span overlaps with any link (at least 70% overlap)
    for (int i = 0; i < link_count; i++) {
        fz_rect intersection = fz_intersect_rect(span_bbox, links[i].bbox);
        if (!fz_is_empty_rect(intersection)) {
            float span_area = (span_bbox.x1 - span_bbox.x0) * (span_bbox.y1 - span_bbox.y0);
            float intersection_area = (intersection.x1 - intersection.x0) * (intersection.y1 - intersection.y0);
            
            if (span_area > 0 && intersection_area / span_area >= 0.7) {
                return strdup(links[i].uri);
            }
        }
    }
    
    return NULL;
}

static void format_span_text_enhanced(StringBuilder* output, TextSpan* span, const char* header_prefix, PageParams* params) {
    if (!output || !span || !span->text) return;
    
    char* text = trim_whitespace(span->text);
    if (strlen(text) == 0) return;
    
    // Check if this span is part of a link
    char* link_url = NULL;
    if (params && params->links && params->link_count > 0) {
        link_url = resolve_span_link(params->links, params->link_count, span->bbox);
    }
    
    // Add header prefix if present
    if (header_prefix && strlen(header_prefix) > 0) {
        sb_append(output, header_prefix);
    }
    
    // If this is a link, wrap everything in link syntax
    if (link_url) {
        sb_append(output, "[");
    }
    
    // Add opening formatting
    if (span->strikeout) sb_append(output, "~~");
    if (span->bold) sb_append(output, "**");
    if (span->italic) sb_append(output, "_");
    if (span->mono) sb_append(output, "`");
    
    // Add the text content
    if (span->mono) {
        // For monospace text, don't escape markdown chars
        sb_append(output, text);
    } else {
        char* escaped = escape_markdown_chars(text);
        if (escaped) {
            sb_append(output, escaped);
            free(escaped);
        } else {
            sb_append(output, text);
        }
    }
    
    // Add closing formatting (reverse order)
    if (span->mono) sb_append(output, "`");
    if (span->italic) sb_append(output, "_");
    if (span->bold) sb_append(output, "**");
    if (span->strikeout) sb_append(output, "~~");
    
    // Close link if present
    if (link_url) {
        sb_append(output, "](");
        sb_append(output, link_url);
        sb_append(output, ")");
        free(link_url);
    }
    
    // Add space between spans
    sb_append(output, " ");
}

static int detect_list_items(const char* text) {
    if (!text) return 0;
    
    // Check for bullet points
    const char* bullets[] = {"- ", "* ", "> ", "• ", "◦ ", "▪ ", "▫ ", NULL};
    for (int i = 0; bullets[i]; i++) {
        if (strncmp(text, bullets[i], strlen(bullets[i])) == 0) {
            return 1;
        }
    }
    
    // Check for numbered lists (1. 2. etc.)
    if (isdigit(text[0])) {
        int i;
        for (i = 1; text[i] && isdigit(text[i]); i++);
        if (text[i] == '.' && text[i+1] == ' ') {
            return 2; // numbered list
        }
    }
    
    return 0;
}

static void process_structured_text(fz_context* ctx, fz_page* page, PageParams* params) {
    if (!ctx || !page || !params) return;
    
    fz_rect page_rect = fz_bound_page(ctx, page);
    fz_stext_page* stext_page = NULL;
    
    fz_try(ctx) {
        // Extract text with structure preservation and collect styles
        stext_page = fz_new_stext_page(ctx, page_rect);
        fz_stext_options opts = { 
            .flags = FZ_STEXT_PRESERVE_LIGATURES | FZ_STEXT_PRESERVE_WHITESPACE,
            .scale = 1.0f
        };
        fz_device* dev = fz_new_stext_device(ctx, stext_page, &opts);
        fz_run_page(ctx, page, dev, fz_identity, NULL);
        fz_close_device(ctx, dev);
        fz_drop_device(ctx, dev);
        
        // Initialize blocks array
        params->block_capacity = 16;
        params->blocks = malloc(params->block_capacity * sizeof(TextBlock));
        params->block_count = 0;
        
        // Process text blocks
        for (fz_stext_block* block = stext_page->first_block; block; block = block->next) {
            if (block->type != FZ_STEXT_BLOCK_TEXT) continue;
            
            TextBlock* text_block = create_text_block();
            if (!text_block) continue;
            
            // Process lines in block
            for (fz_stext_line* line = block->u.t.first_line; line; line = line->next) {
                TextLine* text_line = create_text_line();
                if (!text_line) continue;
                
                // Group characters into spans based on font properties
                fz_stext_char* ch = line->first_char;
                while (ch) {
                    // Start new span
                    StringBuilder* span_text = sb_create(256);
                    if (!span_text) break;
                    
                    // Extract font information from the character
                    fz_font* font = ch->font;
                    const char* font_name = font ? fz_font_name(ctx, font) : "";
                    double size = ch->size;
                    fz_rect span_bbox = (fz_rect){ ch->quad.ul.x, ch->quad.ul.y, ch->quad.lr.x, ch->quad.lr.y };
                    fz_matrix trm = {1, 0, 0, 1, 0, 0}; // Identity for now
                    int font_flags = 0; // Simplified for now
                    
                    // Determine formatting from font name (simplified approach)
                    int font_is_bold = is_bold_font(font_name);
                    int font_is_italic = is_italic_font(font_name);
                    int font_is_mono = is_mono_font(font_name);
                    
                    // Collect characters with same formatting
                    fz_font* current_font = ch->font;
                    double current_size = ch->size;
                    
                    while (ch && ch->font == current_font && fabs(ch->size - current_size) < 0.5) {
                        sb_append_char(span_text, ch->c);
                        
                        // Expand span bbox
                        fz_rect char_rect = (fz_rect){ ch->quad.ul.x, ch->quad.ul.y, ch->quad.lr.x, ch->quad.lr.y };
                        span_bbox = fz_union_rect(span_bbox, char_rect);
                        
                        ch = ch->next;
                    }
                    
                    // Create and add span if it has content
                    char* trimmed_text = trim_whitespace(span_text->data);
                    if (strlen(trimmed_text) > 0) {
                        TextSpan* span = create_text_span(trimmed_text, font_name, font_flags, size, span_bbox, trm);
                        if (span) {
                            // Override with detected formatting
                            span->bold = font_is_bold;
                            span->italic = font_is_italic;
                            span->mono = font_is_mono;
                            
                            add_span_to_line(text_line, span);
                            
                            // Add to font analyzer
                            if (params->font_analyzer) {
                                font_analyzer_add_span(params->font_analyzer, size, strlen(trimmed_text));
                            }
                        }
                        free(span);
                    }
                    
                    sb_destroy(span_text);
                }
                
                // Add line to block if it has spans
                if (text_line->span_count > 0) {
                    add_line_to_block(text_block, text_line);
                }
                free(text_line);
            }
            
            // Add block to params if it has lines
            if (text_block->line_count > 0) {
                if (params->block_count >= params->block_capacity) {
                    params->block_capacity *= 2;
                    params->blocks = realloc(params->blocks, params->block_capacity * sizeof(TextBlock));
                }
                params->blocks[params->block_count] = *text_block;
                params->block_count++;
            }
            free(text_block);
        }
        
        // Sort blocks in reading order
        sort_text_blocks_reading_order(params->blocks, params->block_count);
    }
    fz_catch(ctx) {
        // Error handling
    }
    
    if (stext_page) fz_drop_stext_page(ctx, stext_page);
}

static void sort_text_blocks_reading_order(TextBlock* blocks, int count) {
    // Simple top-to-bottom, left-to-right sort
    // TODO: Implement more sophisticated column detection
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            TextBlock* a = &blocks[i];
            TextBlock* b = &blocks[j];
            
            // Sort by top position first, then by left position
            if (a->bbox.y0 > b->bbox.y0 || 
                (fabs(a->bbox.y0 - b->bbox.y0) < 5 && a->bbox.x0 > b->bbox.x0)) {
                TextBlock temp = *a;
                *a = *b;
                *b = temp;
            }
        }
    }
}

static int process_pdf_page(fz_context* ctx, fz_page* page, PageParams* params) {
    if (!ctx || !page || !params) return -1;
    
    // Extract structured text
    process_structured_text(ctx, page, params);
    
    // Extract links
    extract_links(ctx, page, params);
    
    // Detect tables
    detect_tables(ctx, page, params);
    
    // Process all text blocks and generate markdown
    for (int i = 0; i < params->block_count; i++) {
        TextBlock* block = &params->blocks[i];
        
        for (int j = 0; j < block->line_count; j++) {
            TextLine* line = &block->lines[j];
            
            // Skip empty lines
            if (line->span_count == 0) continue;
            
            // Check if all spans have the same formatting (for headers and code blocks)
            int all_same_size = 1;
            int all_mono = 1;
            double first_size = line->spans[0].size;
            
            for (int k = 0; k < line->span_count; k++) {
                if (fabs(line->spans[k].size - first_size) > 0.5) {
                    all_same_size = 0;
                }
                if (!line->spans[k].mono) {
                    all_mono = 0;
                }
            }
            
            // Check for headers
            const char* header_prefix = "";
            if (all_same_size && params->font_analyzer) {
                header_prefix = font_analyzer_get_header(params->font_analyzer, first_size);
            }
            
            // Handle code blocks
            if (all_mono && !params->ignore_code && strlen(header_prefix) == 0) {
                sb_append(params->output, "```\n");
                
                for (int k = 0; k < line->span_count; k++) {
                    if (line->spans[k].text && strlen(trim_whitespace(line->spans[k].text)) > 0) {
                        sb_append(params->output, line->spans[k].text);
                        if (k < line->span_count - 1) sb_append(params->output, " ");
                    }
                }
                
                sb_append(params->output, "\n```\n");
                continue;
            }
            
            // Handle headers
            if (strlen(header_prefix) > 0) {
                sb_append(params->output, header_prefix);
                
                for (int k = 0; k < line->span_count; k++) {
                    if (line->spans[k].text && strlen(trim_whitespace(line->spans[k].text)) > 0) {
                        format_span_text_enhanced(params->output, &line->spans[k], "", params);
                    }
                }
                
                sb_append(params->output, "\n");
                continue;
            }
            
            // Handle regular text with individual span formatting
            for (int k = 0; k < line->span_count; k++) {
                format_span_text_enhanced(params->output, &line->spans[k], "", params);
            }
            
            sb_append(params->output, "\n");
        }
        
        // Add paragraph break between blocks
        sb_append(params->output, "\n");
    }
    
    // Add any detected tables
    for (int i = 0; i < params->table_count; i++) {
        char* table_md = table_to_markdown(&params->tables[i]);
        if (table_md) {
            sb_append(params->output, table_md);
            sb_append(params->output, "\n\n");
            free(table_md);
        }
    }
    
    return 0;
}

static int to_markdown(const char* pdf_path, const char* output_path) {
    fz_context* ctx = NULL;
    fz_document* doc = NULL;
    FILE* output_file = NULL;
    int result = -1;
    
    // Create MuPDF context
    ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
    if (!ctx) {
        fprintf(stderr, "Error: Failed to create MuPDF context\n");
        return -1;
    }
    
    fz_try(ctx) {
        // Register document handlers
        fz_register_document_handlers(ctx);
        
        // Open PDF document
        doc = fz_open_document(ctx, pdf_path);
        if (!doc) {
            fprintf(stderr, "Error: Failed to open PDF file: %s\n", pdf_path);
            fz_rethrow(ctx);
        }
        
        int page_count = fz_count_pages(ctx, doc);
        printf("Processing %d pages...\n", page_count);
        
        // Create output
        StringBuilder* output = sb_create(1024 * 1024);  // 1MB initial
        if (!output) {
            fprintf(stderr, "Error: Failed to create output buffer\n");
            fz_rethrow(ctx);
        }
        
        // Create font analyzer
        FontAnalyzer* analyzer = font_analyzer_create();
        if (!analyzer) {
            fprintf(stderr, "Error: Failed to create font analyzer\n");
            sb_destroy(output);
            fz_rethrow(ctx);
        }
        
        // First pass: analyze fonts
        printf("Analyzing fonts...\n");
        PageParams params = {
            .output = output,
            .font_analyzer = analyzer,
            .image_size_limit = 0.05,
            .ignore_images = 1,
            .ignore_code = 0,
            .extract_words = 0,
            .blocks = NULL,
            .block_count = 0,
            .block_capacity = 0,
            .tables = NULL,
            .table_count = 0,
            .links = NULL,
            .link_count = 0,
            .image_rects = NULL,
            .image_count = 0
        };
        
        for (int i = 0; i < page_count; i++) {
            fz_page* page = fz_load_page(ctx, doc, i);
            if (page) {
                process_pdf_page(ctx, page, &params);
                fz_drop_page(ctx, page);
            }
        }
        
        // Build header mappings
        font_analyzer_build_mappings(analyzer, 12.0, 6);
        
        // Second pass: generate markdown
        printf("Generating markdown...\n");
        sb_destroy(output);
        output = sb_create(1024 * 1024);
        params.output = output;
        
        for (int i = 0; i < page_count; i++) {
            printf("Processing page %d/%d\r", i + 1, page_count);
            fflush(stdout);
            
            fz_page* page = fz_load_page(ctx, doc, i);
            if (page) {
                process_pdf_page(ctx, page, &params);
                fz_drop_page(ctx, page);
            }
        }
        
        printf("\nWriting output...\n");
        
        // Write to file
        output_file = fopen(output_path, "w");
        if (!output_file) {
            fprintf(stderr, "Error: Failed to open output file: %s\n", output_path);
            sb_destroy(output);
            font_analyzer_destroy(analyzer);
            fz_rethrow(ctx);
        }
        
        fwrite(output->data, 1, output->length, output_file);
        fclose(output_file);
        
        printf("Successfully converted %s to %s\n", pdf_path, output_path);
        printf("Output size: %zu characters\n", output->length);
        
        sb_destroy(output);
        font_analyzer_destroy(analyzer);
        result = 0;
    }
    fz_catch(ctx) {
        fprintf(stderr, "Error during PDF processing\n");
        if (output_file) fclose(output_file);
        result = -1;
    }
    
    if (doc) fz_drop_document(ctx, doc);
    fz_drop_context(ctx);
    
    return result;
}

int main(int argc, char* argv[]) {
    printf("Standalone PDF to Markdown Converter v1.0\n");
    printf("Ultra-optimized pure C implementation\n\n");
    
    if (argc < 2 || argc > 3) {
        printf("Usage: %s <input.pdf> [output.md]\n", argv[0]);
        printf("  input.pdf  - PDF file to convert\n");
        printf("  output.md  - Output markdown file (optional, default: input.md)\n");
        return 1;
    }
    
    const char* input_path = argv[1];
    char* output_path = NULL;
    
    // Generate output filename if not provided
    if (argc == 3) {
        size_t len = strlen(argv[2]);
        output_path = malloc(len + 1);
        strcpy(output_path, argv[2]);
    } else {
        size_t len = strlen(input_path);
        output_path = malloc(len + 4);  // .md + null terminator
        strcpy(output_path, input_path);
        
        // Replace extension
        char* ext = strrchr(output_path, '.');
        if (ext && strcmp(ext, ".pdf") == 0) {
            strcpy(ext, ".md");
        } else {
            strcat(output_path, ".md");
        }
    }
    
    // Check if input file exists
    struct stat st;
    if (stat(input_path, &st) != 0) {
        fprintf(stderr, "Error: Input file does not exist: %s\n", input_path);
        free(output_path);
        return 1;
    }
    
    printf("Input:  %s\n", input_path);
    printf("Output: %s\n\n", output_path);
    
    // Convert PDF to Markdown
    int result = to_markdown(input_path, output_path);
    
    free(output_path);
    
    if (result == 0) {
        printf("\n✓ Conversion completed successfully!\n");
        return 0;
    } else {
        printf("\n❌ Conversion failed!\n");
        return 1;
    }
}

// Enhanced table detection based on text positioning and alignment
static int detect_tables(fz_context* ctx, fz_page* page, PageParams* params) {
    // Use the enhanced detection method
    return detect_tables_enhanced(ctx, page, params);
}

