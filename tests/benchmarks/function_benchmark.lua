#!/usr/bin/env lua
--[[
Function call benchmark for Lua
Tests function call overhead and tail call optimization
]]

-- Recursive factorial (Lua has proper tail call optimization)
function factorial_tail(n, acc)
    acc = acc or 1
    if n <= 1 then
        return acc
    else
        return factorial_tail(n - 1, acc * n)
    end
end

-- Fibonacci with tail call optimization
function fibonacci_tail(n, a, b)
    a = a or 0
    b = b or 1
    if n == 0 then
        return a
    else
        return fibonacci_tail(n - 1, b, a + b)
    end
end

-- Simple recursive countdown
function countdown(n)
    if n <= 0 then
        return 0
    else
        return countdown(n - 1)
    end
end

-- Nested function calls
function nested_calls(n)
    local function level1(x)
        return x + 1
    end
    
    local function level2(x)
        return level1(x) + 1
    end
    
    local function level3(x)
        return level2(x) + 1
    end
    
    return level3(n)
end

-- Mathematical function with multiple operations
function math_heavy(x)
    local function square(n)
        return n * n
    end
    
    local function cube(n)
        return n * n * n
    end
    
    return square(x) + cube(x) + x
end

function main()
    print("=== Lua Function Benchmark ===")
    
    local start_time = os.clock() * 1000000000  -- Convert to nanoseconds
    
    -- Test 1: Tail-recursive factorial
    print("Test 1: Tail-recursive factorial")
    local result1 = factorial_tail(20)
    print("Factorial of 20: " .. result1)
    
    -- Test 2: Tail-recursive fibonacci
    print("Test 2: Tail-recursive fibonacci")
    local result2 = fibonacci_tail(30)
    print("Fibonacci of 30: " .. result2)
    
    -- Test 3: Simple recursive countdown
    print("Test 3: Recursive countdown")
    local result3 = countdown(1000)
    print("Countdown result: " .. result3)
    
    -- Test 4: Nested function calls
    print("Test 4: Nested function calls")
    local total = 0
    for i = 0, 999 do
        total = total + nested_calls(i)
    end
    print("Nested calls total: " .. total)
    
    -- Test 5: Mathematical functions
    print("Test 5: Mathematical functions")
    local math_total = 0
    for i = 0, 999 do
        math_total = math_total + math_heavy(i)
    end
    print("Math functions total: " .. math_total)
    
    local end_time = os.clock() * 1000000000
    local duration = end_time - start_time
    local duration_ms = duration / 1000000
    
    print("=== Benchmark Results ===")
    print("Total time (nanoseconds): " .. string.format("%.0f", duration))
    print("Total time (milliseconds): " .. string.format("%.2f", duration_ms))
    print("Function benchmark completed")
end

main()