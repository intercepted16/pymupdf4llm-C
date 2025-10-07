/*
 * MuPDF JSON Extraction Layer
 *
 * Implements a per-page extractor that emits structured JSON describing
 * textual and non-textual blocks (paragraphs, headings, tables, lists,
 * figures) together with geometry, font metrics and lightweight layout
 * heuristics. Designed for fast downstream consumption by higher level
 * RAG/semantic pipelines.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <mupdf/fitz.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#include <direct.h>
#define strncasecmp _strnicmp
#define strcasecmp _stricmp
#endif

// -----------------------------------------------------------------------------
// Compatibility helpers for older MuPDF headers where style macros may be
// absent. These mirror the guards that existed in the markdown implementation.
// -----------------------------------------------------------------------------
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

#ifndef FZ_FONT_FLAG_BOLD
#define FZ_FONT_FLAG_BOLD 1
#endif
#ifndef FZ_FONT_FLAG_ITALIC
#define FZ_FONT_FLAG_ITALIC 2
#endif

#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif

// -----------------------------------------------------------------------------
// Generic dynamic buffer utilities (append-only string builders).
// -----------------------------------------------------------------------------
typedef struct
{
    char* data;
    size_t length;
    size_t capacity;
} Buffer;

static Buffer* buffer_create(size_t initial)
{
    Buffer* buf = (Buffer*)calloc(1, sizeof(Buffer));
    if (!buf) return NULL;
    buf->capacity = initial > 0 ? initial : 256;
    buf->data = (char*)malloc(buf->capacity);
    if (!buf->data)
    {
        free(buf);
        return NULL;
    }
    buf->data[0] = '\0';
    return buf;
}

static void buffer_destroy(Buffer* buf)
{
    if (!buf) return;
    free(buf->data);
    free(buf);
}

static int buffer_reserve(Buffer* buf, size_t needed)
{
    if (!buf) return -1;
    if (needed <= buf->capacity) return 0;

    size_t new_capacity = buf->capacity;
    while (new_capacity < needed)
    {
        if (new_capacity > (SIZE_MAX / 2))
        {
            new_capacity = needed;
            break;
        }
        new_capacity *= 2;
    }

    char* tmp = (char*)realloc(buf->data, new_capacity);
    if (!tmp) return -1;
    buf->data = tmp;
    buf->capacity = new_capacity;
    return 0;
}

static int buffer_append(Buffer* buf, const char* text)
{
    if (!buf || !text) return -1;
    size_t add = strlen(text);
    size_t needed = buf->length + add + 1;
    if (buffer_reserve(buf, needed) != 0) return -1;
    memcpy(buf->data + buf->length, text, add + 1);
    buf->length += add;
    return 0;
}

static int buffer_append_char(Buffer* buf, char c)
{
    if (!buf) return -1;
    size_t needed = buf->length + 2;
    if (buffer_reserve(buf, needed) != 0) return -1;
    buf->data[buf->length++] = c;
    buf->data[buf->length] = '\0';
    return 0;
}

static int buffer_append_format(Buffer* buf, const char* fmt, ...)
{
    if (!buf || !fmt) return -1;
    va_list args;
    va_start(args, fmt);
    va_list copy;
    va_copy(copy, args);
    int len = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (len < 0)
    {
        va_end(args);
        return -1;
    }
    size_t needed = buf->length + (size_t)len + 1;
    if (buffer_reserve(buf, needed) != 0)
    {
        va_end(args);
        return -1;
    }
    vsnprintf(buf->data + buf->length, (size_t)len + 1, fmt, args);
    va_end(args);
    buf->length += (size_t)len;
    return 0;
}

static int buffer_append_n(Buffer* buf, const char* data, size_t len)
{
    if (!buf || (!data && len > 0)) return -1;

    if (buffer_reserve(buf, buf->length + len + 1) != 0)
    {
        return -1;
    }

    if (len > 0)
    {
        memcpy(buf->data + buf->length, data, len);
        buf->length += len;
    }

    buf->data[buf->length] = '\0';
    return 0;
}

// -----------------------------------------------------------------------------
// Font statistics to compute median/mode font sizes per page.
// -----------------------------------------------------------------------------
#define FONT_BIN_COUNT 512

typedef struct
{
    int counts[FONT_BIN_COUNT];
    double total_size;
    int total_chars;
} FontStats;

static void font_stats_reset(FontStats* stats)
{
    memset(stats, 0, sizeof(FontStats));
}

static void font_stats_add(FontStats* stats, float size)
{
    if (!stats) return;
    if (size <= 0.0f) return;
    int idx = (int)lroundf(size);
    if (idx < 0) idx = 0;
    if (idx >= FONT_BIN_COUNT) idx = FONT_BIN_COUNT - 1;
    stats->counts[idx] += 1;
    stats->total_size += size;
    stats->total_chars += 1;
}

static float font_stats_mode(const FontStats* stats)
{
    if (!stats || stats->total_chars == 0) return 12.0f;
    int best_idx = 0;
    int best_count = 0;
    for (int i = 0; i < FONT_BIN_COUNT; ++i)
    {
        if (stats->counts[i] > best_count)
        {
            best_count = stats->counts[i];
            best_idx = i;
        }
    }
    if (best_idx == 0 && best_count == 0)
    {
        return (float)(stats->total_size / (stats->total_chars ? stats->total_chars : 1));
    }
    return (float)best_idx;
}

static float font_stats_median(const FontStats* stats)
{
    if (!stats || stats->total_chars == 0) return 12.0f;
    int midpoint = stats->total_chars / 2;
    int cumulative = 0;
    for (int i = 0; i < FONT_BIN_COUNT; ++i)
    {
        cumulative += stats->counts[i];
        if (cumulative > midpoint) return (float)i;
    }
    return (float)(stats->total_size / (stats->total_chars ? stats->total_chars : 1));
}

// -----------------------------------------------------------------------------
// Block metadata structures used during extraction.
// -----------------------------------------------------------------------------
typedef enum
{
    BLOCK_PARAGRAPH,
    BLOCK_HEADING,
    BLOCK_TABLE,
    BLOCK_LIST,
    BLOCK_FIGURE,
    BLOCK_OTHER
} BlockType;

static const char* block_type_to_string(BlockType t)
{
    switch (t)
    {
        case BLOCK_PARAGRAPH: return "paragraph";
        case BLOCK_HEADING: return "heading";
        case BLOCK_TABLE: return "table";
        case BLOCK_LIST: return "list";
        case BLOCK_FIGURE: return "figure";
        default: return "other";
    }
}

typedef struct
{
    char* text;               // UTF-8 normalized text (may be empty for non-text blocks)
    size_t text_chars;        // Unicode scalar count (not bytes)
    fz_rect bbox;             // Original MuPDF bounding box
    BlockType type;           // Final classification
    float avg_font_size;      // Average character size in pts
    float bold_ratio;         // Ratio of bold characters
    int line_count;           // Number of lines discovered (for paragraphs/lists)
    float line_spacing_avg;   // Average distance between consecutive baselines
    int column_count;         // For table heuristics
    float column_consistency; // Table alignment score [0,1]
    int row_count;            // For tables
    int cell_count;           // For tables
    float confidence;         // Confidence for tables/headings (currently used for tables)
    int page_number;          // 0-based page index
} BlockInfo;

typedef struct
{
    BlockInfo* items;
    size_t count;
    size_t capacity;
} BlockArray;

static void block_array_init(BlockArray* arr)
{
    arr->items = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

static void block_array_free(BlockArray* arr)
{
    if (!arr) return;
    for (size_t i = 0; i < arr->count; ++i)
    {
        free(arr->items[i].text);
    }
    free(arr->items);
    arr->items = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

static BlockInfo* block_array_push(BlockArray* arr)
{
    if (!arr) return NULL;
    if (arr->count == arr->capacity)
    {
        size_t new_cap = arr->capacity == 0 ? 32 : arr->capacity * 2;
        BlockInfo* tmp = (BlockInfo*)realloc(arr->items, new_cap * sizeof(BlockInfo));
        if (!tmp) return NULL;
        arr->items = tmp;
        arr->capacity = new_cap;
    }
    BlockInfo* info = &arr->items[arr->count++];
    memset(info, 0, sizeof(BlockInfo));
    return info;
}

// -----------------------------------------------------------------------------
// Text normalization helpers.
// -----------------------------------------------------------------------------
static int compare_block_position(const void* a, const void* b)
{
    const BlockInfo* ia = (const BlockInfo*)a;
    const BlockInfo* ib = (const BlockInfo*)b;

    float dy = ia->bbox.y0 - ib->bbox.y0;
    if (fabsf(dy) > 1e-3f)
    {
        return (dy < 0.0f) ? -1 : 1;
    }

    float dx = ia->bbox.x0 - ib->bbox.x0;
    if (fabsf(dx) > 1e-3f)
    {
        return (dx < 0.0f) ? -1 : 1;
    }

    return 0;
}

static char* normalize_text(const char* input)
{
    if (!input) return NULL;
    size_t len = strlen(input);
    char* out = (char*)malloc(len + 1);
    if (!out) return NULL;

    size_t write = 0;
    bool last_space = true;
    bool last_was_newline = false;

    for (size_t i = 0; i < len; ++i)
    {
        unsigned char c = (unsigned char)input[i];
        if (c == '\r') continue;

        if (c == '\n')
        {
            if (write > 0 && out[write - 1] == ' ')
            {
                write -= 1;
            }
            if (!last_was_newline)
            {
                out[write++] = '\n';
            }
            last_space = true;
            last_was_newline = true;
            continue;
        }

        last_was_newline = false;

        if (c == '\t' || c == '\f' || c == '\v')
        {
            c = ' ';
        }

        if (isspace(c))
        {
            if (!last_space && write > 0)
            {
                out[write++] = ' ';
                last_space = true;
            }
            continue;
        }

        out[write++] = (char)c;
        last_space = false;
    }

    // Trim trailing whitespace/newlines
    while (write > 0 && (out[write - 1] == ' ' || out[write - 1] == '\n'))
    {
        write--;
    }

    out[write] = '\0';
    return out;
}

static size_t list_bullet_prefix_len(const char* line, size_t line_len)
{
    if (!line || line_len == 0) return 0;

    size_t idx = 0;
    while (idx < line_len && (line[idx] == ' ' || line[idx] == '\t'))
        idx++;

    static const char* bullets[] = {"-", "•", "o", "*", "·", "�", "‣", "●", "–", NULL};

    for (int i = 0; bullets[i]; ++i)
    {
        size_t blen = strlen(bullets[i]);
        if (blen == 0 || blen > line_len - idx) continue;
        if (strncmp(line + idx, bullets[i], blen) == 0)
        {
            size_t pos = idx + blen;
            while (pos < line_len && (line[pos] == ' ' || line[pos] == '\t')) pos++;
            return pos;
        }
    }

    if (idx < line_len && isdigit((unsigned char)line[idx]))
    {
        size_t pos = idx;
        while (pos < line_len && isdigit((unsigned char)line[pos])) pos++;
        if (pos < line_len && (line[pos] == '.' || line[pos] == ')' || line[pos] == '-'))
        {
            pos++;
            while (pos < line_len && (line[pos] == ' ' || line[pos] == '\t')) pos++;
            return pos;
        }
    }
    else if (idx + 1 < line_len && isalpha((unsigned char)line[idx]) &&
             (line[idx + 1] == '.' || line[idx + 1] == ')'))
    {
        size_t pos = idx + 2;
        while (pos < line_len && (line[pos] == ' ' || line[pos] == '\t')) pos++;
        return pos;
    }

    return 0;
}

static char* normalize_bullets(const char* text)
{
    if (!text) return NULL;

    size_t text_len = strlen(text);
    Buffer* out = buffer_create(text_len + 16);
    if (!out) return NULL;

    const char* cursor = text;
    bool input_had_trailing_newline = text_len > 0 && text[text_len - 1] == '\n';
    bool changed = false;

    while (*cursor)
    {
        const char* line_end = strchr(cursor, '\n');
        size_t line_len = line_end ? (size_t)(line_end - cursor) : strlen(cursor);

        size_t skip = list_bullet_prefix_len(cursor, line_len);
        if (skip > 0)
        {
            buffer_append(out, "- ");
            buffer_append_n(out, cursor + skip, line_len - skip);
            changed = true;
        }
        else
        {
            buffer_append_n(out, cursor, line_len);
        }

        if (line_end)
        {
            buffer_append_char(out, '\n');
            cursor = line_end + 1;
        }
        else
        {
            cursor += line_len;
        }
    }

    if (!input_had_trailing_newline && out->length > 0 && out->data[out->length - 1] == '\n')
    {
        out->length -= 1;
        out->data[out->length] = '\0';
    }

    char* result = changed ? strdup(out->data) : strdup(text);
    buffer_destroy(out);
    return result;
}

static bool ends_with_punctuation(const char* text)
{
    if (!text) return false;
    size_t len = strlen(text);
    while (len > 0 && isspace((unsigned char)text[len - 1]))
    {
        len--;
    }
    if (len == 0) return false;
    char c = text[len - 1];
    return c == '.' || c == ':' || c == ';' || c == '?' || c == '!';
}

static bool is_all_caps(const char* text)
{
    if (!text) return false;
    bool has_alpha = false;
    for (const unsigned char* p = (const unsigned char*)text; *p; ++p)
    {
        if (isalpha(*p))
        {
            has_alpha = true;
            if (!isupper(*p)) return false;
        }
    }
    return has_alpha;
}

static bool starts_with_heading_keyword(const char* text)
{
    static const char* keywords[] = {
        "appendix",
        "chapter",
        "section",
        "heading",
        "article",
        "part",
        NULL
    };

    while (*text == ' ') text++;

    for (const char** keyword = keywords; *keyword; ++keyword)
    {
        size_t len = strlen(*keyword);
        if (strncasecmp(text, *keyword, len) == 0)
        {
            if (text[len] == '\0' || isspace((unsigned char)text[len]) || text[len] == ':' || text[len] == '-')
            {
                return true;
            }
        }
    }

    return false;
}

static bool starts_with_numeric_heading(const char* text)
{
    while (*text == ' ') text++;

    const char* p = text;
    bool seen_digit = false;
    bool seen_separator = false;

    while (*p)
    {
        if (isdigit((unsigned char)*p))
        {
            seen_digit = true;
            p++;
            continue;
        }
        if (*p == '.' || *p == ')' || *p == ':' || *p == '-')
        {
            seen_separator = true;
            p++;
            continue;
        }
        break;
    }

    if (!seen_digit) return false;
    if (!seen_separator) return false;

    if (*p == '\0') return false;
    if (isspace((unsigned char)*p)) return true;
    if (*p == '-' || *p == ')') return true;

    return false;
}

static bool starts_with_bullet(const char* text)
{
    if (!text) return false;
    while (*text == ' ') text++;
    if (*text == '-' && text[1] == ' ') return true;
    if (*text == '*' && text[1] == ' ') return true;
    if ((unsigned char)text[0] == 0xE2 && (unsigned char)text[1] == 0x80 &&
        (unsigned char)text[2] == 0xA2 && text[3] == ' ')
    {
        return true; // Bullet character \u2022 followed by space
    }
    if (isdigit((unsigned char)*text))
    {
        const char* p = text;
        while (isdigit((unsigned char)*p)) p++;
        if ((*p == '.' || *p == ')') && p[1] == ' ') return true;
    }
    return false;
}

static const char* font_weight_from_ratio(float ratio)
{
    return (ratio >= 0.6f) ? "bold" : "normal";
}

static size_t count_unicode_chars(const char* text)
{
    if (!text) return 0;
    size_t count = 0;
    const unsigned char* p = (const unsigned char*)text;
    while (*p)
    {
        if ((*p & 0xC0) != 0x80)
        {
            count++;
        }
        p++;
    }
    return count;
}

// -----------------------------------------------------------------------------
// Column detection heuristics used for table classification.
// -----------------------------------------------------------------------------
#define MAX_COLUMNS 32

static int find_or_add_column(float* columns, int* column_count, float x, float tolerance)
{
    for (int i = 0; i < *column_count; ++i)
    {
        if (fabsf(columns[i] - x) <= tolerance)
        {
            return i;
        }
    }
    if (*column_count >= MAX_COLUMNS) return -1;
    columns[*column_count] = x;
    *column_count += 1;
    return *column_count - 1;
}

// -----------------------------------------------------------------------------
// Block extraction from a MuPDF structured text page.
// -----------------------------------------------------------------------------

typedef struct
{
    float body_font_size;
    float median_font_size;
} PageMetrics;

static PageMetrics compute_page_metrics(const FontStats* stats)
{
    PageMetrics metrics;
    metrics.body_font_size = font_stats_mode(stats);
    metrics.median_font_size = font_stats_median(stats);
    if (metrics.body_font_size <= 0.0f) metrics.body_font_size = 12.0f;
    if (metrics.median_font_size <= 0.0f) metrics.median_font_size = metrics.body_font_size;
    return metrics;
}

static void classify_block(BlockInfo* info, const PageMetrics* metrics, const char* normalized_text)
{
    if (!info || !metrics) return;

    const float heading_threshold = metrics->median_font_size * 1.25f;
    const size_t text_length = info->text_chars;

    bool heading_candidate = false;
    bool font_based_candidate = false;

    if (info->avg_font_size >= heading_threshold && text_length > 0 && text_length <= 160)
    {
        font_based_candidate = true;
        heading_candidate = true;
    }

    if (starts_with_numeric_heading(normalized_text) || starts_with_heading_keyword(normalized_text))
    {
        heading_candidate = true;
    }

    if (is_all_caps(normalized_text) && text_length > 0 && text_length <= 200)
    {
        heading_candidate = true;
    }

    if (font_based_candidate && info->bold_ratio >= 0.35f)
    {
        heading_candidate = true;
    }

    if (heading_candidate && ends_with_punctuation(normalized_text))
    {
        /* Many headings end with a colon or period in technical docs – do not disqualify entirely, just reduce confidence */
        if (!font_based_candidate && !starts_with_numeric_heading(normalized_text) && !starts_with_heading_keyword(normalized_text))
        {
            heading_candidate = false;
        }
    }

    if (heading_candidate)
    {
        info->type = BLOCK_HEADING;
        return;
    }

    if (starts_with_bullet(normalized_text))
    {
        info->type = BLOCK_LIST;
        return;
    }

    if (info->column_count >= 2 && info->row_count >= 2 && info->confidence >= 0.30f)
    {
        info->type = BLOCK_TABLE;
        return;
    }

    if (text_length == 0)
    {
        info->type = BLOCK_OTHER;
        return;
    }

    // Default classification: paragraph if spacing roughly matches font size, else other.
    if (info->line_count <= 1)
    {
        info->type = BLOCK_PARAGRAPH;
        return;
    }

    float spacing = info->line_spacing_avg;
    float font = info->avg_font_size;
    if (font <= 0.0f) font = metrics->body_font_size;

    if (spacing > 0.0f && fabsf(spacing - font) <= font * 0.6f)
    {
        info->type = BLOCK_PARAGRAPH;
    }
    else
    {
        info->type = BLOCK_PARAGRAPH;
    }
}

