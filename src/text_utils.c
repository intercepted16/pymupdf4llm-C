#include "platform_compat.h"

#include "text_utils.h"

#include "buffer.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <mupdf/fitz.h>

bool has_visible_content(const char* text)
{
    if (!text)
        return false;

    for (const char* p = text; *p; p++)
    {
        unsigned char ch = (unsigned char)*p;
        if (ch >= 33 && ch <= 126)
            return true;
    }
    return false;
}

char* normalize_text(const char* input)
{
    if (!input)
        return NULL;
    size_t len = strlen(input);
    char* out = (char*)malloc(len + 1);
    if (!out)
        return NULL;

    size_t write = 0;
    bool last_space = true;
    bool last_was_newline = false;

    for (size_t i = 0; i < len; ++i)
    {
        unsigned char c = (unsigned char)input[i];
        if (c == '\r')
            continue;

        if (c == '\n')
        {
            if (write > 0 && out[write - 1] == ' ')
            {
                write -= 1;
            }
            if (!last_was_newline)
            {
                out[write++] = '\n';
            }
            last_space = true;
            last_was_newline = true;
            continue;
        }

        last_was_newline = false;

        if (c == '\t' || c == '\f' || c == '\v')
        {
            c = ' ';
        }

        if (isspace(c))
        {
            if (!last_space && write > 0)
            {
                out[write++] = ' ';
                last_space = true;
            }
            continue;
        }

        out[write++] = (char)c;
        last_space = false;
    }

    while (write > 0 && (out[write - 1] == ' ' || out[write - 1] == '\n'))
    {
        write--;
    }

    out[write] = '\0';
    return out;
}

bool ends_with_punctuation(const char* text)
{
    if (!text)
        return false;
    size_t len = strlen(text);
    while (len > 0 && isspace((unsigned char)text[len - 1]))
    {
        len--;
    }
    if (len == 0)
        return false;
    char c = text[len - 1];
    return c == '.' || c == ':' || c == ';' || c == '?' || c == '!';
}

bool is_all_caps(const char* text)
{
    if (!text)
        return false;
    bool has_alpha = false;
    for (const unsigned char* p = (const unsigned char*)text; *p; ++p)
    {
        if (isalpha(*p))
        {
            has_alpha = true;
            if (!isupper(*p))
                return false;
        }
    }
    return has_alpha;
}

bool starts_with_heading_keyword(const char* text)
{
    static const char* keywords[] = {"appendix", "chapter", "section", "heading", "article", "part", NULL};

    while (*text == ' ')
        text++;

    for (const char** keyword = keywords; *keyword; ++keyword)
    {
        size_t len = strlen(*keyword);
        if (strncasecmp(text, *keyword, len) == 0)
        {
            if (text[len] == '\0' || isspace((unsigned char)text[len]) || text[len] == ':' || text[len] == '-')
            {
                return true;
            }
        }
    }

    return false;
}

bool starts_with_numeric_heading(const char* text)
{
    while (*text == ' ')
        text++;

    const char* p = text;
    bool seen_digit = false;
    bool seen_separator = false;

    while (*p)
    {
        if (isdigit((unsigned char)*p))
        {
            seen_digit = true;
            p++;
            continue;
        }
        if (*p == '.' || *p == ')' || *p == ':' || *p == '-')
        {
            seen_separator = true;
            p++;
            continue;
        }
        break;
    }

    if (!seen_digit)
        return false;
    if (!seen_separator)
        return false;

    if (*p == '\0')
        return false;
    if (isspace((unsigned char)*p))
        return true;
    if (*p == '-' || *p == ')')
        return true;

    return false;
}

