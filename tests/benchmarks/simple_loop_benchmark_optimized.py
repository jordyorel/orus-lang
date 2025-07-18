#!/usr/bin/env python3
# Simple Loop Optimization Benchmark - Python Version with Manual Optimizations

print("=== Simple Loop Optimization Benchmark ===")

# Test 1: Small loop manually unrolled for Python optimization
print("Test 1: Small loop unrolling")
for outer in range(1, 1001):
    # Manually unroll small loop (1,2,3,4) for Python performance
    x = 1 * 2; y = x + 1; z = y * 3
    x = 2 * 2; y = x + 1; z = y * 3
    x = 3 * 2; y = x + 1; z = y * 3
    x = 4 * 2; y = x + 1; z = y * 3
print("Small loop test completed")

# Test 2: Medium loop kept normal (too large to unroll)
print("Test 2: Medium loop (not unrolled)")
for outer in range(1, 1001):
    for i in range(1, 16):
        x2 = i * 2
        y2 = x2 + 1
        z2 = y2 * 3
print("Medium loop test completed")

# Test 3: Single iteration loop optimized
print("Test 3: Single iteration loop")
for outer in range(1, 1001):
    # Single iteration - fully inlined
    x3 = 5 * 2
    y3 = x3 + 1
    z3 = y3 * 3
print("Single iteration test completed")

# Test 4: Step loop manually unrolled (0,2,4)
print("Test 4: Step loop")
for outer in range(1, 1001):
    # Manually unroll step loop for Python performance
    x4 = 0 * 2; y4 = x4 + 1; z4 = y4 * 3
    x4 = 2 * 2; y4 = x4 + 1; z4 = y4 * 3
    x4 = 4 * 2; y4 = x4 + 1; z4 = y4 * 3
print("Step loop test completed")

# Test 5: Two iteration loop manually unrolled (1,2,3)
print("Test 5: Two iteration loop")
for outer in range(1, 1001):
    # Manually unroll two iteration loop
    x5 = 1 * 2; y5 = x5 + 1; z5 = y5 * 3
    x5 = 2 * 2; y5 = x5 + 1; z5 = y5 * 3
    x5 = 3 * 2; y5 = x5 + 1; z5 = y5 * 3
print("Two iteration test completed")

print("=== Simple Loop Benchmark Complete ===")