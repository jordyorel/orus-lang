#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vm/vm.h"

#define ASSERT_TRUE(cond, message)                                                         \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            fprintf(stderr, "Assertion failed: %s (%s:%d)\n", message, __FILE__, __LINE__); \
            return false;                                                                  \
        }                                                                                  \
    } while (0)

static bool capture_print_output(double value, char* buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) {
        return false;
    }

    if (fflush(stdout) != 0) {
        return false;
    }

    int original_fd = dup(fileno(stdout));
    if (original_fd < 0) {
        return false;
    }

    FILE* tmp = tmpfile();
    if (!tmp) {
        close(original_fd);
        return false;
    }

    int tmp_fd = fileno(tmp);
    if (tmp_fd < 0) {
        fclose(tmp);
        close(original_fd);
        return false;
    }

    if (dup2(tmp_fd, fileno(stdout)) < 0) {
        fclose(tmp);
        close(original_fd);
        return false;
    }

    print_raw_f64(value);

    if (fflush(stdout) != 0) {
        dup2(original_fd, fileno(stdout));
        close(original_fd);
        fclose(tmp);
        return false;
    }

    if (fseek(tmp, 0, SEEK_SET) != 0) {
        dup2(original_fd, fileno(stdout));
        close(original_fd);
        fclose(tmp);
        return false;
    }

    size_t read = fread(buffer, 1, buffer_size - 1, tmp);
    buffer[read] = '\0';

    dup2(original_fd, fileno(stdout));
    close(original_fd);
    fclose(tmp);

    return true;
}

static bool test_prints_small_magnitudes_as_non_zero(void) {
    char output[128];
    ASSERT_TRUE(capture_print_output(1e-18, output, sizeof(output)),
                "capture_print_output should succeed");
    double parsed = strtod(output, NULL);
    ASSERT_TRUE(parsed != 0.0, "Formatted output should not parse to zero for 1e-18");
    ASSERT_TRUE(strchr(output, 'e') != NULL || strchr(output, 'E') != NULL,
                "Small magnitudes should use scientific notation");
    return true;
}

static bool test_preserves_exponent_when_trimming(void) {
    char output[128];
    ASSERT_TRUE(capture_print_output(1.23e-5, output, sizeof(output)),
                "capture_print_output should succeed");
    ASSERT_TRUE(strchr(output, 'e') != NULL || strchr(output, 'E') != NULL,
                "Exponent should be preserved after trimming");
    ASSERT_TRUE(strstr(output, "e-") != NULL, "Exponent sign should be present");
    return true;
}

static bool test_trims_trailing_zeros_for_fixed_point(void) {
    char output[128];
    ASSERT_TRUE(capture_print_output(42.0, output, sizeof(output)),
                "capture_print_output should succeed");
    ASSERT_TRUE(strcmp(output, "42") == 0, "Trailing zeros should be trimmed in fixed format");
    return true;
}

int main(void) {
    struct {
        const char* name;
        bool (*fn)(void);
    } tests[] = {
        {"prints very small magnitudes as non-zero", test_prints_small_magnitudes_as_non_zero},
        {"preserves exponent when trimming", test_preserves_exponent_when_trimming},
        {"trims trailing zeros for fixed-point output", test_trims_trailing_zeros_for_fixed_point},
    };

    int passed = 0;
    int total = (int)(sizeof(tests) / sizeof(tests[0]));

    for (int i = 0; i < total; ++i) {
        if (tests[i].fn()) {
            printf("[PASS] %s\n", tests[i].name);
            passed++;
        } else {
            printf("[FAIL] %s\n", tests[i].name);
            return 1;
        }
    }

    printf("%d/%d VM print formatting tests passed\n", passed, total);
    return 0;
}
