#include "platform_compat.h"
#include "page_extractor.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <dirent.h>
#include <time.h>

/* Ensure the output directory exists */
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

/* Recursively remove a directory and its contents */
static int remove_directory(const char* dir)
{
    DIR* d = opendir(dir);
    if (!d)
        return -1;

    struct dirent* entry;
    while ((entry = readdir(d)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        char path[512];
        snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
        struct stat st;
        if (stat(path, &st) == 0)
        {
            if (S_ISDIR(st.st_mode))
                remove_directory(path);
            else
                unlink(path);
        }
    }
    closedir(d);
    rmdir(dir);
    return 0;
}

/* Merge all page_*.json files into a single JSON array */
static int merge_json_files(const char* temp_dir, const char* output_file)
{
    FILE* out = fopen(output_file, "w");
    if (!out)
    {
        fprintf(stderr, "Error: cannot open output file %s\n", output_file);
        return -1;
    }

    fprintf(out, "[");
    int first_page = 1;

    DIR* d = opendir(temp_dir);
    if (!d)
    {
        fprintf(stderr, "Error: cannot open temp directory %s\n", temp_dir);
        fclose(out);
        return -1;
    }

    /* Collect and sort page files */
    struct dirent** entries = NULL;
    int count = scandir(temp_dir, &entries, NULL, alphasort);
    if (count < 0)
    {
        fprintf(stderr, "Error: failed to scan directory %s\n", temp_dir);
        closedir(d);
        fclose(out);
        return -1;
    }

    for (int i = 0; i < count; i++)
    {
        struct dirent* entry = entries[i];
        if (strncmp(entry->d_name, "page_", 5) != 0 || strcmp(entry->d_name + strlen(entry->d_name) - 5, ".json") != 0)
        {
            free(entry);
            continue;
        }

        /* Extract page number from filename (page_XXX.json) */
        int page_num = 0;
        if (sscanf(entry->d_name, "page_%d.json", &page_num) != 1)
        {
            free(entry);
            continue;
        }

        char path[512];
        snprintf(path, sizeof(path), "%s/%s", temp_dir, entry->d_name);
        FILE* in = fopen(path, "r");
        if (!in)
        {
            free(entry);
            continue;
        }

        if (!first_page)
            fprintf(out, ",");
        first_page = 0;

        /* Output page object with page number and data array */
        fprintf(out, "{\"page\":%d,\"data\":", page_num);

        /* Read the page JSON array and output it as the data field */
        fseek(in, 0, SEEK_END);
        long size = ftell(in);
        fseek(in, 0, SEEK_SET);
        char* content = malloc(size + 1);
        if (content)
        {
            fread(content, 1, size, in);
            content[size] = '\0';

            /* Output the entire JSON array as-is */
            fwrite(content, 1, size, out);
            free(content);
        }

        fprintf(out, "}");
        fclose(in);
        free(entry);
    }

    fprintf(out, "]");
    fclose(out);
    free(entries);
    closedir(d);
    return 0;
}

/* Extract a range of pages in one process */
static int extract_pages_range(const char* pdf_path, const char* output_dir, int start, int end)
{
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
            fz_throw(ctx, FZ_ERROR_GENERIC, "cannot open document");

        for (int i = start; i < end; i++)
        {
            if (extract_page_blocks(ctx, doc, i, output_dir, NULL, 0) != 0)
            {
                fprintf(stderr, "Warning: failed to extract page %d\n", i + 1);
            }
        }
    }
    fz_catch(ctx)
    {
        fprintf(stderr, "Error extracting pages %d-%d: %s\n", start + 1, end, fz_caught_message(ctx));
        status = -1;
    }

    if (doc)
        fz_drop_document(ctx, doc);
    fz_drop_context(ctx);

    return status;
}

/* Master function: split pages across multiple processes */
static int extract_document_multiprocess(const char* pdf_path, const char* output_dir)
{
    if (!pdf_path)
        return -1;

    if (ensure_directory(output_dir) != 0)
        return -1;

    /* Determine number of CPU cores */
    int num_cores = get_num_cores();

    /* Open document once to get total page count */
    fz_context* ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
    if (!ctx)
    {
        fprintf(stderr, "Error: cannot create context\n");
        return -1;
    }

    fz_document* doc = NULL;
    int page_count = 0;
    int error = 0;

    fz_try(ctx)
    {
        fz_register_document_handlers(ctx);
        doc = fz_open_document(ctx, pdf_path);
        page_count = fz_count_pages(ctx, doc);
    }
    fz_catch(ctx)
    {
        fprintf(stderr, "Error: cannot open document %s: %s\n", pdf_path, fz_caught_message(ctx));
        error = 1;
    }

    if (doc)
        fz_drop_document(ctx, doc);
    fz_drop_context(ctx);

    if (error)
        return -1;

    int pages_per_proc = (page_count + num_cores - 1) / num_cores;
    pid_t* pids = malloc(num_cores * sizeof(pid_t));

    // Initialize all pids to 0 (marks unused slots)
    for (int i = 0; i < num_cores; i++)
    {
        pids[i] = 0;
    }

    for (int i = 0; i < num_cores; i++)
    {
        int start = i * pages_per_proc;
        int end = (start + pages_per_proc < page_count) ? start + pages_per_proc : page_count;
        if (start >= page_count)
            break;

        pid_t pid = fork();
        if (pid < 0)
        {
            perror("fork");
            continue;
        }
        if (pid == 0)
        {
            /* Child process: extract its batch of pages */
            int rc = extract_pages_range(pdf_path, output_dir, start, end);
            exit(rc);
        }
        else
        {
            pids[i] = pid;
        }
    }

    /* Wait for all children */
    int final_status = 0;
    for (int i = 0; i < num_cores; i++)
    {
        if (pids[i] <= 0)
            continue;
        int wstatus;
        waitpid(pids[i], &wstatus, 0);
        if (WIFEXITED(wstatus) && WEXITSTATUS(wstatus) != 0)
            final_status = -1;
        else if (WIFSIGNALED(wstatus))
            final_status = -1;
    }

    free(pids);
    return final_status;
}

extern EXPORT int pdf_to_json(const char* pdf_path, const char* output_file)
{
    if (!pdf_path || !output_file)
        return -1;

    /* Create temporary directory */
    char temp_dir[256];
    snprintf(temp_dir, sizeof(temp_dir), ".pymupdf_tmp_%ld_%u", (long)time(NULL), (unsigned)getpid());

    if (ensure_directory(temp_dir) != 0)
        return -1;

    /* Extract pages to temp directory */
    int rc = extract_document_multiprocess(pdf_path, temp_dir);
    if (rc != 0)
    {
        remove_directory(temp_dir);
        return -1;
    }

    /* Merge JSON files into single output file */
    rc = merge_json_files(temp_dir, output_file);

    /* Clean up temp directory */
    remove_directory(temp_dir);

    return rc;
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

    int rc = extract_document_multiprocess(pdf_path, output_dir);
    return (rc == 0) ? 0 : 1;
}
#endif
