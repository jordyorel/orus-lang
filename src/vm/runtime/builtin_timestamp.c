// Orus Language Project

#define _POSIX_C_SOURCE 200809L
#include "runtime/builtins.h"

#include <stdint.h>
#include <time.h>

#ifdef __APPLE__
#include <mach/mach_time.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

#ifdef __APPLE__
static uint64_t timebase_numer = 0;
static uint64_t timebase_denom = 0;
static bool timebase_initialized = false;

static void init_timebase() {
    if (!timebase_initialized) {
        mach_timebase_info_data_t info;
        mach_timebase_info(&info);
        timebase_numer = info.numer;
        timebase_denom = info.denom;
        timebase_initialized = true;
    }
}
#endif

double builtin_timestamp(void) {
#ifdef __APPLE__
    init_timebase();
    uint64_t abs_time = mach_absolute_time();
    uint64_t nanoseconds = (abs_time * timebase_numer) / timebase_denom;
    return (double)nanoseconds / 1e9;
#elif defined(__linux__)
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
    }
    return 0.0;
#elif defined(_WIN32)
    static LARGE_INTEGER frequency = {0};
    if (frequency.QuadPart == 0) {
        QueryPerformanceFrequency(&frequency);
    }
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)frequency.QuadPart;
#else
    return (double)clock() / (double)CLOCKS_PER_SEC;
#endif
}
