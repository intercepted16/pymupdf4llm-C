#include "platform_compat.h"

#include "page_extractor.h"

#include <errno.h>
#include "mupdf/fitz.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

/**
 * @brief Ensure the destination directory exists prior to writing JSON files.
 */
static int ensure_directory(const char* dir)
{
    if (!dir || strlen(dir) == 0)
        return 0;
    struct stat st;
    if (stat(dir, &st) == 0)
    {
        if (S_ISDIR(st.st_mode))
            return 0;
        fprintf(stderr, "Error: %s exists and is not a directory\n", dir);
        return -1;
    }
#if defined(_WIN32)
    if (_mkdir(dir) != 0)
#else
    if (mkdir(dir, 0775) != 0 && errno != EEXIST)
#endif
    {
        fprintf(stderr, "Error: cannot create directory %s (%s)\n", dir, strerror(errno));
        return -1;
    }
    return 0;
}

/**
 * @brief Extract an entire document by iterating every page.
 */
static int extract_document(const char* pdf_path, const char* output_dir)
{
    if (!pdf_path)
        return -1;

    fz_context* ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
    if (!ctx)
    {
        fprintf(stderr, "Error: cannot allocate MuPDF context\n");
        return -1;
    }

    fz_document* doc = NULL;
    int status = 0;

    fz_try(ctx)
    {
        fz_register_document_handlers(ctx);
        doc = fz_open_document(ctx, pdf_path);
        if (!doc)
        {
            fz_throw(ctx, FZ_ERROR_GENERIC, "cannot open document");
        }

        if (ensure_directory(output_dir) != 0)
        {
            fz_throw(ctx, FZ_ERROR_GENERIC, "cannot prepare output directory");
        }

        int page_count = fz_count_pages(ctx, doc);
        for (int i = 0; i < page_count; ++i)
        {
            if (extract_page_blocks(ctx, doc, i, output_dir, NULL, 0) != 0)
            {
                fprintf(stderr, "Warning: failed to extract page %d\n", i + 1);
            }
        }
    }
    fz_always(ctx)
    {
        if (doc)
            fz_drop_document(ctx, doc);
        fz_drop_context(ctx);
    }
    fz_catch(ctx)
    {
        status = -1;
    }

    if (status != 0)
    {
        printf("{\"error\":\"cannot_open_document\"}");
    }

    return status;
}

extern EXPORT int pdf_to_json(const char* pdf_path, const char* output_dir)
{
    if (!pdf_path)
        return -1;
    const char* out = output_dir ? output_dir : ".";
    return extract_document(pdf_path, out);
}

#ifndef NOLIB_MAIN
int main(int argc, char** argv)
{
    if (argc < 2 || argc > 3)
    {
        fprintf(stderr, "Usage: %s <input.pdf> [output_dir]\n", argv[0]);
        return 1;
    }

    const char* pdf_path = argv[1];
    const char* output_dir = (argc >= 3) ? argv[2] : ".";

    int rc = extract_document(pdf_path, output_dir);
    return (rc == 0) ? 0 : 1;
}
#endif
