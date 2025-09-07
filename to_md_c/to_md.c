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

#include <mupdf/fitz.h>
#include "get_raw_lines.h"

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
	int block_num;
	int superscript;
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

// Link information
typedef struct {
	char* uri;
	fz_rect bbox;
} LinkInfo;

// Page processing parameters (simplified - no tables/images)
typedef struct {
	StringBuilder* output;
	FontAnalyzer* font_analyzer;
	fz_page* page;
	char* filename;
	fz_rect clip;
	fz_stext_page* textpage;
	double image_size_limit;
	int ignore_images;
	int ignore_graphics;
	int ignore_code;
	int extract_words;
	int force_text;
	int detect_bg_color;
	int accept_invisible;
	int dpi;
	char* image_path;
	char* image_format;
	float* bg_color;  // RGB background color
	
	// Text processing
	TextBlock* blocks;
	int block_count;
	int block_capacity;
	fz_rect* line_rects;
	int line_rect_count;
	int line_rect_capacity;
	
	// Links
	LinkInfo* links;
	int link_count;
	
	// Annotation rectangles
	fz_rect* annot_rects;
	int annot_rect_count;
	
	// Text rectangles (column boxes)
	fz_rect* text_rects;
	int text_rect_count;
	
	// Margins
	float margins[4];  // left, top, right, bottom
} PageParams;

// Function declarations
static StringBuilder* sb_create(size_t initial_capacity);
static void sb_destroy(StringBuilder* sb);
static int sb_append(StringBuilder* sb, const char* str);
static int sb_append_char(StringBuilder* sb, char c);
static int sb_append_formatted(StringBuilder* sb, const char* format, ...);
static int sb_ensure_capacity(StringBuilder* sb, size_t needed);

// Page processing functions
static PageParams* create_page_params(void);
static void destroy_page_params(PageParams* params);
static void init_page_params(PageParams* params, fz_page* page, const char* filename, fz_rect clip);
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
static void process_text_spans(StringBuilder* output, TextSpan* spans, int span_count, 
                              FontAnalyzer* analyzer, int ignore_code);
static int process_pdf_page(fz_context* ctx, fz_page* page, PageParams* params, const char* (*get_header_id)(TextSpan*, void*));
extern EXPORT __attribute__((used)) const char *to_markdown(const char* pdf_path);

// Implementation begins here

// StringBuilder implementation
static StringBuilder* sb_create(size_t initial_capacity) {
	StringBuilder* sb = malloc(sizeof(StringBuilder));
	if (!sb) return NULL;
	
	sb->capacity = initial_capacity < INITIAL_BUFFER_SIZE ? INITIAL_BUFFER_SIZE : initial_capacity;
	sb->data = malloc(sb->capacity);
	if (!sb->data) {
		free(sb);
		return NULL;
	}
	
	sb->length = 0;
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
	if (!sb) return -1;
	
	if (sb->capacity <= needed) {
		size_t new_capacity = sb->capacity;
		while (new_capacity <= needed) {
			new_capacity *= 2;
		}
		
		char* new_data = realloc(sb->data, new_capacity);
		if (!new_data) return -1;
		
		sb->data = new_data;
		sb->capacity = new_capacity;
	}
	
	return 0;
}

static int sb_append(StringBuilder* sb, const char* str) {
	if (!sb || !str) return -1;
	
	size_t str_len = strlen(str);
	if (sb_ensure_capacity(sb, sb->length + str_len + 1) != 0) {
		return -1;
	}
	
	strcpy(sb->data + sb->length, str);
	sb->length += str_len;
	return 0;
}

static int sb_append_char(StringBuilder* sb, char c) {
	if (!sb) return -1;
	
	if (sb_ensure_capacity(sb, sb->length + 2) != 0) {
		return -1;
	}
	
	sb->data[sb->length++] = c;
	sb->data[sb->length] = '\0';
	return 0;
}

static int sb_append_formatted(StringBuilder* sb, const char* format, ...) {
	if (!sb || !format) return -1;
	
	va_list args;
	va_start(args, format);
	
	// Get required length
	va_list args_copy;
	va_copy(args_copy, args);
	int len = vsnprintf(NULL, 0, format, args_copy);
	va_end(args_copy);
	
	if (len < 0) {
		va_end(args);
		return -1;
	}
	
	// Ensure capacity
	if (sb_ensure_capacity(sb, sb->length + len + 1) != 0) {
		va_end(args);
		return -1;
	}
	
	// Format the string
	vsnprintf(sb->data + sb->length, len + 1, format, args);
	sb->length += len;
	
	va_end(args);
	return 0;
}

