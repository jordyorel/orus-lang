#!/usr/bin/env lua

local start_time = os.clock()

local x = 0
for i = 0, 999999999 do
    x = x + i
end

local end_time = os.clock()
print(x)

-- Print timing info to stderr
io.stderr:write(string.format("Lua execution time: %.6f seconds\n", end_time - start_time))