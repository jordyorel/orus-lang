// Orus Language Project

#include "runtime/builtins.h"
#include "runtime/memory.h"
#include "vm/vm_string_ops.h"

#include <errno.h>
#include <stdio.h>

#if defined(_WIN32)
#    include <io.h>
#else
#    include <unistd.h>
#endif

static void display_default_prompt_if_interactive(void) {
#if defined(_WIN32)
    int stdin_fd = _fileno(stdin);
    int stderr_fd = _fileno(stderr);
    if (stdin_fd >= 0 && stderr_fd >= 0 && _isatty(stdin_fd) && _isatty(stderr_fd)) {
        fputs("input> ", stderr);
        fflush(stderr);
    }
#else
    if (isatty(STDIN_FILENO) && isatty(STDERR_FILENO)) {
        fputs("input> ", stderr);
        fflush(stderr);
    }
#endif
}

static char* read_line_dynamic(size_t* out_capacity, int* out_length) {
    if (!out_capacity || !out_length) {
        return NULL;
    }

    size_t capacity = 128;
    char* buffer = (char*)reallocate(NULL, 0, capacity);
    if (!buffer) {
        return NULL;
    }

    size_t length = 0;
    bool saw_character = false;

    while (1) {
        errno = 0;
        int ch = fgetc(stdin);
        if (ch == EOF) {
            if (ferror(stdin)) {
                int err = errno;
                if (err == EINTR) {
                    clearerr(stdin);
                    continue;
                }
                reallocate(buffer, capacity, 0);
                return NULL;
            }
            if (!saw_character) {
                reallocate(buffer, capacity, 0);
                return NULL;
            }
            break;
        }

        saw_character = true;

        if (ch == '\n') {
            break;
        }

        if (ch == '\r') {
            int next = fgetc(stdin);
            if (next != '\n' && next != EOF) {
                ungetc(next, stdin);
            }
            break;
        }

        if (length + 1 >= capacity) {
            size_t new_capacity = capacity < 1024 ? capacity * 2 : capacity + capacity / 2;
            char* new_buffer = (char*)reallocate(buffer, capacity, new_capacity);
            if (!new_buffer) {
                reallocate(buffer, capacity, 0);
                return NULL;
            }
            buffer = new_buffer;
            capacity = new_capacity;
        }

        buffer[length++] = (char)ch;
    }

    buffer[length] = '\0';

    *out_capacity = capacity;
    *out_length = (int)length;
    return buffer;
}

bool builtin_input(Value* args, int count, Value* out_value) {
    if (!out_value) {
        return false;
    }

    if (count < 0 || count > 1) {
        return false;
    }

    bool has_explicit_prompt = (count == 1 && args);

    if (has_explicit_prompt) {
        Value prompt = args[0];
        if (IS_STRING(prompt)) {
            ObjString* prompt_str = AS_STRING(prompt);
            if (prompt_str && prompt_str->length > 0) {
                const char* prompt_chars = string_get_chars(prompt_str);
                if (prompt_chars) {
                    fwrite(prompt_chars, sizeof(char), (size_t)prompt_str->length, stdout);
                }
            }
        } else {
            printValue(prompt);
        }
    }

    fflush(stdout);

    if (!has_explicit_prompt) {
        display_default_prompt_if_interactive();
    }

    size_t capacity = 0;
    int length = 0;
    char* buffer = read_line_dynamic(&capacity, &length);
    if (!buffer) {
        return false;
    }

    ObjString* result = allocateStringFromBuffer(buffer, capacity, length);
    if (!result) {
        reallocate(buffer, capacity, 0);
        return false;
    }

    *out_value = STRING_VAL(result);
    return true;
}
