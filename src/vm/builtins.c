#include "../../include/builtins.h"
#include <stdio.h>
#include <string.h>

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
        } else if (c == '{' && i + 1 < str->length && str->chars[i + 1] == '}') {
            if (*arg_index < arg_count) {
                printValue(args[(*arg_index)++]);
            } else {
                fputs("{}", stdout);
            }
            i++; // skip '}'
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
