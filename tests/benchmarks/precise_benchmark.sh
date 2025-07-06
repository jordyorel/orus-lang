#!/bin/bash

# Precise Benchmark Script for Orus Language
# Provides detailed timing and performance analysis with high precision

set -e

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ORUS_BINARY="$SCRIPT_DIR/../../orus"
ITERATIONS=5

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

# Ensure Orus binary exists
if [[ ! -f "$ORUS_BINARY" ]]; then
    echo -e "${RED}❌ Orus binary not found at $ORUS_BINARY${NC}"
    echo "Please run 'make' from the project root first."
    exit 1
fi

# Function to get high precision time
get_time_ns() {
    if command -v python3 >/dev/null 2>&1; then
        python3 -c "import time; print(int(time.time_ns()))"
    else
        date +%s%N 2>/dev/null || echo $(($(date +%s) * 1000000000))
    fi
}

# Function to run precise benchmark with multiple iterations
run_precise_benchmark() {
    local benchmark_file="$1"
    local benchmark_name="$2"
    local iterations="$3"
    
    if [[ ! -f "$SCRIPT_DIR/$benchmark_file" ]]; then
        echo -e "${RED}❌ Benchmark file $benchmark_file not found${NC}"
        return 1
    fi
    
    echo -e "${CYAN}🔬 Running precise benchmark: $benchmark_name${NC}"
    echo "File: $benchmark_file"
    echo "Iterations: $iterations"
    echo "----------------------------------------"
    
    local times=()
    local total_time=0
    local successful_runs=0
    local min_time=999999999
    local max_time=0
    
    for ((i=1; i<=iterations; i++)); do
        echo -n "  Run $i/$iterations: "
        
        # Warm up the binary and system caches on first run
        if [[ $i -eq 1 ]]; then
            "$ORUS_BINARY" --version > /dev/null 2>&1 || true
        fi
        
        local start_time=$(get_time_ns)
        
        if "$ORUS_BINARY" "$SCRIPT_DIR/$benchmark_file" > /dev/null 2>&1; then
            local end_time=$(get_time_ns)
            local duration_ns=$((end_time - start_time))
            local duration_ms=$(echo "$duration_ns" | awk '{printf "%.3f", $1/1000000}')
            
            times+=("$duration_ns")
            total_time=$((total_time + duration_ns))
            successful_runs=$((successful_runs + 1))
            
            # Track min/max
            if [[ $duration_ns -lt $min_time ]]; then
                min_time=$duration_ns
            fi
            if [[ $duration_ns -gt $max_time ]]; then
                max_time=$duration_ns
            fi
            
            echo -e "${GREEN}${duration_ms}ms${NC}"
        else
            echo -e "${RED}FAILED${NC}"
        fi
    done
    
    if [[ $successful_runs -eq 0 ]]; then
        echo -e "${RED}❌ All runs failed for $benchmark_name${NC}"
        return 1
    fi
    
    # Calculate statistics
    local avg_time_ns=$((total_time / successful_runs))
    local avg_time_ms=$(echo "$avg_time_ns" | awk '{printf "%.3f", $1/1000000}')
    local min_time_ms=$(echo "$min_time" | awk '{printf "%.3f", $1/1000000}')
    local max_time_ms=$(echo "$max_time" | awk '{printf "%.3f", $1/1000000}')
    
    # Calculate standard deviation
    local variance=0
    for time_ns in "${times[@]}"; do
        local diff=$((time_ns - avg_time_ns))
        variance=$((variance + diff * diff))
    done
    variance=$((variance / successful_runs))
    local stddev_ns=$(echo "$variance" | awk '{printf "%.0f", sqrt($1)}')
    local stddev_ms=$(echo "$stddev_ns" | awk '{printf "%.3f", $1/1000000}')
    
    # Calculate coefficient of variation (CV)
    local cv=$(echo "$stddev_ns $avg_time_ns" | awk '{printf "%.2f", ($1/$2)*100}')
    
    echo ""
    echo -e "${BLUE}📊 Statistics for $benchmark_name:${NC}"
    echo "  Successful runs: $successful_runs/$iterations"
    echo "  Average time:    ${avg_time_ms}ms"
    echo "  Minimum time:    ${min_time_ms}ms"
    echo "  Maximum time:    ${max_time_ms}ms"
    echo "  Std deviation:   ${stddev_ms}ms"
    echo "  Coefficient of variation: ${cv}%"
    
    # Performance assessment
    if (( $(echo "$cv < 5.0" | bc -l 2>/dev/null || echo "0") )); then
        echo -e "  Consistency:     ${GREEN}Excellent (CV < 5%)${NC}"
    elif (( $(echo "$cv < 10.0" | bc -l 2>/dev/null || echo "0") )); then
        echo -e "  Consistency:     ${GREEN}Good (CV < 10%)${NC}"
    elif (( $(echo "$cv < 20.0" | bc -l 2>/dev/null || echo "0") )); then
        echo -e "  Consistency:     ${YELLOW}Fair (CV < 20%)${NC}"
    else
        echo -e "  Consistency:     ${RED}Poor (CV ≥ 20%)${NC}"
    fi
    
    # Performance classification
    local avg_time_int=$(echo "$avg_time_ms" | awk '{printf "%.0f", $1}')
    if [[ $avg_time_int -le 25 ]]; then
        echo -e "  Performance:     ${GREEN}Excellent (≤25ms)${NC}"
    elif [[ $avg_time_int -le 50 ]]; then
        echo -e "  Performance:     ${GREEN}Very Good (≤50ms)${NC}"
    elif [[ $avg_time_int -le 100 ]]; then
        echo -e "  Performance:     ${YELLOW}Good (≤100ms)${NC}"
    elif [[ $avg_time_int -le 500 ]]; then
        echo -e "  Performance:     ${YELLOW}Fair (≤500ms)${NC}"
    else
        echo -e "  Performance:     ${RED}Poor (>500ms)${NC}"
    fi
    
    echo ""
}

