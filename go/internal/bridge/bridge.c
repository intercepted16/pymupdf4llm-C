// why do logic in here > just wrapping fz functions via cgo?
// performance. the thousands of `cgo` calls added up
// (approx. 3x slower overall)
// so, use one function that writes to disk instead, much less overhead.
// this is mostly pre existing logic just written to be used via Go.
// additionally, the raw 'binary' format is used for performance and less disk usage. json gets very taxing on thousands of chars.

#include "bridge.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>

#define EDGE_MIN_LENGTH 3.0
#define EDGE_MAX_WIDTH 3.0



typedef struct
{
    fz_device super;
    fz_device* stext_dev;  
    edge_array* edges;
} combined_device;

static void mupdf_warning_callback(void* user, const char* message)
{
    (void)user;
    (void)message;
}

static void mupdf_error_callback(void* user, const char* message)
{
    (void)user;
    (void)message;
}

static void add_edge(edge_array* arr, double x0, double y0, double x1, double y1, char orientation)
{
    if (arr->count >= arr->capacity)
    {
        int new_cap = arr->capacity == 0 ? 64 : arr->capacity * 2;
        edge* new_items = realloc(arr->items, new_cap * sizeof(edge));
        if (!new_items)
            return;
        arr->items = new_items;
        arr->capacity = new_cap;
    }

    edge* e = &arr->items[arr->count++];
    e->x0 = x0;
    e->y0 = y0;
    e->x1 = x1;
    e->y1 = y1;
    e->orientation = orientation;
}




static void combined_fill_path(fz_context* ctx, fz_device* dev, const fz_path* path, int even_odd, fz_matrix ctm,
                               fz_colorspace* cs, const float* color, float alpha, fz_color_params cp)
{
    combined_device* cdev = (combined_device*)dev;

    
    fz_rect bbox = fz_bound_path(ctx, path, NULL, ctm);
    double width = bbox.x1 - bbox.x0;
    double height = bbox.y1 - bbox.y0;

    if (width > 0 && height > 0)
    {
        add_edge(cdev->edges, bbox.x0, bbox.y0, bbox.x1, bbox.y0, 'h');
        add_edge(cdev->edges, bbox.x0, bbox.y1, bbox.x1, bbox.y1, 'h');
        add_edge(cdev->edges, bbox.x0, bbox.y0, bbox.x0, bbox.y1, 'v');
        add_edge(cdev->edges, bbox.x1, bbox.y0, bbox.x1, bbox.y1, 'v');
    }

    
    if (cdev->stext_dev && cdev->stext_dev->fill_path)
        cdev->stext_dev->fill_path(ctx, cdev->stext_dev, path, even_odd, ctm, cs, color, alpha, cp);
}

static void combined_stroke_path(fz_context* ctx, fz_device* dev, const fz_path* path, const fz_stroke_state* stroke,
                                 fz_matrix ctm, fz_colorspace* cs, const float* color, float alpha, fz_color_params cp)
{
    combined_device* cdev = (combined_device*)dev;

    
    if (!stroke || stroke->linewidth <= EDGE_MAX_WIDTH)
    {
        fz_rect bbox = fz_bound_path(ctx, path, stroke, ctm);
        double width = bbox.x1 - bbox.x0;
        double height = bbox.y1 - bbox.y0;

        if (height <= EDGE_MAX_WIDTH && width >= EDGE_MIN_LENGTH)
            add_edge(cdev->edges, bbox.x0, bbox.y0, bbox.x1, bbox.y0, 'h');
        else if (width <= EDGE_MAX_WIDTH && height >= EDGE_MIN_LENGTH)
            add_edge(cdev->edges, bbox.x0, bbox.y0, bbox.x0, bbox.y1, 'v');
    }

    
    if (cdev->stext_dev && cdev->stext_dev->stroke_path)
        cdev->stext_dev->stroke_path(ctx, cdev->stext_dev, path, stroke, ctm, cs, color, alpha, cp);
}


