#!/usr/bin/env node
// Scope Management Benchmark for Cross-Language Performance Testing
const start = process.hrtime.bigint();
let total = 0;

// Test 1: Nested if scopes with shadowing (50K iterations)
for (let i = 0; i < 50000; i++) {
    let value = i;
    if (value % 2 === 0) {
        value = value + 1;
        if (value % 3 === 0) {
            value = value + 2;
            total += value;
        } else {
            total += value;
        }
    } else {
        total += value;
    }
}

// Test 2: Deeply nested loops with shadowed variables
for (let outer = 0; outer < 100; outer++) {
    for (let inner = 0; inner < 100; inner++) {
        let outer_val = inner;
        for (let deep = 0; deep < 10; deep++) {
            outer_val = deep;
            total += outer_val;
        }
    }
}

console.log(total);
const end = process.hrtime.bigint();
const duration = Number(end - start) / 1e9;
console.error(`Node.js execution time: ${duration.toFixed(6)} seconds`);
