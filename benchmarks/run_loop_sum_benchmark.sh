#!/bin/bash

echo "=== LOOP SUM BENCHMARK: 1 BILLION ITERATIONS ==="
echo "Testing computational performance across languages"
echo "Expected result: 499999999500000000"
echo ""

# Function to run benchmark and extract timing
run_benchmark() {
    local lang=$1
    local cmd=$2
    local file=$3
    
    echo "=== $lang ==="
    echo "Running: $cmd $file"
    
    # Capture both stdout and stderr, but separate them
    result_file=$(mktemp)
    time_file=$(mktemp)
    
    start_time=$(date +%s.%N)
    timeout 120s $cmd $file > "$result_file" 2> "$time_file"
    exit_code=$?
    end_time=$(date +%s.%N)
    
    if [ $exit_code -eq 124 ]; then
        echo "❌ TIMEOUT (>120s)"
        result="TIMEOUT"
        time="TIMEOUT"
    elif [ $exit_code -ne 0 ]; then
        echo "❌ ERROR (exit code: $exit_code)"
        result="ERROR"
        time="ERROR"
    else
        result=$(cat "$result_file")
        # Try to extract timing from stderr, fallback to our measurement
        time_from_stderr=$(grep -o '[0-9]*\.[0-9]* seconds' "$time_file" | head -1 | grep -o '[0-9]*\.[0-9]*')
        if [ -n "$time_from_stderr" ]; then
            time="$time_from_stderr"
        else
            time=$(echo "$end_time - $start_time" | bc -l)
        fi
    fi
    
    echo "Result: $result"
    echo "Time: ${time}s"
    
    # Verify correctness
    if [ "$result" = "499999999500000000" ]; then
        echo "✅ CORRECT"
    else
        echo "❌ INCORRECT"
    fi
    
    echo ""
    
    # Store results for summary
    eval "${lang}_result='$result'"
    eval "${lang}_time='$time'"
    
    rm -f "$result_file" "$time_file"
}

# Make scripts executable
chmod +x benchmarks/loop_sum_benchmark.py
chmod +x benchmarks/loop_sum_benchmark.js
chmod +x benchmarks/loop_sum_benchmark.lua

# Run benchmarks
run_benchmark "Orus" "./orus" "benchmarks/loop_sum_benchmark.orus"
run_benchmark "Python3" "python3" "benchmarks/loop_sum_benchmark.py"
run_benchmark "Node.js" "node" "benchmarks/loop_sum_benchmark.js"
run_benchmark "Lua" "lua" "benchmarks/loop_sum_benchmark.lua"

# Summary
echo "=== PERFORMANCE SUMMARY ==="
printf "%-10s %-20s %-15s %-10s\n" "Language" "Result" "Time (s)" "Status"
printf "%-10s %-20s %-15s %-10s\n" "--------" "------" "--------" "------"

# Helper function to format results
format_result() {
    local lang=$1
    eval "local result=\$${lang}_result"
    eval "local time=\$${lang}_time"
    
    local status="❌"
    if [ "$result" = "499999999500000000" ]; then
        status="✅"
    fi
    
    # Truncate long results for display
    local display_result="$result"
    if [ ${#result} -gt 18 ]; then
        display_result="${result:0:15}..."
    fi
    
    printf "%-10s %-20s %-15s %-10s\n" "$lang" "$display_result" "$time" "$status"
}

format_result "Orus"
format_result "Python3"
format_result "Node.js"
format_result "Lua"

echo ""
echo "=== PERFORMANCE RANKING ==="

# Create array of results for sorting (only correct results)
declare -a times_array
declare -a langs_array

for lang in "Orus" "Python3" "Node.js" "Lua"; do
    eval "local result=\$${lang}_result"
    eval "local time=\$${lang}_time"
    
    if [ "$result" = "499999999500000000" ] && [ "$time" != "TIMEOUT" ] && [ "$time" != "ERROR" ]; then
        times_array+=("$time")
        langs_array+=("$lang")
    fi
done

# Simple bubble sort for times (bash doesn't have great sorting for floats)
n=${#times_array[@]}
for ((i=0; i<n; i++)); do
    for ((j=0; j<n-i-1; j++)); do
        # Compare floating point numbers
        if (( $(echo "${times_array[j]} > ${times_array[j+1]}" | bc -l) )); then
            # Swap times
            temp_time=${times_array[j]}
            times_array[j]=${times_array[j+1]}
            times_array[j+1]=$temp_time
            
            # Swap languages
            temp_lang=${langs_array[j]}
            langs_array[j]=${langs_array[j+1]}
            langs_array[j+1]=$temp_lang
        fi
    done
done

# Display ranking
for ((i=0; i<${#times_array[@]}; i++)); do
    rank=$((i+1))
    case $rank in
        1) medal="🥇" ;;
        2) medal="🥈" ;;
        3) medal="🥉" ;;
        *) medal="  " ;;
    esac
    
    # Calculate ops/sec
    ops_per_sec=$(echo "scale=0; 1000000000 / ${times_array[i]}" | bc -l)
    
    echo "$medal $rank. ${langs_array[i]}: ${times_array[i]}s ($(printf "%'.0f" $ops_per_sec) ops/sec)"
done

echo ""
echo "=== TYPE SYSTEM PERFORMANCE ANALYSIS ==="
echo "This benchmark tests:"
echo "• Large integer arithmetic (i64)"
echo "• Loop optimization"
echo "• Memory management under load"
echo "• Type system performance"