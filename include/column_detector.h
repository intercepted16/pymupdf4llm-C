#ifndef COLUMN_DETECTOR_H
#define COLUMN_DETECTOR_H

#include "block_info.h"

// Detects columns on the page and assigns column_index to each block
void detect_and_assign_columns(BlockArray* blocks);

#endif // COLUMN_DETECTOR_H