static void combined_fill_text(fz_context* ctx, fz_device* dev, const fz_text* text, fz_matrix ctm,
                               fz_colorspace* cs, const float* color, float alpha, fz_color_params cp)
{
    combined_device* cdev = (combined_device*)dev;
    if (cdev->stext_dev && cdev->stext_dev->fill_text)
        cdev->stext_dev->fill_text(ctx, cdev->stext_dev, text, ctm, cs, color, alpha, cp);
}

static void combined_stroke_text(fz_context* ctx, fz_device* dev, const fz_text* text, const fz_stroke_state* stroke,
                                 fz_matrix ctm, fz_colorspace* cs, const float* color, float alpha, fz_color_params cp)
{
    combined_device* cdev = (combined_device*)dev;
    if (cdev->stext_dev && cdev->stext_dev->stroke_text)
        cdev->stext_dev->stroke_text(ctx, cdev->stext_dev, text, stroke, ctm, cs, color, alpha, cp);
}

static void combined_clip_text(fz_context* ctx, fz_device* dev, const fz_text* text, fz_matrix ctm, fz_rect scissor)
{
    combined_device* cdev = (combined_device*)dev;
    if (cdev->stext_dev && cdev->stext_dev->clip_text)
        cdev->stext_dev->clip_text(ctx, cdev->stext_dev, text, ctm, scissor);
}

static void combined_clip_stroke_text(fz_context* ctx, fz_device* dev, const fz_text* text, const fz_stroke_state* stroke,
                                      fz_matrix ctm, fz_rect scissor)
{
    combined_device* cdev = (combined_device*)dev;
    if (cdev->stext_dev && cdev->stext_dev->clip_stroke_text)
        cdev->stext_dev->clip_stroke_text(ctx, cdev->stext_dev, text, stroke, ctm, scissor);
}

static void combined_ignore_text(fz_context* ctx, fz_device* dev, const fz_text* text, fz_matrix ctm)
{
    combined_device* cdev = (combined_device*)dev;
    if (cdev->stext_dev && cdev->stext_dev->ignore_text)
        cdev->stext_dev->ignore_text(ctx, cdev->stext_dev, text, ctm);
}


static void combined_fill_shade(fz_context* ctx, fz_device* dev, fz_shade* shd, fz_matrix ctm, float alpha, fz_color_params cp)
{
    combined_device* cdev = (combined_device*)dev;
    if (cdev->stext_dev && cdev->stext_dev->fill_shade)
        cdev->stext_dev->fill_shade(ctx, cdev->stext_dev, shd, ctm, alpha, cp);
}

static void combined_fill_image(fz_context* ctx, fz_device* dev, fz_image* img, fz_matrix ctm, float alpha, fz_color_params cp)
{
    combined_device* cdev = (combined_device*)dev;
    if (cdev->stext_dev && cdev->stext_dev->fill_image)
        cdev->stext_dev->fill_image(ctx, cdev->stext_dev, img, ctm, alpha, cp);
}

static void combined_fill_image_mask(fz_context* ctx, fz_device* dev, fz_image* img, fz_matrix ctm,
                                     fz_colorspace* cs, const float* color, float alpha, fz_color_params cp)
{
    combined_device* cdev = (combined_device*)dev;
    if (cdev->stext_dev && cdev->stext_dev->fill_image_mask)
        cdev->stext_dev->fill_image_mask(ctx, cdev->stext_dev, img, ctm, cs, color, alpha, cp);
}


static void combined_begin_metatext(fz_context* ctx, fz_device* dev, fz_metatext meta, const char* text)
{
    combined_device* cdev = (combined_device*)dev;
    if (cdev->stext_dev && cdev->stext_dev->begin_metatext)
        cdev->stext_dev->begin_metatext(ctx, cdev->stext_dev, meta, text);
}

static void combined_end_metatext(fz_context* ctx, fz_device* dev)
{
    combined_device* cdev = (combined_device*)dev;
    if (cdev->stext_dev && cdev->stext_dev->end_metatext)
        cdev->stext_dev->end_metatext(ctx, cdev->stext_dev);
}

