-- Lua Control Flow (For Loop) Benchmark

local function now()
  return os.clock() -- CPU time in seconds
end

print("=== Lua Control Flow (For Loop) Benchmark ===")
local start = now()

local N1 = 2000000 -- simple loop iterations
local O2 = 1000    -- nested outer
local I2 = 1000    -- nested inner
local N3 = 1000000 -- while-like iterations

-- Phase 1: simple for-loop sum
print("Phase 1: simple sum loop")
local sum1 = 0
for i = 1, N1 do
  sum1 = sum1 + i
end

-- Phase 2: nested loops with branch
print("Phase 2: nested loops with branch")
local acc2 = 0
for i = 0, O2 do
  local base = i
  for j = 0, I2 do
    local t = base + j
    if (t % 2) == 0 then
      acc2 = acc2 + t
    else
      acc2 = acc2 - 1
    end
  end
end

-- Phase 3: even sum with stepping
print("Phase 3: even sum with stepping loop")
local sum3 = 0
for k = 0, N3 * 2, 2 do
  sum3 = sum3 + k
end

local checksum = sum1 + acc2 + sum3
local elapsed = now() - start

print("Checksum:", checksum)
print("Total execution time:", elapsed)
print("=== Lua Control Flow Benchmark Complete ===")

