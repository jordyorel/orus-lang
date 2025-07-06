#!/usr/bin/env lua
-- Scope Management Benchmark for Cross-Language Performance Testing
local start_time = os.clock()
local total = 0

-- Test 1: Nested if scopes with shadowing (50K iterations)
for i = 0, 49999 do
    local value = i
    if value % 2 == 0 then
        value = value + 1
        if value % 3 == 0 then
            value = value + 2
            total = total + value
        else
            total = total + value
        end
    else
        total = total + value
    end
end

-- Test 2: Deeply nested loops with shadowed variables
for outer = 0, 99 do
    for inner = 0, 99 do
        local outer_val = inner
        for deep = 0, 9 do
            outer_val = deep
            total = total + outer_val
        end
    end
end

print(total)
local duration = os.clock() - start_time
io.stderr:write(string.format("Lua execution time: %.6f seconds\n", duration))
