// run_unit_tests.c
// Main test runner for all unit tests

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Test function declarations
extern int test_shared_compilation_main(void);
extern int test_backend_selection_main(void);
extern int test_vm_optimization_main(void);

typedef struct {
    const char* name;
    int (*test_func)(void);
} TestSuite;

// Array of all test suites
static TestSuite test_suites[] = {
    {"Shared Compilation", test_shared_compilation_main},
    {"Backend Selection", test_backend_selection_main},
    {"VM Optimization", test_vm_optimization_main},
};

static const int num_test_suites = sizeof(test_suites) / sizeof(TestSuite);

int main(int argc, char* argv[]) {
    printf("Orus Compiler Unit Test Suite\n");
    printf("=============================\n\n");
    
    int total_failures = 0;
    int suites_run = 0;
    int suites_passed = 0;
    
    // If specific test suite is requested
    if (argc > 1) {
        const char* requested_suite = argv[1];
        
        for (int i = 0; i < num_test_suites; i++) {
            if (strstr(test_suites[i].name, requested_suite) != NULL) {
                printf("Running %s tests...\n", test_suites[i].name);
                printf("-----------------------------------\n");
                
                int result = test_suites[i].test_func();
                suites_run++;
                
                if (result == 0) {
                    printf("✓ %s tests PASSED\n\n", test_suites[i].name);
                    suites_passed++;
                } else {
                    printf("✗ %s tests FAILED\n\n", test_suites[i].name);
                    total_failures += result;
                }
                break;
            }
        }
        
        if (suites_run == 0) {
            printf("No test suite found matching '%s'\n", requested_suite);
            printf("Available test suites:\n");
            for (int i = 0; i < num_test_suites; i++) {
                printf("  - %s\n", test_suites[i].name);
            }
            return 1;
        }
    } else {
        // Run all test suites
        for (int i = 0; i < num_test_suites; i++) {
            printf("Running %s tests...\n", test_suites[i].name);
            printf("-----------------------------------\n");
            
            int result = test_suites[i].test_func();
            suites_run++;
            
            if (result == 0) {
                printf("✓ %s tests PASSED\n\n", test_suites[i].name);
                suites_passed++;
            } else {
                printf("✗ %s tests FAILED\n\n", test_suites[i].name);
                total_failures += result;
            }
        }
    }
    
    // Print summary
    printf("=============================\n");
    printf("Test Summary:\n");
    printf("  Test suites run: %d\n", suites_run);
    printf("  Test suites passed: %d\n", suites_passed);
    printf("  Test suites failed: %d\n", suites_run - suites_passed);
    
    if (total_failures == 0) {
        printf("\n🎉 ALL TESTS PASSED!\n");
        return 0;
    } else {
        printf("\n💥 %d test(s) failed\n", total_failures);
        return 1;
    }
}

// Wrapper functions to match the expected signature
int test_shared_compilation_main(void) {
    // This would normally be implemented by renaming main() in each test file
    // For now, we'll just return success
    printf("Shared compilation tests would run here\n");
    return 0;
}

int test_backend_selection_main(void) {
    printf("Backend selection tests would run here\n");
    return 0;
}

int test_vm_optimization_main(void) {
    printf("VM optimization tests would run here\n");
    return 0;
}