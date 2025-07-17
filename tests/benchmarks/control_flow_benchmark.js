#!/usr/bin/env node
// Control Flow Performance Benchmark - JavaScript (Node.js)
// Focus on while loops and conditional performance

console.log("=== JavaScript Control Flow Performance Benchmark ===");

const start_time = Date.now();

// === PHASE 1: BASIC WHILE LOOPS ===
console.log("Phase 1: Basic While Loops");

// Simple counting loop
let counter = 0;
while (counter < 100000) {
    counter = counter + 1;
}
console.log("Counter result:", counter);

// Summation loop
let sum_result = 0;
let i = 0;
while (i < 50000) {
    sum_result = sum_result + i;
    i = i + 1;
}
console.log("Sum result:", sum_result);

// Multiplication accumulator
let product = 1;
let j = 1;
while (j <= 20) {
    product = product * j;
    j = j + 1;
}
console.log("Product result:", product);

// === PHASE 2: NESTED LOOPS ===
console.log("Phase 2: Nested Loops");

let nested_sum = 0;
let outer = 0;
while (outer < 500) {
    let inner = 0;
    while (inner < 200) {
        nested_sum = nested_sum + 1;
        inner = inner + 1;
    }
    outer = outer + 1;
}
console.log("Nested sum result:", nested_sum);

// Matrix-like computation
let matrix_result = 0;
let row = 0;
while (row < 300) {
    let col = 0;
    while (col < 333) {
        matrix_result = matrix_result + (row * col);
        col = col + 1;
    }
    row = row + 1;
}
console.log("Matrix result:", matrix_result);

// === PHASE 3: CONDITIONAL PERFORMANCE ===
console.log("Phase 3: Conditional Performance");

// If-else chains in loops
let conditional_sum = 0;
let k = 0;
while (k < 100000) {
    if (k % 5 === 0) {
        conditional_sum = conditional_sum + k;
    } else {
        if (k % 3 === 0) {
            conditional_sum = conditional_sum + (k * 2);
        } else {
            conditional_sum = conditional_sum + 1;
        }
    }
    k = k + 1;
}
console.log("Conditional sum result:", conditional_sum);

// Complex boolean expressions
let bool_ops = 0;
let m = 0;
while (m < 50000) {
    if (m > 100) {
        if (m < 40000) {
            if (m % 7 === 0) {
                bool_ops = bool_ops + 1;
            }
        }
    }
    m = m + 1;
}
console.log("Boolean operations result:", bool_ops);

// === PHASE 4: FIBONACCI SEQUENCE ===
console.log("Phase 4: Fibonacci Sequence");

const fib_n = 35;
let fib_a = 0;
let fib_b = 1;
let fib_i = 2;
while (fib_i <= fib_n) {
    const fib_temp = fib_a + fib_b;
    fib_a = fib_b;
    fib_b = fib_temp;
    fib_i = fib_i + 1;
}
console.log("Fibonacci result:", fib_b);

// === PHASE 5: PRIME NUMBER SIEVE ===
console.log("Phase 5: Prime Number Sieve");

// Simple prime finding
const prime_limit = 10000;
let prime_count = 0;
let candidate = 2;
while (candidate <= prime_limit) {
    let is_prime = true;
    let divisor = 2;
    while (divisor * divisor <= candidate) {
        if (candidate % divisor === 0) {
            is_prime = false;
            divisor = candidate;  // Break out of inner loop
        } else {
            divisor = divisor + 1;
        }
    }
    if (is_prime) {
        prime_count = prime_count + 1;
    }
    candidate = candidate + 1;
}
console.log("Prime count result:", prime_count);

// === PHASE 6: STRING OPERATIONS WITH LOOPS ===
console.log("Phase 6: String Operations with Loops");

// String building simulation (using numbers)
let string_sim = 0;
let char_code = 65;  // ASCII 'A'
let str_length = 0;
while (str_length < 10000) {
    string_sim = string_sim + char_code;
    char_code = char_code + 1;
    if (char_code > 90) {  // ASCII 'Z'
        char_code = 65;  // Reset to 'A'
    }
    str_length = str_length + 1;
}
console.log("String simulation result:", string_sim);

// === PHASE 7: MATHEMATICAL SERIES ===
console.log("Phase 7: Mathematical Series");

// Pi approximation using Leibniz formula
let pi_approx = 0;
let term = 1;
let sign = 1;
let series_i = 0;
while (series_i < 100000) {
    pi_approx = pi_approx + (sign * 1000000 / term);
    sign = -sign;
    term = term + 2;
    series_i = series_i + 1;
}
const pi_result = pi_approx * 4 / 1000000;
console.log("Pi approximation result:", pi_result);

// Square root using Newton's method
const sqrt_target = 123456;
let sqrt_x = sqrt_target / 2;
let sqrt_iterations = 0;
while (sqrt_iterations < 10000) {
    sqrt_x = (sqrt_x + sqrt_target / sqrt_x) / 2;
    sqrt_iterations = sqrt_iterations + 1;
}
console.log("Square root result:", sqrt_x);

// === FINAL BENCHMARK RESULTS ===
const end_time = Date.now();
const total_elapsed = end_time - start_time;

console.log("=== CONTROL FLOW BENCHMARK COMPLETE ===");
console.log("Total execution time:", total_elapsed);
console.log("While loops executed: 15+");
console.log("Nested loops: 2");
console.log("Conditional operations: 150,000+");
console.log("Mathematical algorithms: 4");
console.log("Total iterations: 500,000+");
console.log("Final benchmark score:", total_elapsed * counter / 1000);
console.log("=== JavaScript Control Flow Benchmark Complete ===");