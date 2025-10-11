#include "platform_compat.h"

#include "text_utils.h"

#include "buffer.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

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
