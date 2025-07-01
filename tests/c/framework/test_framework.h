#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Test framework globals
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

// ANSI color codes
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_RESET   "\033[0m"

// Test macros
#define ASSERT(condition, message) \
    do { \
        tests_run++; \
        if (condition) { \
            tests_passed++; \
            printf("  " COLOR_GREEN "✓" COLOR_RESET " %s\n", message); \
        } else { \
            tests_failed++; \
            printf("  " COLOR_RED "✗" COLOR_RESET " %s\n", message); \
        } \
    } while (0)

#define ASSERT_EQ(expected, actual, message) \
    do { \
        tests_run++; \
        if ((expected) == (actual)) { \
            tests_passed++; \
            printf("  " COLOR_GREEN "✓" COLOR_RESET " %s\n", message); \
        } else { \
            tests_failed++; \
            printf("  " COLOR_RED "✗" COLOR_RESET " %s (expected: %d, got: %d)\n", message, (int)(expected), (int)(actual)); \
        } \
    } while (0)

#define ASSERT_STR_EQ(expected, actual, message) \
    do { \
        tests_run++; \
        if (strcmp((expected), (actual)) == 0) { \
            tests_passed++; \
            printf("  " COLOR_GREEN "✓" COLOR_RESET " %s\n", message); \
        } else { \
            tests_failed++; \
            printf("  " COLOR_RED "✗" COLOR_RESET " %s (expected: '%s', got: '%s')\n", message, (expected), (actual)); \
        } \
    } while (0)

#define RUN_TEST(test_func) \
    do { \
        printf(COLOR_YELLOW "Running %s..." COLOR_RESET "\n", #test_func); \
        test_func(); \
        printf("\n"); \
    } while (0)

#define PRINT_TEST_RESULTS() \
    do { \
        printf("========================================\n"); \
        if (tests_failed == 0) { \
            printf(COLOR_GREEN "All %d tests passed!" COLOR_RESET "\n", tests_passed); \
        } else { \
            printf(COLOR_RED "%d test(s) failed, %d test(s) passed." COLOR_RESET "\n", tests_failed, tests_passed); \
        } \
        printf("Total tests run: %d\n", tests_run); \
        printf("========================================\n"); \
    } while (0)

#endif // TEST_FRAMEWORK_H