#include "font_metrics.h"

#include <math.h>
#include <string.h>

void font_stats_reset(FontStats* stats)
{
    if (!stats)
        return;
    memset(stats, 0, sizeof(FontStats));
}

void font_stats_add(FontStats* stats, float size)
{
    if (!stats)
        return;
    if (size <= 0.0f)
        return;
    int idx = (int)lroundf(size);
    if (idx < 0)
        idx = 0;
    if (idx >= FONT_BIN_COUNT)
        idx = FONT_BIN_COUNT - 1;
    stats->counts[idx] += 1;
    stats->total_size += size;
    stats->total_chars += 1;
}

float font_stats_mode(const FontStats* stats)
{
    if (!stats || stats->total_chars == 0)
        return 12.0f;
    int best_idx = 0;
    int best_count = 0;
    for (int i = 0; i < FONT_BIN_COUNT; ++i)
    {
        if (stats->counts[i] > best_count)
        {
            best_count = stats->counts[i];
            best_idx = i;
        }
    }
    if (best_idx == 0 && best_count == 0)
    {
        return (float)(stats->total_size / (stats->total_chars ? stats->total_chars : 1));
    }
    return (float)best_idx;
}

float font_stats_median(const FontStats* stats)
{
    if (!stats || stats->total_chars == 0)
        return 12.0f;
    int midpoint = stats->total_chars / 2;
    int cumulative = 0;
    for (int i = 0; i < FONT_BIN_COUNT; ++i)
    {
        cumulative += stats->counts[i];
        if (cumulative > midpoint)
            return (float)i;
    }
    return (float)(stats->total_size / (stats->total_chars ? stats->total_chars : 1));
}

PageMetrics compute_page_metrics(const FontStats* stats)
{
    PageMetrics metrics;
    metrics.body_font_size = font_stats_mode(stats);
    metrics.median_font_size = font_stats_median(stats);
    if (metrics.body_font_size <= 0.0f)
        metrics.body_font_size = 12.0f;
    if (metrics.median_font_size <= 0.0f)
        metrics.median_font_size = metrics.body_font_size;
    return metrics;
}
