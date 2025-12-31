#ifndef BLOCK_INFO_H
#define BLOCK_INFO_H

#include "mupdf/fitz.h"
#include <stddef.h>
#include "list.h"

#define BLOCK_TYPES \
    X(PARAGRAPH, "text") \
    X(HEADING, "heading") \
    X(TABLE, "table") \
    X(LIST, "list") \
    X(FIGURE, "figure") \
    X(CODE, "code") \
    X(FOOTNOTE, "footnote") \
    X(OTHER, "other")

/**
 * @brief Content block classification emitted by the extractor.
 */
typedef enum {
#define X(name, str) BLOCK_##name,
    BLOCK_TYPES
#undef X
} BlockType;

/**
 * @brief Text styling flags for a span.
 */
typedef struct
{
    unsigned int bold : 1;        /**< Text is bold. */
    unsigned int italic : 1;      /**< Text is italic. */
    unsigned int monospace : 1;   /**< Text is monospace/code. */
    unsigned int strikeout : 1;   /**< Text is struck out. */
    unsigned int superscript : 1; /**< Text is superscript. */
    unsigned int subscript : 1;   /**< Text is subscript. */
} TextStyle;

/**
 * @brief A styled text span within a block.
 */
typedef struct TextSpan
{
    char* text;            /**< UTF-8 text content. */
    TextStyle style;       /**< Styling flags. */
    float font_size;       /**< Font size in points. */
    fz_rect bbox;          /**< Bounding box. */
    struct TextSpan* next; /**< Next span in list. */
} TextSpan;

/**
 * @brief A hyperlink within a block.
 */
typedef struct Link
{
    char* text;        /**< Link display text. */
    char* uri;         /**< Target URI. */
    fz_rect bbox;      /**< Bounding box. */
    struct Link* next; /**< Next link in list. */
} Link;

/**
 * @brief Descriptor for a single extracted block.
 */
typedef struct BlockInfo BlockInfo;

struct BlockInfo
{
    char* text;               /**< UTF-8 normalized text (=may be empty). */
    size_t text_chars;        /**< Unicode scalar count for @ref text. */
    fz_rect bbox;             /**< Original MuPDF bounding box. */
    BlockType type;           /**< Final classification label. */
    float avg_font_size;      /**< Average character size in points. */
    float bold_ratio;         /**< Ratio of characters detected as bold. */
    float italic_ratio;       /**< Ratio of characters detected as italic. */
    float mono_ratio;         /**< Ratio of characters in monospace font. */
    float strikeout_ratio;    /**< Ratio of characters struck out. */
    int line_count;           /**< Number of text lines within the block. */
    float line_spacing_avg;   /**< Average line spacing observed. */
    int column_count;         /**< Estimated number of columns (tables). */
    float column_consistency; /**< Table column alignment score. */
    int row_count;            /**< Estimated row count for tables. */
    int cell_count;           /**< Estimated cell count for tables. */
    float confidence;         /**< Heuristic confidence for tables/headings. */
    int page_number;          /**< Zero-based page index. */
    int heading_level;        /**< Heading level 1-6 (0 if not a heading). */
    int column_index;         /**< Column index for multi-column layout (0-based). */
    void* table_data;         /**< Pointer to TableArray for BLOCK_TABLE type. */
    ListItems* list_items;    /**< Pointer to ListItems for BLOCK_LIST type. */
    TextSpan* spans;          /**< Linked list of styled text spans. */
    Link* links;              /**< Linked list of hyperlinks in this block. */
    int has_superscript;      /**< Contains superscript text (e.g., footnote ref). */
    int is_footnote;          /**< Block is a footnote. */
};

/**
 * @brief Dynamic array container for @ref BlockInfo entries.
 */
typedef struct BlockArray
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
 * @brief Initialize an empty @ref BlockArray instance.
 *
 * @param arr Array to initialize.
 */
void block_array_init(BlockArray* arr);

/**
 * @brief Release all memory held by a block array.
 *
 * @param arr Array to free (=may be NULL).
 */
void block_array_free(BlockArray* arr);

/**
 * @brief Append a zero-initialized block to the array.
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

/**
 * @brief Free a linked list of text spans.
 *
 * @param spans Head of the span list.
 */
void free_spans(TextSpan* spans);

/**
 * @brief Free a linked list of links.
 *
 * @param links Head of the link list.
 */
void free_links(Link* links);

/**
 * @brief Create a new text span.
 *
 * @param text The text content (will be copied).
 * @param style The styling flags.
 * @param font_size The font size.
 * @param bbox The bounding box.
 * @return Newly allocated TextSpan or NULL on failure.
 */
TextSpan* create_text_span(const char* text, TextStyle style, float font_size, fz_rect bbox);

/**
 * @brief Create a new link.
 *
 * @param text The display text (will be copied).
 * @param uri The target URI (will be copied).
 * @param bbox The bounding box.
 * @return Newly allocated Link or NULL on failure.
 */
Link* create_link(const char* text, const char* uri, fz_rect bbox);

#endif // BLOCK_INFO_H
