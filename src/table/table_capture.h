#ifndef TABLE_CAPTURE_H
#define TABLE_CAPTURE_H

#include "mupdf/fitz/device.h"
#include "table.h"


typedef struct
{
    fz_device super;
    EdgeArray edges;
} CaptureDevice;


fz_device* new_capture_device(fz_context* ctx);


#endif // TABLE_CAPTURE_H