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

if [ ! -f "../../orus" ]; then
    echo "Error: orus interpreter not found. Run 'make' first."
    exit 1
fi

# Function to run a benchmark for all languages
run_benchmark() {
    local benchmark_name=$1
    local benchmark_file=$2
    
    echo "=========================================="
    echo "=== $benchmark_name Benchmark ==="
    echo "=========================================="
    echo ""
    
    echo "--- Orus ---"
    printf "Running Orus $benchmark_name: "
    time ../../orus "$benchmark_file.orus"
    echo ""
    
    echo "--- Python ---"
    printf "Running Python $benchmark_name: "
    time python3 "$benchmark_file.py"
    echo ""
    
    echo "--- Node.js ---"
    printf "Running Node.js $benchmark_name: "
    time node "$benchmark_file.js"
    echo ""
    
    echo "--- Lua ---"
    printf "Running Lua $benchmark_name: "
    time lua "$benchmark_file.lua"
    echo ""
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

# Future benchmarks can be added here automatically
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

echo "=========================================="
echo "All Benchmarks Complete"
echo "=========================================="