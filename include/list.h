#ifndef LIST_H
#define LIST_H

#include "mupdf/fitz.h"
#include <stdbool.h>

/**
 * @brief List type (bulleted vs numbered).
 */
typedef enum
{
    LIST_BULLETED, /**< Bulleted list (-, *, •). */
    LIST_NUMBERED  /**< Numbered list (1., 2., a., etc.). */
} ListType;

/**
 * @brief Array of list items for nested list structures.
 */
typedef struct
{
    char** items;    /**< Array of list item texts. */
    int* indents;    /**< Indentation levels for each item. */
    ListType* types; /**< Type of each list item (bulleted/numbered). */
    char** prefixes; /**< Original prefix for numbered lists (1., a., etc.). */
    int count;       /**< Number of items. */
    int capacity;    /**< Allocated capacity. */
} ListItems;

typedef struct {
ListItems* lists;
int count;
} ListArray;

// Forward declare BlockArray to avoid circular dependency
// (full definition is in block_info.h which includes this file)
typedef struct BlockArray BlockArray;



// List detection functions
ListArray* find_lists_on_page(fz_context* ctx, fz_document* doc, int page_number, BlockArray* blocks);

// Complete list processing: detection, text extraction, filtering, and block creation
void process_lists_for_page(fz_context* ctx, fz_stext_page* textpage, ListArray* lists, int page_number,
                              BlockArray* blocks);

/**
 * @brief Consolidate consecutive list blocks into structured lists.
 *
 * This function identifies consecutive BLOCK_LIST items in the blocks array,
 * merges them into consolidated list blocks with proper indentation and type
 * information, and updates the array in-place.
 *
 * @param blocks Array of blocks to consolidate (modified in-place).
 */
void consolidate_lists(BlockArray* blocks);

void free_list_array(ListArray* lists);
#endif // LIST_H


