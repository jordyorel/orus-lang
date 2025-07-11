#!/usr/bin/env node
/**
 * Closure benchmark for JavaScript
 * Tests closure creation and invocation performance
 */

// Test closure creation and invocation
function makeCounter() {
    let count = 0;
    return function() {
        count++;
        return count;
    };
}

// Test closure with captured value
function makeAdder(base) {
    return function(x) {
        return base + x;
    };
}

// Test deep nesting
function makeNested(a) {
    return function(b) {
        return function(c) {
            return a + b + c;
        };
    };
}

// Test multiple closure creation overhead
function createMultipleClosures() {
    let total = 0;
    for (let i = 0; i < 100; i++) {
        const localClosure = function(x) {
            return x * 2;
        };
        total += localClosure(i);
    }
    return total;
}

function main() {
    console.log("=== JavaScript Closure Benchmark ===");
    
    const startTime = process.hrtime.bigint();
    
    // Test 1: Counter closure
    console.log("Test 1: Counter closure creation");
    const counter = makeCounter();
    let counterTotal = 0;
    for (let i = 0; i < 1000; i++) {
        counterTotal += counter();
    }
    console.log(`Counter total: ${counterTotal}`);
    
    // Test 2: Closure with captured value
    console.log("Test 2: Adder closure with capture");
    const add10 = makeAdder(10);
    let adderTotal = 0;
    for (let i = 0; i < 1000; i++) {
        adderTotal += add10(i);
    }
    console.log(`Adder total: ${adderTotal}`);
    
    // Test 3: Nested closures
    console.log("Test 3: Nested closure creation");
    const nested = makeNested(5);
    const mid = nested(10);
    let nestedTotal = 0;
    for (let i = 0; i < 1000; i++) {
        nestedTotal += mid(i);
    }
    console.log(`Nested total: ${nestedTotal}`);
    
    // Test 4: Multiple closure creation overhead
    console.log("Test 4: Multiple closure creation");
    let creationTotal = 0;
    for (let i = 0; i < 100; i++) {
        creationTotal += createMultipleClosures();
    }
    console.log(`Creation total: ${creationTotal}`);
    
    const endTime = process.hrtime.bigint();
    const duration = endTime - startTime;
    const durationMs = Number(duration) / 1000000;
    
    console.log("=== Closure Benchmark Results ===");
    console.log(`Total time (nanoseconds): ${duration}`);
    console.log(`Total time (milliseconds): ${durationMs.toFixed(2)}`);
    console.log("Closure benchmark completed");
}

main();