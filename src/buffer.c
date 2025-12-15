#include "buffer.h"

#include <limits.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int buffer_reserve(Buffer* buf, size_t needed)
{
    if (!buf)
        return -1;
    if (needed <= buf->capacity)
        return 0;

    size_t new_capacity = buf->capacity;
    while (new_capacity < needed)
    {
        if (new_capacity > (SIZE_MAX / 2))
        {
            new_capacity = needed;
            break;
        }
        new_capacity *= 2;
    }

    char* tmp = (char*)realloc(buf->data, new_capacity);
    if (!tmp)
        return -1;
    buf->data = tmp;
    buf->capacity = new_capacity;
    return 0;
}

Buffer* buffer_create(size_t initial)
{
    Buffer* buf = (Buffer*)calloc(1, sizeof(Buffer));
    if (!buf)
        return NULL;
    buf->capacity = initial > 0 ? initial : 256;
    buf->data = (char*)malloc(buf->capacity);
    if (!buf->data)
    {
        free(buf);
        return NULL;
    }
    buf->data[0] = '\0';
    return buf;
}

void buffer_destroy(Buffer* buf)
{
    if (!buf)
        return;
    free(buf->data);
    free(buf);
}

int buffer_append(Buffer* buf, const char* text)
{
    if (!buf || !text)
        return -1;
    size_t add = strlen(text);
    size_t needed = buf->length + add + 1;
    if (buffer_reserve(buf, needed) != 0)
        return -1;
    memcpy(buf->data + buf->length, text, add + 1);
    buf->length += add;
    return 0;
}

int buffer_append_char(Buffer* buf, char c)
{
    if (!buf)
        return -1;
    size_t needed = buf->length + 2;
    if (buffer_reserve(buf, needed) != 0)
        return -1;
    buf->data[buf->length++] = c;
    buf->data[buf->length] = '\0';
    return 0;
}

int buffer_append_format(Buffer* buf, const char* fmt, ...)
{
    if (!buf || !fmt)
        return -1;
    va_list args;
    va_start(args, fmt);
    va_list copy;
    va_copy(copy, args);
    int len = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (len < 0)
    {
        va_end(args);
        return -1;
    }
    size_t needed = buf->length + (size_t)len + 1;
    if (buffer_reserve(buf, needed) != 0)
    {
        va_end(args);
        return -1;
    }
    vsnprintf(buf->data + buf->length, (size_t)len + 1, fmt, args);
    va_end(args);
    buf->length += (size_t)len;
    return 0;
}

int buffer_append_n(Buffer* buf, const char* data, size_t len)
{
    if (!buf || (!data && len > 0))
        return -1;

    if (buffer_reserve(buf, buf->length + len + 1) != 0)
    {
        return -1;
    }

    if (len > 0)
    {
        memcpy(buf->data + buf->length, data, len);
        buf->length += len;
    }

    buf->data[buf->length] = '\0';
    return 0;
}

void buffer_clear(Buffer* buf)
{
    if (!buf)
        return;
    buf->length = 0;
    if (buf->data)
        buf->data[0] = '\0';
}
