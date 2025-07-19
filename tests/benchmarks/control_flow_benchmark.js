#!/usr/bin/env node
// Control Flow Performance Benchmark - JavaScript (For-Loop Version)
// Focus on for loops and optimized control flow patterns

console.log("=== JavaScript Control Flow Performance Benchmark ===");

// Constants for testing
const base1 = 12;
const base2 = 25;
const base3 = 37;
const multiplier = 7;
const offset = 100;
const factor = 3;

let total_result = 0;

// Test 1: Simple invariant expressions
console.log("Test 1: Simple invariant expressions");
for (let outer = 1; outer < 500; outer++) {
    for (let i = 1; i < 10; i++) {
        const expensive_calc1 = base1 * multiplier * 2;
        const expensive_calc2 = base2 + multiplier + offset;
        const expensive_calc3 = base3 * base1 + base2;
        
        const loop_var = i * factor;
        
        const result = expensive_calc1 + expensive_calc2 + expensive_calc3 + loop_var;
        total_result = total_result + result;
    }
}

// Test 2: Nested loops with complex expressions
console.log("Test 2: Nested loops with complex invariants");
for (let outer = 1; outer < 200; outer++) {
    for (let middle = 1; middle < 5; middle++) {
        for (let inner = 1; inner < 4; inner++) {
            const complex_calc1 = (base1 + base2) * (multiplier + offset);
            const complex_calc2 = base3 * base1 * base2 + multiplier;
            const complex_calc3 = (base1 * 2) + (base2 * 3) + (base3 * 4);
            
            const loop_dependent = inner * middle + outer;
            
            const result = complex_calc1 + complex_calc2 + complex_calc3 + loop_dependent;
            total_result = total_result + result;
        }
    }
}

// Test 3: Mixed invariant and variant expressions
console.log("Test 3: Mixed invariant and variant expressions");
for (let i = 1; i < 2000; i++) {
    const invariant1 = base1 * multiplier;
    const invariant2 = base2 + offset;
    const invariant3 = base3 * factor;
    
    const variant1 = i * 2;
    const variant2 = i + 10;
    
    const result = invariant1 + invariant2 + invariant3 + variant1 + variant2;
    total_result = total_result + result;
}

// Test 4: Conditional blocks with invariants
console.log("Test 4: Conditional blocks with invariants");
for (let i = 1; i < 1000; i++) {
    let result;
    if (i % 2 === 0) {
        const invariant_in_condition = base1 * base2 * multiplier;
        const variant_in_condition = i * 3;
        result = invariant_in_condition + variant_in_condition;
    } else {
        const other_invariant = base3 + offset + multiplier;
        const other_variant = i * 5;
        result = other_invariant + other_variant;
    }
    
    total_result = total_result + result;
}

console.log("Test 5: Function call simulation with invariants");
for (let i = 1; i < 800; i++) {
    const expensive_operation1 = base1 * base2 * base3;
    const expensive_operation2 = (multiplier + offset) * factor;
    const expensive_operation3 = base1 + base2 + base3 + multiplier + offset;
    
    const simple_operation = i + 1;
    
    const result = expensive_operation1 + expensive_operation2 + expensive_operation3 + simple_operation;
    total_result = total_result + result;
}

console.log("Control flow benchmark completed");
console.log("Total result:", total_result);
console.log("=== JavaScript Control Flow Benchmark Complete ===");