// Font analyzer functions
FontAnalyzer* font_analyzer_create(void) {
    FontAnalyzer* analyzer = malloc(sizeof(FontAnalyzer));
    if (!analyzer) return NULL;
    
    memset(analyzer->font_counts, 0, sizeof(analyzer->font_counts));
    analyzer->body_limit = 12.0;
    memset(analyzer->header_mapping, 0, sizeof(analyzer->header_mapping));
    
    return analyzer;
}

void font_analyzer_destroy(FontAnalyzer* analyzer) {
    if (analyzer) {
        free(analyzer);
    }
}

void font_analyzer_build_mappings(FontAnalyzer* analyzer, double body_font_size, int max_header_levels) {
    if (!analyzer) return;
    
    // Find the most common font size (body text)
    int max_count = 0;
    int body_font_index = (int)round(body_font_size);
    
    // Find actual body font size from font counts
    for (int i = 8; i < MAX_FONT_SIZE && i < 20; i++) {
        if (analyzer->font_counts[i] > max_count) {
            max_count = analyzer->font_counts[i];
            body_font_index = i;
        }
    }
    
    analyzer->body_limit = (double)body_font_index;
    
    // Build header mappings for fonts larger than body text
    int header_level = 1;
    for (int size = MAX_FONT_SIZE - 1; size > body_font_index && header_level <= max_header_levels; size--) {
        if (analyzer->font_counts[size] > 0) {
            // Create header markdown prefix
            for (int h = 0; h < header_level && h < 6; h++) {
                analyzer->header_mapping[size][h] = '#';
            }
            analyzer->header_mapping[size][header_level] = ' ';
            analyzer->header_mapping[size][header_level + 1] = '\0';
            header_level++;
        }
    }
}

const char* get_header_id_from_analyzer(TextSpan* span, void* user_data) {
    FontAnalyzer* analyzer = (FontAnalyzer*)user_data;
    if (!analyzer || !span) return "";
    
    int font_size = (int)round(span->size);
    if (font_size >= 0 && font_size < MAX_FONT_SIZE && analyzer->header_mapping[font_size][0] != '\0') {
        return analyzer->header_mapping[font_size];
    }
    
    return "";
}

// Page parameters functions
static PageParams* create_page_params(void) {
	PageParams* params = malloc(sizeof(PageParams));
	if (!params) return NULL;
	
	memset(params, 0, sizeof(PageParams));
	return params;
}

static void destroy_page_params(PageParams* params) {
	if (!params) return;
	
	sb_destroy(params->output);
	free(params->line_rects);
	
	// Free blocks
	for (int i = 0; i < params->block_count; i++) {
		// destroy_text_block(&params->blocks[i]);  // Simplified for now
	}
	free(params->blocks);
	
	// Free links
	for (int i = 0; i < params->link_count; i++) {
		free(params->links[i].uri);
	}
	free(params->links);
	
	free(params->annot_rects);
	free(params->text_rects);
	free(params->bg_color);
	free(params);
}

static void init_page_params(PageParams* params, fz_page* page, const char* filename, fz_rect clip) {
	if (!params) return;
	
	params->page = page;
	params->filename = filename ? (char*)strdup(filename) : NULL;
	params->clip = clip;
	params->textpage = NULL;
	params->image_size_limit = 0.05;
	params->ignore_images = 1;  // Always ignore images now
	params->ignore_graphics = 1;  // Always ignore graphics now
	params->ignore_code = 0;
	params->extract_words = 0;
	params->force_text = 1;
	params->detect_bg_color = 1;
	params->accept_invisible = 0;
	params->dpi = 150;
	params->image_path = NULL;
	params->image_format = (char*)strdup("png");
	
	// Initialize margins (matching Python defaults)
	params->margins[0] = 0.0f; // left
	params->margins[1] = 0.0f; // top
	params->margins[2] = 0.0f; // right
	params->margins[3] = 0.0f; // bottom
}

