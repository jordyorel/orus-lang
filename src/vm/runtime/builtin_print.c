// Orus Language Project
// ---------------------------------------------------------------------------
// File: src/vm/runtime/builtin_print.c
// Author: Jordy Orel KONDA
// Description: Implements the builtin print routine for formatted output.


#include "runtime/builtins.h"
#include "vm/vm_constants.h"
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
                print_raw_f64(AS_F64(value));
            }
            break;
        }
        default:
            printValue(value);
            break;
    }
}

static void print_string_interpolated(ObjString* str, Value* args, int* arg_index,
                                      int arg_count) {
    const char* chars = string_get_chars(str);
    if (!chars) {
        printValue(STRING_VAL(str));
        return;
    }

    for (int i = 0; i < str->length; i++) {
        char c = chars[i];
        if (c == '\\') {
            if (i + 1 < str->length) {
                char next = chars[++i];
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
            if (j < str->length && chars[j] == '.') {
                spec[spec_len++] = '.';
                j++;
                while (j < str->length && isdigit((unsigned char)chars[j])) {
                    if (spec_len < 15) spec[spec_len++] = chars[j];
                    j++;
                }
                if (j < str->length && chars[j] == 'f') {
                    if (spec_len < 15) spec[spec_len++] = 'f';
                    j++;
                }
            } else if (j < str->length && (chars[j] == 'x' || chars[j] == 'X' ||
                                            chars[j] == 'b' || chars[j] == 'o')) {
                if (spec_len < 15) spec[spec_len++] = chars[j];
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
    bool first_value = true;

    const char* sep = " ";

    if (count > 0 && IS_STRING(args[0])) {
        ObjString* str = AS_STRING(args[0]);
        print_string_interpolated(str, args + 1, &consumed, count - 1);
        consumed += 1;  // account for the format string
        first_value = false;  // We've already printed something
    }

    for (int arg_index = consumed; arg_index < count; arg_index++) {
        if (!first_value) printf("%s", sep);
        printValue(args[arg_index]);
        first_value = false;
    }
    if (newline) putchar('\n');
    fflush(stdout);
}
