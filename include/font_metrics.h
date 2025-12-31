#ifndef FONT_METRICS_H
#define FONT_METRICS_H

#include <stdbool.h>
#include <stddef.h>

#include "mupdf/fitz.h"

/** Number of histogram buckets used to approximate font size distribution. */
#define FONT_BIN_COUNT 512

/**
 * @brief Running statistics for font size usage within a page.
 */
typedef struct
{
    int counts[FONT_BIN_COUNT]; /**< Frequency counts for rounded point sizes. */
    double total_size;          /**< Sum of all recorded font sizes. */
    int total_chars;            /**< Total number of characters measured. */
} FontStats;

/**
 * @brief Page-level metrics derived from @ref FontStats.
 */
typedef struct
{
    float body_font_size;   /**< Most common font size on the page. */
    float median_font_size; /**< Median font size across characters. */
} PageMetrics;

/**
 * @brief Reset the statistics structure to an empty state.
 *
 * @param stats Statistics instance to clear.
 */
void font_stats_reset(FontStats* stats);

/**
 * @brief Record a glyph font size into the histogram.
 *
 * @param stats Target statistics structure.
 * @param size Glyph size in points.
 */
void font_stats_add(FontStats* stats, float size);

/**
 * @brief Return the mode (most frequent) font size in points.
 *
 * @param stats Source statistics.
 * @return Mode font size or 12pt if no samples were recorded.
 */
float font_stats_mode(const FontStats* stats);

/**
 * @brief Return the median font size in points.
 *
 * @param stats Source statistics.
 * @return Median font size or 12pt if no samples were recorded.
 */
float font_stats_median(const FontStats* stats);

/**
 * @brief Compute derived page metrics used by downstream heuristics.
 *
 * @param stats Source statistics from @ref FontStats.
 * @return Page-level metrics structure.
 */
PageMetrics compute_page_metrics(const FontStats* stats);

/**
 * @brief Block-level font metrics collected during text analysis.
 */
typedef struct
{
    int total_chars;         /**< Total number of characters. */
    int bold_chars;          /**< Number of bold characters. */
    int italic_chars;        /**< Number of italic characters. */
    int mono_chars;          /**< Number of monospace characters. */
    int strikeout_chars;     /**< Number of strikeout characters. */
    int superscript_chars;   /**< Number of superscript characters. */
    float font_size_sum;     /**< Sum of all font sizes. */
    bool has_superscript;    /**< Whether block contains any superscript. */
} BlockFontMetrics;

/**
 * @brief Collect font statistics from all characters in a page.
 *
 * @param ctx MuPDF context.
 * @param textpage Structured text page to analyze.
 * @param stats Output statistics structure.
 */
void collect_font_stats(fz_context* ctx, fz_stext_page* textpage, FontStats* stats);

/**
 * @brief Compute font metrics for a specific text block.
 *
 * @param ctx MuPDF context.
 * @param block Text block to analyze.
 * @param metrics Output metrics structure.
 */
void compute_block_font_metrics(fz_context* ctx, fz_stext_block* block, BlockFontMetrics* metrics);

#endif // FONT_METRICS_H