static void add_figure_block(BlockArray* blocks, fz_rect bbox, int page_number)
{
    BlockInfo* info = block_array_push(blocks);
    if (!info) return;
    info->text = strdup("");
    info->text_chars = 0;
    info->bbox = bbox;
    info->type = BLOCK_FIGURE;
    info->avg_font_size = 0.0f;
    info->bold_ratio = 0.0f;
    info->line_count = 0;
    info->line_spacing_avg = 0.0f;
    info->column_count = 0;
    info->column_consistency = 0.0f;
    info->row_count = 0;
    info->cell_count = 0;
    info->confidence = 0.0f;
    info->page_number = page_number;
}

static void populate_table_metrics(BlockInfo* info, int row_count, int column_count, float consistency)
{
    if (!info) return;
    info->row_count = row_count;
    info->column_count = column_count;
    info->cell_count = row_count * column_count;
    info->column_consistency = consistency;
    float base_score = consistency;
    if (column_count >= 4) base_score += 0.15f;
    if (row_count >= 6) base_score += 0.15f;
    if (base_score > 1.0f) base_score = 1.0f;
    info->confidence = base_score;
}

static void process_text_block(fz_context* ctx,
                               fz_stext_block* block,
                               const PageMetrics* metrics,
                               BlockArray* blocks,
                               int page_number)
{
    if (!block || !metrics || !blocks) return;

    Buffer* text_buf = buffer_create(256);
    if (!text_buf) return;

    int total_chars = 0;
    int bold_chars = 0;
    float font_size_sum = 0.0f;
    int line_count = 0;
    float line_spacing_sum = 0.0f;
    int line_spacing_samples = 0;

    float columns[MAX_COLUMNS];
    int column_line_counts[MAX_COLUMNS];
    memset(columns, 0, sizeof(columns));
    memset(column_line_counts, 0, sizeof(column_line_counts));
    int column_count = 0;
    int lines_with_multiple_columns = 0;
    int rows_with_content = 0;

    float prev_line_y0 = NAN;

    for (fz_stext_line* line = block->u.t.first_line; line; line = line->next)
    {
        if (line_count > 0)
        {
            buffer_append_char(text_buf, '\n');
            if (!isnan(prev_line_y0))
            {
                float delta = fabsf(line->bbox.y0 - prev_line_y0);
                if (delta > 0.01f)
                {
                    line_spacing_sum += delta;
                    line_spacing_samples += 1;
                }
            }
        }
        prev_line_y0 = line->bbox.y0;
        line_count++;

        float prev_x1 = NAN;
        bool line_used_columns[MAX_COLUMNS];
        memset(line_used_columns, 0, sizeof(line_used_columns));

        for (fz_stext_char* ch = line->first_char; ch; ch = ch->next)
        {
            if (ch->c == 0) continue;

            // Convert rune to UTF-8.
            char utf8[8];
            int byte_count = fz_runetochar(utf8, ch->c);
            if (byte_count <= 0) continue;

            buffer_append_format(text_buf, "%.*s", byte_count, utf8);
            total_chars += 1;

            font_size_sum += ch->size;
            if (ch->font && fz_font_is_bold(ctx, ch->font))
            {
                bold_chars += 1;
            }

            fz_rect char_box = fz_rect_from_quad(ch->quad);
            float x0 = char_box.x0;
            float x1 = char_box.x1;
            float gap = (!isnan(prev_x1)) ? fabsf(x0 - prev_x1) : 0.0f;
            bool is_whitespace_char = (ch->c == ' ' || ch->c == '\t' || ch->c == '\r' || ch->c == '\n' || ch->c == 160);

            float tolerance = ch->size * 0.5f;
            if (tolerance < 3.0f) tolerance = 3.0f;

            bool start_new_cell = false;
            if (isnan(prev_x1) || gap > tolerance)
            {
                start_new_cell = true;
            }

            prev_x1 = x1;

            if (start_new_cell && !is_whitespace_char)
            {
                int idx = find_or_add_column(columns, &column_count, x0, tolerance);
                if (idx >= 0)
                {
                    line_used_columns[idx] = true;
                }
            }
        }

        int line_column_total = 0;
        for (int c = 0; c < column_count; ++c)
        {
            if (line_used_columns[c])
            {
                column_line_counts[c] += 1;
                line_column_total += 1;
            }
        }

        if (line_column_total > 0)
        {
            rows_with_content += 1;
        }
        if (line_column_total >= 2)
        {
            lines_with_multiple_columns += 1;
        }
    }

    BlockInfo* info = block_array_push(blocks);
    if (!info)
    {
        buffer_destroy(text_buf);
        return;
    }

    char* normalized = normalize_text(text_buf->data);
    buffer_destroy(text_buf);

    if (!normalized)
    {
        // Fallback to empty string on allocation failure.
        normalized = strdup("");
    }

    char* normalized_bullets = normalize_bullets(normalized);
    if (normalized_bullets)
    {
        free(normalized);
        normalized = normalized_bullets;
    }

    info->text = normalized ? normalized : strdup("");
    info->text_chars = count_unicode_chars(info->text);
    info->bbox = block->bbox;
    info->avg_font_size = (total_chars > 0) ? (font_size_sum / (float)total_chars) : 0.0f;
    info->bold_ratio = (total_chars > 0) ? ((float)bold_chars / (float)total_chars) : 0.0f;
    info->line_count = line_count;
    info->line_spacing_avg = (line_spacing_samples > 0) ? (line_spacing_sum / (float)line_spacing_samples) : 0.0f;
    info->column_count = column_count;

    int effective_rows = rows_with_content > 0 ? rows_with_content : line_count;

    if (column_count >= 2 && rows_with_content >= 2)
    {
        float consistency = 0.0f;
        for (int c = 0; c < column_count; ++c)
        {
            consistency += (float)column_line_counts[c] / (float)(rows_with_content ? rows_with_content : 1);
        }
        consistency = consistency / (float)(column_count ? column_count : 1);
        if (consistency > 1.0f) consistency = 1.0f;
        populate_table_metrics(info, rows_with_content, column_count, consistency);
        if (rows_with_content > 0 && lines_with_multiple_columns < rows_with_content / 2)
        {
            info->confidence *= 0.75f;
        }
    }
    else
    {
        info->row_count = effective_rows;
        info->cell_count = 0;
        info->confidence = 0.0f;
    }

    classify_block(info, metrics, info->text);
    info->page_number = page_number;

    // Ensure tables have empty text to avoid duplication per specification.
    if (info->type == BLOCK_TABLE)
    {
        free(info->text);
        info->text = strdup("");
        info->text_chars = 0;
    }
}