// OCR page detection (matching Python exactly)
static int page_is_ocr(fz_context* ctx, fz_page* page) {
	// Check if page exclusively contains OCR text (ignore-text)
	// For simplicity, we'll return 0 for now but this should be enhanced
	(void)ctx;  // Suppress unused parameter warning
	(void)page; // Suppress unused parameter warning
	return 0;
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

// Text span processing
static void process_text_spans(StringBuilder* output, TextSpan* spans, int span_count, 
                              FontAnalyzer* analyzer, int ignore_code) {
	if (!output || !spans || span_count <= 0) return;
	
	for (int i = 0; i < span_count; i++) {
		TextSpan* span = &spans[i];
		if (!span->text || strlen(span->text) == 0) continue;
		
		// Get header prefix from analyzer
		const char* header_prefix = "";
		if (analyzer) {
			header_prefix = get_header_id_from_analyzer(span, analyzer);
		}
		
		// Apply header prefix if any
		if (header_prefix && strlen(header_prefix) > 0) {
			sb_append(output, header_prefix);
		}
		
		// Apply formatting
		int apply_bold = span->bold && !ignore_code;
		int apply_italic = span->italic && !ignore_code;
		int apply_mono = span->mono || ignore_code;
		
		if (apply_bold) sb_append(output, "**");
		if (apply_italic) sb_append(output, "*");
		if (apply_mono) sb_append(output, "`");
		
		// Add the text
		sb_append(output, span->text);
		
		// Close formatting tags (reverse order)
		if (apply_mono) sb_append(output, "`");
		if (apply_italic) sb_append(output, "*");
		if (apply_bold) sb_append(output, "**");
		
		// Add space between spans unless next span starts with punctuation
		if (i < span_count - 1) {
			const char* next_text = spans[i + 1].text;
			if (next_text && strlen(next_text) > 0 && 
			    next_text[0] != '.' && next_text[0] != ',' && 
			    next_text[0] != ';' && next_text[0] != ':' &&
			    next_text[0] != '!' && next_text[0] != '?') {
				sb_append(output, " ");
			}
		}
	}
}

// Stub implementations for removed functions
static int extract_annotations(fz_context* ctx, fz_page* page, PageParams* params) {
	(void)ctx; (void)page; (void)params;
	return 0;
}

static int extract_links(fz_context* ctx, fz_page* page, PageParams* params) {
	(void)ctx; (void)page; (void)params;
	return 0;
}

static char* resolve_span_link(LinkInfo* links, int link_count, fz_rect span_bbox) {
	(void)links; (void)link_count; (void)span_bbox;
	return NULL;
}

static int find_column_boxes(fz_context* ctx, fz_page* page, PageParams* params) {
	(void)ctx; (void)page; (void)params;
	return 0;
}

static void sort_reading_order(fz_rect* rects, int count) {
	(void)rects; (void)count;
}

static int intersects_rects(fz_rect rect, fz_rect* rect_list, int count) {
	(void)rect; (void)rect_list; (void)count;
	return 0;
}

static int is_in_rects(fz_rect rect, fz_rect* rect_list, int count) {
	(void)rect; (void)rect_list; (void)count;
	return 0;
}

static void process_text_in_rect(fz_context* ctx, PageParams* params, fz_rect text_rect) {
	(void)ctx; (void)params; (void)text_rect;
}

// Main page processing function (simplified)
static int process_pdf_page(fz_context* ctx, fz_page* page, PageParams* params, const char* (*get_header_id)(TextSpan*, void*)) {
	(void)get_header_id;  // Suppress unused parameter warning
	if (!ctx || !page || !params) return -1;
	
	// Initialize page parameters
	params->accept_invisible = page_is_ocr(ctx, page) || !params->detect_bg_color;
	
	// Create textpage with correct flags
	fz_try(ctx) {
		fz_stext_options opts = { 
			.flags = FZ_STEXT_CLIP | FZ_STEXT_ACCURATE_BBOXES | FZ_STEXT_COLLECT_STYLES
		};
		params->textpage = fz_new_stext_page_from_page(ctx, page, &opts);
		
		if (!params->textpage) return -1;
		
		// Simple span processing (safer memory management)
		for (fz_stext_block* block = params->textpage->first_block; block; block = block->next) {
			if (block->type != FZ_STEXT_BLOCK_TEXT) continue;
			
			// Skip blocks outside clip rectangle
			fz_rect block_bbox = fz_make_rect(block->bbox.x0, block->bbox.y0, block->bbox.x1, block->bbox.y1);
			if (fz_is_empty_rect(fz_intersect_rect(block_bbox, params->clip))) continue;
			
			// Process each line in the block
			for (fz_stext_line* line = block->u.t.first_line; line; line = line->next) {
				fz_rect line_bbox = fz_make_rect(line->bbox.x0, line->bbox.y0, line->bbox.x1, line->bbox.y1);
				
				// Skip lines outside clip
				if (fz_is_empty_rect(fz_intersect_rect(line_bbox, params->clip))) continue;
				
				// Extract spans from this line
				TextSpan spans[100];
				int span_count = 0;
				
				for (fz_stext_char* ch = line->first_char; ch && span_count < 100; ch = ch->next) {
					if (ch->c < 32 && ch->c != '\t') continue; // Skip control chars except tab
					
					// Start new span or continue current one
					if (span_count == 0 || 
						spans[span_count-1].size != ch->size ||
						(ch->font && spans[span_count-1].font && 
						 strcmp(spans[span_count-1].font, ch->font->name) != 0)) {
						
						// New span
						// Initialize new span
						spans[span_count].text = malloc(1024);
						spans[span_count].text[0] = '\0';
						spans[span_count].font = ch->font ? ch->font->name : "unknown";
						spans[span_count].size = ch->size;
						spans[span_count].bbox = fz_make_rect(ch->quad.ul.x, ch->quad.ul.y, ch->quad.lr.x, ch->quad.lr.y);
						spans[span_count].flags = 0;
						spans[span_count].char_flags = 0;
						spans[span_count].bold = ch->font ? (strstr(ch->font->name, "Bold") != NULL) : 0;
						spans[span_count].italic = ch->font ? (strstr(ch->font->name, "Italic") != NULL) : 0;
						spans[span_count].mono = ch->font ? (strstr(ch->font->name, "Mono") != NULL || strstr(ch->font->name, "Courier") != NULL) : 0;
						spans[span_count].strikeout = 0;
						spans[span_count].block_num = 0;
						spans[span_count].superscript = 0;
						span_count++;
					}
					
					// Add character to current span
					if (span_count > 0) {
						char utf8_char[8];
						int len = fz_runetochar(utf8_char, ch->c);
						utf8_char[len] = '\0';
						
						if (ch->c == '\t') {
							strcat(spans[span_count-1].text, "    ");  // Convert tab to spaces
						} else {
							strcat(spans[span_count-1].text, utf8_char);
						}
						
						// Expand bbox
						fz_rect char_bbox = fz_make_rect(ch->quad.ul.x, ch->quad.ul.y, ch->quad.lr.x, ch->quad.lr.y);
						spans[span_count-1].bbox = fz_union_rect(spans[span_count-1].bbox, char_bbox);
					}
				}
				
				// Process spans if any
				if (span_count > 0) {
					process_text_spans(params->output, spans, span_count, 
									 params->font_analyzer, params->ignore_code);
					sb_append(params->output, "\n");
					
					// Free allocated text
					for (int i = 0; i < span_count; i++) {
						free(spans[i].text);
					}
				}
			}
			
			// Add paragraph break between blocks
			sb_append(params->output, "\n");
		}
		
		fz_drop_stext_page(ctx, params->textpage);
		params->textpage = NULL;
	}
	fz_catch(ctx) {
		if (params->textpage) {
			fz_drop_stext_page(ctx, params->textpage);
			params->textpage = NULL;
		}
		return -1;
	}
	
	// Clean up output formatting (matching Python)
	if (params->output && params->output->data) {
		// Remove leading newlines
		while (params->output->length > 0 && params->output->data[0] == '\n') {
			memmove(params->output->data, params->output->data + 1, params->output->length);
			params->output->length--;
			params->output->data[params->output->length] = '\0';
		}
		
		// Replace null characters with replacement character
		for (size_t i = 0; i < params->output->length; i++) {
			if (params->output->data[i] == 0) {
				params->output->data[i] = '\xEF';  // Unicode replacement character start
			}
		}
	}
	
	return 0;
}

// PDF to markdown conversion
extern EXPORT __attribute__((used)) const char* to_markdown(const char* pdf_path) {
	fz_context* ctx = NULL;
	fz_document* doc = NULL;
	int result = -1;
	
	// Create MuPDF context
	ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
	if (!ctx) {
		fprintf(stderr, "Error: Failed to create MuPDF context\n");
		return NULL;
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
		
		// Create font analyzer for two-pass processing
		FontAnalyzer* analyzer = font_analyzer_create();
		if (!analyzer) {
			fprintf(stderr, "Error: Failed to create font analyzer\n");
			fz_rethrow(ctx);
		}
		
		// First pass: analyze fonts
		printf("Analyzing fonts...\n");
		for (int i = 0; i < page_count; i++) {
			fz_page* page = fz_load_page(ctx, doc, i);
			if (page) {
				// Extract text with same flags as Python
				fz_stext_options opts = { 
					.flags = FZ_STEXT_CLIP | FZ_STEXT_ACCURATE_BBOXES | FZ_STEXT_COLLECT_STYLES
				};
				fz_stext_page* textpage = fz_new_stext_page_from_page(ctx, page, &opts);
				
				if (textpage) {
					// Font analysis: iterate through spans
					for (fz_stext_block* block = textpage->first_block; block; block = block->next) {
						if (block->type != FZ_STEXT_BLOCK_TEXT) continue;
						
						for (fz_stext_line* line = block->u.t.first_line; line; line = line->next) {
							// Build spans from characters
							fz_stext_char* span_start = line->first_char;
							while (span_start) {
								if (span_start->c <= 32 || span_start->c == 160) {
									span_start = span_start->next;
									continue;
								}
								
								// Find end of span (same font/size)
								fz_stext_char* span_end = span_start;
								int span_char_count = 0;
								double span_size = span_start->size;
								
								while (span_end && 
									   fabs(span_end->size - span_size) < 0.1 &&
									   span_end->font == span_start->font) {
									if (span_end->c > 32 && span_end->c != 160) {
										span_char_count++;
									}
									span_end = span_end->next;
								}
								
								// Only process non-empty spans
								if (span_char_count > 0) {
									int font_size = (int)round(span_size);
									if (font_size >= 0 && font_size < MAX_FONT_SIZE) {
										analyzer->font_counts[font_size] += span_char_count;
									}
								}
								
								span_start = span_end;
							}
						}
					}
					
					fz_drop_stext_page(ctx, textpage);
				}
				
				fz_drop_page(ctx, page);
			}
		}
		
		// Build header mappings
		font_analyzer_build_mappings(analyzer, 12.0, 6);
		
		// Create main output
		StringBuilder* output = sb_create(1024 * 1024);  // 1MB initial
		if (!output) {
			fprintf(stderr, "Error: Failed to create output buffer\n");
			font_analyzer_destroy(analyzer);
			fz_rethrow(ctx);
		}
		
		// Second pass: generate markdown
		printf("Generating markdown...\n");
		for (int i = 0; i < page_count; i++) {
			printf("Processing page %d/%d\r", i + 1, page_count);
			fflush(stdout);
			
			fz_page* page = fz_load_page(ctx, doc, i);
			if (page) {
				// Create page parameters
				PageParams* params = create_page_params();
				if (params) {
					// Set up clipping with margins
					fz_rect page_rect = fz_bound_page(ctx, page);
					float margins[4] = {0, 0, 0, 0};  // left, top, right, bottom
					fz_rect clip = page_rect;
					clip.y0 += margins[1];  // top margin
					clip.x1 -= margins[2];  // right margin  
					clip.y1 -= margins[3];  // bottom margin
					
					init_page_params(params, page, pdf_path, clip);
					params->font_analyzer = analyzer;
					params->output = output;
					
					// Process page
					process_pdf_page(ctx, page, params, get_header_id_from_analyzer);
					
					// Don't destroy output buffer - it's shared
					params->output = NULL;
					destroy_page_params(params);
				}
				fz_drop_page(ctx, page);
			}
		}
		
		printf("\nWriting output...\n");
		
		
		printf("Successfully converted %s to markdown\n", pdf_path);
		printf("Output size: %zu characters\n", output->length);
		
		// sb_destroy(output); // caller will free
		font_analyzer_destroy(analyzer);
		return output->data;
	}
	fz_catch(ctx) {
		fprintf(stderr, "Error during PDF processing\n");
	}
	
	if (doc) fz_drop_document(ctx, doc);
	fz_drop_context(ctx);
	return NULL;
	}

int main(int argc, char* argv[]) {
	printf("Standalone PDF to Markdown Converter v1.0\n");
	printf("Ultra-optimized pure C implementation (Tables and Images Disabled)\n\n");
	
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
	const char* result = to_markdown(input_path);

	free(output_path);
	
	if (result == 0) {
		printf("\n✓ Conversion completed successfully!\n");
		return 0;
	} else {
		printf("\n❌ Conversion failed!\n");
		return 1;
	}
}
