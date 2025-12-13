#include "platform_compat.h"

#include "page_extractor.h"

#include "block_info.h"
#include "buffer.h"
#include "font_metrics.h"
#include "text_utils.h"
#include "table.h"

#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    if (*column_count >= MAX_COLUMNS)
        return -1;
    columns[*column_count] = x;
    *column_count += 1;
    return *column_count - 1;
}

static void add_figure_block(BlockArray* blocks, fz_rect bbox, int page_number)
{
    BlockInfo* info = block_array_push(blocks);
    if (!info)
        return;
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
    if (!info)
        return;
    info->row_count = row_count;
    info->column_count = column_count;
    info->cell_count = row_count * column_count;
    info->column_consistency = consistency;
    float base_score = consistency;
    if (column_count >= 4)
        base_score += 0.15f;
    if (row_count >= 6)
        base_score += 0.15f;
    if (base_score > 1.0f)
        base_score = 1.0f;
    info->confidence = base_score;
}

// Check if text is a lone number (likely page number)
static bool is_lone_page_number(const char* text)
{
    if (!text)
        return false;

    // Skip leading whitespace
    while (*text == ' ' || *text == '\t')
        text++;

    // Count digits
    const char* start = text;
    int digit_count = 0;
    while (*text >= '0' && *text <= '9')
    {
        digit_count++;
        text++;
    }

    // Skip trailing whitespace
    while (*text == ' ' || *text == '\t')
        text++;

    // Must be only digits (1-4 digits typical for page numbers) and nothing else
    return digit_count > 0 && digit_count <= 4 && *text == '\0' && (text - start) == digit_count;
}

// Check if block is in top or bottom margin area
static bool is_in_margin_area(fz_rect bbox, fz_rect page_bbox, float threshold_percent)
{
    float page_height = page_bbox.y1 - page_bbox.y0;
    float threshold = page_height * threshold_percent;

    // Top margin
    if (bbox.y0 < page_bbox.y0 + threshold)
        return true;

    // Bottom margin
    if (bbox.y1 > page_bbox.y1 - threshold)
        return true;

    return false;
}

static void classify_block(BlockInfo* info, const PageMetrics* metrics, const char* normalized_text)
{
    if (!info || !metrics)
        return;

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
        if (!font_based_candidate && !starts_with_numeric_heading(normalized_text) &&
            !starts_with_heading_keyword(normalized_text))
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

    if (info->line_count <= 1)
    {
        info->type = BLOCK_PARAGRAPH;
        return;
    }

    float spacing = info->line_spacing_avg;
    float font = info->avg_font_size;
    if (font <= 0.0f)
        font = metrics->body_font_size;

    if (spacing > 0.0f && fabsf(spacing - font) <= font * 0.6f)
    {
        info->type = BLOCK_PARAGRAPH;
    }
    else
    {
        info->type = BLOCK_PARAGRAPH;
    }
}

