/* Unity Test Framework for C
 * Lightweight testing framework for C unit tests
 */

#ifndef UNITY_H
#define UNITY_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

// Test result counters
typedef struct {
    int tests_run;
    int tests_passed;
    int tests_failed;
} UnityResults;

extern UnityResults unity_results;

// Test macros
#define UNITY_BEGIN() \
    do { \
        unity_results.tests_run = 0; \
        unity_results.tests_passed = 0; \
        unity_results.tests_failed = 0; \
        printf("Unity Test Framework - Starting Tests\n"); \
        printf("=====================================\n"); \
    } while(0)

#define UNITY_END() \
    do { \
        printf("=====================================\n"); \
        printf("Tests Run: %d, Passed: %d, Failed: %d\n", \
               unity_results.tests_run, unity_results.tests_passed, unity_results.tests_failed); \
        return (unity_results.tests_failed == 0) ? 0 : 1; \
    } while(0)

#define RUN_TEST(test_func) \
    do { \
        printf("Running %s... ", #test_func); \
        unity_results.tests_run++; \
        test_func(); \
        printf("PASS\n"); \
        unity_results.tests_passed++; \
    } while(0)

// Assertion macros
#define TEST_ASSERT_TRUE(condition) \
    do { \
        if (!(condition)) { \
            printf("FAIL\n  Expected TRUE but was FALSE at %s:%d\n", __FILE__, __LINE__); \
            unity_results.tests_failed++; \
            return; \
        } \
    } while(0)

#define TEST_ASSERT_FALSE(condition) \
    do { \
        if (condition) { \
            printf("FAIL\n  Expected FALSE but was TRUE at %s:%d\n", __FILE__, __LINE__); \
            unity_results.tests_failed++; \
            return; \
        } \
    } while(0)

#define TEST_ASSERT_EQUAL_INT(expected, actual) \
    do { \
        if ((expected) != (actual)) { \
            printf("FAIL\n  Expected %d but was %d at %s:%d\n", (expected), (actual), __FILE__, __LINE__); \
            unity_results.tests_failed++; \
            return; \
        } \
    } while(0)

#define TEST_ASSERT_EQUAL_UINT8(expected, actual) \
    do { \
        if ((expected) != (actual)) { \
            printf("FAIL\n  Expected 0x%02X but was 0x%02X at %s:%d\n", (expected), (actual), __FILE__, __LINE__); \
            unity_results.tests_failed++; \
            return; \
        } \
    } while(0)

#define TEST_ASSERT_NOT_NULL(pointer) \
    do { \
        if ((pointer) == NULL) { \
            printf("FAIL\n  Expected non-NULL pointer at %s:%d\n", __FILE__, __LINE__); \
            unity_results.tests_failed++; \
            return; \
        } \
    } while(0)

#define TEST_ASSERT_NULL(pointer) \
    do { \
        if ((pointer) != NULL) { \
            printf("FAIL\n  Expected NULL pointer at %s:%d\n", __FILE__, __LINE__); \
            unity_results.tests_failed++; \
            return; \
        } \
    } while(0)

#endif // UNITY_H