static void collect_font_stats(fz_stext_page* textpage, FontStats* stats)
{
    font_stats_reset(stats);
    for (fz_stext_block* block = textpage->first_block; block; block = block->next)
    {
        if (block->type != FZ_STEXT_BLOCK_TEXT) continue;
        for (fz_stext_line* line = block->u.t.first_line; line; line = line->next)
        {
            for (fz_stext_char* ch = line->first_char; ch; ch = ch->next)
            {
                font_stats_add(stats, ch->size);
            }
        }
    }
}

static int extract_page_blocks(fz_context* ctx,
                               fz_document* doc,
                               int page_number,
                               const char* output_dir,
                               char* error_buffer,
                               size_t error_buffer_size)
{
    (void)error_buffer;
    (void)error_buffer_size;

    fz_page* page = NULL;
    fz_stext_page* textpage = NULL;
    BlockArray blocks;
    block_array_init(&blocks);

    int status = 0;

    fz_try(ctx)
    {
        page = fz_load_page(ctx, doc, page_number);

        fz_stext_options opts;
        memset(&opts, 0, sizeof(opts));
        opts.flags = FZ_STEXT_CLIP | FZ_STEXT_ACCURATE_BBOXES | FZ_STEXT_COLLECT_STYLES;

        textpage = fz_new_stext_page_from_page(ctx, page, &opts);
        if (!textpage)
        {
            fz_throw(ctx, FZ_ERROR_GENERIC, "text extraction failed");
        }

        FontStats stats;
        collect_font_stats(textpage, &stats);
        PageMetrics metrics = compute_page_metrics(&stats);

        for (fz_stext_block* block = textpage->first_block; block; block = block->next)
        {
            if (block->type == FZ_STEXT_BLOCK_TEXT)
            {
                process_text_block(ctx, block, &metrics, &blocks, page_number);
            }
            else if (block->type == FZ_STEXT_BLOCK_IMAGE)
            {
                add_figure_block(&blocks, block->bbox, page_number);
            }
        }
    }
    fz_always(ctx)
    {
        if (textpage)
        {
            fz_drop_stext_page(ctx, textpage);
        }
        if (page)
        {
            fz_drop_page(ctx, page);
        }
    }
    fz_catch(ctx)
    {
        status = -1;
    }

    if (status != 0)
    {
        block_array_free(&blocks);
        return -1;
    }

    // Sort by top-to-bottom, left-to-right.
    if (blocks.count > 1)
    {
        qsort(blocks.items, blocks.count, sizeof(BlockInfo), compare_block_position);
    }

    // Build JSON output.
    Buffer* json = buffer_create(1024);
    if (!json)
    {
        block_array_free(&blocks);
        return -1;
    }

    buffer_append(json, "[");

    for (size_t i = 0; i < blocks.count; ++i)
    {
        BlockInfo* info = &blocks.items[i];
        if (i > 0) buffer_append(json, ",");

        // Escape text for JSON.
        char* escaped = NULL;
        {
            const char* src = info->text ? info->text : "";
            size_t len = strlen(src);
            Buffer* esc = buffer_create(len + 16);
            if (esc)
            {
                for (size_t k = 0; k < len; ++k)
                {
                    unsigned char c = (unsigned char)src[k];
                    switch (c)
                    {
                        case '\\': buffer_append(esc, "\\\\"); break;
                        case '"': buffer_append(esc, "\\\""); break;
                        case '\n': buffer_append(esc, "\\n"); break;
                        case '\r': buffer_append(esc, "\\r"); break;
                        case '\t': buffer_append(esc, "\\t"); break;
                        default:
                            if (c < 0x20)
                            {
                                buffer_append_format(esc, "\\u%04x", c);
                            }
                            else
                            {
                                buffer_append_char(esc, (char)c);
                            }
                            break;
                    }
                }
                escaped = strdup(esc->data);
                buffer_destroy(esc);
            }
            if (!escaped)
            {
                escaped = strdup("");
            }
        }

        buffer_append(json, "{");
        buffer_append_format(json, "\"type\":\"%s\"", block_type_to_string(info->type));
        buffer_append_format(json, ",\"text\":\"%s\"", escaped ? escaped : "");
        buffer_append_format(json, ",\"bbox\":[%.2f,%.2f,%.2f,%.2f]",
                             info->bbox.x0, info->bbox.y0, info->bbox.x1, info->bbox.y1);
        buffer_append_format(json, ",\"font_size\":%.2f", info->avg_font_size);
        buffer_append_format(json, ",\"font_weight\":\"%s\"", font_weight_from_ratio(info->bold_ratio));
        buffer_append_format(json, ",\"page_number\":%d", info->page_number);
        buffer_append_format(json, ",\"length\":%zu", info->text_chars);

        if (info->type == BLOCK_PARAGRAPH || info->type == BLOCK_LIST)
        {
            buffer_append_format(json, ",\"lines\":%d", info->line_count);
        }

        if (info->type == BLOCK_TABLE)
        {
            buffer_append_format(json, ",\"row_count\":%d", info->row_count);
            buffer_append_format(json, ",\"col_count\":%d", info->column_count);
            buffer_append_format(json, ",\"cell_count\":%d", info->cell_count);
            if (info->confidence > 0.0f)
            {
                buffer_append_format(json, ",\"confidence\":%.2f", info->confidence);
            }
        }

        buffer_append(json, "}");
        free(escaped);
    }

    buffer_append(json, "]");

    // Build output path: output_dir/page_XXX.json
    char filename[64];
    snprintf(filename, sizeof(filename), "page_%03d.json", page_number + 1);

    size_t path_len = strlen(output_dir);
    bool needs_slash = path_len > 0 && output_dir[path_len - 1] != '/' && output_dir[path_len - 1] != '\\';
    size_t full_len = path_len + (needs_slash ? 1 : 0) + strlen(filename) + 1;
    char* full_path = (char*)malloc(full_len);
    if (!full_path)
    {
        buffer_destroy(json);
        block_array_free(&blocks);
        return -1;
    }

    strcpy(full_path, output_dir);
    if (needs_slash)
    {
        strcat(full_path, "/");
    }
    strcat(full_path, filename);

    FILE* out = fopen(full_path, "wb");
    if (!out)
    {
        fprintf(stderr, "Error: failed to open %s for writing (%s)\n", full_path, strerror(errno));
        free(full_path);
        buffer_destroy(json);
        block_array_free(&blocks);
        return -1;
    }

    fwrite(json->data, 1, json->length, out);
    fclose(out);

    free(full_path);
    buffer_destroy(json);

    block_array_free(&blocks);
    return 0;
}

