#!/usr/bin/env node
/**
 * Function call benchmark for JavaScript/Node.js
 * Tests function call overhead and recursion performance
 */

// Recursive factorial (not tail-call optimized in most JS engines)
function factorialRecursive(n, acc = 1) {
    if (n <= 1) {
        return acc;
    } else {
        return factorialRecursive(n - 1, acc * n);
    }
}

// Fibonacci with manual tail call simulation
function fibonacciTail(n, a = 0, b = 1) {
    if (n === 0) {
        return a;
    } else {
        return fibonacciTail(n - 1, b, a + b);
    }
}

// Simple recursive countdown
function countdown(n) {
    if (n <= 0) {
        return 0;
    } else {
        return countdown(n - 1);
    }
}

// Nested function calls
function nestedCalls(n) {
    function level1(x) {
        return x + 1;
    }
    
    function level2(x) {
        return level1(x) + 1;
    }
    
    function level3(x) {
        return level2(x) + 1;
    }
    
    return level3(n);
}

// Mathematical function with multiple operations
function mathHeavy(x) {
    function square(n) {
        return n * n;
    }
    
    function cube(n) {
        return n * n * n;
    }
    
    return square(x) + cube(x) + x;
}

function main() {
    console.log("=== JavaScript Function Benchmark ===");
    
    const startTime = process.hrtime.bigint();
    
    // Test 1: Recursive factorial
    console.log("Test 1: Recursive factorial");
    const result1 = factorialRecursive(20);
    console.log(`Factorial of 20: ${result1}`);
    
    // Test 2: Fibonacci
    console.log("Test 2: Fibonacci");
    const result2 = fibonacciTail(30);
    console.log(`Fibonacci of 30: ${result2}`);
    
    // Test 3: Simple recursive countdown
    console.log("Test 3: Recursive countdown");
    const result3 = countdown(1000);
    console.log(`Countdown result: ${result3}`);
    
    // Test 4: Nested function calls
    console.log("Test 4: Nested function calls");
    let total = 0;
    for (let i = 0; i < 1000; i++) {
        total += nestedCalls(i);
    }
    console.log(`Nested calls total: ${total}`);
    
    // Test 5: Mathematical functions
    console.log("Test 5: Mathematical functions");
    let mathTotal = 0;
    for (let i = 0; i < 1000; i++) {
        mathTotal += mathHeavy(i);
    }
    console.log(`Math functions total: ${mathTotal}`);
    
    const endTime = process.hrtime.bigint();
    const duration = endTime - startTime;
    const durationMs = Number(duration) / 1000000;
    
    console.log("=== Benchmark Results ===");
    console.log(`Total time (nanoseconds): ${duration}`);
    console.log(`Total time (milliseconds): ${durationMs.toFixed(2)}`);
    console.log("Function benchmark completed");
}

main();