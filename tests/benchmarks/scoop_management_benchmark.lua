#!/usr/bin/env lua
-- Scoop Management Benchmark for Lua
local start_time = os.clock()
local scoops = 0

-- Test 1: Nested scoop operations with variable shadowing
for i = 1, 50000 do
    local scoop = i
    if scoop % 2 == 0 then
        scoop = scoop + 1
        if scoop % 3 == 0 then
            scoop = scoop + 2
            scoops = scoops + scoop
        else
            scoops = scoops + scoop
        end
    else
        scoops = scoops + scoop
    end
end

-- Test 2: Deeply nested loops incrementing scoop count
for bucket = 1, 100 do
    for cone = 1, 100 do
        local scoop_amt = cone
        for extra = 1, 10 do
            scoop_amt = extra
            scoops = scoops + scoop_amt
        end
    end
end

print(scoops)
local duration = os.clock() - start_time
io.stderr:write(string.format("Lua scoop benchmark time: %.6f seconds\n", duration))
