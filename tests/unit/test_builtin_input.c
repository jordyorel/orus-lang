#define _POSIX_C_SOURCE 200809L

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "runtime/builtins.h"
#include "runtime/memory.h"

#define ASSERT_TRUE(cond, message)                                                         \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            fprintf(stderr, "Assertion failed: %s (%s:%d)\n", message, __FILE__, __LINE__); \
            return false;                                                                  \
        }                                                                                  \
    } while (0)

static bool redirect_stdin_to_buffer(const char* contents, int* saved_fd, FILE** temp_file) {
    if (!saved_fd || !temp_file) {
        return false;
    }

    FILE* temp = tmpfile();
    if (!temp) {
        return false;
    }

    size_t length = contents ? strlen(contents) : 0;
    if (length > 0) {
        if (fwrite(contents, 1, length, temp) != length) {
            fclose(temp);
            return false;
        }
    }
    rewind(temp);

    int original_fd = dup(fileno(stdin));
    if (original_fd == -1) {
        fclose(temp);
        return false;
    }

    if (dup2(fileno(temp), fileno(stdin)) == -1) {
        close(original_fd);
        fclose(temp);
        return false;
    }

    clearerr(stdin);

    *saved_fd = original_fd;
    *temp_file = temp;
    return true;
}

static void restore_stdin_from_buffer(int saved_fd, FILE* temp_file) {
    if (saved_fd >= 0) {
        dup2(saved_fd, fileno(stdin));
        close(saved_fd);
        clearerr(stdin);
    }
    if (temp_file) {
        fclose(temp_file);
    }
}

static bool redirect_stdout_to_capture(int* saved_fd, FILE** capture_file) {
    if (!saved_fd || !capture_file) {
        return false;
    }

    FILE* capture = tmpfile();
    if (!capture) {
        return false;
    }

    int original_fd = dup(fileno(stdout));
    if (original_fd == -1) {
        fclose(capture);
        return false;
    }

    if (dup2(fileno(capture), fileno(stdout)) == -1) {
        close(original_fd);
        fclose(capture);
        return false;
    }

    clearerr(stdout);

    *saved_fd = original_fd;
    *capture_file = capture;
    return true;
}

static void restore_stdout_from_capture(int saved_fd, FILE* capture_file) {
    if (saved_fd >= 0) {
        fflush(stdout);
        dup2(saved_fd, fileno(stdout));
        close(saved_fd);
        clearerr(stdout);
    }
    if (capture_file) {
        fclose(capture_file);
    }
}

static bool read_capture(FILE* capture_file, char* buffer, size_t buffer_size, size_t* out_length) {
    if (!capture_file || !buffer || buffer_size == 0) {
        return false;
    }

    long current = ftell(capture_file);
    if (current < 0) {
        return false;
    }

    if (fseek(capture_file, 0, SEEK_SET) != 0) {
        return false;
    }

    size_t read = fread(buffer, 1, buffer_size - 1, capture_file);
    buffer[read] = '\0';

    if (out_length) {
        *out_length = read;
    }

    if (fseek(capture_file, current, SEEK_SET) != 0) {
        return false;
    }

    return true;
}

static bool test_builtin_input_reads_line_without_prompt(void) {
    initVM();

    int saved_stdin_fd = -1;
    FILE* temp_stdin = NULL;
    if (!redirect_stdin_to_buffer("hello world\n", &saved_stdin_fd, &temp_stdin)) {
        freeVM();
        return false;
    }

    Value out = BOOL_VAL(false);
    bool ok = builtin_input(NULL, 0, &out);

    restore_stdin_from_buffer(saved_stdin_fd, temp_stdin);

    if (!ok) {
        freeVM();
        return false;
    }

    ASSERT_TRUE(IS_STRING(out), "builtin_input should produce a string value");
    ObjString* str = AS_STRING(out);
    ASSERT_TRUE(str != NULL, "Resulting string should be non-null");
    ASSERT_TRUE(str->length == 11, "Input should capture characters up to newline");
    ASSERT_TRUE(strncmp(str->chars, "hello world", (size_t)str->length) == 0,
                "Captured text should match input");

    freeVM();
    return true;
}

