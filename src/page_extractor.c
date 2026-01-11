#include "page_extractor.h"

#include "block_info.h"
#include "buffer.h"
#include "font_metrics.h"
#include "text_utils.h"
#include "table.h"
#include "list.h"
#include "table/table_utils.h"
#include "column_detector.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "serialize.h"

#define MAX_COLUMNS 32

static void classify_block(BlockInfo* info, const PageMetrics* metrics, const char* normalized_text)
{
    if (!info || !metrics)
        return;

    const float heading_threshold = metrics->median_font_size * 1.25f;
    const size_t text_length = info->text_chars;

    if (info->line_count > 1 && starts_with_bullet(normalized_text))
    {
        info->type = BLOCK_LIST;
        return;
    }

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

    if (!heading_candidate && info->bold_ratio >= 0.8f && text_length > 0 && text_length <= 80 && info->line_count <= 2)
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

        if (info->avg_font_size >= 18.0f)
            info->heading_level = 1;
        else if (info->avg_font_size >= 14.0f)
            info->heading_level = 2;
        else if (info->avg_font_size >= 12.0f)
            info->heading_level = 3;
        else
            info->heading_level = 4;

        return;
    }

    if (starts_with_bullet(normalized_text))
    {
        info->type = BLOCK_LIST;
        return;
    }


    if (text_length == 0)
    {
        info->type = BLOCK_OTHER;
        return;
    }

    info->type = BLOCK_PARAGRAPH;
}

static bool stext_line_starts_with_bullet(fz_stext_line* line)
{
    if (!line || !line->first_char)
        return false;

    char buf[16];
    int pos = 0;
    for (fz_stext_char* ch = line->first_char; ch && pos < 12; ch = ch->next)
    {
            continue;
        int n = fz_runetochar(buf + pos, ch->c);
        if (n > 0)
            pos += n;
    }
    buf[pos] = '\0';
    return starts_with_bullet(buf);
}

static bool stext_line_is_bold(fz_context* ctx, fz_stext_line* line)
{
    if (!line)
        return false;
    int bold_chars = 0;
    int total_chars = 0;
    for (fz_stext_char* ch = line->first_char; ch; ch = ch->next)
    {
        if (ch->c == 0 || isspace(ch->c))
            continue;
        total_chars++;
        if (ch->font && fz_font_is_bold(ctx, ch->font))
            bold_chars++;
    }
    if (total_chars == 0)
        return false;
    return (float)bold_chars / (float)total_chars > 0.70f;
}

