#include "page_extractor.h"

#include "block_info.h"
#include "buffer.h"
#include "font_metrics.h"
#include "text_utils.h"
#include "table.h"
#include "list.h"
#include "table/table_utils.h"

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

    // Check for list FIRST if the block has multiple lines
    // This prevents "1. Item\n2. Item\n3. Item" from being classified as a numeric heading
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

    // Everything else defaults to paragraph
    info->type = BLOCK_PARAGRAPH;
}

static void process_text_block(fz_context* ctx, fz_stext_block* block, const PageMetrics* metrics, BlockArray* blocks,
                               int page_number)
{
    if (!block || !metrics || !blocks)
        return;

    // Compute block font metrics using the helper from font_metrics.c
    BlockFontMetrics font_metrics;
    compute_block_font_metrics(ctx, block, &font_metrics);

    Buffer* text_buf = buffer_create(256);
    if (!text_buf)
        return;

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

    // Track spans for styled output
    TextSpan* first_span = NULL;
    TextSpan* last_span = NULL;

    // Current span accumulator
    Buffer* span_buf = buffer_create(64);
    TextStyle current_style = {0};
    float current_font_size = 0.0f;
    fz_rect current_span_bbox = fz_empty_rect;

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
        bool line_used_columns[MAX_COLUMNS] = {0};

        for (fz_stext_char* ch = line->first_char; ch; ch = ch->next)
        {
            if (ch->c == 0)
                continue;

            char utf8[8];
            int byte_count = fz_runetochar(utf8, ch->c);
            if (byte_count <= 0)
                continue;

            buffer_append_format(text_buf, "%.*s", byte_count, utf8);

            // Detect text styling for span building
            bool is_bold = ch->font && fz_font_is_bold(ctx, ch->font);
            bool is_italic = ch->font && fz_font_is_italic(ctx, ch->font);
            bool is_mono = ch->font && fz_font_is_monospaced(ctx, ch->font);

            // Check for superscript/subscript via position-based detection
            fz_rect char_box = fz_rect_from_quad(ch->quad);
            bool is_super = is_superscript_position(char_box.y0, line->bbox.y0, ch->size);
            bool is_sub = is_subscript_position(char_box.y1, line->bbox.y1, ch->size);

            // Build styled spans
            TextStyle char_style = {0};
            char_style.bold = is_bold ? 1 : 0;
            char_style.italic = is_italic ? 1 : 0;
            char_style.monospace = is_mono ? 1 : 0;
            char_style.superscript = is_super ? 1 : 0;
            char_style.subscript = is_sub ? 1 : 0;

            // Check if style changed - if so, flush current span
            if (span_buf->length > 0 && (memcmp(&char_style, &current_style, sizeof(TextStyle)) != 0 ||
                                         fabsf(ch->size - current_font_size) > 0.5f))
            {
                // Flush current span
                TextSpan* span = create_text_span(span_buf->data, current_style, current_font_size, current_span_bbox);
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

            // Accumulate to current span
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

    // Flush any remaining span
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
    if (!info)
    {
        buffer_destroy(text_buf);
        free_spans(first_span);
        return;
    }

    char* normalized = normalize_text(text_buf->data);
    buffer_destroy(text_buf);

    if (!normalized)
    {
        normalized = strdup("");
    }

    // NOTE: We no longer call normalize_bullets() here because it replaces
    // all list markers (including numbered ones like "1.") with "-", which
    // prevents proper detection of numbered vs bulleted lists later.
    // The original markers are preserved in info->text for list type detection.

    info->text = normalized ? normalized : strdup("");
    info->text_chars = count_unicode_chars(info->text);
    info->bbox = block->bbox;

    // Use precomputed font metrics from font_metrics.c helper
    info->avg_font_size =
        (font_metrics.total_chars > 0) ? (font_metrics.font_size_sum / (float)font_metrics.total_chars) : 0.0f;
    info->bold_ratio =
        (font_metrics.total_chars > 0) ? ((float)font_metrics.bold_chars / (float)font_metrics.total_chars) : 0.0f;
    info->italic_ratio =
        (font_metrics.total_chars > 0) ? ((float)font_metrics.italic_chars / (float)font_metrics.total_chars) : 0.0f;
    info->mono_ratio =
        (font_metrics.total_chars > 0) ? ((float)font_metrics.mono_chars / (float)font_metrics.total_chars) : 0.0f;
    info->strikeout_ratio =
        (font_metrics.total_chars > 0) ? ((float)font_metrics.strikeout_chars / (float)font_metrics.total_chars) : 0.0f;

    info->line_count = line_count;
    info->line_spacing_avg = (line_spacing_samples > 0) ? (line_spacing_sum / (float)line_spacing_samples) : 0.0f;
    info->column_count = column_count;
    info->has_superscript = font_metrics.has_superscript ? 1 : 0;
    info->spans = first_span;
    info->links = NULL;
    info->heading_level = 0;
    info->column_index = 0;

    // Calculate column metrics using table.c function
    calculate_column_metrics(column_count, rows_with_content, lines_with_multiple_columns, column_line_counts, info,
                             line_count);

    classify_block(info, metrics, info->text);
    info->page_number = page_number;

    // Detect code blocks based on monospace ratio
    if (info->mono_ratio >= 0.8f && info->type == BLOCK_PARAGRAPH && info->line_count >= 2)
    {
        info->type = BLOCK_CODE;
    }

    // Detect potential footnotes (superscripted small text at certain positions)
    if (font_metrics.has_superscript && info->text_chars < 100 &&
        font_metrics.superscript_chars > font_metrics.total_chars / 2)
    {
        info->is_footnote = 1;
    }

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
                            cleaned[write++] = info->text[i];
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

int extract_page_blocks(fz_context* ctx, fz_document* doc, int page_number, const char* output_dir, const char* error_buffer,
                        size_t error_buffer_size)
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

        // Load page links for later association with text blocks
        page_links = fz_load_links(ctx, page);

        FontStats stats;
        collect_font_stats(ctx, stext_page, &stats);
        PageMetrics metrics = compute_page_metrics(&stats);

        fz_rect page_bounds = fz_bound_page(ctx, page);

        for (fz_stext_block* block = stext_page->first_block; block; block = block->next)
        {
            if (block->type == FZ_STEXT_BLOCK_TEXT)
            {
                process_text_block(ctx, block, &metrics, &blocks, page_number);

                // Associate links with this block
                if (blocks.count > 0 && page_links)
                {
                    BlockInfo* info = &blocks.items[blocks.count - 1];
                    for (fz_link* link = page_links; link; link = link->next)
                    {
                        // Check if link overlaps with this block
                        fz_rect isect = fz_intersect_rect(info->bbox, link->rect);
                        if (!fz_is_empty_rect(isect))
                        {
                            // Extract text at link location and create link entry
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
        }

        // Detect, process, and add tables (all handled in table.c)
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
        qsort(blocks.items, blocks.count, sizeof(BlockInfo), compare_block_position);
    }

    // Consolidate consecutive list items into structured lists
    consolidate_lists(&blocks);

    // Filter out text blocks with no visible content
    {
        size_t write_idx = 0;
        for (size_t read_idx = 0; read_idx < blocks.count; read_idx++)
        {
            BlockInfo* block = &blocks.items[read_idx];
            // Keep blocks that have text content, or are non-text types (TABLE, FIGURE, OTHER)
            bool is_text_type =
                (block->type == BLOCK_PARAGRAPH || block->type == BLOCK_HEADING || block->type == BLOCK_LIST ||
                 block->type == BLOCK_CODE || block->type == BLOCK_FOOTNOTE);

            if (!is_text_type || block->text_chars > 0)
            {
                // Keep this block
                if (write_idx != read_idx)
                {
                    blocks.items[write_idx] = blocks.items[read_idx];
                }
                write_idx++;
            }
            else
            {
                // Discard this block - free its resources
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