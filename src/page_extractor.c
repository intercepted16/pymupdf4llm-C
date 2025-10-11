#include "platform_compat.h"

#include "page_extractor.h"

#include "block_info.h"
#include "buffer.h"
#include "font_metrics.h"
#include "text_utils.h"

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

static Buffer* serialize_blocks_to_json(const BlockArray* blocks)
{
    Buffer* json = buffer_create(1024);
    if (!json)
        return NULL;

    buffer_append(json, "[");
    for (size_t i = 0; i < blocks->count; ++i)
    {
        BlockInfo* info = &blocks->items[i];
        if (i > 0)
            buffer_append(json, ",");

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

    if (blocks.count > 1)
    {
        qsort(blocks.items, blocks.count, sizeof(BlockInfo), compare_block_position);
    }

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
