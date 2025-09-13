/*
 * Test program for improved table detection
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "include/improved_table_detection.h"
#include "include/table_extraction.h"

char **get_tables(const char *pdf_path, int page_number, int *found)
{
    *found = 0;
    if (!pdf_path || page_number < 0 || !found)
        return NULL;
    fz_context *ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
    if (!ctx)
        return NULL;
    fz_document *doc = NULL;
    char **result = NULL;

    fz_try(ctx)
    {
        fz_register_document_handlers(ctx);
        doc = fz_open_document(ctx, pdf_path);
        result = extract_tables_as_markdown(ctx, doc, page_number, found);
    }
    fz_catch(ctx)
    {
        if (result) {
            free_table_markdown_array(result, *found);
        }
        result = NULL;
        *found = 0;
    }
    fz_always(ctx)
    {
        if (doc)
            fz_drop_document(ctx, doc);
        fz_drop_context(ctx);
    }
    fz_catch(ctx) { /* Ignore errors in cleanup */ }

    return result;
}

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s <pdf_path> <page_number>\n", argv[0]);
        return 1;
    }

    const char *pdf_path = argv[1];
    int page_number = atoi(argv[2]);

    int tables_found = 0;
    char **tables = get_tables(pdf_path, page_number, &tables_found);
    if (!tables || tables_found == 0)
    {
        printf("No tables found on page %d of %s\n", page_number, pdf_path);
        return 0;
    }
    printf("Found %d table(s) on page %d of %s:\n\n", tables_found, page_number, pdf_path);
    for (int i = 0; i < tables_found; i++)
    {
        if (tables[i])
        {
            printf("Table %d:\n%s\n\n", i + 1, tables[i]);
            free(tables[i]);
        }
    }
    free(tables);

    return 0;
}
