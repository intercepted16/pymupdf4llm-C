#ifndef PAGE_EXTRACTOR_H
#define PAGE_EXTRACTOR_H

#include "mupdf/fitz.h"

#include "block_info.h"
#include "buffer.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Extract the blocks for a single page and write them as JSON to disk.
     *
     * @param ctx MuPDF context.
     * @param doc Opened MuPDF document.
     * @param page_number Zero-based page index to extract.
     * @param output_dir Destination directory for the JSON file.
     * @param error_buffer Optional buffer for error messages (unused).
     * @param error_buffer_size Size of @p error_buffer in bytes (unused).
     * @return 0 on success, -1 when extraction fails.
     */
    int extract_page_blocks(fz_context* ctx, fz_document* doc, int page_number, const char* output_dir,
                            char* error_buffer, size_t error_buffer_size);

    /**
     * @brief Extract a single page from a document and return JSON text.
     *
     * @param pdf_path Filesystem path to the PDF document.
     * @param page_number Zero-based page index to extract.
     * @return Newly allocated JSON string on success, NULL on failure.
     */
    EXPORT char* page_to_json_string(const char* pdf_path, int page_number);

#ifdef __cplusplus
}
#endif

#endif // PAGE_EXTRACTOR_H