bool is_bullet_rune(int rune)
{
    return (rune == '-' || rune == '*' || rune == 'o' || rune == '+' || rune == '>' || rune == 0x2022 || // •
            rune == 0x2023 ||                                                                            // ‣
            rune == 0x2043 ||                                                                            // ⁃
            rune == 0x25AA ||                                                                            // ▪
            rune == 0x25AB ||                                                                            // ▫
            rune == 0x25B6 ||                                                                            // ▶
            rune == 0x25C6 ||                                                                            // ◆
            rune == 0x25CB ||                                                                            // ○
            rune == 0x25CF ||                                                                            // ●
            rune == 0x25E6 ||                                                                            // ◦
            rune == 0x00B7 || // · (Middle dot)
            rune == 0x2027 || // ‧ (Hyphenation point)
            rune == 0xF0B7 || //  (Wingdings bullet)
            rune == 0xF076 || // ❖ (Wingdings diamond)
            rune == 0xF0B6 || // bullets identified by user
            rune == 0xF0D8 || // Wingdings arrow
            rune == 0xF0D7 || // Wingdings arrow
            rune == 0xF0A3 || // Wingdings check
            rune == 0x2713 || // ✓
            rune == 0x2714);  // ✔
}

bool starts_with_bullet(const char* text)
{
    if (!text)
        return false;

    // Skip leading horizontal whitespace
    while (*text == ' ' || *text == '\t')
        text++;

    if (!*text)
        return false;

    // Multi-byte markers (Unicode)
    int rune;
    int len = fz_chartorune(&rune, text);

    if (is_bullet_rune(rune))
    {
        // Require a space or similar boundary after a bullet
        if (text[len] == ' ' || text[len] == '\t' || text[len] == '\0' || text[len] == '\n')
            return true;
    }

    // Numbered list detection: "1. ", "1) ", " (1) "
    const char* p = text;

    // Handle leading parenthesis if present: "(1) "
    if (*p == '(')
        p++;

    if (isdigit((unsigned char)*p))
    {
        while (isdigit((unsigned char)*p))
            p++;

        // Match "1. ", "1) "
        if ((*p == '.' || *p == ')') && (p[1] == ' ' || p[1] == '\t' || p[1] == '\n' || p[1] == '\0'))
            return true;
    }

    // Lowercase alpha list: "a. ", "b. "
    if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z'))
    {
        if ((p[1] == '.' || p[1] == ')') && (p[2] == ' ' || p[2] == '\t' || p[2] == '\n' || p[2] == '\0'))
            return true;
    }

    return false;
}

bool starts_with_number(const char* text, char** out_prefix)
{
    if (!text)
        return false;

    const char* p = text;
    while (*p == ' ' || *p == '\t')
        p++;

    const char* start = p;

    // Check for digit sequence: 1., 2., 10., etc.
    if (isdigit((unsigned char)*p))
    {
        while (isdigit((unsigned char)*p))
            p++;
        if ((*p == '.' || *p == ')') && (p[1] == ' ' || p[1] == '\t'))
        {
            if (out_prefix)
            {
                size_t len = (p - start) + 1;
                *out_prefix = (char*)malloc(len + 1);
                if (*out_prefix)
                {
                    memcpy(*out_prefix, start, len);
                    (*out_prefix)[len] = '\0';
                }
            }
            return true;
        }
        return false;
    }

    // Check for letter sequence: a., b., A., B., etc.
    if (isalpha((unsigned char)*p) && (p[1] == '.' || p[1] == ')') && (p[2] == ' ' || p[2] == '\t'))
    {
        if (out_prefix)
        {
            *out_prefix = (char*)malloc(3);
            if (*out_prefix)
            {
                (*out_prefix)[0] = *p;
                (*out_prefix)[1] = p[1];
                (*out_prefix)[2] = '\0';
            }
        }
        return true;
    }

    // Check for Roman numerals: i., ii., iii., I., II., III., etc.
    if (*p == 'i' || *p == 'v' || *p == 'x' || *p == 'I' || *p == 'V' || *p == 'X')
    {
        const char* roman_start = p;
        while (*p == 'i' || *p == 'v' || *p == 'x' || *p == 'I' || *p == 'V' || *p == 'X')
            p++;
        if ((*p == '.' || *p == ')') && (p[1] == ' ' || p[1] == '\t'))
        {
            if (out_prefix)
            {
                size_t len = (p - roman_start) + 1;
                *out_prefix = (char*)malloc(len + 1);
                if (*out_prefix)
                {
                    memcpy(*out_prefix, roman_start, len);
                    (*out_prefix)[len] = '\0';
                }
            }
            return true;
        }
    }

    return false;
}