static void process_text_block(fz_context* ctx, fz_stext_block* block, const PageMetrics* metrics, BlockArray* blocks,
                               int page_number)
{
    if (!block || !metrics || !blocks)
        return;

    Buffer* text_buf = buffer_create(256);
    if (!text_buf)
        return;

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
            if (ch->c == 0)
                continue;

            char utf8[8];
            int byte_count = fz_runetochar(utf8, ch->c);
            if (byte_count <= 0)
                continue;

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
            if (tolerance < 3.0f)
                tolerance = 3.0f;

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
        if (consistency > 1.0f)
            consistency = 1.0f;
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

    // Skip lone page numbers in margin areas
    if (is_lone_page_number(info->text))
    {
        // Mark for removal by clearing text
        free(info->text);
        info->text = strdup("");
        info->text_chars = 0;
        info->type = BLOCK_OTHER;
    }

    if (info->type == BLOCK_TABLE)
    {
        free(info->text);
        info->text = strdup("");
        info->text_chars = 0;
    }
    else if (info->type == BLOCK_PARAGRAPH || info->type == BLOCK_HEADING)
    {
        // Handle hyphenated line breaks and replace newlines with spaces
        if (info->text)
        {
            size_t len = strlen(info->text);
            char* cleaned = (char*)malloc(len + 1);
            if (cleaned)
            {
                size_t write = 0;
                for (size_t i = 0; i < len; i++)
                {
                    if (info->text[i] == '-' && i + 1 < len && info->text[i + 1] == '\n')
                    {
                        // Hyphen before newline - likely a hyphenated word break
                        // Skip both the hyphen and the newline to join the word
                        i++; // Skip the newline too
                    }
                    else if (info->text[i] == '-' && i + 1 < len && info->text[i + 1] == ' ')
                    {
                        // Check if this is "- " at start of line (bullet) or mid-text hyphen
                        if (i == 0 || (i > 0 && info->text[i - 1] == '\n'))
                        {
                            // Bullet pattern "- " at line start - keep it
                            cleaned[write++] = info->text[i];
                        }
                        else
                        {
                            // Mid-text hyphen followed by space - keep it
                            cleaned[write++] = info->text[i];
                        }
                    }
                    else if (info->text[i] == '\n')
                    {
                        // Regular newline - replace with space
                        cleaned[write++] = ' ';
                    }
                    else
                    {
                        cleaned[write++] = info->text[i];
                    }
                }
                cleaned[write] = '\0';
                free(info->text);
                info->text = cleaned;
                info->text_chars = count_unicode_chars(info->text);
            }
        }
    }
}

static void collect_font_stats(fz_stext_page* textpage, FontStats* stats)
{
    font_stats_reset(stats);
    for (fz_stext_block* block = textpage->first_block; block; block = block->next)
    {
        if (block->type != FZ_STEXT_BLOCK_TEXT)
            continue;
        for (fz_stext_line* line = block->u.t.first_line; line; line = line->next)
        {
            for (fz_stext_char* ch = line->first_char; ch; ch = ch->next)
            {
                font_stats_add(stats, ch->size);
            }
        }
    }
}

static char* clean_list_item_text(const char* text)
{
    if (!text)
        return strdup("");

    const char* p = text;

    // Skip leading whitespace
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
        p++;

    // Skip bullet markers: -, •, o, *, ·, etc.
    if (*p == '-' || *p == '*' || *p == 'o' || (unsigned char)*p == 0xE2 /* UTF-8 bullet */)
    {
        p++;
        // Skip the rest of multi-byte UTF-8 bullets
        while (*p && ((unsigned char)*p & 0xC0) == 0x80)
            p++;

        // Skip whitespace after bullet
        while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
            p++;
    }

    // Now copy the rest, replacing newlines with spaces
    size_t len = strlen(p);
    char* result = (char*)malloc(len + 1);
    if (!result)
        return strdup("");

    size_t write = 0;
    bool last_space = false;

    for (size_t i = 0; p[i]; i++)
    {
        unsigned char c = (unsigned char)p[i];

        if (c == '\n' || c == '\r' || c == '\t')
        {
            if (!last_space && write > 0)
            {
                result[write++] = ' ';
                last_space = true;
            }
        }
        else if (c == ' ')
        {
            if (!last_space && write > 0)
            {
                result[write++] = ' ';
                last_space = true;
            }
        }
        else
        {
            result[write++] = (char)c;
            last_space = false;
        }
    }

    // Trim trailing whitespace
    while (write > 0 && result[write - 1] == ' ')
        write--;

    result[write] = '\0';
    return result;
}

