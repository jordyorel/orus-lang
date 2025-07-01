#!/bin/bash

echo "=== LOOP SUM BENCHMARK: 1 BILLION ITERATIONS ==="
echo "Testing computational performance across languages"
echo "Expected result: 499999999500000000"
echo ""

# Test Orus
echo "=== Orus (with new Type System) ==="
start_time=$(date +%s.%N)
orus_result=$(./orus benchmarks/loop_sum_benchmark.orus 2>/dev/null)
end_time=$(date +%s.%N)
orus_time=$(echo "$end_time - $start_time" | bc -l)

echo "Result: $orus_result"
echo "Time: ${orus_time}s"
if [ "$orus_result" = "499999999500000000" ]; then
    echo "✅ CORRECT"
    orus_ops=$(echo "scale=0; 1000000000 / $orus_time" | bc -l)
    echo "Operations per second: $(printf "%'.0f" $orus_ops)"
else
    echo "❌ INCORRECT"
fi
echo ""

# Test Python3 (if available)
if command -v python3 &> /dev/null; then
    echo "=== Python3 ==="
    start_time=$(date +%s.%N)
    python_result=$(python3 benchmarks/loop_sum_benchmark.py 2>/dev/null)
    end_time=$(date +%s.%N)
    python_time=$(echo "$end_time - $start_time" | bc -l)
    
    echo "Result: $python_result"
    echo "Time: ${python_time}s"
    if [ "$python_result" = "499999999500000000" ]; then
        echo "✅ CORRECT"
        python_ops=$(echo "scale=0; 1000000000 / $python_time" | bc -l)
        echo "Operations per second: $(printf "%'.0f" $python_ops)"
    else
        echo "❌ INCORRECT"
    fi
    echo ""
fi

# Test Node.js (if available)
if command -v node &> /dev/null; then
    echo "=== Node.js ==="
    start_time=$(date +%s.%N)
    node_result=$(node benchmarks/loop_sum_benchmark.js 2>/dev/null)
    end_time=$(date +%s.%N)
    node_time=$(echo "$end_time - $start_time" | bc -l)
    
    echo "Result: $node_result"
    echo "Time: ${node_time}s"
    if [ "$node_result" = "499999999500000000" ]; then
        echo "✅ CORRECT"
        node_ops=$(echo "scale=0; 1000000000 / $node_time" | bc -l)
        echo "Operations per second: $(printf "%'.0f" $node_ops)"
    else
        echo "❌ INCORRECT"
    fi
    echo ""
fi

# Test Lua (if available)
if command -v lua &> /dev/null; then
    echo "=== Lua ==="
    start_time=$(date +%s.%N)
    lua_result=$(lua benchmarks/loop_sum_benchmark.lua 2>/dev/null)
    end_time=$(date +%s.%N)
    lua_time=$(echo "$end_time - $start_time" | bc -l)
    
    echo "Result: $lua_result"
    echo "Time: ${lua_time}s"
    if [ "$lua_result" = "499999999500000000" ]; then
        echo "✅ CORRECT"
        lua_ops=$(echo "scale=0; 1000000000 / $lua_time" | bc -l)
        echo "Operations per second: $(printf "%'.0f" $lua_ops)"
    else
        echo "❌ INCORRECT (Lua may have precision limits)"
    fi
    echo ""
fi

echo "=== SUMMARY ==="
echo "Orus (Type System): ${orus_time}s"
if [ -n "$python_time" ]; then
    speedup=$(echo "scale=2; $python_time / $orus_time" | bc -l)
    echo "Python3: ${python_time}s (Orus is ${speedup}x faster)"
fi
if [ -n "$node_time" ]; then
    speedup=$(echo "scale=2; $node_time / $orus_time" | bc -l)
    echo "Node.js: ${node_time}s (Orus is ${speedup}x faster)"
fi
if [ -n "$lua_time" ]; then
    speedup=$(echo "scale=2; $lua_time / $orus_time" | bc -l)
    echo "Lua: ${lua_time}s (Orus is ${speedup}x faster)"
fi