#define _POSIX_C_SOURCE 200809L
#include "runtime/builtins.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>

#ifdef __APPLE__
#include <mach/mach_time.h>
#elif defined(__linux__)
#include <time.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

// High-precision timing infrastructure
#ifdef __APPLE__
static uint64_t timebase_numer = 0;
static uint64_t timebase_denom = 0;
static bool timebase_initialized = false;

// Initialize mach timebase (called once)
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

static void print_formatted_value(Value value, const char* spec);

static void print_binary(unsigned long long value) {
    char buf[BINARY_BUFFER_SIZE];
    int index = BINARY_BUFFER_LAST_INDEX;
    buf[BINARY_BUFFER_LAST_INDEX] = '\0';
    if (value == 0) {
        putchar('0');
        return;
    }
    while (value && index) {
        buf[--index] = (value & 1) ? '1' : '0';
        value >>= 1;
    }
    fputs(&buf[index], stdout);
}

static void print_formatted_value(Value value, const char* spec) {
    if (!spec || spec[0] == '\0') {
        printValue(value);
        return;
    }

    switch (value.type) {
        case VAL_I32: {
            long long v = AS_I32(value);
            if (strcmp(spec, "b") == 0) {
                print_binary((unsigned long long)v);
            } else if (strcmp(spec, "x") == 0) {
                printf("%llx", v);
            } else if (strcmp(spec, "X") == 0) {
                printf("%llX", v);
            } else if (strcmp(spec, "o") == 0) {
                printf("%llo", v);
            } else {
                printf("%lld", v);
            }
            break;
        }
        case VAL_I64: {
            long long v = AS_I64(value);
            if (strcmp(spec, "b") == 0) {
                print_binary((unsigned long long)v);
            } else if (strcmp(spec, "x") == 0) {
                printf("%llx", v);
            } else if (strcmp(spec, "X") == 0) {
                printf("%llX", v);
            } else if (strcmp(spec, "o") == 0) {
                printf("%llo", v);
            } else {
                printf("%lld", v);
            }
            break;
        }
        case VAL_U32: {
            unsigned long long v = AS_U32(value);
            if (strcmp(spec, "b") == 0) {
                print_binary(v);
            } else if (strcmp(spec, "x") == 0) {
                printf("%llx", v);
            } else if (strcmp(spec, "X") == 0) {
                printf("%llX", v);
            } else if (strcmp(spec, "o") == 0) {
                printf("%llo", v);
            } else {
                printf("%llu", v);
            }
            break;
        }
        case VAL_U64: {
            unsigned long long v = AS_U64(value);
            if (strcmp(spec, "b") == 0) {
                print_binary(v);
            } else if (strcmp(spec, "x") == 0) {
                printf("%llx", v);
            } else if (strcmp(spec, "X") == 0) {
                printf("%llX", v);
            } else if (strcmp(spec, "o") == 0) {
                printf("%llo", v);
            } else {
                printf("%llu", v);
            }
            break;
        }
        case VAL_F64: {
            if (spec[0] == '.' && isdigit((unsigned char)spec[1])) {
                int prec = atoi(spec + 1);
                char fmt[16];
                snprintf(fmt, sizeof(fmt), "%%.%df", prec);
                printf(fmt, AS_F64(value));
            } else {
                printf("%g", AS_F64(value));
            }
            break;
        }
        default:
            printValue(value);
            break;
    }
}

static void print_string_interpolated(ObjString* str, Value* args, int* arg_index, int arg_count) {
    for (int i = 0; i < str->length; i++) {
        char c = str->chars[i];
        if (c == '\\') {
            if (i + 1 < str->length) {
                char next = str->chars[++i];
                switch (next) {
                    case 'n': putchar('\n'); break;
                    case 't': putchar('\t'); break;
                    case '"': putchar('"'); break;
                    case '\\': putchar('\\'); break;
                    default: putchar(next); break;
                }
            }
        } else if (c == '@') {
            char spec[16];
            int spec_len = 0;
            int j = i + 1;
            if (j < str->length && str->chars[j] == '.') {
                spec[spec_len++] = '.';
                j++;
                while (j < str->length && isdigit((unsigned char)str->chars[j])) {
                    if (spec_len < 15) spec[spec_len++] = str->chars[j];
                    j++;
                }
                if (j < str->length && str->chars[j] == 'f') {
                    if (spec_len < 15) spec[spec_len++] = 'f';
                    j++;
                }
            } else if (j < str->length && (str->chars[j]=='x' || str->chars[j]=='X' || str->chars[j]=='b' || str->chars[j]=='o')) {
                if (spec_len < 15) spec[spec_len++] = str->chars[j];
                j++;
            }
            spec[spec_len] = '\0';
            i = j - 1;
            if (*arg_index < arg_count) {
                print_formatted_value(args[(*arg_index)++], spec_len > 0 ? spec : NULL);
            } else {
                putchar('@');
                fputs(spec, stdout);
            }
        } else {
            putchar(c);
        }
    }
}

void builtin_print(Value* args, int count, bool newline, const char* separator) {
    int consumed = 0;
    bool first_value = true;
    
    // Use default space separator if none provided
    const char* sep = separator ? separator : " ";
    
    if (count > 0 && IS_STRING(args[0])) {
        ObjString* str = AS_STRING(args[0]);
        print_string_interpolated(str, args + 1, &consumed, count - 1);
        consumed += 1; // account for the format string
        first_value = false; // We've already printed something
    }
    
    for (int arg_index = consumed; arg_index < count; arg_index++) {
        if (!first_value) printf("%s", sep);
        printValue(args[arg_index]);
        first_value = false;
    }
    if (newline) putchar('\n');
    fflush(stdout);
}

void builtin_print_with_sep_value(Value* args, int count, bool newline, Value separator_value) {
    const char* sep = " "; // default
    
    // Extract separator string from Value
    if (IS_STRING(separator_value)) {
        ObjString* sepObj = AS_STRING(separator_value);
        sep = sepObj->chars;
    }
    
    builtin_print(args, count, newline, sep);
}

// Cross-platform high-precision timestamp function
// Returns SECONDS since an arbitrary but monotonic starting point (as double)
double builtin_time_stamp() {
#ifdef __APPLE__
// macOS: Use mach_absolute_time() → seconds (double)
init_timebase();
uint64_t abs_time = mach_absolute_time();
uint64_t nanoseconds = (abs_time * timebase_numer) / timebase_denom;
return (double)nanoseconds / 1e9;  // ns → s

#elif defined(__linux__)
    // Linux: Use clock_gettime → seconds (double)
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;  // s + ns → s
    }
    return 0.0;  // Error fallback

#elif defined(_WIN32)
// Windows: QueryPerformanceCounter → seconds (double)
static LARGE_INTEGER frequency = {0};
if (frequency.QuadPart == 0) {
    QueryPerformanceFrequency(&frequency);
}

LARGE_INTEGER counter;
QueryPerformanceCounter(&counter);
return (double)counter.QuadPart / (double)frequency.QuadPart;  // counts → s

#else
// Fallback: clock() → seconds (double)
#include <time.h>
    return (double)clock() / (double)CLOCKS_PER_SEC;
#endif
}