static void consolidate_lists(BlockArray* blocks)
{
    if (!blocks || blocks->count == 0)
        return;

    size_t write_idx = 0;
    for (size_t read_idx = 0; read_idx < blocks->count; ++read_idx)
    {
        BlockInfo* current = &blocks->items[read_idx];

        // If not a list, just copy and continue
        if (current->type != BLOCK_LIST)
        {
            if (write_idx != read_idx)
            {
                blocks->items[write_idx] = *current;
            }
            write_idx++;
            continue;
        }

        // Start of a list - find all consecutive list items
        size_t list_start = read_idx;
        size_t list_end = read_idx;

        // Look ahead for consecutive list items (within reasonable vertical distance)
        for (size_t j = read_idx + 1; j < blocks->count; ++j)
        {
            BlockInfo* next = &blocks->items[j];
            BlockInfo* prev = &blocks->items[j - 1];

            if (next->type != BLOCK_LIST)
                break;

            // Check if items are close vertically (within 2x font size)
            float vertical_gap = next->bbox.y0 - prev->bbox.y1;
            float max_gap = prev->avg_font_size * 2.5f;
            if (max_gap < 20.0f)
                max_gap = 20.0f;

            if (vertical_gap > max_gap)
                break;

            list_end = j;
        }

        // Create consolidated list block
        ListItems* list_items = (ListItems*)malloc(sizeof(ListItems));
        if (!list_items)
        {
            // Fallback: just copy the first item
            if (write_idx != read_idx)
            {
                blocks->items[write_idx] = *current;
            }
            write_idx++;
            continue;
        }

        int item_count = (int)(list_end - list_start + 1);
        list_items->items = (char**)calloc(item_count, sizeof(char*));
        list_items->count = 0;
        list_items->capacity = item_count;

        if (!list_items->items)
        {
            free(list_items);
            if (write_idx != read_idx)
            {
                blocks->items[write_idx] = *current;
            }
            write_idx++;
            continue;
        }

        // Collect all list item texts
        fz_rect combined_bbox = blocks->items[list_start].bbox;
        float total_font_size = 0.0f;
        float total_bold_ratio = 0.0f;
        int total_lines = 0;

        for (size_t j = list_start; j <= list_end; ++j)
        {
            BlockInfo* item = &blocks->items[j];

            // Clean the list item text (remove bullets, newlines, etc.)
            char* cleaned = clean_list_item_text(item->text);
            list_items->items[list_items->count++] = cleaned;

            // Free the original text
            free(item->text);
            item->text = NULL;

            // Expand combined bbox
            combined_bbox = fz_union_rect(combined_bbox, item->bbox);
            total_font_size += item->avg_font_size;
            total_bold_ratio += item->bold_ratio;
            total_lines += item->line_count;
        }

        // Create the consolidated list block
        BlockInfo consolidated = {0};
        consolidated.text = strdup(""); // Empty text, content is in list_items
        consolidated.text_chars = 0;
        consolidated.bbox = combined_bbox;
        consolidated.type = BLOCK_LIST;
        consolidated.avg_font_size = total_font_size / item_count;
        consolidated.bold_ratio = total_bold_ratio / item_count;
        consolidated.line_count = total_lines;
        consolidated.line_spacing_avg = blocks->items[list_start].line_spacing_avg;
        consolidated.page_number = blocks->items[list_start].page_number;
        consolidated.list_items = list_items;

        blocks->items[write_idx] = consolidated;
        write_idx++;

        // Skip past all the list items we just consolidated
        read_idx = list_end;
    }

    // Update the actual count
    blocks->count = write_idx;
}

static int list_has_visible_content(ListItems* list)
{
    if (!list || list->count == 0)
        return 0;

    for (int i = 0; i < list->count; i++)
    {
        if (!list->items[i])
            continue;

        for (const char* p = list->items[i]; *p; p++)
        {
            unsigned char ch = (unsigned char)*p;
            if (ch >= 33 && ch <= 126)
                return 1;
        }
    }
    return 0;
}