static void combined_close_device(fz_context* ctx, fz_device* dev)
{
    combined_device* cdev = (combined_device*)dev;
    if (cdev->stext_dev)
        fz_close_device(ctx, cdev->stext_dev);
}

static void combined_drop_device(fz_context* ctx, fz_device* dev)
{
    combined_device* cdev = (combined_device*)dev;
    if (cdev->stext_dev)
        fz_drop_device(ctx, cdev->stext_dev);
}

static void free_edge_array(edge_array* edges)
{
    if (!edges)
        return;
    free(edges->items);
    edges->items = NULL;
    edges->count = 0;
    edges->capacity = 0;
}

static void write_all_char_data(FILE* out, fz_context* ctx, fz_stext_page* stext, int total_chars)
{
    if (total_chars == 0)
        return;

    fchar* char_buffer = malloc(total_chars * sizeof(fchar));
    if (!char_buffer)
        return;

    int char_idx = 0;
    fz_font* last_font = NULL;
    uint8_t cached_bold = 0, cached_italic = 0, cached_mono = 0;

    for (fz_stext_block* block = stext->first_block; block; block = block->next)
    {
        if (block->type != FZ_STEXT_BLOCK_TEXT)
            continue;

        for (fz_stext_line* line = block->u.t.first_line; line; line = line->next)
        {
            for (fz_stext_char* ch = line->first_char; ch; ch = ch->next)
            {
                fchar* rc = &char_buffer[char_idx++];
                rc->codepoint = ch->c;
                rc->size = ch->size;

                fz_rect char_bbox = fz_rect_from_quad(ch->quad);
                rc->bbox_x0 = char_bbox.x0;
                rc->bbox_y0 = char_bbox.y0;
                rc->bbox_x1 = char_bbox.x1;
                rc->bbox_y1 = char_bbox.y1;

                if (ch->font != last_font)
                {
                    cached_bold = (ch->font && fz_font_is_bold(ctx, ch->font)) ? 1 : 0;
                    cached_italic = (ch->font && fz_font_is_italic(ctx, ch->font)) ? 1 : 0;
                    cached_mono = (ch->font && fz_font_is_monospaced(ctx, ch->font)) ? 1 : 0;
                    last_font = ch->font;
                }
                rc->is_bold = cached_bold;
                rc->is_italic = cached_italic;
                rc->is_monospaced = cached_mono;
            }
        }
    }

    fwrite(char_buffer, sizeof(fchar), total_chars, out);
    free(char_buffer);
}

static int count_links(fz_link* links)
{
    int count = 0;
    for (fz_link* l = links; l; l = l->next)
        count++;
    return count;
}

