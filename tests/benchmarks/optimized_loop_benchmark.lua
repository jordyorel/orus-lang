#!/usr/bin/env lua
local TRIALS = 5
local ITER_SIMPLE = 5000000
local NEST_OUTER = 800
local NEST_INNER = 800
local ARRAY_LENGTH = 2048
local ARRAY_REPEATS = 512

local seed_values = {}
for i = 0, ARRAY_LENGTH - 1 do
    seed_values[#seed_values + 1] = i * 3
end

local total_simple = 0.0
local total_nested = 0.0
local total_array = 0.0
local checksum = 0

print("=== Lua Optimized Loop Benchmark ===")
print("trials:", TRIALS)

for trial = 0, TRIALS - 1 do
    local start_simple = os.clock()
    local simple_sum = 0
    for i = 0, ITER_SIMPLE - 1 do
        simple_sum = simple_sum + i
    end
    local elapsed_simple = os.clock() - start_simple
    total_simple = total_simple + elapsed_simple

    local start_nested = os.clock()
    local nested_acc = 0
    for outer = 0, NEST_OUTER - 1 do
        local inner = 0
        while inner < NEST_INNER do
            local combined = outer * inner
            if combined % 2 == 0 then
                nested_acc = nested_acc + combined
            else
                nested_acc = nested_acc - inner
            end
            inner = inner + 1
        end
    end
    local elapsed_nested = os.clock() - start_nested
    total_nested = total_nested + elapsed_nested

    local start_array = os.clock()
    local array_total = 0
    local repeat_count = 0
    while repeat_count < ARRAY_REPEATS do
        for idx = 1, #seed_values do
            array_total = array_total + seed_values[idx]
        end
        repeat_count = repeat_count + 1
    end
    local elapsed_array = os.clock() - start_array
    total_array = total_array + elapsed_array

    checksum = checksum + simple_sum + nested_acc + array_total

    print("trial", trial, "simple:", elapsed_simple, "nested:", elapsed_nested, "array:", elapsed_array)
end

local trials_f = TRIALS + 0.0
print("average_simple:", total_simple / trials_f)
print("average_nested:", total_nested / trials_f)
print("average_array:", total_array / trials_f)
print("checksum:", checksum)
print("=== Benchmark complete ===")