static int table_has_visible_content(BlockInfo* info)
{
    if (!info || !info->table_data)
        return 0;

    // Check if table has meaningful dimensions
    if (info->row_count <= 0 || info->column_count <= 0)
        return 0;

    // Check if table has actual cell content
    Table* table = (Table*)info->table_data;
    if (!table || table->count <= 0)
        return 0;

    // Look for at least one cell with visible text
    for (int row_idx = 0; row_idx < table->count; row_idx++)
    {
        if (!table->rows[row_idx].cells)
            continue;

        for (int cell_idx = 0; cell_idx < table->rows[row_idx].count; cell_idx++)
        {
            const char* text = table->rows[row_idx].cells[cell_idx].text;
            if (!text)
                continue;

            for (const char* p = text; *p; p++)
            {
                unsigned char ch = (unsigned char)*p;
                if (ch >= 33 && ch <= 126)
                    return 1;
            }
        }
    }

    return 0;
}

static Buffer* serialize_blocks_to_json(const BlockArray* blocks)
{
    Buffer* json = buffer_create(1024);
    if (!json)
        return NULL;

    buffer_append(json, "[");
    int first_block = 1;
    for (size_t i = 0; i < blocks->count; ++i)
    {
        BlockInfo* info = &blocks->items[i];

        // Skip tables with no visible content
        if (info->type == BLOCK_TABLE)
        {
            if (!table_has_visible_content(info))
            {
                continue;
            }
        }

        // Skip lists with no visible content
        if (info->type == BLOCK_LIST)
        {
            if (!list_has_visible_content(info->list_items))
            {
                continue;
            }
        }

        // Skip blocks with no visible content (non-table, non-figure, non-list blocks)
        if (info->type != BLOCK_TABLE && info->type != BLOCK_FIGURE && info->type != BLOCK_LIST)
        {
            int has_visible = 0;
            if (info->text)
            {
                for (const char* p = info->text; *p; p++)
                {
                    unsigned char ch = (unsigned char)*p;
                    if (ch >= 33 && ch <= 126)
                    {
                        has_visible = 1;
                        break;
                    }
                }
            }
            if (!has_visible)
            {
                continue;
            }
        }

        if (!first_block)
            buffer_append(json, ",");
        first_block = 0;

        Buffer* esc = buffer_create(info->text ? strlen(info->text) + 16 : 16);
        if (!esc)
            esc = buffer_create(16);
        if (esc)
        {
            const char* src = info->text ? info->text : "";
            for (size_t k = 0; src[k]; ++k)
            {
                unsigned char c = (unsigned char)src[k];
                switch (c)
                {
                case '\\':
                    buffer_append(esc, "\\\\");
                    break;
                case '"':
                    buffer_append(esc, "\\\"");
                    break;
                case '\n':
                    buffer_append(esc, "\\n");
                    break;
                case '\r':
                    buffer_append(esc, "\\r");
                    break;
                case '\t':
                    buffer_append(esc, "\\t");
                    break;
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
        if (esc)
            buffer_destroy(esc);
        if (!escaped)
            escaped = strdup("");

        buffer_append(json, "{");
        buffer_append_format(json, "\"type\":\"%s\"", block_type_to_string(info->type));
        buffer_append_format(json, ",\"text\":\"%s\"", escaped ? escaped : "");
        buffer_append_format(json, ",\"bbox\":[%.2f,%.2f,%.2f,%.2f]", info->bbox.x0, info->bbox.y0, info->bbox.x1,
                             info->bbox.y1);
        buffer_append_format(json, ",\"font_size\":%.2f", info->avg_font_size);
        buffer_append_format(json, ",\"font_weight\":\"%s\"", font_weight_from_ratio(info->bold_ratio));
        buffer_append_format(json, ",\"length\":%zu", info->text_chars);

        if (info->type == BLOCK_PARAGRAPH)
        {
            buffer_append_format(json, ",\"lines\":%d", info->line_count);
        }

        if (info->type == BLOCK_LIST && info->list_items)
        {
            ListItems* list = info->list_items;
            buffer_append(json, ",\"items\":[");
            for (int li = 0; li < list->count; li++)
            {
                if (li > 0)
                    buffer_append(json, ",");

                // Escape list item text
                buffer_append(json, "\"");
                if (list->items[li])
                {
                    for (const char* p = list->items[li]; *p; p++)
                    {
                        unsigned char ch = (unsigned char)*p;
                        switch (ch)
                        {
                        case '\\':
                            buffer_append(json, "\\\\");
                            break;
                        case '"':
                            buffer_append(json, "\\\"");
                            break;
                        case '\n':
                            buffer_append(json, "\\n");
                            break;
                        case '\r':
                            buffer_append(json, "\\r");
                            break;
                        case '\t':
                            buffer_append(json, "\\t");
                            break;
                        default:
                            if (ch < 0x20)
                                buffer_append_format(json, "\\u%04x", ch);
                            else
                                buffer_append_char(json, (char)ch);
                            break;
                        }
                    }
                }
                buffer_append(json, "\"");
            }
            buffer_append(json, "]");
        }

        if (info->type == BLOCK_TABLE && info->table_data)
        {
            Table* table = (Table*)info->table_data;
            buffer_append_format(json, ",\"row_count\":%d", info->row_count);
            buffer_append_format(json, ",\"col_count\":%d", info->column_count);
            buffer_append_format(json, ",\"cell_count\":%d", info->cell_count);
            if (info->confidence > 0.0f)
            {
                buffer_append_format(json, ",\"confidence\":%.2f", info->confidence);
            }

            // Serialize rows
            buffer_append(json, ",\"rows\":[");
            for (int r = 0; r < table->count; r++)
            {
                if (r > 0)
                    buffer_append(json, ",");
                TableRow* row = &table->rows[r];
                buffer_append(json, "{");
                buffer_append_format(json, "\"bbox\":[%.2f,%.2f,%.2f,%.2f]", row->bbox.x0, row->bbox.y0, row->bbox.x1,
                                     row->bbox.y1);
                buffer_append(json, ",\"cells\":[");

                int first_cell = 1;
                for (int c = 0; c < row->count; c++)
                {
                    TableCell* cell = &row->cells[c];

                    // Skip cells with empty bboxes
                    if (fz_is_empty_rect(cell->bbox))
                    {
                        continue;
                    }

                    // Skip cells with no visible content (whitelist ASCII 33-126)
                    int has_visible = 0;
                    if (cell->text)
                    {
                        for (const char* p = cell->text; *p; p++)
                        {
                            unsigned char ch = (unsigned char)*p;
                            if (ch >= 33 && ch <= 126)
                            {
                                has_visible = 1;
                                break;
                            }
                        }
                    }
                    if (!has_visible)
                    {
                        continue;
                    }

                    if (!first_cell)
                        buffer_append(json, ",");
                    first_cell = 0;

                    buffer_append(json, "{");
                    buffer_append_format(json, "\"bbox\":[%.2f,%.2f,%.2f,%.2f]", cell->bbox.x0, cell->bbox.y0,
                                         cell->bbox.x1, cell->bbox.y1);

                    // Escape and output cell text
                    buffer_append(json, ",\"text\":\"");
                    if (cell->text)
                    {
                        for (const char* p = cell->text; *p; p++)
                        {
                            unsigned char ch = (unsigned char)*p;
                            switch (ch)
                            {
                            case '\\':
                                buffer_append(json, "\\\\");
                                break;
                            case '"':
                                buffer_append(json, "\\\"");
                                break;
                            case '\n':
                                buffer_append(json, "\\n");
                                break;
                            case '\r':
                                buffer_append(json, "\\r");
                                break;
                            case '\t':
                                buffer_append(json, "\\t");
                                break;
                            default:
                                if (ch < 0x20)
                                    buffer_append_format(json, "\\u%04x", ch);
                                else
                                    buffer_append_char(json, (char)ch);
                                break;
                            }
                        }
                    }
                    buffer_append(json, "\"}");
                }
                buffer_append(json, "]}");
            }
            buffer_append(json, "]");
        }

        buffer_append(json, "}");
        free(escaped);
    }

    buffer_append(json, "]");
    return json;
}
int extract_page_blocks(fz_context* ctx, fz_document* doc, int page_number, const char* output_dir, char* error_buffer,
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

        fz_rect page_bounds = fz_bound_page(ctx, page);

        for (fz_stext_block* block = textpage->first_block; block; block = block->next)
        {
            if (block->type == FZ_STEXT_BLOCK_TEXT)
            {
                process_text_block(ctx, block, &metrics, &blocks, page_number);

                // Filter blocks in top/bottom margins if they look like headers/footers
                if (blocks.count > 0)
                {
                    BlockInfo* info = &blocks.items[blocks.count - 1];

                    // Filter very narrow blocks (likely rotated margin text)
                    float width = info->bbox.x1 - info->bbox.x0;
                    float height = info->bbox.y1 - info->bbox.y0;
                    if (width < 30.0f && height > 200.0f)
                    {
                        free(info->text);
                        info->text = strdup("");
                        info->text_chars = 0;
                    }

                    if (is_in_margin_area(info->bbox, page_bounds, 0.08f)) // 8% margin
                    {
                        // If it's short text in margin, likely a header/footer
                        if (info->text_chars > 0 && info->text_chars < 200)
                        {
                            // Filter page numbers
                            if (is_lone_page_number(info->text))
                            {
                                free(info->text);
                                info->text = strdup("");
                                info->text_chars = 0;
                            }
                            // Filter typical headers (short text in top margin)
                            else if (info->bbox.y0 < page_bounds.y0 + (page_bounds.y1 - page_bounds.y0) * 0.08f)
                            {
                                // Header in top margin - filter if it looks like a title/header
                                // (mostly uppercase, contains common header keywords, or is a heading type)
                                if (info->type == BLOCK_HEADING || is_all_caps(info->text))
                                {
                                    free(info->text);
                                    info->text = strdup("");
                                    info->text_chars = 0;
                                }
                            }
                        }
                    }
                }
            }
            else if (block->type == FZ_STEXT_BLOCK_IMAGE)
            {
                add_figure_block(&blocks, block->bbox, page_number);
            }
        }

        TableArray* tables = find_tables_on_page(ctx, doc, page_number, &blocks);

        // Validate line-based tables: check for quality issues
        // A valid table should have:
        // - At least 2 rows with content
        // - Consistent column counts across all rows (no variation)
        // - At least 2 columns
        // - No empty rows (all rows should have cells)
        // - Valid bounding boxes (within reasonable page bounds)
        bool tables_valid = false;
        fz_rect page_rect = fz_bound_page(ctx, page);

        if (tables && tables->count > 0)
        {
            for (int t = 0; t < tables->count; t++)
            {
                Table* table = &tables->tables[t];

                // Check if table bbox is within reasonable page bounds
                // Allow some margin but reject obviously invalid values
                if (table->bbox.y0 < page_rect.y0 - 50 || table->bbox.y1 > page_rect.y1 + 50 ||
                    table->bbox.x0 < page_rect.x0 - 50 || table->bbox.x1 > page_rect.x1 + 50)
                {
                    continue; // Skip this table, invalid bounds
                }

                int rows_with_cells = 0;
                int empty_rows = 0;
                int expected_cols = -1;
                bool consistent = true;

                for (int r = 0; r < table->count; r++)
                {
                    TableRow* row = &table->rows[r];

                    // Check for valid row bbox
                    if (row->bbox.y0 < page_rect.y0 - 10 || row->bbox.y1 > page_rect.y1 + 10)
                    {
                        empty_rows++; // Treat as invalid/empty
                        continue;
                    }

                    if (row->count > 0)
                    {
                        rows_with_cells++;
                        if (expected_cols < 0)
                            expected_cols = row->count;
                        else if (row->count != expected_cols)
                            consistent = false;
                    }
                    else
                    {
                        empty_rows++;
                    }
                }

                // Valid if: all rows have same column count, at least 2 valid rows,
                // at least 2 columns, and no empty/invalid rows
                if (rows_with_cells >= 2 && expected_cols >= 2 && consistent && empty_rows == 0)
                {
                    tables_valid = true;
                    break;
                }
            }
        }

        // Fallback: synthesize a 2-column table from text if no valid line-based tables found
        if (!tables_valid)
        {
            if (tables)
            {
                free_table_array(tables);
                tables = NULL;
            }
            tables = synthesize_text_table_two_col(ctx, textpage, &metrics);
        }

        if (tables && tables->count > 0)
        {
            // Extract text from each table and add as new blocks
            for (int t = 0; t < tables->count; t++)
            {
                Table* table = &tables->tables[t];

                // Extract text from each cell and store it
                for (int r = 0; r < table->count; r++)
                {
                    TableRow* row = &table->rows[r];

                    for (int c = 0; c < row->count; c++)
                    {
                        TableCell* cell = &row->cells[c];
                        fz_rect cell_rect = cell->bbox;

                        // Extract text with proper spacing between words
                        char* cell_text = extract_text_with_spacing(ctx, textpage, &cell_rect);

                        if (cell_text && cell_text[0])
                        {
                            // Normalize cell text: remove newlines and excess whitespace
                            char* normalized = normalize_text(cell_text);
                            free(cell_text);

                            if (normalized)
                            {
                                // Replace newlines with spaces in cells
                                for (char* p = normalized; *p; p++)
                                {
                                    if (*p == '\n' || *p == '\r')
                                    {
                                        *p = ' ';
                                    }
                                }
                                cell->text = normalized; // Store in cell structure
                            }
                        }
                        else
                        {
                            free(cell_text);
                        }

                        if (!cell->text)
                        {
                            cell->text = strdup(""); // Ensure non-NULL
                        }
                    }
                }

                // -------------------------------------------------------------
                // PATCH: Remove Empty Columns (Whitelist Visible ASCII)
                // -------------------------------------------------------------
                if (table->count > 0 && table->rows[0].count > 0)
                {
                    int col_count = table->rows[0].count;
                    int* keep_col = (int*)calloc(col_count, sizeof(int));
                    int keep_count = 0;

                    // 1. Identify which columns have actual content
                    for (int c = 0; c < col_count; c++)
                    {
                        int has_text = 0;
                        for (int r = 0; r < table->count; r++)
                        {
                            char* txt = table->rows[r].cells[c].text;
                            if (txt && *txt)
                            {
                                // Whitelist strategy: Only keep if we find a visible ASCII character (33-126)
                                // This automatically ignores spaces (32), tabs (9), and all weird Unicode.
                                for (char* p = txt; *p; p++)
                                {
                                    unsigned char uc = (unsigned char)*p;
                                    if (uc >= 33 && uc <= 126)
                                    {
                                        has_text = 1;
                                        break;
                                    }
                                }
                            }
                            if (has_text)
                                break; // Found content, column is valid
                        }

                        if (has_text)
                        {
                            keep_col[c] = 1;
                            keep_count++;
                        }
                    }

                    // 2. Compress table if we found empty columns
                    if (keep_count > 0 && keep_count < col_count)
                    {
                        for (int r = 0; r < table->count; r++)
                        {
                            TableRow* row = &table->rows[r];
                            int target_idx = 0;

                            for (int source_idx = 0; source_idx < col_count; source_idx++)
                            {
                                if (keep_col[source_idx])
                                {
                                    if (target_idx != source_idx)
                                    {
                                        // Move valid cell to new position
                                        row->cells[target_idx] = row->cells[source_idx];
                                    }
                                    target_idx++;
                                }
                                else
                                {
                                    // This is a ghost/empty column.
                                    // Free the text to prevent memory leak.
                                    if (row->cells[source_idx].text)
                                    {
                                        free(row->cells[source_idx].text);
                                        row->cells[source_idx].text = NULL;
                                    }
                                }
                            }
                            // Update row count to match new number of columns
                            row->count = keep_count;
                        }
                    }
                    free(keep_col);
                }
                // -------------------------------------------------------------
                // END PATCH
                // -------------------------------------------------------------

                // Now find overlapping text blocks and remove them
                for (size_t b = 0; b < blocks.count; b++)
                {
                    BlockInfo* block = &blocks.items[b];

                    // Check if this block overlaps significantly with the table
                    float overlap_x = fminf(block->bbox.x1, table->bbox.x1) - fmaxf(block->bbox.x0, table->bbox.x0);
                    float overlap_y = fminf(block->bbox.y1, table->bbox.y1) - fmaxf(block->bbox.y0, table->bbox.y0);

                    if (overlap_x > 0 && overlap_y > 0)
                    {
                        // Calculate overlap ratio
                        float block_area = (block->bbox.x1 - block->bbox.x0) * (block->bbox.y1 - block->bbox.y0);
                        float overlap_area = overlap_x * overlap_y;

                        // If block overlaps >70% with table, mark it for removal
                        if (block_area > 0 && overlap_area / block_area > 0.7)
                        {
                            // Mark this text block's text as empty (will be replaced by table)
                            free(block->text);
                            block->text = strdup("");
                            block->text_chars = 0;
                        }
                    }
                }

                // Add a new table block with the structured data
                BlockInfo* table_block = block_array_push(&blocks);
                if (table_block)
                {
                    table_block->text = strdup(""); // No text, structure is in table_data
                    table_block->text_chars = 0;
                    table_block->bbox = table->bbox;
                    table_block->type = BLOCK_TABLE;
                    table_block->avg_font_size = 0.0f;
                    table_block->bold_ratio = 0.0f;
                    table_block->line_count = table->count;
                    table_block->line_spacing_avg = 0.0f;
                    // Update column count based on the potentially compressed first row
                    table_block->column_count = (table->count > 0) ? table->rows[0].count : 0;
                    table_block->row_count = table->count;
                    table_block->cell_count = table_block->row_count * table_block->column_count;
                    table_block->confidence = 1.0f;
                    table_block->page_number = page_number;
                    table_block->column_consistency = 1.0f;

                    // Deep copy the table structure so we can safely free the tables array
                    table_block->table_data = malloc(sizeof(Table));
                    if (table_block->table_data)
                    {
                        Table* new_table = (Table*)table_block->table_data;
                        new_table->bbox = table->bbox;
                        new_table->count = table->count;
                        new_table->rows = table->rows;

                        // Transfer ownership - null out in original to prevent double-free
                        table->rows = NULL;
                        table->count = 0;
                    }
                }
            }
            // Free the table array structure (but not the table rows we transferred)
            free_table_array(tables);
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

    if (blocks.count > 1)
    {
        qsort(blocks.items, blocks.count, sizeof(BlockInfo), compare_block_position);
    }

    // Consolidate consecutive list items into structured lists
    consolidate_lists(&blocks);

    Buffer* json = serialize_blocks_to_json(&blocks);
    if (!json)
    {
        block_array_free(&blocks);
        return -1;
    }

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
EXPORT char* page_to_json_string(const char* pdf_path, int page_number)
{
    if (!pdf_path || page_number < 0)
        return NULL;

    fz_context* ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
    if (!ctx)
        return NULL;

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
            if (textpage)
                fz_drop_stext_page(ctx, textpage);
            if (page)
                fz_drop_page(ctx, page);
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

        // Consolidate consecutive list items into structured lists
        consolidate_lists(&blocks);

        Buffer* json = serialize_blocks_to_json(&blocks);
        if (!json)
        {
            block_array_free(&blocks);
            fz_throw(ctx, FZ_ERROR_GENERIC, "allocation failed");
        }

        result = strdup(json->data);
        buffer_destroy(json);
        block_array_free(&blocks);
    }
    fz_always(ctx)
    {
        if (doc)
            fz_drop_document(ctx, doc);
        fz_drop_context(ctx);
    }
    fz_catch(ctx)
    {
        result = NULL;
    }

    return result;
}
