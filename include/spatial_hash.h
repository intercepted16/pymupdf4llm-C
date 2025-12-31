// Spatial hash implementation for fast geometric lookups

#ifndef SPATIAL_HASH_H
#define SPATIAL_HASH_H

#include "table.h"

#define HASH_SIZE 4096

typedef struct PointNode
{
    Point point;
    struct PointNode* next;
} PointNode;

typedef struct
{
    PointNode* buckets[HASH_SIZE];
    PointNode* node_pool;
    int pool_size;
    int pool_capacity;
} SpatialHash;

void init_spatial_hash(SpatialHash* hash);
void add_to_spatial_hash(SpatialHash* hash, Point p);
int find_in_spatial_hash(SpatialHash* hash, double x, double y);
void collect_points_from_hash(SpatialHash* hash, PointArray* arr);
void free_spatial_hash(SpatialHash* hash);

#endif // SPATIAL_HASH_H