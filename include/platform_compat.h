#ifndef PLATFORM_COMPAT_H
#define PLATFORM_COMPAT_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#ifdef _WIN32
#include <direct.h>
#define strncasecmp _strnicmp
#define strcasecmp _stricmp
#endif

#ifdef __APPLE__
#include <strings.h>   // for strncasecmp, strcasecmp
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

#include "mupdf/fitz.h"

#ifndef FZ_STEXT_CLIP
#define FZ_STEXT_CLIP 1
#endif

#ifndef FZ_STEXT_ACCURATE_BBOXES
#define FZ_STEXT_ACCURATE_BBOXES 2
#endif

#ifndef FZ_STEXT_COLLECT_STYLES
#define FZ_STEXT_COLLECT_STYLES 32768
#endif

#ifndef FZ_STEXT_USE_GID_FOR_UNKNOWN_UNICODE
#define FZ_STEXT_USE_GID_FOR_UNKNOWN_UNICODE 4
#endif

#ifndef FZ_STEXT_PRESERVE_LIGATURES
#define FZ_STEXT_PRESERVE_LIGATURES 8
#endif

#ifndef FZ_STEXT_PRESERVE_WHITESPACE
#define FZ_STEXT_PRESERVE_WHITESPACE 16
#endif

#ifndef FZ_FONT_FLAG_BOLD
#define FZ_FONT_FLAG_BOLD 1
#endif

#ifndef FZ_FONT_FLAG_ITALIC
#define FZ_FONT_FLAG_ITALIC 2
#endif

#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif

/* --- Added CPU detection helper for multiprocess --- */
static inline int get_num_cores(void)
{
    int num_cores = 1;

#if defined(_SC_NPROCESSORS_ONLN)
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    if (n > 0)
        num_cores = (int)n;
#elif defined(__APPLE__)
    int n;
    size_t len = sizeof(n);
    int mib[2] = {CTL_HW, HW_AVAILCPU};
    if (sysctl(mib, 2, &n, &len, NULL, 0) == 0 && n > 0)
        num_cores = n;
#endif

    return num_cores;
}

#endif // PLATFORM_COMPAT_H