bool is_superscript_position(float char_y0, float line_y0, float char_size)
{
    // In PDF coordinates, y increases downward
    // A superscript character's top (y0) is higher (smaller value) than the line's top
    // Use char_size to avoid false positives from minor positioning differences
    float threshold = char_size * 0.3f;
    return (line_y0 - char_y0) > threshold; // char_y0 is noticeably above line_y0
}

bool is_subscript_position(float char_y1, float line_y1, float char_size)
{
    // In PDF coordinates, y increases downward
    // A subscript character's bottom (y1) is lower (larger value) than the line's bottom
    float threshold = char_size * 0.3f;
    return (char_y1 - line_y1) > threshold; // char_y1 is noticeably below line_y1
}

const char* font_weight_from_ratio(float ratio)
{
    return (ratio >= 0.6f) ? "bold" : "normal";
}

size_t count_unicode_chars(const char* text)
{
    if (!text)
        return 0;
    size_t count = 0;
    const unsigned char* p = (const unsigned char*)text;
    while (*p)
    {
        if ((*p & 0xC0) != 0x80)
        {
            count++;
        }
        p++;
    }
    return count;
}

// fz_copy_rect doesn't extract spaces properly
char* extract_text_with_spacing(void* ctx_ptr, void* page_ptr, const void* rect_ptr)
{
    fz_context* ctx = (fz_context*)ctx_ptr;
    fz_stext_page* page = (fz_stext_page*)page_ptr;
    fz_rect rect = *(fz_rect*)rect_ptr;

    if (!ctx || !page)
        return NULL;

    Buffer* buf = buffer_create(256);
    if (!buf)
        return NULL;

    float prev_x1 = -1000.0f;
    float prev_y = -1000.0f;
    int prev_char = 0; // Track previous character for context

    // Walk through all blocks and lines
    for (fz_stext_block* block = page->first_block; block; block = block->next)
    {
        if (block->type != FZ_STEXT_BLOCK_TEXT)
            continue;

        for (fz_stext_line* line = block->u.t.first_line; line; line = line->next)
        {
            // Check if line intersects with rectangle
            if (line->bbox.y1 < rect.y0 || line->bbox.y0 > rect.y1)
                continue;

            // Extract characters with proper spacing
            for (fz_stext_char* ch = line->first_char; ch; ch = ch->next)
            {
                fz_rect char_box = fz_rect_from_quad(ch->quad);

                // Use center point of character for containment check (more lenient)
                float char_center_x = (char_box.x0 + char_box.x1) / 2.0f;
                float char_center_y = (char_box.y0 + char_box.y1) / 2.0f;

                // Check if character center is within rect (with small margin)
                float margin = 2.0f;
                if (char_center_x < rect.x0 - margin || char_center_x > rect.x1 + margin)
                    continue;
                if (char_center_y < rect.y0 - margin || char_center_y > rect.y1 + margin)
                    continue;
                if (ch->c == 0 || ch->c == 0xFEFF)
                    continue; // Skip null and BOM

                // Check if we need to add space before this character (based on gap detection)
                if (buf->length > 0)
                {
                    float y_diff = fabsf(char_box.y0 - prev_y);
                    float x_gap = char_box.x0 - prev_x1;

                    // Base tolerance on character size
                    float x_tolerance = ch->size * 0.5f;
                    if (x_tolerance < 3.0f)
                        x_tolerance = 3.0f;

                    // Y tolerance needs to be more generous due to FZ_STEXT_ACCURATE_BBOXES
                    // Punctuation like '.' and ',' have different baseline positions
                    float y_tolerance = ch->size * 0.8f; // More generous for Y

                    // Be more lenient for punctuation, digits, and currency symbols
                    // These often have tight kerning or baseline differences
                    int is_punct_or_digit =
                        (ch->c == '.' || ch->c == ',' || ch->c == '$' || ch->c == '%' || ch->c == ':' || ch->c == ';' ||
                         ch->c == '\'' || ch->c == '"' || ch->c == '-' || ch->c == '(' || ch->c == ')' ||
                         (ch->c >= '0' && ch->c <= '9'));
                    int prev_is_punct_or_digit =
                        (prev_char == '.' || prev_char == ',' || prev_char == '$' || prev_char == '%' ||
                         prev_char == ':' || prev_char == ';' || prev_char == '\'' || prev_char == '"' ||
                         prev_char == '-' || prev_char == '(' || prev_char == ')' ||
                         (prev_char >= '0' && prev_char <= '9'));

                    // Increase tolerance significantly if either character is punctuation/digit
                    if (is_punct_or_digit || prev_is_punct_or_digit)
                    {
                        x_tolerance = ch->size * 1.5f; // Much higher tolerance for X
                        y_tolerance = ch->size * 1.5f; // Much higher tolerance for Y
                        if (x_tolerance < 8.0f)
                            x_tolerance = 8.0f;
                        if (y_tolerance < 10.0f)
                            y_tolerance = 10.0f;
                    }

                    // New line if Y position changed significantly
                    if (y_diff > y_tolerance)
                    {
                        buffer_append_char(buf, ' ');
                    }
                    // Add space if there's a gap larger than tolerance
                    else if (x_gap > x_tolerance)
                    {
                        buffer_append_char(buf, ' ');
                    }
                }

                // Append the character
                if (ch->c < 0x80)
                {
                    buffer_append_char(buf, (char)ch->c);
                }
                else
                {
                    // UTF-8 encoding
                    char utf8[8];
                    int len = fz_runetochar(utf8, ch->c);
                    utf8[len] = '\0';
                    buffer_append(buf, utf8);
                }

                prev_x1 = char_box.x1;
                prev_y = char_box.y0;
                prev_char = ch->c;
            }
        }
    }

    char* result = buf->length > 0 ? strdup(buf->data) : strdup("");
    buffer_destroy(buf);
    return result;
}

