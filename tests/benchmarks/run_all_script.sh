#!/bin/bash

# Comprehensive Orus vs Lua Performance Benchmark
# Single unified runner script

set -e

echo "======================================"
echo "🚀 ORUS VS LUA BENCHMARK SUITE"
echo "======================================"
echo

# Check if required binaries exist
if ! command -v lua &> /dev/null; then
    echo "❌ Error: lua not found. Please install Lua."
    exit 1
fi

if [ ! -f "../../orus" ]; then
    echo "❌ Error: orus binary not found. Please build the project first."
    echo "   Run: make clean && make"
    exit 1
fi

# Function to run benchmark with timing
run_benchmark() {
    local lang=$1
    local file=$2
    local cmd=$3
    
    echo "🔄 Running $lang benchmark..."
    echo "   Command: $cmd"
    echo "   File: $file"
    echo
    
    # Run 3 times and take the best time
    best_time=999999
    for run in 1 2 3; do
        echo "   → Run $run/3"
        
        # Use time command for more precise measurement
        if /usr/bin/time -p $cmd > /tmp/benchmark_output_$(echo $lang | tr '[:upper:]' '[:lower:]').txt 2> /tmp/time_output.txt; then
            # Extract real time from time output
            elapsed=$(grep "^real" /tmp/time_output.txt | awk '{print $2}')
            
            # Compare with best time using awk
            if awk "BEGIN {exit !($elapsed < $best_time)}"; then
                best_time=$elapsed
            fi
        else
            echo "   ❌ Error running $lang benchmark"
            echo "   Output:"
            cat /tmp/benchmark_output_$(echo $lang | tr '[:upper:]' '[:lower:]').txt
            return 1
        fi
    done
    
    echo "   ⏱️  Best time: ${best_time}s"
    echo
    
    # Show output from best run
    echo "📊 $lang Results:"
    echo "----------------------------------------"
    cat /tmp/benchmark_output_$(echo $lang | tr '[:upper:]' '[:lower:]').txt
    echo "----------------------------------------"
    echo
    
    # Store the best time for comparison
    eval "$(echo $lang | tr '[:upper:]' '[:lower:]')_time=$best_time"
}

# Navigate to benchmark directory
cd "$(dirname "$0")"

echo "📍 Working directory: $(pwd)"
echo

# Run Orus benchmark
if [ -f "final_benchmark.orus" ]; then
    run_benchmark "ORUS" "final_benchmark.orus" "../../orus final_benchmark.orus"
else
    echo "❌ Error: final_benchmark.orus not found"
    exit 1
fi

# Run Lua benchmark  
if [ -f "final_benchmark.lua" ]; then
    run_benchmark "LUA" "final_benchmark.lua" "lua final_benchmark.lua"
else
    echo "❌ Error: final_benchmark.lua not found"
    exit 1
fi

echo "======================================"
echo "📈 PERFORMANCE COMPARISON"
echo "======================================"
echo

# Performance comparison
if [ -n "$orus_time" ] && [ -n "$lua_time" ] && [ "$orus_time" != "0.00" ] && [ "$lua_time" != "0.00" ]; then
    echo "⏱️  Orus time:    ${orus_time}s"
    echo "⏱️  Lua time:     ${lua_time}s"
    echo
    
    # Check if times are measurable
    if awk "BEGIN {exit !($orus_time > 0 && $lua_time > 0)}"; then
        # Calculate speedup using awk for better compatibility
        speedup=$(awk "BEGIN {printf \"%.2f\", $lua_time / $orus_time}")
        percentage=$(awk "BEGIN {printf \"%.1f\", (($lua_time - $orus_time) / $lua_time) * 100}")
        
        if awk "BEGIN {exit !($orus_time < $lua_time)}"; then
            echo "🏆 ORUS WINS!"
            echo "   ${speedup}x faster than Lua"
            echo "   ${percentage}% performance improvement"
        elif awk "BEGIN {exit !($lua_time < $orus_time)}"; then
            slowdown=$(awk "BEGIN {printf \"%.2f\", $orus_time / $lua_time}")
            percentage=$(awk "BEGIN {printf \"%.1f\", (($orus_time - $lua_time) / $orus_time) * 100}")
            echo "🥈 Lua wins"
            echo "   ${slowdown}x faster than Orus"
            echo "   ${percentage}% slower than Lua"
        else
            echo "🤝 Tie! Both languages performed equally"
        fi
    else
        echo "⚠️  Times too small to measure accurately - both languages are very fast!"
    fi
else
    echo "❌ Could not compare performance - missing or invalid timing data"
    echo "   Orus time: ${orus_time:-unknown}"
    echo "   Lua time: ${lua_time:-unknown}"
fi

echo
echo "✅ Benchmark suite completed!"
echo "======================================"

# Cleanup
rm -f /tmp/benchmark_output_*.txt