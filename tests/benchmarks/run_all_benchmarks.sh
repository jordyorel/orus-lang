#!/bin/bash

# Universal Benchmark Runner
# Runs all available benchmarks across multiple languages
# Benchmarks: arithmetic, control flow, and any future benchmarks

echo "=========================================="
echo "Orus Cross-Language Benchmark Suite"
echo "=========================================="
echo "Running all available benchmarks across languages"
echo "Languages: Orus, Python, Node.js, Lua"
echo ""

# Check if executables exist
if ! command -v python3 &> /dev/null; then
    echo "Error: python3 not found"
    exit 1
fi

if ! command -v node &> /dev/null; then
    echo "Error: node (Node.js) not found"
    exit 1
fi

if ! command -v lua &> /dev/null; then
    echo "Error: lua not found"
    exit 1
fi

if ! command -v bc &> /dev/null; then
    echo "Error: bc (calculator) not found"
    exit 1
fi

if [ ! -f "../../orus" ]; then
    echo "Error: orus interpreter not found. Run 'make' first."
    exit 1
fi

# Arrays for storing benchmark names and results
benchmarks=()
benchmark_results=()

# Function to run a benchmark for all languages
run_benchmark() {
    local benchmark_name=$1
    local benchmark_file=$2
    
    # Add to our list of benchmarks
    benchmarks+=("$benchmark_name")
    
    # Create a results file for this benchmark
    local results_file="$RESULTS_DIR/${benchmark_name// /_}_results.txt"
    
    echo "=========================================="
    echo "=== $benchmark_name Benchmark ==="
    echo "=========================================="
    echo ""
    
    # Store Orus time
    echo "--- Orus ---"
    printf "Running Orus $benchmark_name: "
    # Capture time output for Orus since it doesn't have built-in timing
    start_time=$(date +%s.%N)
    ../../orus "$benchmark_file.orus"
    end_time=$(date +%s.%N)
    orus_time=$(echo "$end_time - $start_time" | bc -l)
    printf "Orus execution time: %.6f seconds\n" $orus_time
    printf "\nreal\t%.3fs\n" $orus_time
    echo ""
    
    # Store Python time
    echo "--- Python ---"
    printf "Running Python $benchmark_name: "
    python_start=$(date +%s.%N)
    python3 "$benchmark_file.py"
    python_end=$(date +%s.%N)
    python_time=$(echo "$python_end - $python_start" | bc -l)
    printf "Python execution time: %.6f seconds\n" $python_time
    printf "\nreal\t%.3fs\n" $python_time
    echo ""
    
    # Store Node.js time
    echo "--- Node.js ---"
    printf "Running Node.js $benchmark_name: "
    node_start=$(date +%s.%N)
    node "$benchmark_file.js"
    node_end=$(date +%s.%N)
    node_time=$(echo "$node_end - $node_start" | bc -l)
    printf "Node.js execution time: %.6f seconds\n" $node_time
    printf "\nreal\t%.3fs\n" $node_time
    echo ""
    
    # Store Lua time
    echo "--- Lua ---"
    printf "Running Lua $benchmark_name: "
    lua_start=$(date +%s.%N)
    lua "$benchmark_file.lua"
    lua_end=$(date +%s.%N)
    lua_time=$(echo "$lua_end - $lua_start" | bc -l)
    printf "Lua execution time: %.6f seconds\n" $lua_time
    printf "\nreal\t%.3fs\n" $lua_time
    echo ""
    
    # Store results in memory instead of files
    local result_summary="$benchmark_name|$orus_time|$python_time|$node_time|$lua_time"
    
    # Determine the winner for this benchmark
    winner="Orus"
    fastest=$orus_time
    
    if (( $(echo "$python_time < $fastest" | bc -l) )); then
        winner="Python"
        fastest=$python_time
    fi
    
    if (( $(echo "$node_time < $fastest" | bc -l) )); then
        winner="Node.js"
        fastest=$node_time
    fi
    
    if (( $(echo "$lua_time < $fastest" | bc -l) )); then
        winner="Lua"
        fastest=$lua_time
    fi
    
    # Store complete result with winner
    result_summary="$result_summary|$winner|$fastest"
    benchmark_results+=("$result_summary")
}


# Run all available benchmarks
echo "Starting comprehensive benchmark suite..."
echo ""

