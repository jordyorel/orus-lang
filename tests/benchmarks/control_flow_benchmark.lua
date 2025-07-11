#!/usr/bin/env lua
-- Control Flow Benchmark for Cross-Language Performance Testing
-- Equivalent to control_flow_benchmark.orus

local start_time = os.clock()

-- Test 1: Simple For Loop Performance (1 million iterations)
local simple_counter = 0
for i = 0, 999999 do
    simple_counter = simple_counter + 1
end

print(simple_counter)

-- Test 2: Nested Loop Performance (1000 x 1000)
local nested_total = 0
for i = 0, 999 do
    for j = 0, 999 do
        nested_total = nested_total + 1
    end
end

print(nested_total)

-- Test 3: While Loop with Conditional (100K iterations)
local while_counter = 0
local condition_hits = 0
while while_counter < 100000 do
    if while_counter % 2 == 0 then
        condition_hits = condition_hits + 1
    end
    while_counter = while_counter + 1
end

print(condition_hits)

-- Test 4: Conditional Logic (50K iterations)
local complex_result = 0
for i = 0, 49999 do
    if i % 3 == 0 then
        complex_result = complex_result + 3
    else
        if i % 5 == 0 then
            complex_result = complex_result + 5
        else
            complex_result = complex_result + 1
        end
    end
end

print(complex_result)

-- Test 5: Loop with Conditional Processing (10K iterations)
local break_continue_total = 0
local processed_count = 0
for i = 0, 9999 do
    if i % 100 == 0 then
        break_continue_total = break_continue_total + 0
    else
        break_continue_total = break_continue_total + 1
        processed_count = processed_count + 1
    end
end

print(break_continue_total)
print(processed_count)

-- Test 6: Short Jump Stress Test - Tight Nested Loops
local tight_nested_total = 0
for a = 0, 199 do
    for b = 0, 199 do
        for c = 0, 4 do
            tight_nested_total = tight_nested_total + 1
        end
    end
end

print(tight_nested_total)

-- Test 7: Dense Conditionals
local dense_conditional_total = 0
for i = 0, 19999 do
    if i % 2 == 0 then
        dense_conditional_total = dense_conditional_total + 1
    end
    if i % 3 == 0 then
        dense_conditional_total = dense_conditional_total + 2
    end
    if i % 5 == 0 then
        dense_conditional_total = dense_conditional_total + 3
    end
    if i % 7 == 0 then
        dense_conditional_total = dense_conditional_total + 4
    end
end

print(dense_conditional_total)

-- Test 8: Mixed Control Flow
local mixed_total = 0
for outer = 0, 99 do
    local inner_count = 0
    while inner_count < 50 do
        if inner_count % 3 == 0 then
            if outer % 2 == 0 then
                mixed_total = mixed_total + 1
            else
                mixed_total = mixed_total + 2
            end
        else
            mixed_total = mixed_total + 1
        end
        inner_count = inner_count + 1
    end
end

print(mixed_total)

local end_time = os.clock()
io.stderr:write(string.format("Lua execution time: %.6f seconds\n", end_time - start_time))