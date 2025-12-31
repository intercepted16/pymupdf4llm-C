#include "utils.h"
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <dirent.h>
#include <unistd.h>

int ensure_directory(const char* dir)
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

int remove_directory(const char* dir)
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

int compare_float_asc(const void* a, const void* b)
{
    float fa = *(const float*)a;
    float fb = *(const float*)b;
    if (fa < fb)
        return -1;
    if (fa > fb)
        return 1;
    return 0;
}

float median_inplace(float* values, int count)
{
    if (!values || count <= 0)
        return -1.0f; // Sentinel
    qsort(values, (size_t)count, sizeof(float), compare_float_asc);
    if (count & 1)
        return values[count / 2];
    return 0.5f * (values[count / 2 - 1] + values[count / 2]);
}