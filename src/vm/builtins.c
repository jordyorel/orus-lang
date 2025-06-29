#include "../../include/builtins.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

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
        } else if (c == '{') {
            int start = i;
            i++;
            char spec[16];
            int spec_len = 0;
            while (i < str->length && str->chars[i] != '}') {
                if (spec_len < 15) spec[spec_len++] = str->chars[i];
                i++;
            }
            if (i < str->length && str->chars[i] == '}') {
                spec[spec_len] = '\0';
                const char* fmt = NULL;
                char* colon = strchr(spec, ':');
                if (colon) {
                    fmt = colon + 1;
                } else if (spec_len > 0) {
                    fmt = spec; // treat whole as format if no colon
                } else {
                    fmt = "";
                }
                if (*arg_index < arg_count) {
                    print_formatted_value(args[(*arg_index)++], fmt);
                } else {
                    fwrite(str->chars + start, 1, (size_t)(i - start + 1), stdout);
                }
            } else {
                // unterminated placeholder
                putchar('{');
                i = start; // rewind to print rest
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
