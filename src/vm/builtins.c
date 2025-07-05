#define _POSIX_C_SOURCE 200809L
#include "../../include/builtins.h"
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
    char buf[65];
    int index = 64;
    buf[64] = '\0';
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

void builtin_print(Value* args, int count, bool newline) {
    int consumed = 0;
    if (count > 0 && IS_STRING(args[0])) {
        ObjString* str = AS_STRING(args[0]);
        print_string_interpolated(str, args + 1, &consumed, count - 1);
        consumed += 1; // account for the format string
    }
    for (int arg_index = consumed; arg_index < count; arg_index++) {
        if (arg_index > 0) putchar(' ');
        printValue(args[arg_index]);
    }
    if (newline) putchar('\n');
    fflush(stdout);
}

// Cross-platform high-precision timestamp function
// Returns nanoseconds since an arbitrary but monotonic starting point
int64_t builtin_time_stamp() {
#ifdef __APPLE__
    // macOS: Use mach_absolute_time() - fastest and most precise
    init_timebase();
    uint64_t abs_time = mach_absolute_time();
    return (int64_t)((abs_time * timebase_numer) / timebase_denom);
    
#elif defined(__linux__)
    // Linux: Use clock_gettime with CLOCK_MONOTONIC
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (int64_t)(ts.tv_sec * 1000000000LL + ts.tv_nsec);
    }
    return 0; // Error fallback
    
#elif defined(_WIN32)
    // Windows: Use QueryPerformanceCounter
    static LARGE_INTEGER frequency = {0};
    if (frequency.QuadPart == 0) {
        QueryPerformanceFrequency(&frequency);
    }
    
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    
    // Convert to nanoseconds
    return (int64_t)((counter.QuadPart * 1000000000LL) / frequency.QuadPart);
    
#else
    // Fallback: Use standard clock() - less precise but portable
    #include <time.h>
    return (int64_t)((clock() * 1000000000LL) / CLOCKS_PER_SEC);
#endif
}
