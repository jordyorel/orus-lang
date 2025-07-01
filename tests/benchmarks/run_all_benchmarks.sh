#!/bin/bash

# Universal Benchmark Runner
# Runs all available benchmarks across multiple languages
# Benchmarks: arithmetic, control flow, and any future benchmarks

echo "=========================================="
echo "Orus Cross-Language Benchmark Suite"
echo "=========================================="

# Check which languages are available
AVAILABLE_LANGUAGES=""
if command -v python3 &> /dev/null; then
    AVAILABLE_LANGUAGES="$AVAILABLE_LANGUAGES Python"
fi
if command -v node &> /dev/null; then
    AVAILABLE_LANGUAGES="$AVAILABLE_LANGUAGES Node.js"
fi
if command -v lua &> /dev/null; then
    AVAILABLE_LANGUAGES="$AVAILABLE_LANGUAGES Lua"
fi
if command -v julia &> /dev/null; then
    AVAILABLE_LANGUAGES="$AVAILABLE_LANGUAGES Julia"
fi

echo "Running all available benchmarks across languages"
echo "Languages: Orus$AVAILABLE_LANGUAGES"
echo ""

# Check if critical executables exist
if ! command -v python3 &> /dev/null; then
    echo "Warning: python3 not found - skipping Python benchmarks"
fi

if ! command -v node &> /dev/null; then
    echo "Warning: node (Node.js) not found - skipping Node.js benchmarks"
fi

if ! command -v lua &> /dev/null; then
    echo "Warning: lua not found - skipping Lua benchmarks"
fi

if ! command -v julia &> /dev/null; then
    echo "Warning: julia not found - skipping Julia benchmarks"
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
    
    echo "=========================================="
    echo "=== $benchmark_name Benchmark ==="
    echo "=========================================="
    echo ""
    
    # Initialize times with high default values for missing languages
    local orus_time=999999
    local python_time=999999
    local node_time=999999
    local lua_time=999999
    local julia_time=999999
    
    # Store Orus time
    echo "--- Orus ---"
    printf "Running Orus $benchmark_name: "
    start_time=$(date +%s.%N)
    ../../orus "$benchmark_file.orus"
    end_time=$(date +%s.%N)
    orus_time=$(echo "$end_time - $start_time" | bc -l)
    printf "Orus execution time: %.6f seconds\n" $orus_time
    printf "\nreal\t%.3fs\n" $orus_time
    echo ""
    
    # Store Python time (if available)
    if command -v python3 &> /dev/null && [ -f "$benchmark_file.py" ]; then
        echo "--- Python ---"
        printf "Running Python $benchmark_name: "
        python_start=$(date +%s.%N)
        python3 "$benchmark_file.py"
        python_end=$(date +%s.%N)
        python_time=$(echo "$python_end - $python_start" | bc -l)
        printf "Python execution time: %.6f seconds\n" $python_time
        printf "\nreal\t%.3fs\n" $python_time
        echo ""
    fi
    
    # Store Node.js time (if available)
    if command -v node &> /dev/null && [ -f "$benchmark_file.js" ]; then
        echo "--- Node.js ---"
        printf "Running Node.js $benchmark_name: "
        node_start=$(date +%s.%N)
        node "$benchmark_file.js"
        node_end=$(date +%s.%N)
        node_time=$(echo "$node_end - $node_start" | bc -l)
        printf "Node.js execution time: %.6f seconds\n" $node_time
        printf "\nreal\t%.3fs\n" $node_time
        echo ""
    fi
    
    # Store Lua time (if available)
    if command -v lua &> /dev/null && [ -f "$benchmark_file.lua" ]; then
        echo "--- Lua ---"
        printf "Running Lua $benchmark_name: "
        lua_start=$(date +%s.%N)
        lua "$benchmark_file.lua"
        lua_end=$(date +%s.%N)
        lua_time=$(echo "$lua_end - $lua_start" | bc -l)
        printf "Lua execution time: %.6f seconds\n" $lua_time
        printf "\nreal\t%.3fs\n" $lua_time
        echo ""
    fi
    
    # Store Julia time (if available)
    if command -v julia &> /dev/null && [ -f "$benchmark_file.jl" ]; then
        echo "--- Julia ---"
        printf "Running Julia $benchmark_name: "
        julia_start=$(date +%s.%N)
        julia "$benchmark_file.jl"
        julia_end=$(date +%s.%N)
        julia_time=$(echo "$julia_end - $julia_start" | bc -l)
        printf "Julia execution time: %.6f seconds\n" $julia_time
        printf "\nreal\t%.3fs\n" $julia_time
        echo ""
    fi
    
    # Store results in memory
    local result_summary="$benchmark_name|$orus_time|$python_time|$node_time|$lua_time|$julia_time"
    
    # Determine the winner for this benchmark
    winner="Orus"
    fastest=$orus_time
    
    if (( $(echo "$python_time < $fastest && $python_time < 999999" | bc -l) )); then
        winner="Python"
        fastest=$python_time
    fi
    
    if (( $(echo "$node_time < $fastest && $node_time < 999999" | bc -l) )); then
        winner="Node.js"
        fastest=$node_time
    fi
    
    if (( $(echo "$lua_time < $fastest && $lua_time < 999999" | bc -l) )); then
        winner="Lua"
        fastest=$lua_time
    fi
    
    if (( $(echo "$julia_time < $fastest && $julia_time < 999999" | bc -l) )); then
        winner="Julia"
        fastest=$julia_time
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
julia_wins=0

