#include "list.h"
#include "block_info.h"
#include "text_utils.h"

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// Normalize private use bullets to standard
int normalize_private_list_unicode(int rune)
{
    if (rune == 0xF0B7 || rune == 0xF076 || rune == 0xF0B6)
        return 0x2022; // Convert to standard bullet •
    return rune;
}

/**
 * @brief Clean list item text by removing markers and extracting metadata.
 *
 * This function:
 * - Detects list type (numbered vs bulleted)
 * - Extracts the original prefix (e.g., "1.", "a.", "-")
 * - Removes the marker and returns cleaned text
 * - Replaces newlines with spaces within the item
 *
 * @param text Raw list item text with marker
 * @param out_type Output parameter for detected list type
 * @param out_prefix Output parameter for original prefix (must be freed by caller)
 * @return Cleaned list item text (must be freed by caller)
 */
static char* clean_list_item_text(const char* text, ListType* out_type, char** out_prefix)
{
    if (!text)
    {
        if (out_type)
            *out_type = LIST_BULLETED;
        if (out_prefix)
            *out_prefix = NULL;
        return strdup("");
    }

    const char* p = text;

    // Skip leading whitespace
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
        p++;

    // Detect list type and extract prefix
    ListType type = LIST_BULLETED;
    char* prefix = NULL;

    // Check for numbered list first
    if (starts_with_number(p, &prefix))
    {
        type = LIST_NUMBERED;
        // Skip past the prefix
        while (*p && *p != ' ' && *p != '\t')
            p++;
    }
    // Check for bullet markers
    int rune;
    int rlen = fz_chartorune(&rune, p);
    rune = normalize_private_list_unicode(rune);
    if (is_bullet_rune(rune))
    {
        // Only treat as marker if followed by space or end of string
        if (p[rlen] == ' ' || p[rlen] == '\t' || p[rlen] == '\0' || p[rlen] == '\n')
        {
            type = LIST_BULLETED;
            p += rlen;
        }
    }

    // Skip whitespace after marker
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
        p++;

    // Now copy the rest, replacing newlines with spaces
    size_t len = strlen(p);
    char* result = malloc(len + 1);
    if (!result)
    {
        free(prefix);
        *out_type = LIST_BULLETED;
        *out_prefix = NULL;
        return strdup("");
    }

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

    *out_type = type;
    *out_prefix = prefix;

    return result;
}

/**
 * @brief Consolidate consecutive list blocks into structured lists.
 *
 * This function:
 * - Identifies consecutive BLOCK_LIST items
 * - Merges them into a single consolidated list block
 * - Handles multi-line list items properly
 * - Calculates indentation levels
 * - Preserves list type information (numbered vs bulleted)
 * - Cleans up original blocks after consolidation
 *
 * @param blocks Array of blocks to consolidate (modified in-place)
 */