static int extract_page_to_file(fz_context* ctx, fz_document* doc, int page_num, const char* output_path)
{
    fz_page* page = NULL;
    fz_stext_page* stext = NULL;
    fz_link* page_links = NULL;
    FILE* out = NULL;
    int status = 0;
    edge_array edges = {0};
    fz_device* combined_dev = NULL;

    fz_try(ctx)
    {
        page = fz_load_page(ctx, doc, page_num);
        fz_rect bounds = fz_bound_page(ctx, page);

        page_links = fz_load_links(ctx, page);
        int link_count = count_links(page_links);

        fz_stext_options opts = {0};
        opts.flags = FZ_STEXT_PRESERVE_WHITESPACE | FZ_STEXT_ACCURATE_BBOXES | FZ_STEXT_COLLECT_STYLES;
        
        
        stext = fz_new_stext_page(ctx, bounds);
        
        
        fz_device* stext_dev = fz_new_stext_device(ctx, stext, &opts);
        
        
        combined_device* cdev = fz_new_derived_device(ctx, combined_device);
        combined_dev = &cdev->super;
        cdev->stext_dev = stext_dev;
        cdev->edges = &edges;
        
        
        combined_dev->close_device = combined_close_device;
        combined_dev->drop_device = combined_drop_device;
        combined_dev->fill_path = combined_fill_path;
        combined_dev->stroke_path = combined_stroke_path;
        combined_dev->fill_text = combined_fill_text;
        combined_dev->stroke_text = combined_stroke_text;
        combined_dev->clip_text = combined_clip_text;
        combined_dev->clip_stroke_text = combined_clip_stroke_text;
        combined_dev->ignore_text = combined_ignore_text;
        combined_dev->fill_shade = combined_fill_shade;
        combined_dev->fill_image = combined_fill_image;
        combined_dev->fill_image_mask = combined_fill_image_mask;
        combined_dev->begin_metatext = combined_begin_metatext;
        combined_dev->end_metatext = combined_end_metatext;
        
        
        fz_run_page_contents(ctx, page, combined_dev, fz_identity, NULL);
        fz_close_device(ctx, combined_dev);
        

        out = fopen(output_path, "wb");
        if (!out)
            fz_throw(ctx, FZ_ERROR_GENERIC, "cannot open output file");

        setvbuf(out, NULL, _IOFBF, 256 * 1024);

        int total_blocks = 0, total_lines = 0, total_chars = 0;
        int blocks_capacity = 64, lines_capacity = 256;
        fblock* blocks_buffer = malloc(blocks_capacity * sizeof(fblock));
        fline* lines_buffer = malloc(lines_capacity * sizeof(fline));

        if (!blocks_buffer || !lines_buffer)
        {
            free(blocks_buffer);
            free(lines_buffer);
            fz_throw(ctx, FZ_ERROR_GENERIC, "cannot allocate buffers");
        }

        int line_idx = 0;
        int char_idx = 0;

        for (fz_stext_block* block = stext->first_block; block; block = block->next)
        {

            if (total_blocks >= blocks_capacity)
            {
                blocks_capacity *= 2;
                fblock* new_blocks = realloc(blocks_buffer, blocks_capacity * sizeof(fblock));
                if (!new_blocks)
                {
                    free(blocks_buffer);
                    free(lines_buffer);
                    fz_throw(ctx, FZ_ERROR_GENERIC, "cannot realloc blocks");
                }
                blocks_buffer = new_blocks;
            }

            fblock* rb = &blocks_buffer[total_blocks++];
            rb->type = block->type;
            rb->bbox_x0 = block->bbox.x0;
            rb->bbox_y0 = block->bbox.y0;
            rb->bbox_x1 = block->bbox.x1;
            rb->bbox_y1 = block->bbox.y1;
            rb->line_start = line_idx;
            rb->line_count = 0;

            if (block->type == FZ_STEXT_BLOCK_TEXT)
            {
                for (fz_stext_line* line = block->u.t.first_line; line; line = line->next)
                {

                    if (total_lines >= lines_capacity)
                    {
                        lines_capacity *= 2;
                        fline* new_lines = realloc(lines_buffer, lines_capacity * sizeof(fline));
                        if (!new_lines)
                        {
                            free(blocks_buffer);
                            free(lines_buffer);
                            fz_throw(ctx, FZ_ERROR_GENERIC, "cannot realloc lines");
                        }
                        lines_buffer = new_lines;
                    }

                    rb->line_count++;
                    fline* rl = &lines_buffer[total_lines++];
                    rl->bbox_x0 = line->bbox.x0;
                    rl->bbox_y0 = line->bbox.y0;
                    rl->bbox_x1 = line->bbox.x1;
                    rl->bbox_y1 = line->bbox.y1;
                    rl->char_start = char_idx;
                    rl->char_count = 0;

                    for (fz_stext_char* ch = line->first_char; ch; ch = ch->next)
                    {
                        rl->char_count++;
                        total_chars++;
                    }
                    char_idx += rl->char_count;
                }
                line_idx += rb->line_count;
            }
        }

        int page_number = page_num + 1;
        fwrite(&page_number, sizeof(int), 1, out);
        fwrite(&bounds, sizeof(fz_rect), 1, out);
        fwrite(&total_blocks, sizeof(int), 1, out);
        fwrite(&total_lines, sizeof(int), 1, out);
        fwrite(&total_chars, sizeof(int), 1, out);
        fwrite(&edges.count, sizeof(int), 1, out);
        fwrite(&link_count, sizeof(int), 1, out);

        fwrite(blocks_buffer, sizeof(fblock), total_blocks, out);
        fwrite(lines_buffer, sizeof(fline), total_lines, out);

        free(blocks_buffer);
        free(lines_buffer);

        write_all_char_data(out, ctx, stext, total_chars);

        if (edges.count > 0)
            fwrite(edges.items, sizeof(edge), edges.count, out);

        for (fz_link* l = page_links; l; l = l->next)
        {

            struct
            {
                float x0, y0, x1, y1;
            } rect = {l->rect.x0, l->rect.y0, l->rect.x1, l->rect.y1};
            fwrite(&rect, sizeof(rect), 1, out);

            const char* uri = l->uri ? l->uri : "";
            int uri_len = strlen(uri);
            fwrite(&uri_len, sizeof(int), 1, out);
            if (uri_len > 0)
                fwrite(uri, 1, uri_len, out);
        }

        fclose(out);
        out = NULL;

    }
    fz_always(ctx)
    {
        if (out)
            fclose(out);
        if (combined_dev)
            fz_drop_device(ctx, combined_dev);
        if (page_links)
            fz_drop_link(ctx, page_links);
        if (stext)
            fz_drop_stext_page(ctx, stext);
        if (page)
            fz_drop_page(ctx, page);
        free_edge_array(&edges);
    }
    fz_catch(ctx)
    {
        status = -1;
    }

    return status;
}