# Process benchmark results from memory
for result in "${benchmark_results[@]}"; do
    # Parse the result string: name|orus_time|python_time|node_time|lua_time|julia_time|winner|fastest_time
    IFS='|' read -r benchmark_name orus_time python_time node_time lua_time julia_time winner fastest_time <<< "$result"
    
    # Count wins
    if [ "$winner" = "Orus" ]; then
        orus_wins=$((orus_wins + 1))
    elif [ "$winner" = "Python" ]; then
        python_wins=$((python_wins + 1))
    elif [ "$winner" = "Node.js" ]; then
        node_wins=$((node_wins + 1))
    elif [ "$winner" = "Lua" ]; then
        lua_wins=$((lua_wins + 1))
    elif [ "$winner" = "Julia" ]; then
        julia_wins=$((julia_wins + 1))
    fi
    
    # Print benchmark results
    echo "=== $benchmark_name Benchmark Results ==="
    printf "| %-15s | %-12s | %-10s |\n" "Language" "Time (sec)" "Winner"
    printf "|-----------------|--------------|------------|\n"
    
    # Print results with winner markers (only for available languages)
    orus_mark=""; if [ "$winner" = "Orus" ]; then orus_mark="🏆"; fi
    printf "| %-15s | %-12s | %-10s |\n" "Orus" "$orus_time" "$orus_mark"
    
    if (( $(echo "$python_time < 999999" | bc -l) )); then
        python_mark=""; if [ "$winner" = "Python" ]; then python_mark="🏆"; fi
        printf "| %-15s | %-12s | %-10s |\n" "Python" "$python_time" "$python_mark"
    fi
    
    if (( $(echo "$node_time < 999999" | bc -l) )); then
        node_mark=""; if [ "$winner" = "Node.js" ]; then node_mark="🏆"; fi
        printf "| %-15s | %-12s | %-10s |\n" "Node.js" "$node_time" "$node_mark"
    fi
    
    if (( $(echo "$lua_time < 999999" | bc -l) )); then
        lua_mark=""; if [ "$winner" = "Lua" ]; then lua_mark="🏆"; fi
        printf "| %-15s | %-12s | %-10s |\n" "Lua" "$lua_time" "$lua_mark"
    fi
    
    if (( $(echo "$julia_time < 999999" | bc -l) )); then
        julia_mark=""; if [ "$winner" = "Julia" ]; then julia_mark="🏆"; fi
        printf "| %-15s | %-12s | %-10s |\n" "Julia" "$julia_time" "$julia_mark"
    fi
    
    echo ""
done

# Print the summary table
echo "Performance Rankings:"
echo "--------------------"
echo "Orus: $orus_wins wins"
if command -v python3 &> /dev/null; then
    echo "Python: $python_wins wins"
fi
if command -v node &> /dev/null; then
    echo "Node.js: $node_wins wins"
fi
if command -v lua &> /dev/null; then
    echo "Lua: $lua_wins wins"
fi
if command -v julia &> /dev/null; then
    echo "Julia: $julia_wins wins"
fi
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

if [ $julia_wins -gt $max_wins ]; then
    max_wins=$julia_wins
    overall_winner="Julia"
fi

echo "🏆 Overall Winner: $overall_winner with $max_wins benchmark wins! 🏆"

echo "=========================================="
echo "All Benchmarks Complete"
echo "=========================================="