#!/usr/bin/env lua
--[[
Closure benchmark for Lua
Tests closure creation and invocation performance
--]]

-- Test closure creation and invocation
function makeCounter()
    local count = 0
    return function()
        count = count + 1
        return count
    end
end

-- Test closure with captured value
function makeAdder(base)
    return function(x)
        return base + x
    end
end

-- Test deep nesting
function makeNested(a)
    return function(b)
        return function(c)
            return a + b + c
        end
    end
end

-- Test multiple closure creation overhead
function createMultipleClosures()
    local total = 0
    for i = 0, 99 do
        local localClosure = function(x)
            return x * 2
        end
        total = total + localClosure(i)
    end
    return total
end

function main()
    print("=== Lua Closure Benchmark ===")
    
    local startTime = os.clock() * 1000000000  -- Convert to nanoseconds
    
    -- Test 1: Counter closure
    print("Test 1: Counter closure creation")
    local counter = makeCounter()
    local counterTotal = 0
    for i = 1, 1000 do
        counterTotal = counterTotal + counter()
    end
    print("Counter total: " .. counterTotal)
    
    -- Test 2: Closure with captured value
    print("Test 2: Adder closure with capture")
    local add10 = makeAdder(10)
    local adderTotal = 0
    for i = 0, 999 do
        adderTotal = adderTotal + add10(i)
    end
    print("Adder total: " .. adderTotal)
    
    -- Test 3: Nested closures
    print("Test 3: Nested closure creation")
    local nested = makeNested(5)
    local mid = nested(10)
    local nestedTotal = 0
    for i = 0, 999 do
        nestedTotal = nestedTotal + mid(i)
    end
    print("Nested total: " .. nestedTotal)
    
    -- Test 4: Multiple closure creation overhead
    print("Test 4: Multiple closure creation")
    local creationTotal = 0
    for i = 1, 100 do
        creationTotal = creationTotal + createMultipleClosures()
    end
    print("Creation total: " .. creationTotal)
    
    local endTime = os.clock() * 1000000000  -- Convert to nanoseconds
    local duration = endTime - startTime
    local durationMs = duration / 1000000
    
    print("=== Closure Benchmark Results ===")
    print("Total time (nanoseconds): " .. math.floor(duration))
    print("Total time (milliseconds): " .. string.format("%.2f", durationMs))
    print("Closure benchmark completed")
end

main()