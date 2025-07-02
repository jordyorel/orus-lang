-- Comprehensive Lua Benchmark - Baseline Comparison
print("=== Lua Performance Benchmark ===")

-- Test 1: No LICM optimization (recalculates invariants each iteration)
print("Test 1: LICM optimization")
local a = 17
local b = 23
local c = 31
local d = 37

local licm_result = 0
for i = 0, 149999 do
    local inv1 = a * b + c
    local inv2 = d * a - b
    local expr = inv1 + inv2
    licm_result = licm_result + expr + i
end

print("LICM result:", licm_result)

-- Test 2: Nested loops with intensive computation
print("Test 2: Nested loops")
local nested_sum = 0
for outer = 0, 199 do
    for inner = 0, 199 do
        local calc = outer * inner + 13
        nested_sum = nested_sum + calc
    end
end

print("Nested result:", nested_sum)

-- Test 3: Large range with stepping
print("Test 3: Large range stepping")
local step_result = 0
for i = 0, 19995000, 5000 do
    step_result = step_result + i
end

print("Step result:", step_result)

-- Test 4: Mixed arithmetic operations
print("Test 4: Mixed arithmetic")
local mixed_result = 0.0
local base = 3.14159
local factor = 2.71828

for i = 0, 119999 do
    local calc = base * factor
    mixed_result = mixed_result + calc + i
end

print("Mixed result:", mixed_result)

-- Test 5: Complex mathematical operations
print("Test 5: Mathematical operations")
local math_result = 0
for i = 0, 99999 do
    local temp = i * 3 + 7
    local result = temp * temp - i
    math_result = math_result + result
end

print("Math result:", math_result)

print("=== Benchmark Complete ===")