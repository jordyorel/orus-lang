#!/usr/bin/env node
// Universal Arithmetic Benchmark for Cross-Language Performance Testing
// Equivalent to arithmetic_benchmark.orus

const start = process.hrtime.bigint();

let total = 0;

// Test 1: Basic Addition Loop (1 million iterations)
for (let i = 0; i < 1000000; i++) {
    total += i;
}

console.log(total);

// Test 2: Mixed Arithmetic Operations (100K iterations)
let result = 1.0;
for (let i = 0; i < 100000; i++) {
    result += 1.5;
    result *= 1.01;
    result /= 1.005;
    result -= 0.5;
}

console.log(result);

// Test 3: Integer Arithmetic Performance
let factorial_approx = 1;
for (let i = 1; i < 20; i++) {
    factorial_approx *= i;
}

console.log(factorial_approx);

// Test 4: Division and Modulo Operations
let division_sum = 0;
for (let i = 1; i < 10000; i++) {
    division_sum += Math.floor(1000000 / i) + (1000000 % i);
}

console.log(division_sum);

// Test 5: Floating Point Precision
let precision_test = 0.0;
for (let i = 0; i < 50000; i++) {
    precision_test += 0.1;
    precision_test -= 0.05;
    precision_test *= 1.001;
}

console.log(precision_test);

const end = process.hrtime.bigint();
const duration = Number(end - start) / 1000000000;
console.error(`Node.js execution time: ${duration.toFixed(6)} seconds`);