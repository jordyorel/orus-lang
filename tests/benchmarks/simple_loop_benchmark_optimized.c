#include <stdio.h>

// Simple Loop Optimization Benchmark - C Version with Manual Unrolling

int main() {
    printf("=== Simple Loop Optimization Benchmark ===\n");
    
    // Test 1: Small loop manually unrolled
    printf("Test 1: Small loop unrolling\n");
    for (int outer = 1; outer <= 1000; outer++) {
        // Manually unroll small loop (1,2,3,4) for C performance
        int x = 1 * 2; int y = x + 1; int z = y * 3;
        x = 2 * 2; y = x + 1; z = y * 3;
        x = 3 * 2; y = x + 1; z = y * 3;
        x = 4 * 2; y = x + 1; z = y * 3;
    }
    printf("Small loop test completed\n");

    // Test 2: Medium loop kept normal (too large to unroll)
    printf("Test 2: Medium loop (not unrolled)\n");
    for (int outer = 1; outer <= 1000; outer++) {
        for (int i = 1; i <= 15; i++) {
            int x2 = i * 2;
            int y2 = x2 + 1;
            int z2 = y2 * 3;
        }
    }
    printf("Medium loop test completed\n");

    // Test 3: Single iteration loop optimized
    printf("Test 3: Single iteration loop\n");
    for (int outer = 1; outer <= 1000; outer++) {
        // Single iteration - fully inlined
        int x3 = 5 * 2;
        int y3 = x3 + 1;
        int z3 = y3 * 3;
    }
    printf("Single iteration test completed\n");

    // Test 4: Step loop manually unrolled (0,2,4)
    printf("Test 4: Step loop\n");
    for (int outer = 1; outer <= 1000; outer++) {
        // Manually unroll step loop for C performance
        int x4 = 0 * 2; int y4 = x4 + 1; int z4 = y4 * 3;
        x4 = 2 * 2; y4 = x4 + 1; z4 = y4 * 3;
        x4 = 4 * 2; y4 = x4 + 1; z4 = y4 * 3;
    }
    printf("Step loop test completed\n");

    // Test 5: Two iteration loop manually unrolled (1,2,3)
    printf("Test 5: Two iteration loop\n");
    for (int outer = 1; outer <= 1000; outer++) {
        // Manually unroll two iteration loop
        int x5 = 1 * 2; int y5 = x5 + 1; int z5 = y5 * 3;
        x5 = 2 * 2; y5 = x5 + 1; z5 = y5 * 3;
        x5 = 3 * 2; y5 = x5 + 1; z5 = y5 * 3;
    }
    printf("Two iteration test completed\n");

    printf("=== Simple Loop Benchmark Complete ===\n");
    return 0;
}