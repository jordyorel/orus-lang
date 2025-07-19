#!/usr/bin/env lua
-- Control Flow Performance Benchmark - Lua (For-Loop Version)
-- Focus on for loops and optimized control flow patterns

print("=== Lua Control Flow Performance Benchmark ===")

-- Constants for testing
local base1 = 12
local base2 = 25
local base3 = 37
local multiplier = 7
local offset = 100
local factor = 3

local total_result = 0

-- Test 1: Simple invariant expressions
print("Test 1: Simple invariant expressions")
for outer = 1, 499 do
    for i = 1, 9 do
        local expensive_calc1 = base1 * multiplier * 2
        local expensive_calc2 = base2 + multiplier + offset
        local expensive_calc3 = base3 * base1 + base2
        
        local loop_var = i * factor
        
        local result = expensive_calc1 + expensive_calc2 + expensive_calc3 + loop_var
        total_result = total_result + result
    end
end

-- Test 2: Nested loops with complex expressions
print("Test 2: Nested loops with complex invariants")
for outer = 1, 199 do
    for middle = 1, 4 do
        for inner = 1, 3 do
            local complex_calc1 = (base1 + base2) * (multiplier + offset)
            local complex_calc2 = base3 * base1 * base2 + multiplier
            local complex_calc3 = (base1 * 2) + (base2 * 3) + (base3 * 4)
            
            local loop_dependent = inner * middle + outer
            
            local result = complex_calc1 + complex_calc2 + complex_calc3 + loop_dependent
            total_result = total_result + result
        end
    end
end

-- Test 3: Mixed invariant and variant expressions
print("Test 3: Mixed invariant and variant expressions")
for i = 1, 1999 do
    local invariant1 = base1 * multiplier
    local invariant2 = base2 + offset
    local invariant3 = base3 * factor
    
    local variant1 = i * 2
    local variant2 = i + 10
    
    local result = invariant1 + invariant2 + invariant3 + variant1 + variant2
    total_result = total_result + result
end

-- Test 4: Conditional blocks with invariants
print("Test 4: Conditional blocks with invariants")
for i = 1, 999 do
    local result
    if i % 2 == 0 then
        local invariant_in_condition = base1 * base2 * multiplier
        local variant_in_condition = i * 3
        result = invariant_in_condition + variant_in_condition
    else
        local other_invariant = base3 + offset + multiplier
        local other_variant = i * 5
        result = other_invariant + other_variant
    end
    
    total_result = total_result + result
end

print("Test 5: Function call simulation with invariants")
for i = 1, 799 do
    local expensive_operation1 = base1 * base2 * base3
    local expensive_operation2 = (multiplier + offset) * factor
    local expensive_operation3 = base1 + base2 + base3 + multiplier + offset
    
    local simple_operation = i + 1
    
    local result = expensive_operation1 + expensive_operation2 + expensive_operation3 + simple_operation
    total_result = total_result + result
end

print("Control flow benchmark completed")
print("Total result:", total_result)
print("=== Lua Control Flow Benchmark Complete ===")