static int extract_page_range(const char* pdf_path, const char* output_dir, int start_page, int end_page)
{
    fz_context* ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
    if (!ctx)
        return -1;

    fz_set_warning_callback(ctx, mupdf_warning_callback, NULL);
    fz_set_error_callback(ctx, mupdf_error_callback, NULL);

    fz_document* doc = NULL;
    int status = 0;

    fz_try(ctx)
    {
        fz_register_document_handlers(ctx);
        doc = fz_open_document(ctx, pdf_path);

        for (int i = start_page; i < end_page; i++)
        {
            char filename[512];
            snprintf(filename, sizeof(filename), "%s/page_%03d.raw", output_dir, i + 1);
            if (extract_page_to_file(ctx, doc, i, filename) != 0)
                fprintf(stderr, "Warning: failed to extract page %d\n", i + 1);
        }
    }
    fz_always(ctx)
    {
        if (doc)
            fz_drop_document(ctx, doc);
    }
    fz_catch(ctx)
    {
        status = -1;
    }

    fz_drop_context(ctx);
    return status;
}

char* extract_all_pages(const char* pdf_path)
{
    if (!pdf_path)
        return NULL;

    char* temp_dir = malloc(256);
    if (!temp_dir)
        return NULL;

    snprintf(temp_dir, 256, ".pymupdfllm_c_%ld_%u", (long)time(NULL), (unsigned)getpid());
    mkdir(temp_dir, 0755);

    fz_context* ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
    fz_set_warning_callback(ctx, mupdf_warning_callback, NULL);
    fz_set_error_callback(ctx, mupdf_error_callback, NULL);

    if (!ctx)
    {
        free(temp_dir);
        return NULL;
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
        error = 1;
    }

    if (doc)
        fz_drop_document(ctx, doc);
    fz_drop_context(ctx);

    if (error || page_count == 0)
    {
        free(temp_dir);
        return NULL;
    }

    int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (num_cores <= 0)
        num_cores = 4;

    int pages_per_proc = (page_count + num_cores - 1) / num_cores;
    pid_t* pids = malloc(num_cores * sizeof(pid_t));
    if (!pids)
    {
        free(temp_dir);
        return NULL;
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
            
            int rc = extract_page_range(pdf_path, temp_dir, start, end);
            exit(rc);
        }
        pids[i] = pid;
    }

    
    for (int i = 0; i < num_cores; i++)
    {
        if (pids[i] > 0)
        {
            int wstatus;
            waitpid(pids[i], &wstatus, 0);
        }
    }

    free(pids);
    
    return temp_dir;
}

