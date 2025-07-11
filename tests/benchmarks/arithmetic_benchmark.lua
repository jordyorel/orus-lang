#!/usr/bin/env lua
-- Universal Arithmetic Benchmark for Cross-Language Performance Testing
-- Equivalent to arithmetic_benchmark.orus

local start_time = os.clock()

local total = 0

-- Test 1: Basic Addition Loop (1 million iterations)
for i = 0, 999999 do
    total = total + i
end

print(total)

-- Test 2: Mixed Arithmetic Operations (100K iterations)
local result = 1.0
for i = 0, 99999 do
    result = result + 1.5
    result = result * 1.01
    result = result / 1.005
    result = result - 0.5
end

print(result)

-- Test 3: Integer Arithmetic Performance
local factorial_approx = 1
for i = 1, 19 do
    factorial_approx = factorial_approx * i
end

print(factorial_approx)

-- Test 4: Division and Modulo Operations
local division_sum = 0
for i = 1, 9999 do
    division_sum = division_sum + math.floor(1000000 / i) + (1000000 % i)
end

print(division_sum)

-- Test 5: Floating Point Precision
local precision_test = 0.0
for i = 0, 49999 do
    precision_test = precision_test + 0.1
    precision_test = precision_test - 0.05
    precision_test = precision_test * 1.001
end

print(precision_test)

local end_time = os.clock()
io.stderr:write(string.format("Lua execution time: %.6f seconds\n", end_time - start_time))