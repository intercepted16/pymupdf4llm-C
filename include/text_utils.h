#ifndef TEXT_UTILS_H
#define TEXT_UTILS_H

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Normalize whitespace and newline handling within a text block.
 *
 * @param input Original text (may be NULL).
 * @return Newly allocated normalized string or NULL on allocation failure.
 */
char* normalize_text(const char* input);

/**
 * @brief Canonicalize leading bullet markers to "- " format.
 *
 * @param text Input text (may be NULL).
 * @return Newly allocated string with normalized bullets or NULL on failure.
 */
char* normalize_bullets(const char* text);

/**
 * @brief Determine whether a string ends with sentence punctuation.
 *
 * @param text Input string.
 * @return true if the last non-space character is ., :, ;, ?, or !.
 */
bool ends_with_punctuation(const char* text);

/**
 * @brief Determine whether all alphabetic runes in a string are uppercase.
 *
 * @param text Input string.
 * @return true if every alphabetic character is uppercase and at least one exists.
 */
bool is_all_caps(const char* text);

/**
 * @brief Check if a string starts with common heading keywords.
 *
 * @param text Input string.
 * @return true if a known keyword prefix is detected.
 */
bool starts_with_heading_keyword(const char* text);

/**
 * @brief Detect numeric or outline-style heading prefixes (e.g. "1.2" or "I.").
 *
 * @param text Input string.
 * @return true if the prefix resembles a structured heading label.
 */
bool starts_with_numeric_heading(const char* text);

/**
 * @brief Identify bullet or numbered list prefixes.
 *
 * @param text Input string.
 * @return true if the string appears to start with a bullet marker.
 */
bool starts_with_bullet(const char* text);

/**
 * @brief Map a bold ratio to the textual weight label expected in JSON output.
 *
 * @param ratio Fraction of characters detected as bold.
 * @return "bold" when the ratio is >= 0.6, otherwise "normal".
 */
const char* font_weight_from_ratio(float ratio);

/**
 * @brief Count Unicode scalars in a UTF-8 encoded string.
 *
 * @param text UTF-8 input string.
 * @return Number of Unicode codepoints.
 */
size_t count_unicode_chars(const char* text);

#endif // TEXT_UTILS_H