static int ensure_directory(const char* dir)
{
    if (!dir || strlen(dir) == 0) return 0;
    struct stat st;
    if (stat(dir, &st) == 0)
    {
        if (S_ISDIR(st.st_mode)) return 0;
        fprintf(stderr, "Error: %s exists and is not a directory\n", dir);
        return -1;
    }
#if defined(_WIN32)
    if (_mkdir(dir) != 0)
#else
    if (mkdir(dir, 0775) != 0 && errno != EEXIST)
#endif
    {
        fprintf(stderr, "Error: cannot create directory %s (%s)\n", dir, strerror(errno));
        return -1;
    }
    return 0;
}

// -----------------------------------------------------------------------------
// Public entry points.
// -----------------------------------------------------------------------------

static int extract_document(const char* pdf_path, const char* output_dir)
{
    if (!pdf_path) return -1;

    fz_context* ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
    if (!ctx)
    {
        fprintf(stderr, "Error: cannot allocate MuPDF context\n");
        return -1;
    }

    fz_document* doc = NULL;
    int status = 0;

    fz_try(ctx)
    {
        fz_register_document_handlers(ctx);
        doc = fz_open_document(ctx, pdf_path);
        if (!doc)
        {
            fz_throw(ctx, FZ_ERROR_GENERIC, "cannot open document");
        }

        if (ensure_directory(output_dir) != 0)
        {
            fz_throw(ctx, FZ_ERROR_GENERIC, "cannot prepare output directory");
        }

        int page_count = fz_count_pages(ctx, doc);
        for (int i = 0; i < page_count; ++i)
        {
            if (extract_page_blocks(ctx, doc, i, output_dir, NULL, 0) != 0)
            {
                fprintf(stderr, "Warning: failed to extract page %d\n", i + 1);
            }
        }
    }
    fz_always(ctx)
    {
        if (doc) fz_drop_document(ctx, doc);
        fz_drop_context(ctx);
    }
    fz_catch(ctx)
    {
        status = -1;
    }

    if (status != 0)
    {
        printf("{\"error\":\"cannot_open_document\"}");
    }

    return status;
}

