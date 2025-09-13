#ifndef TABLE_EXTRACTION_H
#define TABLE_EXTRACTION_H

#include <mupdf/fitz.h>

// Extract tables from a specific page and return them as markdown strings
// Returns array of markdown strings, one for each table found on the page
// count: will be set to the number of tables found
// The caller is responsible for freeing the returned strings and the array
char **extract_tables_as_markdown(fz_context *ctx, fz_document *doc, int page_number, int *count);

// Free the result returned by extract_tables_as_markdown
void free_table_markdown_array(char **table_markdowns, int count);

#endif // TABLE_EXTRACTION_H
