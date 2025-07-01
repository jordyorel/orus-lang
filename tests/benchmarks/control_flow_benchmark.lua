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

local end_time = os.clock()
io.stderr:write(string.format("Lua execution time: %.6f seconds\n", end_time - start_time))