static bool test_builtin_input_allows_empty_line(void) {
    initVM();

    int saved_stdin_fd = -1;
    FILE* temp_stdin = NULL;
    if (!redirect_stdin_to_buffer("\n", &saved_stdin_fd, &temp_stdin)) {
        freeVM();
        return false;
    }

    Value out = BOOL_VAL(true);
    bool ok = builtin_input(NULL, 0, &out);

    restore_stdin_from_buffer(saved_stdin_fd, temp_stdin);

    if (!ok) {
        freeVM();
        return false;
    }

    ASSERT_TRUE(IS_STRING(out), "Empty line should still produce a string");
    ObjString* str = AS_STRING(out);
    ASSERT_TRUE(str != NULL, "Resulting string should be allocated");
    ASSERT_TRUE(str->length == 0, "Empty line should result in zero-length string");

    freeVM();
    return true;
}

static bool test_builtin_input_returns_false_on_eof(void) {
    initVM();

    int saved_stdin_fd = -1;
    FILE* temp_stdin = NULL;
    if (!redirect_stdin_to_buffer("", &saved_stdin_fd, &temp_stdin)) {
        freeVM();
        return false;
    }

    Value sentinel = BOOL_VAL(true);
    bool ok = builtin_input(NULL, 0, &sentinel);

    restore_stdin_from_buffer(saved_stdin_fd, temp_stdin);

    if (ok) {
        freeVM();
        return false;
    }

    ASSERT_TRUE(IS_BOOL(sentinel) && AS_BOOL(sentinel),
                "Output value should remain untouched on EOF");

    freeVM();
    return true;
}

static bool test_builtin_input_writes_prompt(void) {
    initVM();

    int saved_stdin_fd = -1;
    FILE* temp_stdin = NULL;
    if (!redirect_stdin_to_buffer("value\n", &saved_stdin_fd, &temp_stdin)) {
        freeVM();
        return false;
    }

    int saved_stdout_fd = -1;
    FILE* capture = NULL;
    if (!redirect_stdout_to_capture(&saved_stdout_fd, &capture)) {
        restore_stdin_from_buffer(saved_stdin_fd, temp_stdin);
        freeVM();
        return false;
    }

    Value prompt_args[1];
    prompt_args[0] = STRING_VAL(allocateString(">>> ", 4));

    Value out = BOOL_VAL(false);
    bool ok = builtin_input(prompt_args, 1, &out);

    fflush(stdout);

    char buffer[32];
    size_t written = 0;
    bool captured = read_capture(capture, buffer, sizeof(buffer), &written);

    restore_stdout_from_capture(saved_stdout_fd, capture);
    restore_stdin_from_buffer(saved_stdin_fd, temp_stdin);

    if (!ok || !captured) {
        freeVM();
        return false;
    }

    ASSERT_TRUE(written == 4, "Prompt output length should match prompt text");
    ASSERT_TRUE(strncmp(buffer, ">>> ", written) == 0, "Prompt output should match provided string");

    ASSERT_TRUE(IS_STRING(out), "Prompted input should still produce string value");
    ObjString* str = AS_STRING(out);
    ASSERT_TRUE(str != NULL, "Captured string should be allocated");
    ASSERT_TRUE(str->length == 5, "Captured input should exclude newline");
    ASSERT_TRUE(strncmp(str->chars, "value", (size_t)str->length) == 0,
                "Captured input should match provided line");

    freeVM();
    return true;
}

int main(void) {
    bool (*tests[])(void) = {
        test_builtin_input_reads_line_without_prompt,
        test_builtin_input_allows_empty_line,
        test_builtin_input_returns_false_on_eof,
        test_builtin_input_writes_prompt,
    };

    const char* names[] = {
        "builtin_input captures characters before newline",
        "builtin_input returns empty string for blank line",
        "builtin_input signals failure on EOF",
        "builtin_input writes prompt and captures response",
    };

    int passed = 0;
    int total = (int)(sizeof(tests) / sizeof(tests[0]));

    for (int i = 0; i < total; i++) {
        if (tests[i]()) {
            printf("[PASS] %s\n", names[i]);
            passed++;
        } else {
            printf("[FAIL] %s\n", names[i]);
            return 1;
        }
    }

    printf("%d/%d builtin input tests passed\n", passed, total);
    return 0;
}
