// Spatial hash implementation for fast geometric lookups
#include "spatial_hash.h"
#include "stdlib.h"
#include "string.h"

#define GRID_CELL_SIZE 2.0

#define POINT_TOLERANCE 0.1

#define RESIZE_RATE 2

#define HASH_PRIME_A 73856093
#define HASH_PRIME_B 19349663

#define INITIAL_POOL_CAPACITY 256
#define INITIAL_ARRAY_CAPACITY 16

static int hash_point(double x, double y)
{
    int ix = (int)(x / GRID_CELL_SIZE);
    int iy = (int)(y / GRID_CELL_SIZE);
    return ((ix * 73856093) ^ (iy * 19349663)) & (HASH_SIZE - 1);
}

void init_spatial_hash(SpatialHash* hash)
{
    memset(hash->buckets, 0, sizeof(hash->buckets));
    hash->pool_capacity = 256;
    hash->node_pool = malloc(hash->pool_capacity * sizeof(PointNode));
    hash->pool_size = 0;
}

// PointArray methods are now provided via table.h and implemented in table_core.c

void add_to_spatial_hash(SpatialHash* hash, Point p)
{
    int h = hash_point(p.x, p.y);

    // Check if point already exists (deduplication)
    for (PointNode* node = hash->buckets[h]; node; node = node->next)
    {
        if (fabs(node->point.x - p.x) < POINT_TOLERANCE && fabs(node->point.y - p.y) < POINT_TOLERANCE)
        {
            return; // Already exists
        }
    }

    // Expand pool if needed
    if (hash->pool_size >= hash->pool_capacity)
    {
        PointNode* old_pool = hash->node_pool;
        hash->pool_capacity *= RESIZE_RATE;
        hash->node_pool = realloc(hash->node_pool, hash->pool_capacity * sizeof(PointNode));

        // If realloc moved the memory, update all bucket pointers
        if (hash->node_pool != old_pool)
        {
            // Update all bucket head pointers
            for (int i = 0; i < HASH_SIZE; i++)
            {
                if (hash->buckets[i])
                {
                    // Calculate offset from old base and apply to new base
                    ptrdiff_t offset = hash->buckets[i] - old_pool;
                    hash->buckets[i] = hash->node_pool + offset;
                }
            }
            // Update all next pointers within nodes
            for (int i = 0; i < hash->pool_size; i++)
            {
                if (hash->node_pool[i].next)
                {
                    ptrdiff_t offset = hash->node_pool[i].next - old_pool;
                    hash->node_pool[i].next = hash->node_pool + offset;
                }
            }
        }
    }

    PointNode* node = &hash->node_pool[hash->pool_size++];
    node->point = p;
    node->next = hash->buckets[h];
    hash->buckets[h] = node;
}

int find_in_spatial_hash(SpatialHash* hash, double x, double y)
{
    int h = hash_point(x, y);
    for (PointNode* node = hash->buckets[h]; node; node = node->next)
    {
        if (fabs(node->point.x - x) < POINT_TOLERANCE && fabs(node->point.y - y) < POINT_TOLERANCE)
        {
            return 1;
        }
    }
    return 0;
}

void collect_points_from_hash(SpatialHash* hash, PointArray* arr)
{
    for (int i = 0; i < hash->pool_size; i++)
    {
        add_to_point_array(arr, hash->node_pool[i].point);
    }
}

void free_spatial_hash(SpatialHash* hash)
{
    free(hash->node_pool);
}
