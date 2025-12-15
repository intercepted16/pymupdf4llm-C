#include "platform_compat.h"

#include "text_utils.h"

#include "buffer.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <mupdf/fitz.h>

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

static size_t list_bullet_prefix_len(const char* line, size_t line_len)
{
    if (!line || line_len == 0)
        return 0;

    size_t idx = 0;
    while (idx < line_len && (line[idx] == ' ' || line[idx] == '\t'))
        idx++;

    static const char* bullets[] = {"-", "•", "o", "*", "·", "�", "‣", "●", "–", NULL};

    for (int i = 0; bullets[i]; ++i)
    {
        size_t blen = strlen(bullets[i]);
        if (blen == 0 || blen > line_len - idx)
            continue;
        if (strncmp(line + idx, bullets[i], blen) == 0)
        {
            size_t pos = idx + blen;
            while (pos < line_len && (line[pos] == ' ' || line[pos] == '\t'))
                pos++;
            return pos;
        }
    }

    if (idx < line_len && isdigit((unsigned char)line[idx]))
    {
        size_t pos = idx;
        while (pos < line_len && isdigit((unsigned char)line[pos]))
            pos++;
        if (pos < line_len && (line[pos] == '.' || line[pos] == ')' || line[pos] == '-'))
        {
            pos++;
            while (pos < line_len && (line[pos] == ' ' || line[pos] == '\t'))
                pos++;
            return pos;
        }
    }
    else if (idx + 1 < line_len && isalpha((unsigned char)line[idx]) && (line[idx + 1] == '.' || line[idx + 1] == ')'))
    {
        size_t pos = idx + 2;
        while (pos < line_len && (line[pos] == ' ' || line[pos] == '\t'))
            pos++;
        return pos;
    }

    return 0;
}

char* normalize_bullets(const char* text)
{
    if (!text)
        return NULL;

    size_t text_len = strlen(text);
    Buffer* out = buffer_create(text_len + 16);
    if (!out)
        return NULL;

    const char* cursor = text;
    bool input_had_trailing_newline = text_len > 0 && text[text_len - 1] == '\n';
    bool changed = false;

    while (*cursor)
    {
        const char* line_end = strchr(cursor, '\n');
        size_t line_len = line_end ? (size_t)(line_end - cursor) : strlen(cursor);

        size_t skip = list_bullet_prefix_len(cursor, line_len);
        if (skip > 0)
        {
            buffer_append(out, "- ");
            buffer_append_n(out, cursor + skip, line_len - skip);
            changed = true;
        }
        else
        {
            buffer_append_n(out, cursor, line_len);
        }

        if (line_end)
        {
            buffer_append_char(out, '\n');
            cursor = line_end + 1;
        }
        else
        {
            cursor += line_len;
        }
    }

    if (!input_had_trailing_newline && out->length > 0 && out->data[out->length - 1] == '\n')
    {
        out->length -= 1;
        out->data[out->length] = '\0';
    }

    char* result = changed ? strdup(out->data) : strdup(text);
    buffer_destroy(out);
    return result;
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

bool starts_with_bullet(const char* text)
{
    if (!text)
        return false;
    while (*text == ' ')
        text++;
    if (*text == '-' && text[1] == ' ')
        return true;
    if (*text == '*' && text[1] == ' ')
        return true;
    if ((unsigned char)text[0] == 0xE2 && (unsigned char)text[1] == 0x80 && (unsigned char)text[2] == 0xA2 &&
        text[3] == ' ')
    {
        return true;
    }
    if (isdigit((unsigned char)*text))
    {
        const char* p = text;
        while (isdigit((unsigned char)*p))
            p++;
        if ((*p == '.' || *p == ')') && p[1] == ' ')
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

int get_indent_level(const char* text, float base_indent)
{
    if (!text || base_indent <= 0)
        return 0;

    int spaces = 0;
    while (*text == ' ')
    {
        spaces++;
        text++;
    }
    while (*text == '\t')
    {
        spaces += 4; // Treat tab as 4 spaces
        text++;
    }

    // Calculate indent level (each level is typically 2-4 spaces)
    float indent_width = base_indent > 0 ? base_indent : 4.0f;
    return (int)(spaces / indent_width);
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
                if (char_box.x0 < rect.x0 || char_box.x1 > rect.x1)
                    continue;
                if (char_box.y0 < rect.y0 || char_box.y1 > rect.y1)
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
