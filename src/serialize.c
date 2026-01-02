#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "block_info.h"
#include "buffer.h"
#include "table.h"
#include "text_utils.h"
#include "serialize.h"

static void serialize_block_base_fields(Buffer* json, const BlockInfo* info)
{
    buffer_append_format(json, "\"type\":\"%s\"", block_type_to_string(info->type));
    buffer_append_format(json, ",\"bbox\":[%.2f,%.2f,%.2f,%.2f]", info->bbox.x0, info->bbox.y0, info->bbox.x1,
                         info->bbox.y1);
    buffer_append_format(json, ",\"length\":%zu", info->text_chars);
}

static Link* find_link_for_text(const BlockInfo* info, const char* text)
{
    if (!info->links || !text || !text[0])
        return NULL;

    for (Link* link = info->links; link; link = link->next)
    {
        if (!link->text || !link->uri)
            continue;

        if (strstr(link->text, text) || strstr(text, link->text))
            return link;
    }
    return NULL;
}

Buffer* serialize_blocks_to_json(const BlockArray* blocks)
{
    Buffer* json = buffer_create(1024);
    if (!json)
        return NULL;

    buffer_append(json, "[");
    int first_block = 1;
    for (size_t i = 0; i < blocks->count; ++i)
    {
        BlockInfo* info = &blocks->items[i];

        // Trust that blocks are already validated at creation time
        // Tables are validated in process_tables_for_page
        // Lists are validated in consolidate_lists
        // Text blocks with empty content are filtered elsewhere

        if (!first_block)
            buffer_append(json, ",");
        first_block = 0;

        buffer_append(json, "{");
        serialize_block_base_fields(json, info);

        if (info->type == BLOCK_PARAGRAPH || info->type == BLOCK_CODE)
        {
            buffer_append_format(json, ",\"lines\":%d", info->line_count);
        }

        if (info->type == BLOCK_HEADING && info->heading_level > 0)
        {
            buffer_append_format(json, ",\"level\":%d", info->heading_level);
        }

        // Serialize styled spans (ALWAYS output spans for blocks with text content)
        if (info->spans)
        {
            buffer_append(json, ",\"spans\":[");
            int first_span = 1;
            for (TextSpan* span = info->spans; span; span = span->next)
            {
                if (!span->text)
                    continue;

                char* trimmed = trim_whitespace(span->text);
                if (!trimmed || !trimmed[0])
                    continue;

                if (!first_span)
                    buffer_append(json, ",");
                first_span = 0;

                buffer_append(json, "{\"text\":\"");
                buffer_sappend(json, trimmed);
                buffer_append(json, "\"");

                if (span->style.bold)
                    buffer_append(json, ",\"bold\":true");
                else
                    buffer_append(json, ",\"bold\":false");

                if (span->style.italic)
                    buffer_append(json, ",\"italic\":true");
                else
                    buffer_append(json, ",\"italic\":false");

                if (span->style.monospace)
                    buffer_append(json, ",\"monospace\":true");
                else
                    buffer_append(json, ",\"monospace\":false");

                if (span->style.strikeout)
                    buffer_append(json, ",\"strikeout\":true");
                else
                    buffer_append(json, ",\"strikeout\":false");

                if (span->style.superscript)
                    buffer_append(json, ",\"superscript\":true");
                else
                    buffer_append(json, ",\"superscript\":false");

                if (span->style.subscript)
                    buffer_append(json, ",\"subscript\":true");
                else
                    buffer_append(json, ",\"subscript\":false");

                buffer_append_format(json, ",\"font_size\":%.2f", span->font_size);

                Link* span_link = find_link_for_text(info, span->text);
                if (span_link)
                {
                    buffer_append(json, ",\"link\":true");
                    buffer_append(json, ",\"uri\":\"");
                    buffer_sappend(json, span_link->uri);
                    buffer_append(json, "\"");
                }
                else
                {
                    buffer_append(json, ",\"link\":false");
                    buffer_append(json, ",\"uri\":false");
                }

                buffer_append(json, "}");
            }
            buffer_append(json, "]");
        }
        else if (info->text && info->text[0] && info->type != BLOCK_TABLE)
        {
            // Synthesize a span if we have text but no spans (e.g. for some lists)
            buffer_append(json, ",\"spans\":[{\"text\":\"");
            char* trimmed = trim_whitespace(info->text);
            buffer_sappend(json, trimmed);
            buffer_append(json, "\",\"bold\":false,\"italic\":false,\"monospace\":false,\"strikeout\":false,"
                                "\"superscript\":false,\"subscript\":false");
            buffer_append_format(json, ",\"font_size\":%.2f", info->avg_font_size);
            buffer_append(json, ",\"link\":false,\"uri\":false}]");
        }
        else
        {
            buffer_append(json, ",\"spans\":[]");
        }

        if (info->type == BLOCK_LIST && info->list_items)
        {
            ListItems* list = info->list_items;
            buffer_append(json, ",\"items\":[");
            for (int li = 0; li < list->count; li++)
            {
                if (li > 0)
                    buffer_append(json, ",");

                buffer_append(json, "{\"spans\":[{\"text\":\"");
                // Escape list item text
                if (list->items[li])
                {
                    char* trimmed = trim_whitespace(list->items[li]);
                    buffer_sappend(json, trimmed);
                }
                buffer_append(json, "\",\"bold\":false,\"italic\":false,\"monospace\":false,\"strikeout\":false,"
                                    "\"superscript\":false,\"subscript\":false");
                buffer_append_format(json, ",\"font_size\":%.2f", info->avg_font_size);
                buffer_append(json, ",\"link\":false,\"uri\":false}]");

                // Add list item type
                if (list->types)
                {
                    buffer_append_format(json, ",\"list_type\":\"%s\"",
                                         list->types[li] == LIST_NUMBERED ? "numbered" : "bulleted");
                }
                else
                {
                    buffer_append(json, ",\"list_type\":false");
                }

                // Add indent level
                if (list->indents)
                {
                    buffer_append_format(json, ",\"indent\":%d", list->indents[li]);
                }
                else
                {
                    buffer_append(json, ",\"indent\":false");
                }

                // Add original prefix for numbered lists
                if (list->prefixes && list->prefixes[li])
                {
                    buffer_append(json, ",\"prefix\":\"");
                    buffer_sappend(json, list->prefixes[li]);
                    buffer_append(json, "\"");
                }
                else
                {
                    buffer_append(json, ",\"prefix\":false");
                }

                buffer_append(json, "}");
            }
            buffer_append(json, "]");
        }
        else if (info->type == BLOCK_LIST)
        {
            buffer_append(json, ",\"items\":[]");
        }

        if (info->type == BLOCK_TABLE && info->table_data)
        {
            Table* table = (Table*)info->table_data;

            // Count visible rows - inline check since validation happened at creation
            int visible_row_count = 0;
            for (int r = 0; r < table->count; r++)
            {
                TableRow* row = &table->rows[r];
                for (int c = 0; c < row->count; c++)
                {
                    // Inline: cell is visible if it has a valid bbox and visible text
                    TableCell* cell = &row->cells[c];
                    if (!fz_is_empty_rect(cell->bbox) && cell->text && has_visible_content(cell->text))
                    {
                        visible_row_count++;
                        break;
                    }
                }
            }

            buffer_append_format(json, ",\"row_count\":%d", visible_row_count);
            buffer_append_format(json, ",\"col_count\":%d", info->column_count);
            buffer_append_format(json, ",\"cell_count\":%d", info->column_count);

            // Serialize rows
            buffer_append(json, ",\"rows\":[");
            int first_row = 1;
            for (int r = 0; r < table->count; r++)
            {
                TableRow* row = &table->rows[r];

                // Check if this row has any visible cells - inline check
                int row_has_visible_cells = 0;
                for (int c = 0; c < row->count; c++)
                {
                    TableCell* cell = &row->cells[c];
                    if (!fz_is_empty_rect(cell->bbox) && cell->text && has_visible_content(cell->text))
                    {
                        row_has_visible_cells = 1;
                        break;
                    }
                }

                // Skip empty rows
                if (!row_has_visible_cells)
                    continue;

                if (!first_row)
                    buffer_append(json, ",");
                first_row = 0;

                buffer_append(json, "{");
                buffer_append_format(json, "\"bbox\":[%.2f,%.2f,%.2f,%.2f]", row->bbox.x0, row->bbox.y0, row->bbox.x1,
                                     row->bbox.y1);
                buffer_append(json, ",\"cells\":[");

                int first_cell = 1;
                for (int c = 0; c < row->count; c++)
                {
                    TableCell* cell = &row->cells[c];
                    // Inline: skip if cell has empty bbox or no visible text
                    if (fz_is_empty_rect(cell->bbox) || !cell->text || !has_visible_content(cell->text))
                        continue;

                    if (!first_cell)
                        buffer_append(json, ",");
                    first_cell = 0;

                    buffer_append(json, "{");
                    buffer_append_format(json, "\"bbox\":[%.2f,%.2f,%.2f,%.2f]", cell->bbox.x0, cell->bbox.y0,
                                         cell->bbox.x1, cell->bbox.y1);

                    // Escape and output cell text as spans
                    buffer_append(json, ",\"spans\":[{\"text\":\"");
                    if (cell->text)
                    {
                        char* trimmed = trim_whitespace(cell->text);
                        buffer_sappend(json, trimmed);
                    }
                    buffer_append(json, "\",\"bold\":false,\"italic\":false,\"monospace\":false,\"strikeout\":false,"
                                        "\"superscript\":false,\"subscript\":false");
                    buffer_append_format(json, ",\"font_size\":%.2f", info->avg_font_size);
                    buffer_append(json, ",\"link\":false,\"uri\":false}]}");
                }
                buffer_append(json, "]}");
            }
            buffer_append(json, "]");
        }
        else if (info->type == BLOCK_TABLE)
        {
            buffer_append(json, ",\"rows\":[]");
        }

        buffer_append(json, "}");
    }

    buffer_append(json, "]");
    return json;
}
