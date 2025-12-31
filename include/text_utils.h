#ifndef TEXT_UTILS_H
#define TEXT_UTILS_H

#include <stdbool.h>
#include <stddef.h>
#include <mupdf/fitz.h>

/**
 * @brief Normalize whitespace and newline handling within a text block.
 *
 * @param input Original text (may be NULL).
 * @return Newly allocated normalized string or NULL on allocation failure.
 */
char* normalize_text(const char* input);

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
 * @brief Identify numbered list prefixes (1., 2., a., etc.).
 *
 * @param text Input string.
 * @param out_prefix If not NULL, receives the prefix string (caller must free).
 * @return true if the string starts with a numbered list marker.
 */
bool starts_with_number(const char* text, char** out_prefix);

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

/**
 * @brief Extract text from a rectangle with proper spacing between characters.
 *
 * @param ctx MuPDF context.
 * @param page Text page structure.
 * @param rect Rectangle to extract text from.
 * @return Newly allocated string with properly spaced text or NULL on failure.
 */
char* extract_text_with_spacing(void* ctx, void* page, const void* rect);

/**
 * @brief Check if a character is a superscript based on its position.
 *
 * @param char_y0 The top Y coordinate of the character.
 * @param line_y0 The top Y coordinate of the line.
 * @param char_size The character size.
 * @return true if the character appears to be superscript.
 */
bool is_superscript_position(float char_y0, float line_y0, float char_size);

/**
 * @brief Check if a character is a subscript based on its position.
 *
 * @param char_y1 The bottom Y coordinate of the character.
 * @param line_y1 The bottom Y coordinate of the line.
 * @param char_size The character size.
 * @return true if the character appears to be subscript.
 */
bool is_subscript_position(float char_y1, float line_y1, float char_size);

/**
 * @brief Check if text has visible content (printable ASCII).
 *
 * @param text Input string.
 * @return true if text contains visible characters (ASCII 33-126).
 */
bool has_visible_content(const char* text);

bool is_lone_page_number(const char* text);

bool is_in_margin_area(fz_rect bbox, fz_rect page_bbox, float threshold_percent);

#endif // TEXT_UTILS_H
