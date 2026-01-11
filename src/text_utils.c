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
}

bool starts_with_bullet(const char* text)
{
    if (!text)
        return false;

    while (*text == ' ' || *text == '\t')
        text++;

    if (!*text)
        return false;

    int rune;
    int len = fz_chartorune(&rune, text);

    if (is_bullet_rune(rune))
    {
        if (text[len] == ' ' || text[len] == '\t' || text[len] == '\0' || text[len] == '\n')
            return true;
    }

    const char* p = text;

    if (*p == '(')
        p++;

    if (isdigit((unsigned char)*p))
    {
        while (isdigit((unsigned char)*p))
            p++;

        if ((*p == '.' || *p == ')') && (p[1] == ' ' || p[1] == '\t' || p[1] == '\n' || p[1] == '\0'))
            return true;
    }

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
    float threshold = char_size * 0.3f;
}

bool is_footnote_reference(int rune, float char_size, float prev_char_size, int prev_rune, bool prev_was_footnote)
{
    
    if (rune < '0' || rune > '9')
        return false;
    
    if (prev_was_footnote && prev_rune >= '0' && prev_rune <= '9')
        return true;
    
    if (prev_rune == 0 || prev_rune == ' ' || prev_rune == '\t' || prev_rune == '\n' ||
        prev_rune == '(' || prev_rune == '[' || prev_rune == '$' || prev_rune == '#')
        return false;
    
    if (prev_char_size > 0 && char_size < prev_char_size * 0.80f)
        return true;
    
    return false;
}

bool is_subscript_position(float char_y1, float line_y1, float char_size)
{
    float threshold = char_size * 0.3f;
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
    int prev_char = 0;

    for (fz_stext_block* block = page->first_block; block; block = block->next)
    {
        if (block->type != FZ_STEXT_BLOCK_TEXT)
            continue;

        for (fz_stext_line* line = block->u.t.first_line; line; line = line->next)
        {
            if (line->bbox.y1 < rect.y0 || line->bbox.y0 > rect.y1)
                continue;

            for (fz_stext_char* ch = line->first_char; ch; ch = ch->next)
            {
                fz_rect char_box = fz_rect_from_quad(ch->quad);

                float char_center_x = (char_box.x0 + char_box.x1) / 2.0f;
                float char_center_y = (char_box.y0 + char_box.y1) / 2.0f;

                float margin = 2.0f;
                if (char_center_x < rect.x0 - margin || char_center_x > rect.x1 + margin)
                    continue;
                if (char_center_y < rect.y0 - margin || char_center_y > rect.y1 + margin)
                    continue;
                if (ch->c == 0 || ch->c == 0xFEFF)
                    continue;

                if (buf->length > 0)
                {
                    float y_diff = fabsf(char_box.y0 - prev_y);
                    float x_gap = char_box.x0 - prev_x1;

                    float x_tolerance = ch->size * 0.5f;
                    if (x_tolerance < 3.0f)
                        x_tolerance = 3.0f;

                    float y_tolerance = ch->size * 0.3f;
                    if (y_tolerance < 2.0f)
                        y_tolerance = 2.0f;


                    int is_punct_or_digit =
                        (ch->c == '.' || ch->c == ',' || ch->c == '$' || ch->c == '%' || ch->c == ':' || ch->c == ';' ||
                         ch->c == '\'' || ch->c == '"' || ch->c == '-' || ch->c == '(' || ch->c == ')' ||
                         (ch->c >= '0' && ch->c <= '9'));
                    int prev_is_punct_or_digit =
                        (prev_char == '.' || prev_char == ',' || prev_char == '$' || prev_char == '%' ||
                         prev_char == ':' || prev_char == ';' || prev_char == '\'' || prev_char == '"' ||
                         prev_char == '-' || prev_char == '(' || prev_char == ')' ||
                         (prev_char >= '0' && prev_char <= '9'));

                    if (is_punct_or_digit || prev_is_punct_or_digit)
                    {
                        if (x_tolerance < 8.0f)
                            x_tolerance = 8.0f;
                        if (y_tolerance < 10.0f)
                            y_tolerance = 10.0f;
                    }

                    if (y_diff > y_tolerance)
                    {
                        buffer_append_char(buf, ' ');
                    }
                    else if (x_gap > x_tolerance)
                    {
                        buffer_append_char(buf, ' ');
                    }
                }

                if (ch->c < 0x80)
                {
                    buffer_append_char(buf, (char)ch->c);
                }
                else
                {
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

    while (*text == ' ' || *text == '\t')
        text++;

    const char* start = text;
    int digit_count = 0;
    while (*text >= '0' && *text <= '9')
    {
        digit_count++;
        text++;
    }

    while (*text == ' ' || *text == '\t')
        text++;

    return digit_count > 0 && digit_count <= 4 && *text == '\0' && (text - start) == digit_count;
}

bool is_in_margin_area(fz_rect bbox, fz_rect page_bbox, float threshold_percent)
{
    float page_height = page_bbox.y1 - page_bbox.y0;
    float threshold = page_height * threshold_percent;

    if (bbox.y0 < page_bbox.y0 + threshold)
        return true;

    if (bbox.y1 > page_bbox.y1 - threshold)
        return true;

    return false;
}