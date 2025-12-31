// utils.h
#ifndef UTILS_H
#define UTILS_H
#endif

#include "platform_compat.h"
#include <stdlib.h>

#include <stdio.h>

#define IS_SENTINEL(x) ((x) < 0)
#define SET_SENTINEL(x) ((x) = -1.0f)
#define CMP_FLOAT(a, b) ((a) > (b) ? 1 : (a) < (b) ? -1 : 0)
// clang-format off
#define DEFINE_ARRAY_METHODS(Type, PascalName, snake_name) \
    static void init_##snake_name##_array(PascalName##Array* arr) { \
        arr->items = NULL; arr->count = 0; arr->capacity = 0; \
    } \
    static void add_to_##snake_name##_array(PascalName##Array* arr, Type item) { \
        if (arr->count >= arr->capacity) { \
            arr->capacity = arr->capacity == 0 ? 16 : arr->capacity * 2; \
            arr->items = realloc(arr->items, arr->capacity * sizeof(Type)); \
        } \
        arr->items[arr->count++] = item; \
    } \
    static void free_##snake_name##_array(PascalName##Array* arr) { free(arr->items); }

// clang-format on

// clang-format off
#define DEFINE_ARRAY_METHODS_PUBLIC(Type, PascalName, snake_name) \
    void init_##snake_name##_array(PascalName##Array* arr) { \
        arr->items = NULL; arr->count = 0; arr->capacity = 0; \
    } \
    void add_to_##snake_name##_array(PascalName##Array* arr, Type item) { \
        if (arr->count >= arr->capacity) { \
            arr->capacity = arr->capacity == 0 ? 16 : arr->capacity * 2; \
            arr->items = realloc(arr->items, arr->capacity * sizeof(Type)); \
        } \
        arr->items[arr->count++] = item; \
    } \
    void free_##snake_name##_array(PascalName##Array* arr) { free(arr->items); }
// clang-format on

int remove_directory(const char* dir);
int ensure_directory(const char* dir);


int compare_float_asc(const void* a, const void* b);
float median_inplace(float* values, int count);


