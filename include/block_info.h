#ifndef BLOCK_INFO_H
#define BLOCK_INFO_H

#include "mupdf/fitz.h"
#include <stddef.h>

/**
 * @brief Content block classification emitted by the extractor.
 */
typedef enum
{
    BLOCK_PARAGRAPH, /**< Flowing text block. */
    BLOCK_HEADING,   /**< Heading or title. */
    BLOCK_TABLE,     /**< Table structure. */
    BLOCK_LIST,      /**< Bullet or numbered list. */
    BLOCK_FIGURE,    /**< Non-textual figure or image. */
    BLOCK_OTHER      /**< Fallback classification. */
} BlockType;

/**
 * @brief Descriptor for a single extracted block.
 */
typedef struct
{
    char* text;               /**< UTF-8 normalized text (may be empty). */
    size_t text_chars;        /**< Unicode scalar count for @ref text. */
    fz_rect bbox;             /**< Original MuPDF bounding box. */
    BlockType type;           /**< Final classification label. */
    float avg_font_size;      /**< Average character size in points. */
    float bold_ratio;         /**< Ratio of characters detected as bold. */
    int line_count;           /**< Number of text lines within the block. */
    float line_spacing_avg;   /**< Average line spacing observed. */
    int column_count;         /**< Estimated number of columns (tables). */
    float column_consistency; /**< Table column alignment score. */
    int row_count;            /**< Estimated row count for tables. */
    int cell_count;           /**< Estimated cell count for tables. */
    float confidence;         /**< Heuristic confidence for tables/headings. */
    int page_number;          /**< Zero-based page index. */
} BlockInfo;

/**
 * @brief Dynamic array container for @ref BlockInfo entries.
 */
typedef struct
{
    BlockInfo* items; /**< Pointer to contiguous storage. */
    size_t count;     /**< Number of active entries. */
    size_t capacity;  /**< Allocated capacity. */
} BlockArray;

/**
 * @brief Convert a block type to its JSON representation.
 *
 * @param t Block type value.
 * @return NUL-terminated string literal describing @p t.
 */
const char* block_type_to_string(BlockType t);

/**
 * @brief Initialise an empty @ref BlockArray instance.
 *
 * @param arr Array to initialise.
 */
void block_array_init(BlockArray* arr);

/**
 * @brief Release all memory held by a block array.
 *
 * @param arr Array to free (may be NULL).
 */
void block_array_free(BlockArray* arr);

/**
 * @brief Append a zero-initialised block to the array.
 *
 * @param arr Target array.
 * @return Pointer to the newly appended block or NULL on allocation failure.
 */
BlockInfo* block_array_push(BlockArray* arr);

/**
 * @brief Compare two blocks by position (top-to-bottom, left-to-right).
 *
 * @param a Pointer to the first block.
 * @param b Pointer to the second block.
 * @return Negative, zero, or positive to order @p a relative to @p b.
 */
int compare_block_position(const void* a, const void* b);

#endif // BLOCK_INFO_H