extern EXPORT int pdf_to_json(const char* pdf_path, const char* output_dir)
{
    if (!pdf_path) return -1;
    const char* out = output_dir ? output_dir : ".";
    return extract_document(pdf_path, out);
}

// Optional convenience API for single-page extraction returning an allocated
// JSON string. Caller must free() the returned buffer.
extern EXPORT char* page_to_json_string(const char* pdf_path, int page_number)
{
    if (!pdf_path || page_number < 0) return NULL;

    fz_context* ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
    if (!ctx) return NULL;

    fz_document* doc = NULL;
    char* result = NULL;

    fz_try(ctx)
    {
        fz_register_document_handlers(ctx);
        doc = fz_open_document(ctx, pdf_path);
        if (!doc)
        {
            fz_throw(ctx, FZ_ERROR_GENERIC, "cannot open document");
        }
        int page_count = fz_count_pages(ctx, doc);
        if (page_number >= page_count)
        {
            fz_throw(ctx, FZ_ERROR_GENERIC, "page out of range");
        }

        // Reuse extraction logic but capture JSON in-memory instead of writing file.
        fz_page* page = NULL;
        fz_stext_page* textpage = NULL;
        BlockArray blocks;
        block_array_init(&blocks);

        fz_try(ctx)
        {
            page = fz_load_page(ctx, doc, page_number);
            fz_stext_options opts;
            memset(&opts, 0, sizeof(opts));
            opts.flags = FZ_STEXT_CLIP | FZ_STEXT_ACCURATE_BBOXES | FZ_STEXT_COLLECT_STYLES;
            textpage = fz_new_stext_page_from_page(ctx, page, &opts);
            if (!textpage)
            {
                fz_throw(ctx, FZ_ERROR_GENERIC, "text extraction failed");
            }

            FontStats stats;
            collect_font_stats(textpage, &stats);
            PageMetrics metrics = compute_page_metrics(&stats);

            for (fz_stext_block* block = textpage->first_block; block; block = block->next)
            {
                if (block->type == FZ_STEXT_BLOCK_TEXT)
                {
                    process_text_block(ctx, block, &metrics, &blocks, page_number);
                }
                else if (block->type == FZ_STEXT_BLOCK_IMAGE)
                {
                    add_figure_block(&blocks, block->bbox, page_number);
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
            block_array_free(&blocks);
            fz_throw(ctx, FZ_ERROR_GENERIC, "page extraction failed");
        }

        if (blocks.count > 1)
        {
            qsort(blocks.items, blocks.count, sizeof(BlockInfo), compare_block_position);
        }

        Buffer* json = buffer_create(1024);
        if (!json)
        {
            block_array_free(&blocks);
            fz_throw(ctx, FZ_ERROR_GENERIC, "allocation failed");
        }

        buffer_append(json, "[");
        for (size_t i = 0; i < blocks.count; ++i)
        {
            BlockInfo* info = &blocks.items[i];
            if (i > 0) buffer_append(json, ",");

            Buffer* esc = buffer_create(info->text ? strlen(info->text) + 16 : 16);
            if (!esc) esc = buffer_create(16);
            if (esc)
            {
                const char* src = info->text ? info->text : "";
                for (size_t k = 0; src[k]; ++k)
                {
                    unsigned char c = (unsigned char)src[k];
                    switch (c)
                    {
                        case '\\': buffer_append(esc, "\\\\"); break;
                        case '"': buffer_append(esc, "\\\""); break;
                        case '\n': buffer_append(esc, "\\n"); break;
                        case '\r': buffer_append(esc, "\\r"); break;
                        case '\t': buffer_append(esc, "\\t"); break;
                        default:
                            if (c < 0x20)
                                buffer_append_format(esc, "\\u%04x", c);
                            else
                                buffer_append_char(esc, (char)c);
                            break;
                    }
                }
            }
            char* escaped = esc ? strdup(esc->data) : strdup("");
            if (esc) buffer_destroy(esc);
            if (!escaped) escaped = strdup("");

            buffer_append(json, "{");
            buffer_append_format(json, "\"type\":\"%s\"", block_type_to_string(info->type));
            buffer_append_format(json, ",\"text\":\"%s\"", escaped ? escaped : "");
            buffer_append_format(json, ",\"bbox\":[%.2f,%.2f,%.2f,%.2f]",
                                 info->bbox.x0, info->bbox.y0, info->bbox.x1, info->bbox.y1);
            buffer_append_format(json, ",\"font_size\":%.2f", info->avg_font_size);
            buffer_append_format(json, ",\"font_weight\":\"%s\"", font_weight_from_ratio(info->bold_ratio));
            buffer_append_format(json, ",\"page_number\":%d", info->page_number);
            buffer_append_format(json, ",\"length\":%zu", info->text_chars);
            if (info->type == BLOCK_PARAGRAPH || info->type == BLOCK_LIST)
                buffer_append_format(json, ",\"lines\":%d", info->line_count);
            if (info->type == BLOCK_TABLE)
            {
                buffer_append_format(json, ",\"row_count\":%d", info->row_count);
                buffer_append_format(json, ",\"col_count\":%d", info->column_count);
                buffer_append_format(json, ",\"cell_count\":%d", info->cell_count);
                if (info->confidence > 0.0f)
                    buffer_append_format(json, ",\"confidence\":%.2f", info->confidence);
            }
            buffer_append(json, "}");
            free(escaped);
        }
        buffer_append(json, "]");

        result = strdup(json->data);
        buffer_destroy(json);
        block_array_free(&blocks);
    }
    fz_always(ctx)
    {
        if (doc) fz_drop_document(ctx, doc);
        fz_drop_context(ctx);
    }
    fz_catch(ctx)
    {
        result = NULL;
    }

    return result;
}

#ifndef NOLIB_MAIN
int main(int argc, char** argv)
{
    if (argc < 2 || argc > 3)
    {
        fprintf(stderr, "Usage: %s <input.pdf> [output_dir]\n", argv[0]);
        return 1;
    }

    const char* pdf_path = argv[1];
    const char* output_dir = (argc >= 3) ? argv[2] : ".";

    int rc = extract_document(pdf_path, output_dir);
    return (rc == 0) ? 0 : 1;
}
#endif