static void finalize_block_info(fz_context* ctx, BlockInfo* info, fz_rect page_bounds, fz_stext_page* stext_page,
                                fz_link* page_links)
{
    if (!info)
        return;

    if (page_links)
    {
        for (fz_link* link = page_links; link; link = link->next)
        {
            fz_rect isect = fz_intersect_rect(info->bbox, link->rect);
            if (!fz_is_empty_rect(isect))
            {
                char* link_text = extract_text_with_spacing(ctx, stext_page, &link->rect);
                if (link_text && link_text[0] && link->uri)
                {
                    Link* new_link = create_link(link_text, link->uri, link->rect);
                    if (new_link)
                    {
                        new_link->next = info->links;
                        info->links = new_link;
                    }
                }
                free(link_text);
            }
        }
    }

    float width = info->bbox.x1 - info->bbox.x0;
    float height = info->bbox.y1 - info->bbox.y0;
    if (width < 30.0f && height > 200.0f)
    {
        free(info->text);
        info->text = strdup("");
        info->text_chars = 0;
    }

    if (is_in_margin_area(info->bbox, page_bounds, 0.08f))
    {
        if (info->text_chars > 0 && info->text_chars < 200)
        {
            if (is_lone_page_number(info->text))
            {
                free(info->text);
                info->text = strdup("");
                info->text_chars = 0;
            }
            else if (info->bbox.y0 < page_bounds.y0 + (page_bounds.y1 - page_bounds.y0) * 0.08f)
            {
                if (info->type == BLOCK_HEADING || is_all_caps(info->text))
                {
                    free(info->text);
                    info->text = strdup("");
                    info->text_chars = 0;
                }
            }
        }
    }

    if (info->type == BLOCK_TABLE)
    {
        free(info->text);
        info->text = strdup("");
        info->text_chars = 0;
    }
    else if (info->type == BLOCK_PARAGRAPH || info->type == BLOCK_HEADING)
    {
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
                    }
                    else if (info->text[i] == '\n')
                    {
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

static void process_text_block(fz_context* ctx, fz_stext_block* block, const PageMetrics* metrics, BlockArray* blocks,
                               int page_number, fz_rect page_bounds, fz_stext_page* stext_page, fz_link* page_links)
{
    fz_stext_line* current_line = block->u.t.first_line;

    while (current_line)
    {
        Buffer* text_buf = buffer_create(256);
        if (!text_buf)
            return;

        int lines_in_sub_block = 0;
        float line_spacing_sum = 0.0f;
        int line_spacing_samples = 0;
        float prev_line_y0 = NAN;

        float columns[MAX_COLUMNS];
        int column_line_counts[MAX_COLUMNS];
        memset(columns, 0, sizeof(columns));
        memset(column_line_counts, 0, sizeof(column_line_counts));
        int column_count = 0;
        int lines_with_multiple_columns = 0;
        int rows_with_content = 0;

        TextSpan* first_span = NULL;
        TextSpan* last_span = NULL;
        Buffer* span_buf = buffer_create(64);
        TextStyle current_style = {0};
        float current_font_size = 0.0f;
        fz_rect current_span_bbox = fz_empty_rect;

        BlockFontMetrics sub_metrics = {0};
        fz_rect sub_bbox = fz_empty_rect;
        bool sub_block_is_list = stext_line_starts_with_bullet(current_line);

        while (current_line)
        {
            if (lines_in_sub_block > 0)
            {
                bool line_is_bullet = stext_line_starts_with_bullet(current_line);
                bool line_is_bold = stext_line_is_bold(ctx, current_line);

                if (line_is_bullet != sub_block_is_list)
                    break;

                if (line_is_bold)
                {
                    float current_bold_ratio = (sub_metrics.total_chars > 0)
                                                   ? ((float)sub_metrics.bold_chars / (float)sub_metrics.total_chars)
                                                   : 0.0f;
                    if (current_bold_ratio < 0.5f)
                        break;
                }
                else if (sub_block_is_list)
                {
                }
            }

            if (lines_in_sub_block > 0)
            {
                buffer_append_char(text_buf, '\n');
                if (!isnan(prev_line_y0))
                {
                    float delta = fabsf(current_line->bbox.y0 - prev_line_y0);
                    if (delta > 0.01f)
                    {
                        line_spacing_sum += delta;
                        line_spacing_samples += 1;
                    }
                }
            }
            prev_line_y0 = current_line->bbox.y0;
            lines_in_sub_block++;
            sub_bbox = fz_union_rect(sub_bbox, current_line->bbox);

            float prev_x1 = NAN;
            bool line_used_columns[MAX_COLUMNS] = {0};
            int prev_rune = 0;
            float prev_char_size = 0.0f;
            bool prev_was_footnote = false;

            for (fz_stext_char* ch = current_line->first_char; ch; ch = ch->next)
            {
                if (ch->c == 0)
                    continue;
                sub_metrics.total_chars++;
                sub_metrics.font_size_sum += ch->size;

                char utf8[8];
                int byte_count = fz_runetochar(utf8, ch->c);
                if (byte_count <= 0)
                    continue;
                buffer_append_format(text_buf, "%.*s", byte_count, utf8);

                bool is_bold = ch->font && fz_font_is_bold(ctx, ch->font);
                bool is_italic = ch->font && fz_font_is_italic(ctx, ch->font);
                bool is_mono = ch->font && fz_font_is_monospaced(ctx, ch->font);
                fz_rect char_box = fz_rect_from_quad(ch->quad);
                bool is_super = is_superscript_position(char_box.y0, current_line->bbox.y0, ch->size);
                bool is_sub = is_subscript_position(char_box.y1, current_line->bbox.y1, ch->size);
                
                bool is_footnote = is_footnote_reference(ch->c, ch->size, prev_char_size, prev_rune, prev_was_footnote);
                if (!is_super && is_footnote)
                {
                    is_super = true;
                }

                if (is_bold)
                    sub_metrics.bold_chars++;
                if (is_italic)
                    sub_metrics.italic_chars++;
                if (is_mono)
                    sub_metrics.mono_chars++;
                if (is_super)
                {
                    sub_metrics.superscript_chars++;
                    sub_metrics.has_superscript = true;
                }

                TextStyle char_style = {0};
                char_style.bold = is_bold ? 1 : 0;
                char_style.italic = is_italic ? 1 : 0;
                char_style.monospace = is_mono ? 1 : 0;
                char_style.superscript = is_super ? 1 : 0;
                char_style.subscript = is_sub ? 1 : 0;

                if (span_buf->length > 0 && (memcmp(&char_style, &current_style, sizeof(TextStyle)) != 0 ||
                                             fabsf(ch->size - current_font_size) > 0.5f))
                {
                    TextSpan* span =
                        create_text_span(span_buf->data, current_style, current_font_size, current_span_bbox);
                    if (span)
                    {
                        if (!first_span)
                            first_span = span;
                        else
                            last_span->next = span;
                        last_span = span;
                    }
                    buffer_clear(span_buf);
                    current_span_bbox = fz_empty_rect;
                }

                buffer_append_format(span_buf, "%.*s", byte_count, utf8);
                current_style = char_style;
                current_font_size = ch->size;
                if (fz_is_empty_rect(current_span_bbox))
                    current_span_bbox = char_box;
                else
                    current_span_bbox = fz_union_rect(current_span_bbox, char_box);

                float x0 = char_box.x0;
                float x1 = char_box.x1;
                float gap = (!isnan(prev_x1)) ? fabsf(x0 - prev_x1) : 0.0f;
                bool is_ws = (ch->c == ' ' || ch->c == '\t' || ch->c == '\r' || ch->c == '\n' || ch->c == 160);
                float tol = ch->size * 0.5f;
                if (tol < 3.0f)
                    tol = 3.0f;
                bool start_new_cell = false;
                if (isnan(prev_x1) || gap > tol)
                    start_new_cell = true;
                prev_x1 = x1;
                if (start_new_cell && !is_ws)
                {
                    int idx = find_or_add_column(columns, &column_count, x0, tol);
                    if (idx >= 0)
                        line_used_columns[idx] = true;
                }
                
                prev_rune = ch->c;
                prev_char_size = ch->size;
                prev_was_footnote = is_footnote;
            }
            int line_column_total = 0;
            for (int c = 0; c < column_count; ++c)
                if (line_used_columns[c])
                {
                    column_line_counts[c] += 1;
                    line_column_total += 1;
                }
            if (line_column_total > 0)
                rows_with_content += 1;
            if (line_column_total >= 2)
                lines_with_multiple_columns += 1;

            current_line = current_line->next;
        }

        if (span_buf->length > 0)
        {
            TextSpan* span = create_text_span(span_buf->data, current_style, current_font_size, current_span_bbox);
            if (span)
            {
                if (!first_span)
                    first_span = span;
                else
                    last_span->next = span;
                last_span = span;
            }
        }
        buffer_destroy(span_buf);

        BlockInfo* info = block_array_push(blocks);
        if (info)
        {
            char* normalized = normalize_text(text_buf->data);
            info->text = normalized ? normalized : strdup("");
            info->text_chars = count_unicode_chars(info->text);
            info->bbox = sub_bbox;
            info->avg_font_size =
                (sub_metrics.total_chars > 0) ? (sub_metrics.font_size_sum / (float)sub_metrics.total_chars) : 0.0f;
            info->bold_ratio =
                (sub_metrics.total_chars > 0) ? ((float)sub_metrics.bold_chars / (float)sub_metrics.total_chars) : 0.0f;
            info->italic_ratio = (sub_metrics.total_chars > 0)
                                     ? ((float)sub_metrics.italic_chars / (float)sub_metrics.total_chars)
                                     : 0.0f;
            info->mono_ratio =
                (sub_metrics.total_chars > 0) ? ((float)sub_metrics.mono_chars / (float)sub_metrics.total_chars) : 0.0f;
            info->strikeout_ratio = (sub_metrics.total_chars > 0)
                                        ? ((float)sub_metrics.strikeout_chars / (float)sub_metrics.total_chars)
                                        : 0.0f;
            info->line_count = lines_in_sub_block;
            info->line_spacing_avg =
                (line_spacing_samples > 0) ? (line_spacing_sum / (float)line_spacing_samples) : 0.0f;
            info->column_count = column_count;
            info->has_superscript = sub_metrics.has_superscript ? 1 : 0;
            info->spans = first_span;
            info->links = NULL;
            info->page_number = page_number;
            calculate_column_metrics(column_count, rows_with_content, lines_with_multiple_columns, column_line_counts,
                                     info, lines_in_sub_block);
            classify_block(info, metrics, info->text);

            if (info->mono_ratio >= 0.8f && info->type == BLOCK_PARAGRAPH && info->line_count >= 2)
                info->type = BLOCK_CODE;
            if (sub_metrics.has_superscript && info->text_chars < 100 &&
                sub_metrics.superscript_chars > sub_metrics.total_chars / 2)
                info->is_footnote = 1;

            finalize_block_info(ctx, info, page_bounds, stext_page, page_links);
        }
        buffer_destroy(text_buf);
    }
}

int extract_page_blocks(fz_context* ctx, fz_document* doc, int page_number, const char* output_dir,
                        const char* error_buffer, size_t error_buffer_size)
{
    (void)error_buffer;
    (void)error_buffer_size;

    fz_page* page = NULL;
    fz_stext_page* stext_page = NULL;
    fz_link* page_links = NULL;
    BlockArray blocks;
    block_array_init(&blocks);

    int status = 0;

    fz_try(ctx)
    {
        page = fz_load_page(ctx, doc, page_number);

        fz_stext_options opts = {0};
        opts.flags = FZ_STEXT_CLIP | FZ_STEXT_ACCURATE_BBOXES | FZ_STEXT_COLLECT_STYLES;

        stext_page = fz_new_stext_page_from_page(ctx, page, &opts);
        if (!stext_page)
        {
            fz_throw(ctx, FZ_ERROR_GENERIC, "text extraction failed");
        }

        page_links = fz_load_links(ctx, page);

        FontStats stats;
        collect_font_stats(ctx, stext_page, &stats);
        PageMetrics metrics = compute_page_metrics(&stats);

        fz_rect page_bounds = fz_bound_page(ctx, page);

        for (fz_stext_block* block = stext_page->first_block; block; block = block->next)
        {
            if (block->type == FZ_STEXT_BLOCK_TEXT)
            {
                process_text_block(ctx, block, &metrics, &blocks, page_number, page_bounds, stext_page, page_links);
            }
        }

        TableArray* tables = find_tables_on_page(ctx, doc, page_number, &blocks);
        if (tables)
        {
            process_tables_for_page(ctx, stext_page, tables, page_number, &blocks);
            free_table_array(tables);
        }
    }
    fz_always(ctx)
    {
        if (page_links)
        {
            fz_drop_link(ctx, page_links);
        }
        if (stext_page)
        {
            fz_drop_stext_page(ctx, stext_page);
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
        detect_and_assign_columns(&blocks);
        qsort(blocks.items, blocks.count, sizeof(BlockInfo), compare_block_position);
    }

    consolidate_lists(&blocks);

    {
        size_t write_idx = 0;
        for (size_t read_idx = 0; read_idx < blocks.count; read_idx++)
        {
            BlockInfo* block = &blocks.items[read_idx];
            bool is_text_type =
                (block->type == BLOCK_PARAGRAPH || block->type == BLOCK_HEADING || block->type == BLOCK_LIST ||
                 block->type == BLOCK_CODE || block->type == BLOCK_FOOTNOTE || block->type == BLOCK_OTHER);

            bool keep = false;
            if (!is_text_type)
            {
            }
            else if (block->type == BLOCK_LIST)
            {
                if (block->list_items)
                {
                    for (int li = 0; li < block->list_items->count; li++)
                    {
                        if (has_visible_content(block->list_items->items[li]))
                        {
                            keep = true;
                            break;
                        }
                    }
                }
            }
            else
            {
                if (has_visible_content(block->text))
                {
                    keep = true;
                }
            }

            if (keep)
            {
                if (write_idx != read_idx)
                {
                    blocks.items[write_idx] = blocks.items[read_idx];
                }
                write_idx++;
            }
            else
            {
                free_spans(block->spans);
                free_links(block->links);
                free(block->text);
            }
        }
        blocks.count = write_idx;
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