# Arithmetic Benchmark
if [ -f "arithmetic_benchmark.orus" ]; then
    run_benchmark "Arithmetic" "arithmetic_benchmark"
fi

# Control Flow Benchmark
if [ -f "control_flow_benchmark.orus" ]; then
    run_benchmark "Control Flow" "control_flow_benchmark"
fi

# String Manipulation Benchmark (when available)
if [ -f "string_benchmark.orus" ]; then
    run_benchmark "String Manipulation" "string_benchmark"
fi

# Function Call Overhead Benchmark (when available)
if [ -f "function_benchmark.orus" ]; then
    run_benchmark "Function Call Overhead" "function_benchmark"
fi

# Memory Allocation Benchmark (when available)
if [ -f "memory_benchmark.orus" ]; then
    run_benchmark "Memory Allocation" "memory_benchmark"
fi

# I/O Benchmark (when available)
if [ -f "io_benchmark.orus" ]; then
    run_benchmark "I/O Operations" "io_benchmark"
fi

# Generate a summary report
echo "=========================================="
echo "Generating Performance Summary"
echo "=========================================="
echo ""

# Statistics for each language
orus_wins=0
python_wins=0
node_wins=0
lua_wins=0

# Process benchmark results from memory
for result in "${benchmark_results[@]}"; do
    # Parse the result string: name|orus_time|python_time|node_time|lua_time|winner|fastest_time
    IFS='|' read -r benchmark_name orus_time python_time node_time lua_time winner fastest_time <<< "$result"
    
    # Count wins
    if [ "$winner" = "Orus" ]; then
        orus_wins=$((orus_wins + 1))
    elif [ "$winner" = "Python" ]; then
        python_wins=$((python_wins + 1))
    elif [ "$winner" = "Node.js" ]; then
        node_wins=$((node_wins + 1))
    elif [ "$winner" = "Lua" ]; then
        lua_wins=$((lua_wins + 1))
    fi
    
    # Print benchmark results
    echo "=== $benchmark_name Benchmark Results ==="
    printf "| %-15s | %-12s | %-10s |\n" "Language" "Time (sec)" "Winner"
    printf "|-----------------|--------------|------------|\n"
    
    # Calculate relative performance for each language
    orus_relative=$(echo "scale=2; ($orus_time / $fastest_time)" | bc -l)
    python_relative=$(echo "scale=2; ($python_time / $fastest_time)" | bc -l)
    node_relative=$(echo "scale=2; ($node_time / $fastest_time)" | bc -l)
    lua_relative=$(echo "scale=2; ($lua_time / $fastest_time)" | bc -l)
    
    # Print results with winner markers
    orus_mark=""; if [ "$winner" = "Orus" ]; then orus_mark="🏆"; fi
    python_mark=""; if [ "$winner" = "Python" ]; then python_mark="🏆"; fi
    node_mark=""; if [ "$winner" = "Node.js" ]; then node_mark="🏆"; fi
    lua_mark=""; if [ "$winner" = "Lua" ]; then lua_mark="🏆"; fi
    
    printf "| %-15s | %-12s | %-10s |\n" "Orus" "$orus_time" "$orus_mark"
    printf "| %-15s | %-12s | %-10s |\n" "Python" "$python_time" "$python_mark"
    printf "| %-15s | %-12s | %-10s |\n" "Node.js" "$node_time" "$node_mark"
    printf "| %-15s | %-12s | %-10s |\n" "Lua" "$lua_time" "$lua_mark"
    echo ""
done

# Print the summary table
echo "Performance Rankings:"
echo "--------------------"
echo "Orus: $orus_wins wins"
echo "Python: $python_wins wins"
echo "Node.js: $node_wins wins"
echo "Lua: $lua_wins wins"
echo ""

# Determine overall winner
max_wins=$orus_wins
overall_winner="Orus"

if [ $python_wins -gt $max_wins ]; then
    max_wins=$python_wins
    overall_winner="Python"
fi

if [ $node_wins -gt $max_wins ]; then
    max_wins=$node_wins
    overall_winner="Node.js"
fi

if [ $lua_wins -gt $max_wins ]; then
    max_wins=$lua_wins
    overall_winner="Lua"
fi

echo "🏆 Overall Winner: $overall_winner with $max_wins benchmark wins! 🏆"

echo "=========================================="
echo "All Benchmarks Complete"
echo "=========================================="