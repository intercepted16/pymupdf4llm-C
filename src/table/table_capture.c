#include "table.h"
#include "table_capture.h"

static void capture_stroke_path(fz_context* ctx, fz_device* dev_, const fz_path* path, const fz_stroke_state* stroke,
                                fz_matrix ctm, fz_colorspace* colorspace, const float* color, float alpha,
                                fz_color_params color_params)
{
    (void)colorspace;
    (void)color;
    (void)alpha;
    (void)color_params;

    CaptureDevice* dev = (CaptureDevice*)dev_;
    fz_rect bbox = fz_bound_path(ctx, path, stroke, ctm);

    double width = bbox.x1 - bbox.x0;
    double height = bbox.y1 - bbox.y0;

    if (height <= EDGE_MAX_WIDTH && width >= EDGE_MIN_LENGTH)
        add_to_edge_array(&dev->edges, (Edge){bbox.x0, bbox.y0, bbox.x1, bbox.y0, 'h'});
    else if (width <= EDGE_MAX_WIDTH && height >= EDGE_MIN_LENGTH)
        add_to_edge_array(&dev->edges, (Edge){bbox.x0, bbox.y0, bbox.x0, bbox.y1, 'v'});
}

static void capture_fill_path(fz_context* ctx, fz_device* dev_, const fz_path* path, int even_odd, fz_matrix ctm,
                              fz_colorspace* colorspace, const float* color, float alpha, fz_color_params color_params)
{
    (void)ctx;
    (void)path;
    (void)even_odd;
    (void)ctm;
    (void)colorspace;
    (void)color;
    (void)alpha;
    (void)color_params;

    CaptureDevice* dev = (CaptureDevice*)dev_;
    fz_rect bbox = fz_bound_path(ctx, path, NULL, ctm);

    double width = bbox.x1 - bbox.x0;
    double height = bbox.y1 - bbox.y0;

    if (width > 0 && height > 0)
    {
        add_to_edge_array(&dev->edges, (Edge){bbox.x0, bbox.y0, bbox.x1, bbox.y0, 'h'});
        add_to_edge_array(&dev->edges, (Edge){bbox.x0, bbox.y1, bbox.x1, bbox.y1, 'h'});
        add_to_edge_array(&dev->edges, (Edge){bbox.x0, bbox.y0, bbox.x0, bbox.y1, 'v'});
        add_to_edge_array(&dev->edges, (Edge){bbox.x1, bbox.y0, bbox.x1, bbox.y1, 'v'});
    }
}

static void capture_close_device(fz_context* ctx, fz_device* dev_)
{
    (void)ctx;
    (void)dev_;
}

static void capture_drop_device(fz_context* ctx, fz_device* dev_)
{
    (void)ctx;
    CaptureDevice* dev = (CaptureDevice*)dev_;
    free_edge_array(&dev->edges);
}

fz_device* new_capture_device(fz_context* ctx)
{
    CaptureDevice* dev = fz_new_derived_device(ctx, CaptureDevice);
    dev->super.close_device = capture_close_device;
    dev->super.drop_device = capture_drop_device;
    dev->super.stroke_path = capture_stroke_path;
    dev->super.fill_path = capture_fill_path;
    init_edge_array(&dev->edges);
    return (fz_device*)dev;
}