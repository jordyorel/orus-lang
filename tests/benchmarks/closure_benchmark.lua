#!/usr/bin/env lua

-- Comprehensive Orus Language Benchmark - Lua Version
-- Tests all currently supported features in one complex benchmark

print("=== Orus Comprehensive Performance Benchmark ===")

local start_time = math.floor(os.clock() * 1000000) -- microseconds

-- === ARITHMETIC OPERATIONS INTENSIVE TEST ===
print("Phase 1: Intensive Arithmetic Operations")

-- Complex arithmetic chains
local a = 100
local b = 50
local c = 25
local d = 12
local e = 6

-- Multi-step calculations
local result1 = a + b * c - math.floor(d / e)
local result2 = math.floor((a - b) * (c + d) / e)
local result3 = math.floor(a * b / c) + d - e
local result4 = math.floor((a + b + c + d + e) / (a - b - c - d - e))

-- Nested arithmetic expressions
local complex1 = math.floor(((a + b) * c - d) / ((e + d) * c - b))
local complex2 = math.floor((a * b + c * d) / (a - b + c - d))
local complex3 = math.floor((math.floor(a / b) * c + d) - (math.floor(e * d / c) + b))

-- Iterative calculations (simulating loops)
local sum_val = 0
local counter = 0
counter = counter + 1
sum_val = sum_val + counter
counter = counter + 1
sum_val = sum_val + counter
counter = counter + 1
sum_val = sum_val + counter
counter = counter + 1
sum_val = sum_val + counter
counter = counter + 1
sum_val = sum_val + counter
counter = counter + 1
sum_val = sum_val + counter
counter = counter + 1
sum_val = sum_val + counter
counter = counter + 1
sum_val = sum_val + counter
counter = counter + 1
sum_val = sum_val + counter
counter = counter + 1
sum_val = sum_val + counter
counter = counter + 1
sum_val = sum_val + counter
counter = counter + 1
sum_val = sum_val + counter
counter = counter + 1
sum_val = sum_val + counter
counter = counter + 1
sum_val = sum_val + counter
counter = counter + 1
sum_val = sum_val + counter
counter = counter + 1
sum_val = sum_val + counter
counter = counter + 1
sum_val = sum_val + counter
counter = counter + 1
sum_val = sum_val + counter
counter = counter + 1
sum_val = sum_val + counter
counter = counter + 1
sum_val = sum_val + counter

print("Arithmetic Phase Results:")
print("Complex result 1:", complex1)
print("Complex result 2:", complex2)
print("Complex result 3:", complex3)
print("Iterative sum:", sum_val)

-- === VARIABLE OPERATIONS INTENSIVE TEST ===
print("Phase 2: Intensive Variable Operations")

-- Variable assignment chains
local var1 = 10
local var2 = var1 * 2
local var3 = var2 + var1
local var4 = var3 - var2
local var5 = var4 * var1
local var6 = math.floor(var5 / var2)
local var7 = var6 + var3
local var8 = var7 - var4
local var9 = var8 * var5
local var10 = math.floor(var9 / var6)

-- Variable swapping simulation
local temp_a = var1
local temp_b = var2
var1 = temp_b
var2 = temp_a

-- Complex variable interdependencies
local base = 5
local multiplier = 3
local offset = 7
local threshold = 20

local calc_a = base * multiplier + offset
local calc_b = calc_a - threshold
local calc_c = math.floor(calc_b * base / multiplier)
local calc_d = calc_c + offset - threshold
local calc_e = math.floor(calc_d * multiplier / base)

-- Variable reuse patterns
local accumulator = 0
accumulator = accumulator + base
accumulator = accumulator * multiplier
accumulator = accumulator - offset
accumulator = accumulator + threshold
accumulator = math.floor(accumulator / base)
accumulator = accumulator * offset
accumulator = accumulator - multiplier

print("Variable Phase Results:")
print("Final var10:", var10)
print("Swapped var1:", var1)
print("Swapped var2:", var2)
print("Calc chain result:", calc_e)
print("Accumulator result:", accumulator)

-- === LITERAL VALUES INTENSIVE TEST ===
print("Phase 3: Intensive Literal Operations")

-- Large number calculations
local big1 = 1000000
local big2 = 500000
local big3 = 250000
local big4 = 125000
local big5 = 62500

-- Operations with large numbers
local large_sum = big1 + big2 + big3 + big4 + big5
local large_diff = big1 - big2 - big3 - big4 - big5
local large_prod = math.floor(big1 / 1000) * math.floor(big2 / 1000)
local large_div = math.floor(big1 / big2) * math.floor(big3 / big4)

