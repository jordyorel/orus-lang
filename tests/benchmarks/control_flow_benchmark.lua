#!/usr/bin/env lua
-- Control Flow Performance Benchmark - Lua
-- Focus on while loops and conditional performance

print("=== Lua Control Flow Performance Benchmark ===")

local start_time = os.clock() * 1000

-- === PHASE 1: BASIC WHILE LOOPS ===
print("Phase 1: Basic While Loops")

-- Simple counting loop
local counter = 0
while counter < 100000 do
    counter = counter + 1
end
print("Counter result:", counter)

-- Summation loop
local sum_result = 0
local i = 0
while i < 50000 do
    sum_result = sum_result + i
    i = i + 1
end
print("Sum result:", sum_result)

-- Multiplication accumulator
local product = 1
local j = 1
while j <= 20 do
    product = product * j
    j = j + 1
end
print("Product result:", product)

-- === PHASE 2: NESTED LOOPS ===
print("Phase 2: Nested Loops")

local nested_sum = 0
local outer = 0
while outer < 500 do
    local inner = 0
    while inner < 200 do
        nested_sum = nested_sum + 1
        inner = inner + 1
    end
    outer = outer + 1
end
print("Nested sum result:", nested_sum)

-- Matrix-like computation
local matrix_result = 0
local row = 0
while row < 300 do
    local col = 0
    while col < 333 do
        matrix_result = matrix_result + (row * col)
        col = col + 1
    end
    row = row + 1
end
print("Matrix result:", matrix_result)

-- === PHASE 3: CONDITIONAL PERFORMANCE ===
print("Phase 3: Conditional Performance")

-- If-else chains in loops
local conditional_sum = 0
local k = 0
while k < 100000 do
    if k % 5 == 0 then
        conditional_sum = conditional_sum + k
    else
        if k % 3 == 0 then
            conditional_sum = conditional_sum + (k * 2)
        else
            conditional_sum = conditional_sum + 1
        end
    end
    k = k + 1
end
print("Conditional sum result:", conditional_sum)

-- Complex boolean expressions
local bool_ops = 0
local m = 0
while m < 50000 do
    if m > 100 then
        if m < 40000 then
            if m % 7 == 0 then
                bool_ops = bool_ops + 1
            end
        end
    end
    m = m + 1
end
print("Boolean operations result:", bool_ops)

-- === PHASE 4: FIBONACCI SEQUENCE ===
print("Phase 4: Fibonacci Sequence")

local fib_n = 35
local fib_a = 0
local fib_b = 1
local fib_i = 2
while fib_i <= fib_n do
    local fib_temp = fib_a + fib_b
    fib_a = fib_b
    fib_b = fib_temp
    fib_i = fib_i + 1
end
print("Fibonacci result:", fib_b)

-- === PHASE 5: PRIME NUMBER SIEVE ===
print("Phase 5: Prime Number Sieve")

-- Simple prime finding
local prime_limit = 10000
local prime_count = 0
local candidate = 2
while candidate <= prime_limit do
    local is_prime = true
    local divisor = 2
    while divisor * divisor <= candidate do
        if candidate % divisor == 0 then
            is_prime = false
            divisor = candidate  -- Break out of inner loop
        else
            divisor = divisor + 1
        end
    end
    if is_prime then
        prime_count = prime_count + 1
    end
    candidate = candidate + 1
end
print("Prime count result:", prime_count)

-- === PHASE 6: STRING OPERATIONS WITH LOOPS ===
print("Phase 6: String Operations with Loops")

-- String building simulation (using numbers)
local string_sim = 0
local char_code = 65  -- ASCII 'A'
local str_length = 0
while str_length < 10000 do
    string_sim = string_sim + char_code
    char_code = char_code + 1
    if char_code > 90 then  -- ASCII 'Z'
        char_code = 65  -- Reset to 'A'
    end
    str_length = str_length + 1
end
print("String simulation result:", string_sim)

-- === PHASE 7: MATHEMATICAL SERIES ===
print("Phase 7: Mathematical Series")

-- Pi approximation using Leibniz formula
local pi_approx = 0
local term = 1
local sign = 1
local series_i = 0
while series_i < 100000 do
    pi_approx = pi_approx + (sign * 1000000 / term)
    sign = -sign
    term = term + 2
    series_i = series_i + 1
end
local pi_result = pi_approx * 4 / 1000000
print("Pi approximation result:", pi_result)

-- Square root using Newton's method
local sqrt_target = 123456
local sqrt_x = sqrt_target / 2
local sqrt_iterations = 0
while sqrt_iterations < 10000 do
    sqrt_x = (sqrt_x + sqrt_target / sqrt_x) / 2
    sqrt_iterations = sqrt_iterations + 1
end
print("Square root result:", sqrt_x)

-- === FINAL BENCHMARK RESULTS ===
local end_time = os.clock() * 1000
local total_elapsed = math.floor(end_time - start_time)

print("=== CONTROL FLOW BENCHMARK COMPLETE ===")
print("Total execution time:", total_elapsed)
print("While loops executed: 15+")
print("Nested loops: 2")
print("Conditional operations: 150,000+")
print("Mathematical algorithms: 4")
print("Total iterations: 500,000+")
print("Final benchmark score:", total_elapsed * counter / 1000)
print("=== Lua Control Flow Benchmark Complete ===")