int read_page(const char* filepath, page_data* out)
{
    if (!filepath || !out)
        return -1;

    memset(out, 0, sizeof(page_data));
    FILE* in = fopen(filepath, "rb");
    if (!in)
        return -1;

    fz_rect bounds;
    int edge_count, link_count;
    if (fread(&out->page_number, sizeof(int), 1, in) != 1 || fread(&bounds, sizeof(fz_rect), 1, in) != 1 ||
        fread(&out->block_count, sizeof(int), 1, in) != 1 || fread(&out->line_count, sizeof(int), 1, in) != 1 ||
        fread(&out->char_count, sizeof(int), 1, in) != 1 || fread(&edge_count, sizeof(int), 1, in) != 1 ||
        fread(&link_count, sizeof(int), 1, in) != 1)
    {
        fclose(in);
        return -1;
    }

    out->page_x0 = bounds.x0;
    out->page_y0 = bounds.y0;
    out->page_x1 = bounds.x1;
    out->page_y1 = bounds.y1;
    out->edge_count = edge_count;
    out->link_count = link_count;

    out->blocks = malloc(out->block_count * sizeof(fblock));
    out->lines = malloc(out->line_count * sizeof(fline));
    out->chars = malloc(out->char_count * sizeof(fchar));
    out->edges = malloc(out->edge_count * sizeof(edge));
    out->links = malloc(out->link_count * sizeof(flink));

    if (!out->blocks || !out->lines || !out->chars || !out->edges || !out->links)
    {
        free_page(out);
        fclose(in);
        return -1;
    }

    if (fread(out->blocks, sizeof(fblock), out->block_count, in) != (size_t)out->block_count ||
        fread(out->lines, sizeof(fline), out->line_count, in) != (size_t)out->line_count ||
        fread(out->chars, sizeof(fchar), out->char_count, in) != (size_t)out->char_count)
    {
        free_page(out);
        fclose(in);
        return -1;
    }

    if (edge_count > 0 && fread(out->edges, sizeof(edge), edge_count, in) != (size_t)edge_count)
    {
        free_page(out);
        fclose(in);
        return -1;
    }

    for (int i = 0; i < link_count; i++)
    {
        float rect_x0, rect_y0, rect_x1, rect_y1;
        int uri_len;

        if (fread(&rect_x0, sizeof(float), 1, in) != 1 || fread(&rect_y0, sizeof(float), 1, in) != 1 ||
            fread(&rect_x1, sizeof(float), 1, in) != 1 || fread(&rect_y1, sizeof(float), 1, in) != 1 ||
            fread(&uri_len, sizeof(int), 1, in) != 1)
        {
            free_page(out);
            fclose(in);
            return -1;
        }

        out->links[i].rect_x0 = rect_x0;
        out->links[i].rect_y0 = rect_y0;
        out->links[i].rect_x1 = rect_x1;
        out->links[i].rect_y1 = rect_y1;

        if (uri_len > 0)
        {
            out->links[i].uri = malloc(uri_len + 1);
            if (!out->links[i].uri || fread(out->links[i].uri, 1, uri_len, in) != (size_t)uri_len)
            {
                free_page(out);
                fclose(in);
                return -1;
            }
            out->links[i].uri[uri_len] = '\0';
        }
        else
        {
            out->links[i].uri = malloc(1);
            if (out->links[i].uri)
                out->links[i].uri[0] = '\0';
        }
    }

    fclose(in);
    return 0;
}

void free_page(page_data* data)
{
    if (!data)
        return;
    free(data->blocks);
    free(data->lines);
    free(data->chars);
    free(data->edges);
    if (data->links)
    {
        for (int i = 0; i < data->link_count; i++)
            free(data->links[i].uri);
        free(data->links);
    }
    memset(data, 0, sizeof(page_data));
}
