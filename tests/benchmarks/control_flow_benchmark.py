#!/usr/bin/env python3
# Control Flow Performance Benchmark - Python (For-Loop Version)
# Focus on for loops and optimized control flow patterns

print("=== Python Control Flow Performance Benchmark ===")

# Constants for testing
base1 = 12
base2 = 25
base3 = 37
multiplier = 7
offset = 100
factor = 3

total_result = 0

# Test 1: Simple invariant expressions
print("Test 1: Simple invariant expressions")
for outer in range(1, 500):
    for i in range(1, 10):
        expensive_calc1 = base1 * multiplier * 2
        expensive_calc2 = base2 + multiplier + offset
        expensive_calc3 = base3 * base1 + base2
        
        loop_var = i * factor
        
        result = expensive_calc1 + expensive_calc2 + expensive_calc3 + loop_var
        total_result = total_result + result

# Test 2: Nested loops with complex expressions
print("Test 2: Nested loops with complex invariants")
for outer in range(1, 200):
    for middle in range(1, 5):
        for inner in range(1, 4):
            complex_calc1 = (base1 + base2) * (multiplier + offset)
            complex_calc2 = base3 * base1 * base2 + multiplier
            complex_calc3 = (base1 * 2) + (base2 * 3) + (base3 * 4)
            
            loop_dependent = inner * middle + outer
            
            result = complex_calc1 + complex_calc2 + complex_calc3 + loop_dependent
            total_result = total_result + result

# Test 3: Mixed invariant and variant expressions
print("Test 3: Mixed invariant and variant expressions")
for i in range(1, 2000):
    invariant1 = base1 * multiplier
    invariant2 = base2 + offset
    invariant3 = base3 * factor
    
    variant1 = i * 2
    variant2 = i + 10
    
    result = invariant1 + invariant2 + invariant3 + variant1 + variant2
    total_result = total_result + result

# Test 4: Conditional blocks with invariants
print("Test 4: Conditional blocks with invariants")
for i in range(1, 1000):
    if i % 2 == 0:
        invariant_in_condition = base1 * base2 * multiplier
        variant_in_condition = i * 3
        result = invariant_in_condition + variant_in_condition
    else:
        other_invariant = base3 + offset + multiplier
        other_variant = i * 5
        result = other_invariant + other_variant
    
    total_result = total_result + result

print("Test 5: Function call simulation with invariants")
for i in range(1, 800):
    expensive_operation1 = base1 * base2 * base3
    expensive_operation2 = (multiplier + offset) * factor
    expensive_operation3 = base1 + base2 + base3 + multiplier + offset
    
    simple_operation = i + 1
    
    result = expensive_operation1 + expensive_operation2 + expensive_operation3 + simple_operation
    total_result = total_result + result

print("Control flow benchmark completed")
print("Total result:", total_result)
print("=== Python Control Flow Benchmark Complete ===")