void consolidate_lists(BlockArray* blocks)
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

        // First pass: count total lines across all blocks to allocate proper space
        int total_line_count = 0;
        for (size_t j = list_start; j <= list_end; ++j)
        {
            BlockInfo* item = &blocks->items[j];
            if (!item->text || strlen(item->text) == 0)
            {
                total_line_count++;
                continue;
            }
            // Count newlines to determine number of items
            const char* p = item->text;
            int lines = 1;
            while (*p)
            {
                if (*p == '\n')
                    lines++;
                p++;
            }
            total_line_count += lines;
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

        list_items->items = (char**)calloc(total_line_count, sizeof(char*));
        list_items->indents = (int*)calloc(total_line_count, sizeof(int));
        list_items->types = (ListType*)calloc(total_line_count, sizeof(ListType));
        list_items->prefixes = (char**)calloc(total_line_count, sizeof(char*));
        list_items->count = 0;
        list_items->capacity = total_line_count;

        if (!list_items->items || !list_items->indents || !list_items->types || !list_items->prefixes)
        {
            free(list_items->items);
            free(list_items->indents);
            free(list_items->types);
            free(list_items->prefixes);
            free(list_items);
            if (write_idx != read_idx)
            {
                blocks->items[write_idx] = *current;
            }
            write_idx++;
            continue;
        }

        // Get base x position for indent calculation
        float base_x = blocks->items[list_start].bbox.x0;
        float base_font_size = blocks->items[list_start].avg_font_size;
        if (base_font_size < 8.0f)
            base_font_size = 12.0f;

        // Collect all list item texts
        fz_rect combined_bbox = blocks->items[list_start].bbox;
        float total_font_size = 0.0f;
        float total_bold_ratio = 0.0f;
        int total_lines = 0;

        for (size_t j = list_start; j <= list_end; ++j)
        {
            BlockInfo* item = &blocks->items[j];

            // Split the block's text by newlines - each line is a separate list item
            if (!item->text || strlen(item->text) == 0)
            {
                // Empty block, skip
                continue;
            }

            // Make a copy to tokenize
            char* text_copy = strdup(item->text);
            if (!text_copy)
                continue;

            char* saveptr = NULL;
            char* line = strtok_r(text_copy, "\n", &saveptr);

            while (line)
            {
                // Skip empty lines
                const char* p = line;
                while (*p == ' ' || *p == '\t')
                    p++;
                if (*p == '\0')
                {
                    line = strtok_r(NULL, "\n", &saveptr);
                    continue;
                }

                // Check if we need to expand the arrays
                if (list_items->count >= list_items->capacity)
                {
                    int new_cap = list_items->capacity * 2;
                    char** new_items = (char**)realloc(list_items->items, new_cap * sizeof(char*));
                    int* new_indents = (int*)realloc(list_items->indents, new_cap * sizeof(int));
                    ListType* new_types = (ListType*)realloc(list_items->types, new_cap * sizeof(ListType));
                    char** new_prefixes = (char**)realloc(list_items->prefixes, new_cap * sizeof(char*));
                    if (new_items && new_indents && new_types && new_prefixes)
                    {
                        list_items->items = new_items;
                        list_items->indents = new_indents;
                        list_items->types = new_types;
                        list_items->prefixes = new_prefixes;
                        list_items->capacity = new_cap;
                    }
                }

                // Clean the list item text and get its type
                ListType item_type;
                char* item_prefix = NULL;
                char* cleaned = clean_list_item_text(line, &item_type, &item_prefix);

                list_items->items[list_items->count] = cleaned;
                list_items->types[list_items->count] = item_type;
                list_items->prefixes[list_items->count] = item_prefix;

                // Calculate indent level based on x position offset
                float x_offset = item->bbox.x0 - base_x;
                int indent = (int)(x_offset / (base_font_size * 2));
                if (indent < 0)
                    indent = 0;
                if (indent > 6)
                    indent = 6;
                list_items->indents[list_items->count] = indent;

                list_items->count++;

                line = strtok_r(NULL, "\n", &saveptr);
            }

            free(text_copy);

            // Free the original text
            free(item->text);
            item->text = NULL;

            // Free spans and links
            if (item->spans)
            {
                free_spans(item->spans);
                item->spans = NULL;
            }
            if (item->links)
            {
                free_links(item->links);
                item->links = NULL;
            }

            // Expand combined bbox
            combined_bbox = fz_union_rect(combined_bbox, item->bbox);
            total_font_size += item->avg_font_size;
            total_bold_ratio += item->bold_ratio;
            total_lines += item->line_count;
        }

        // Validate that the list has visible content before creating the block
        bool has_visible_items = false;
        for (int li = 0; li < list_items->count; li++)
        {
            if (list_items->items[li] && has_visible_content(list_items->items[li]))
            {
                has_visible_items = true;
                break;
            }
        }

        if (!has_visible_items)
        {
            // Skip this list - it has no visible content
            free(list_items->items);
            free(list_items->indents);
            free(list_items->types);
            free(list_items->prefixes);
            free(list_items);
            continue;
        }

        // Create the consolidated list block
        BlockInfo consolidated = {0};
        consolidated.text = strdup(""); // Empty text, content is in list_items
        consolidated.text_chars = 0;
        consolidated.bbox = combined_bbox;
        consolidated.type = BLOCK_LIST;
        int block_count = (int)(list_end - list_start + 1);
        consolidated.avg_font_size = block_count > 0 ? total_font_size / block_count : 12.0f;
        consolidated.bold_ratio = block_count > 0 ? total_bold_ratio / block_count : 0.0f;
        consolidated.line_count = total_lines;
        consolidated.line_spacing_avg = blocks->items[list_start].line_spacing_avg;
        consolidated.page_number = blocks->items[list_start].page_number;
        consolidated.list_items = list_items;
        consolidated.spans = NULL;
        consolidated.links = NULL;

        blocks->items[write_idx] = consolidated;
        write_idx++;

        // Skip past all the list items we just consolidated
        read_idx = list_end;
    }

    // Update the actual count
    blocks->count = write_idx;
}

void free_list_array(ListArray* lists)
{
    if (!lists)
        return;

    if (lists->lists)
    {
        for (int i = 0; i < lists->count; i++)
        {
            ListItems* list = &lists->lists[i];
            if (list->items)
            {
                for (int j = 0; j < list->count; j++)
                {
                    free(list->items[j]);
                    free(list->prefixes[j]);
                }
                free(list->items);
            }
            free(list->indents);
            free(list->types);
            free(list->prefixes);
        }
        free(lists->lists);
    }
    free(lists);
}