char* trim_whitespace(char* text)
{
    if (!text)
        return text;

    while (*text && isspace((unsigned char)*text))
    {
        text++;
    }

    if (*text == '\0')
        return text;

    char* end = text + strlen(text) - 1;
    while (end > text && isspace((unsigned char)*end))
    {
        end--;
    }

    *(end + 1) = '\0';

    return text;
}

bool is_lone_page_number(const char* text)
{
    if (!text)
        return false;

    // Skip leading whitespace
    while (*text == ' ' || *text == '\t')
        text++;

    // Count digits
    const char* start = text;
    int digit_count = 0;
    while (*text >= '0' && *text <= '9')
    {
        digit_count++;
        text++;
    }

    // Skip trailing whitespace
    while (*text == ' ' || *text == '\t')
        text++;

    // Must be only digits (1-4 digits typical for page numbers) and nothing else
    return digit_count > 0 && digit_count <= 4 && *text == '\0' && (text - start) == digit_count;
}

// Check if block is in top or bottom margin area
bool is_in_margin_area(fz_rect bbox, fz_rect page_bbox, float threshold_percent)
{
    float page_height = page_bbox.y1 - page_bbox.y0;
    float threshold = page_height * threshold_percent;

    // Top margin
    if (bbox.y0 < page_bbox.y0 + threshold)
        return true;

    // Bottom margin
    if (bbox.y1 > page_bbox.y1 - threshold)
        return true;

    return false;
}