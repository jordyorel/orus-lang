#!/usr/bin/env lua
-- Simple Loop Optimization Benchmark - Lua Version with LuaJIT Optimizations

print("=== Simple Loop Optimization Benchmark ===")

-- Test 1: Small loop manually unrolled for Lua/LuaJIT optimization
print("Test 1: Small loop unrolling")
for outer = 1, 1000 do
    -- Manually unroll small loop (1,2,3,4) for Lua performance
    local x = 1 * 2; local y = x + 1; local z = y * 3
    x = 2 * 2; y = x + 1; z = y * 3
    x = 3 * 2; y = x + 1; z = y * 3
    x = 4 * 2; y = x + 1; z = y * 3
end
print("Small loop test completed")

-- Test 2: Medium loop kept normal (too large to unroll)
print("Test 2: Medium loop (not unrolled)")
for outer = 1, 1000 do
    for i = 1, 15 do
        local x2 = i * 2
        local y2 = x2 + 1
        local z2 = y2 * 3
    end
end
print("Medium loop test completed")

-- Test 3: Single iteration loop optimized
print("Test 3: Single iteration loop")
for outer = 1, 1000 do
    -- Single iteration - fully inlined
    local x3 = 5 * 2
    local y3 = x3 + 1
    local z3 = y3 * 3
end
print("Single iteration test completed")

-- Test 4: Step loop manually unrolled (0,2,4)
print("Test 4: Step loop")
for outer = 1, 1000 do
    -- Manually unroll step loop for Lua performance
    local x4 = 0 * 2; local y4 = x4 + 1; local z4 = y4 * 3
    x4 = 2 * 2; y4 = x4 + 1; z4 = y4 * 3
    x4 = 4 * 2; y4 = x4 + 1; z4 = y4 * 3
end
print("Step loop test completed")

-- Test 5: Two iteration loop manually unrolled (1,2,3)
print("Test 5: Two iteration loop")
for outer = 1, 1000 do
    -- Manually unroll two iteration loop
    local x5 = 1 * 2; local y5 = x5 + 1; local z5 = y5 * 3
    x5 = 2 * 2; y5 = x5 + 1; z5 = y5 * 3
    x5 = 3 * 2; y5 = x5 + 1; z5 = y5 * 3
end
print("Two iteration test completed")

print("=== Simple Loop Benchmark Complete ===")