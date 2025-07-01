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

const end = process.hrtime.bigint();
const duration = Number(end - start) / 1000000000;
console.error(`Node.js execution time: ${duration.toFixed(6)} seconds`);