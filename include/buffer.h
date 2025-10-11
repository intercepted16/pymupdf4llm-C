#ifndef BUFFER_H
#define BUFFER_H

#include <stddef.h>

/**
 * @brief Growable byte buffer used for JSON assembly.
 */
typedef struct
{
    char* data;      /**< Pointer to the underlying storage. */
    size_t length;   /**< Number of meaningful bytes stored. */
    size_t capacity; /**< Allocated size of @ref data in bytes. */
} Buffer;

/**
 * @brief Allocate a new buffer with an optional initial capacity.
 *
 * @param initial Desired starting capacity in bytes (0 selects a default).
 * @return Pointer to the allocated buffer or NULL on allocation failure.
 */
Buffer* buffer_create(size_t initial);

/**
 * @brief Release memory held by a buffer instance.
 *
 * @param buf Buffer instance created with @ref buffer_create.
 */
void buffer_destroy(Buffer* buf);

/**
 * @brief Append a NUL-terminated string to the buffer.
 *
 * @param buf Target buffer.
 * @param text String to append (must not be NULL).
 * @return 0 on success, -1 if resizing fails or inputs are invalid.
 */
int buffer_append(Buffer* buf, const char* text);

/**
 * @brief Append a single character to the buffer.
 *
 * @param buf Target buffer.
 * @param c Character to append.
 * @return 0 on success, -1 if resizing fails or inputs are invalid.
 */
int buffer_append_char(Buffer* buf, char c);

/**
 * @brief Append formatted text produced by printf-style formatting.
 *
 * @param buf Target buffer.
 * @param fmt printf-style format string.
 * @param ... Format arguments matching @p fmt.
 * @return 0 on success, -1 if resizing fails or inputs are invalid.
 */
int buffer_append_format(Buffer* buf, const char* fmt, ...);

/**
 * @brief Append a fixed number of bytes from a memory region.
 *
 * @param buf Target buffer.
 * @param data Source bytes (may be NULL if @p len is 0).
 * @param len Number of bytes to append.
 * @return 0 on success, -1 if resizing fails or inputs are invalid.
 */
int buffer_append_n(Buffer* buf, const char* data, size_t len);

#endif // BUFFER_H