-- Mixed literal and variable operations
local mixed1 = 42 + var1 * 17 - 8
local mixed2 = 99 - math.floor(var2 / 3) + 21
local mixed3 = 77 * var3 + 13 - 56
local mixed4 = math.floor(88 / var4) - 44 + 33

-- Decimal-like operations (using integer division)
local decimal1 = math.floor(100 * 355 / 113)
local decimal2 = math.floor(1000 * 22 / 7)
local decimal3 = math.floor(10000 * 618 / 1000)

print("Literal Phase Results:")
print("Large sum:", large_sum)
print("Large difference:", large_diff)
print("Large product:", large_prod)
print("Mixed calculation 1:", mixed1)
print("Mixed calculation 2:", mixed2)
print("Decimal approximation 1:", decimal1)
print("Decimal approximation 2:", decimal2)

-- === TIMESTAMP OPERATIONS INTENSIVE TEST ===
print("Phase 4: Intensive Timestamp Operations")

-- Multiple timestamp measurements
local ts1 = math.floor(os.clock() * 1000000)
local ts2 = math.floor(os.clock() * 1000000)
local ts3 = math.floor(os.clock() * 1000000)
local ts4 = math.floor(os.clock() * 1000000)
local ts5 = math.floor(os.clock() * 1000000)

-- Timestamp arithmetic
local ts_diff1 = ts2 - ts1
local ts_diff2 = ts3 - ts2
local ts_diff3 = ts4 - ts3
local ts_diff4 = ts5 - ts4

-- Complex timestamp calculations
local ts_total = ts_diff1 + ts_diff2 + ts_diff3 + ts_diff4
local ts_avg = math.floor(ts_total / 4)
local ts_max = ts_diff1
local temp_check = ts_diff2 - ts_max
temp_check = ts_diff3 - ts_max
temp_check = ts_diff4 - ts_max

-- Timestamp with arithmetic operations
local ts_calc1 = ts1 + 1000
local ts_calc2 = math.floor(ts2 * 2 / 2)
local ts_calc3 = ts3 - 500 + 500
local ts_calc4 = math.floor((ts4 + ts5) / 2)

print("Timestamp Phase Results:")
print("Timestamp 1:", ts1)
print("Timestamp 5:", ts5)
print("Total time diff:", ts_total)
print("Average time diff:", ts_avg)
print("Complex timestamp calc:", ts_calc4)

-- === PRINT OPERATIONS INTENSIVE TEST ===
print("Phase 5: Intensive Print Operations")

-- Multiple print statements with different data
print("Testing multiple print operations:")
print("Number:", 42)
print("Calculation:", 10 + 5)
print("Variable:", accumulator)
print("Expression:", (a + b) * c)
print("Large number:", big1)
print("Time:", ts1)

-- Print with complex expressions
print("Complex expression 1:", math.floor(((100 + 200) * 3) / 2))
print("Complex expression 2:", (500 - 100) * 2 + 50)
print("Complex expression 3:", math.floor(1000 / 10) + math.floor(200 / 4))

-- === COMPREHENSIVE INTEGRATION TEST ===
print("Phase 6: Comprehensive Integration")

-- Combine all features in complex calculations
local integration_start = math.floor(os.clock() * 1000000)

-- Complex integrated calculation
local step1 = math.floor(big1 / 1000) + (var1 * var2)
local step2 = step1 - (complex1 * 10)
local step3 = step2 + (ts_avg * 100)
local step4 = math.floor(step3 / (accumulator + 1))
local step5 = step4 * math.floor(decimal1 / 100)

-- Multiple variable interactions
local chain_a = step1 + step2
local chain_b = step3 + step4
local chain_c = step5 + chain_a
local chain_d = chain_b + chain_c
local final_result = math.floor(chain_d / 4)

-- Final timestamp
local integration_end = math.floor(os.clock() * 1000000)
local integration_time = integration_end - integration_start

print("Integration Phase Results:")
print("Step 1:", step1)
print("Step 2:", step2)
print("Step 3:", step3)
print("Step 4:", step4)
print("Step 5:", step5)
print("Final integrated result:", final_result)
print("Integration time:", integration_time)

-- === FINAL BENCHMARK RESULTS ===
local end_time = math.floor(os.clock() * 1000000)
local total_elapsed = end_time - start_time

print("=== BENCHMARK COMPLETE ===")
print("Total execution time:", total_elapsed)
print("Operations completed: 200+")
print("Variables used: 50+")
print("Arithmetic operations: 100+")
print("Print statements: 40+")
print("Timestamp operations: 20+")
print("Final benchmark score:", total_elapsed + final_result)
print("=== Orus Comprehensive Benchmark Complete ===")
