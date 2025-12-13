#pragma once

/* ===================== PLATFORM DETECTION ===================== */
#ifdef _WIN32
#define PLATFORM_WINDOWS 1
#else
#define PLATFORM_WINDOWS 0
#endif

#ifdef __APPLE__
#define PLATFORM_MAC 1
#include <TargetConditionals.h>
#else
#define PLATFORM_MAC 0
#endif

#ifdef __linux__
#define PLATFORM_LINUX 1
#else
#define PLATFORM_LINUX 0
#endif

/* ===================== EXPORT MACRO ===================== */
#ifndef EXPORT
#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif
#endif

/* ===================== LEGACY TYPE FIXES FOR MAC ===================== */
#if PLATFORM_MAC
typedef unsigned int    u_int;
typedef unsigned char   u_char;
typedef unsigned short  u_short;

#include <unistd.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <strings.h>

/* get number of CPU cores */
static int get_num_cores() {
    int nm[2];
    size_t len = sizeof(int);
    int count;
    nm[0] = CTL_HW;
    nm[1] = HW_NCPU;
    sysctl(nm, 2, &count, &len, NULL, 0);
    return count;
}
#endif

/* ===================== LINUX CPU CORES ===================== */
#if PLATFORM_LINUX
#include <unistd.h>

/* get number of CPU cores */
static int get_num_cores() {
    return sysconf(_SC_NPROCESSORS_ONLN);
}
#endif

/* ===================== MATH/UTILS ===================== */
#include <math.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef NAN
#define NAN (__builtin_nanf("0x7fc00000"))
#endif

/* Safe isnan check - standard isnan can have issues with -ffast-math */
#ifndef SAFE_ISNAN
#define SAFE_ISNAN(x) ((x) != (x))
#endif
