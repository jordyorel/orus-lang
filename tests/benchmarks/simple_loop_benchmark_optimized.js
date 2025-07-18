#!/usr/bin/env node
// Simple Loop Optimization Benchmark - JavaScript Version with V8 Optimizations

console.log("=== Simple Loop Optimization Benchmark ===");

// Test 1: Small loop manually unrolled for V8 optimization
console.log("Test 1: Small loop unrolling");
for (let outer = 1; outer <= 1000; outer++) {
    // Manually unroll small loop (1,2,3,4) for V8 performance
    let x = 1 * 2, y = x + 1, z = y * 3;
    x = 2 * 2; y = x + 1; z = y * 3;
    x = 3 * 2; y = x + 1; z = y * 3;
    x = 4 * 2; y = x + 1; z = y * 3;
}
console.log("Small loop test completed");

// Test 2: Medium loop kept normal (too large to unroll)
console.log("Test 2: Medium loop (not unrolled)");
for (let outer = 1; outer <= 1000; outer++) {
    for (let i = 1; i <= 15; i++) {
        let x2 = i * 2;
        let y2 = x2 + 1;
        let z2 = y2 * 3;
    }
}
console.log("Medium loop test completed");

// Test 3: Single iteration loop optimized
console.log("Test 3: Single iteration loop");
for (let outer = 1; outer <= 1000; outer++) {
    // Single iteration - fully inlined
    let x3 = 5 * 2;
    let y3 = x3 + 1;
    let z3 = y3 * 3;
}
console.log("Single iteration test completed");

// Test 4: Step loop manually unrolled (0,2,4)
console.log("Test 4: Step loop");
for (let outer = 1; outer <= 1000; outer++) {
    // Manually unroll step loop for V8 performance
    let x4 = 0 * 2, y4 = x4 + 1, z4 = y4 * 3;
    x4 = 2 * 2; y4 = x4 + 1; z4 = y4 * 3;
    x4 = 4 * 2; y4 = x4 + 1; z4 = y4 * 3;
}
console.log("Step loop test completed");

// Test 5: Two iteration loop manually unrolled (1,2,3)
console.log("Test 5: Two iteration loop");
for (let outer = 1; outer <= 1000; outer++) {
    // Manually unroll two iteration loop
    let x5 = 1 * 2, y5 = x5 + 1, z5 = y5 * 3;
    x5 = 2 * 2; y5 = x5 + 1; z5 = y5 * 3;
    x5 = 3 * 2; y5 = x5 + 1; z5 = y5 * 3;
}
console.log("Two iteration test completed");

console.log("=== Simple Loop Benchmark Complete ===");