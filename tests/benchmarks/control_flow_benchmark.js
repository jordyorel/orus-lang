#!/usr/bin/env node
// Control Flow Benchmark for Cross-Language Performance Testing
// Equivalent to control_flow_benchmark.orus

const start = process.hrtime.bigint();

// Test 1: Simple For Loop Performance (1 million iterations)
let simple_counter = 0;
for (let i = 0; i < 1000000; i++) {
    simple_counter++;
}

console.log(simple_counter);

// Test 2: Nested Loop Performance (1000 x 1000)
let nested_total = 0;
for (let i = 0; i < 1000; i++) {
    for (let j = 0; j < 1000; j++) {
        nested_total++;
    }
}

console.log(nested_total);

// Test 3: While Loop with Conditional (100K iterations)
let while_counter = 0;
let condition_hits = 0;
while (while_counter < 100000) {
    if (while_counter % 2 === 0) {
        condition_hits++;
    }
    while_counter++;
}

console.log(condition_hits);

// Test 4: Conditional Logic (50K iterations)
let complex_result = 0;
for (let i = 0; i < 50000; i++) {
    if (i % 3 === 0) {
        complex_result += 3;
    } else {
        if (i % 5 === 0) {
            complex_result += 5;
        } else {
            complex_result += 1;
        }
    }
}

console.log(complex_result);

// Test 5: Loop with Conditional Processing (10K iterations)
let break_continue_total = 0;
let processed_count = 0;
for (let i = 0; i < 10000; i++) {
    if (i % 100 === 0) {
        break_continue_total += 0;
    } else {
        break_continue_total += 1;
        processed_count++;
    }
}

console.log(break_continue_total);
console.log(processed_count);

// Test 6: Short Jump Stress Test - Tight Nested Loops
let tight_nested_total = 0;
for (let a = 0; a < 200; a++) {
    for (let b = 0; b < 200; b++) {
        for (let c = 0; c < 5; c++) {
            tight_nested_total++;
        }
    }
}

console.log(tight_nested_total);

// Test 7: Dense Conditionals
let dense_conditional_total = 0;
for (let i = 0; i < 20000; i++) {
    if (i % 2 === 0) {
        dense_conditional_total += 1;
    }
    if (i % 3 === 0) {
        dense_conditional_total += 2;
    }
    if (i % 5 === 0) {
        dense_conditional_total += 3;
    }
    if (i % 7 === 0) {
        dense_conditional_total += 4;
    }
}

console.log(dense_conditional_total);

// Test 8: Mixed Control Flow
let mixed_total = 0;
for (let outer = 0; outer < 100; outer++) {
    let inner_count = 0;
    while (inner_count < 50) {
        if (inner_count % 3 === 0) {
            if (outer % 2 === 0) {
                mixed_total += 1;
            } else {
                mixed_total += 2;
            }
        } else {
            mixed_total += 1;
        }
        inner_count++;
    }
}

console.log(mixed_total);

const end = process.hrtime.bigint();
const duration = Number(end - start) / 1000000000;
console.error(`Node.js execution time: ${duration.toFixed(6)} seconds`);