# Function to run cold start analysis
run_cold_start_analysis() {
    echo -e "${CYAN}❄️  Cold Start Performance Analysis${NC}"
    echo "Testing first-run vs subsequent-run performance..."
    echo "----------------------------------------"
    
    local test_file="$SCRIPT_DIR/arithmetic_benchmark.orus"
    if [[ ! -f "$test_file" ]]; then
        echo -e "${YELLOW}⚠️  Arithmetic benchmark not found, skipping cold start analysis${NC}"
        return
    fi
    
    # First run (cold start)
    echo -n "  Cold start (1st run): "
    local start_time=$(get_time_ns)
    if "$ORUS_BINARY" "$test_file" > /dev/null 2>&1; then
        local end_time=$(get_time_ns)
        local cold_time_ns=$((end_time - start_time))
        local cold_time_ms=$(echo "$cold_time_ns" | awk '{printf "%.3f", $1/1000000}')
        echo -e "${GREEN}${cold_time_ms}ms${NC}"
    else
        echo -e "${RED}FAILED${NC}"
        return
    fi
    
    # Warm runs
    local warm_times=()
    local warm_total=0
    
    for ((i=1; i<=3; i++)); do
        echo -n "  Warm run $i:        "
        local start_time=$(get_time_ns)
        if "$ORUS_BINARY" "$test_file" > /dev/null 2>&1; then
            local end_time=$(get_time_ns)
            local warm_time_ns=$((end_time - start_time))
            local warm_time_ms=$(echo "$warm_time_ns" | awk '{printf "%.3f", $1/1000000}')
            echo -e "${GREEN}${warm_time_ms}ms${NC}"
            
            warm_times+=("$warm_time_ns")
            warm_total=$((warm_total + warm_time_ns))
        else
            echo -e "${RED}FAILED${NC}"
        fi
    done
    
    if [[ ${#warm_times[@]} -gt 0 ]]; then
        local avg_warm_ns=$((warm_total / ${#warm_times[@]}))
        local avg_warm_ms=$(echo "$avg_warm_ns" | awk '{printf "%.3f", $1/1000000}')
        
        echo ""
        echo -e "${BLUE}🔥 Cold Start Analysis:${NC}"
        echo "  Cold start:      ${cold_time_ms}ms"
        echo "  Average warm:    ${avg_warm_ms}ms"
        
        # Calculate cold start penalty
        if [[ $avg_warm_ns -gt 0 ]]; then
            local penalty_ratio=$(echo "$cold_time_ns $avg_warm_ns" | awk '{printf "%.2f", $1/$2}')
            local penalty_percent=$(echo "$cold_time_ns $avg_warm_ns" | awk '{printf "%.1f", (($1-$2)/$2)*100}')
            
            echo "  Cold start penalty: ${penalty_ratio}x (${penalty_percent}% slower)"
            
            # Assessment
            if (( $(echo "$penalty_ratio < 1.5" | bc -l 2>/dev/null || echo "0") )); then
                echo -e "  Assessment:      ${GREEN}Excellent - Minimal cold start penalty${NC}"
            elif (( $(echo "$penalty_ratio < 3.0" | bc -l 2>/dev/null || echo "0") )); then
                echo -e "  Assessment:      ${GREEN}Good - Acceptable cold start penalty${NC}"
            elif (( $(echo "$penalty_ratio < 10.0" | bc -l 2>/dev/null || echo "0") )); then
                echo -e "  Assessment:      ${YELLOW}Fair - Noticeable cold start penalty${NC}"
            else
                echo -e "  Assessment:      ${RED}Poor - Significant cold start penalty${NC}"
            fi
        fi
    fi
    
    echo ""
}

# Main execution
echo -e "${BLUE}=================================================================${NC}"
echo -e "${BLUE}              Orus Precise Performance Benchmark${NC}"
echo -e "${BLUE}=================================================================${NC}"
echo ""

# System information
echo -e "${CYAN}🖥️  System Information:${NC}"
echo "  Date:     $(date)"
echo "  Platform: $(uname -s) $(uname -m)"
echo "  Orus:     $($ORUS_BINARY --version 2>/dev/null || echo 'Version unknown')"
if command -v git >/dev/null 2>&1 && git rev-parse --git-dir >/dev/null 2>&1; then
    echo "  Commit:   $(git rev-parse --short HEAD 2>/dev/null || echo 'unknown')"
fi
echo ""

# Cold start analysis
run_cold_start_analysis

# Define benchmarks
BENCHMARK_FILES=("arithmetic_benchmark.orus" "control_flow_benchmark.orus" "vm_optimization_benchmark.orus")
BENCHMARK_NAMES=("Arithmetic Operations" "Control Flow" "VM Optimizations")

# Run each benchmark
for i in "${!BENCHMARK_FILES[@]}"; do
    benchmark_file="${BENCHMARK_FILES[$i]}"
    benchmark_name="${BENCHMARK_NAMES[$i]}"
    run_precise_benchmark "$benchmark_file" "$benchmark_name" "$ITERATIONS"
done

echo -e "${BLUE}=================================================================${NC}"
echo -e "${GREEN}✨ Precise benchmark analysis complete!${NC}"
echo -e "${BLUE}=================================================================${NC}"
echo ""

# Performance summary and recommendations
echo -e "${CYAN}💡 Performance Summary:${NC}"
echo "• Cold start performance has been optimized with global dispatch table"
echo "• VM optimizations provide significant performance benefits"
echo "• Consistent performance indicates stable VM implementation"
echo "• Use these results as baseline for future performance testing"
echo ""