#include "block_info.h"
#include "table.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

void block_info_clear(BlockInfo* info);

const char* block_type_to_string(BlockType t)
{
    switch (t)
    {
#define X(name, str)                                                                                                   \
    case BLOCK_##name:                                                                                                 \
        return str;
        BLOCK_TYPES;
#undef X
    default:
        return "unknown";
    }
}

void block_array_init(BlockArray* arr)
{
    if (!arr)
        return;
    arr->items = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

void block_array_free(BlockArray* arr)
{
    if (!arr)
        return;
    for (size_t i = 0; i < arr->count; ++i)
    {
        block_info_clear(&arr->items[i]);
    }
    free(arr->items);
    arr->items = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

void free_spans(TextSpan* spans)
{
    while (spans)
    {
        TextSpan* next = spans->next;
        free(spans->text);
        free(spans);
        spans = next;
    }
}

void free_links(Link* links)
{
    while (links)
    {
        Link* next = links->next;
        free(links->text);
        free(links->uri);
        free(links);
        links = next;
    }
}

TextSpan* create_text_span(const char* text, TextStyle style, float font_size, fz_rect bbox)
{
    TextSpan* span = (TextSpan*)malloc(sizeof(TextSpan));
    if (!span)
        return NULL;
    span->text = text ? strdup(text) : strdup("");
    span->style = style;
    span->font_size = font_size;
    span->bbox = bbox;
    span->next = NULL;
    return span;
}

Link* create_link(const char* text, const char* uri, fz_rect bbox)
{
    Link* link = (Link*)malloc(sizeof(Link));
    if (!link)
        return NULL;
    link->text = text ? strdup(text) : strdup("");
    link->uri = uri ? strdup(uri) : strdup("");
    link->bbox = bbox;
    link->next = NULL;
    return link;
}

void block_info_clear(BlockInfo* info)
{
    if (!info)
        return;

    free(info->text);
    info->text = NULL;

    if (info->table_data)
    {
        Table* table = (Table*)info->table_data;
        if (table->rows)
        {
            for (int j = 0; j < table->count; ++j)
            {
                if (table->rows[j].cells)
                {
                    for (int k = 0; k < table->rows[j].count; ++k)
                    {
                        free(table->rows[j].cells[k].text);
                    }
                    free(table->rows[j].cells);
                }
            }
            free(table->rows);
        }
        free(table);
        info->table_data = NULL;
    }

    if (info->list_items)
    {
        ListItems* list = info->list_items;
        for (int j = 0; j < list->count; ++j)
        {
            free(list->items[j]);
            if (list->prefixes)
                free(list->prefixes[j]);
        }
        free(list->items);
        free(list->indents);
        free(list->types);
        free(list->prefixes);
        free(list);
        info->list_items = NULL;
    }

    if (info->spans)
    {
        free_spans(info->spans);
        info->spans = NULL;
    }

    if (info->links)
    {
        free_links(info->links);
        info->links = NULL;
    }

    info->text_chars = 0;
}

BlockInfo* block_array_push(BlockArray* arr)
{
    if (!arr)
        return NULL;
    if (arr->count == arr->capacity)
    {
        size_t new_cap = arr->capacity == 0 ? 32 : arr->capacity * 2;
        BlockInfo* tmp = (BlockInfo*)realloc(arr->items, new_cap * sizeof(BlockInfo));
        if (!tmp)
            return NULL;
        arr->items = tmp;
        arr->capacity = new_cap;
    }
    BlockInfo* info = &arr->items[arr->count++];
    memset(info, 0, sizeof(BlockInfo));
    return info;
}

int compare_block_position(const void* a, const void* b)
{
    const BlockInfo* ia = (const BlockInfo*)a;
    const BlockInfo* ib = (const BlockInfo*)b;

    // Primary sort key: column index
    if (ia->column_index != ib->column_index)
    {
        return ia->column_index - ib->column_index;
    }

    // Secondary sort key: vertical position
    float dy = ia->bbox.y0 - ib->bbox.y0;
    if (fabsf(dy) > 1e-3f)
    {
        return (dy < 0.0f) ? -1 : 1;
    }

    // Tertiary sort key: horizontal position
    float dx = ia->bbox.x0 - ib->bbox.x0;
    if (fabsf(dx) > 1e-3f)
    {
        return (dx < 0.0f) ? -1 : 1;
    }

    return 0;
}
