#!/bin/bash

# Performance Regression Test for Orus Language
# Tests for performance regressions against established baselines
# Returns:
#   0 = Performance within acceptable range (PASS)
#   1 = Minor performance regression (WARNING)
#   2 = Critical performance regression (FAIL)

set -e

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ORUS_BINARY="$SCRIPT_DIR/../../orus"
RESULTS_LOG="$SCRIPT_DIR/performance_results.log"
BASELINES_FILE="$SCRIPT_DIR/performance_baselines.txt"

# Performance thresholds (percentage increase from baseline)
WARNING_THRESHOLD=15    # 15% increase triggers warning
FAILURE_THRESHOLD=50    # 50% increase triggers failure

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Ensure Orus binary exists
if [[ ! -f "$ORUS_BINARY" ]]; then
    echo -e "${RED}❌ Orus binary not found at $ORUS_BINARY${NC}"
    echo "Please run 'make' from the project root first."
    exit 2
fi

# Create baseline file if it doesn't exist
if [[ ! -f "$BASELINES_FILE" ]]; then
    echo "# Orus Performance Baselines (in milliseconds)" > "$BASELINES_FILE"
    echo "# Format: benchmark_name=baseline_time_ms" >> "$BASELINES_FILE"
    echo "arithmetic=50" >> "$BASELINES_FILE"
    echo "control_flow=100" >> "$BASELINES_FILE"
    echo "scope_management=60" >> "$BASELINES_FILE"
    echo "vm_optimization=25" >> "$BASELINES_FILE"
fi

# Function to get baseline for a benchmark
get_baseline() {
    local benchmark="$1"
    if [[ -f "$BASELINES_FILE" ]]; then
        grep "^${benchmark}=" "$BASELINES_FILE" | cut -d'=' -f2
    else
        echo "100"  # Default baseline
    fi
}

# Function to update baseline
update_baseline() {
    local benchmark="$1"
    local new_time="$2"
    
    if [[ -f "$BASELINES_FILE" ]]; then
        if grep -q "^${benchmark}=" "$BASELINES_FILE"; then
            # Update existing baseline
            sed -i.bak "s/^${benchmark}=.*/${benchmark}=${new_time}/" "$BASELINES_FILE"
        else
            # Add new baseline
            echo "${benchmark}=${new_time}" >> "$BASELINES_FILE"
        fi
    fi
}

# Function to run a benchmark and measure time
run_benchmark() {
    local benchmark_file="$1"
    local benchmark_name="$2"
    local iterations="${3:-3}"  # Default 3 iterations
    local result_var="$4"  # Variable to store result
    
    if [[ ! -f "$SCRIPT_DIR/$benchmark_file" ]]; then
        echo -e "${YELLOW}⚠️  Benchmark file $benchmark_file not found, skipping${NC}"
        eval "$result_var=0"
        return 1
    fi
    
    echo -e "${BLUE}🔍 Running $benchmark_name benchmark...${NC}"
    
    local total_time=0
    local successful_runs=0
    
    # Warmup for consistent measurements
    echo "  Warming up..."
    "$ORUS_BINARY" --version > /dev/null 2>&1 || true
    "$ORUS_BINARY" "$SCRIPT_DIR/$benchmark_file" > /dev/null 2>&1 || true
    sleep 0.2
    
    for ((i=1; i<=iterations; i++)); do
        echo "  Run $i/$iterations..."
        
        # Use multiple samples and take median for better accuracy
        local run_times=()
        for ((sample=1; sample<=3; sample++)); do
            # Use high-precision timing with Linux optimization
            if [[ -r /proc/uptime ]]; then
                local start_time=$(awk '{print int($1 * 1000000000)}' /proc/uptime)
            else
                local start_time=$(date +%s%N 2>/dev/null || echo $(($(date +%s) * 1000000000)))
            fi
            
            if "$ORUS_BINARY" "$SCRIPT_DIR/$benchmark_file" > /dev/null 2>&1; then
                if [[ -r /proc/uptime ]]; then
                    local end_time=$(awk '{print int($1 * 1000000000)}' /proc/uptime)
                else
                    local end_time=$(date +%s%N 2>/dev/null || echo $(($(date +%s) * 1000000000)))
                fi
                local duration=$(((end_time - start_time) / 1000000))  # Convert to milliseconds
                run_times+=("$duration")
            fi
            [[ $sample -lt 3 ]] && sleep 0.05
        done
        
        if [[ ${#run_times[@]} -gt 0 ]]; then
            # Use median of samples
            IFS=$'\n' sorted_times=($(sort -n <<< "${run_times[*]}"))
            local median_idx=$(( ${#sorted_times[@]} / 2 ))
            local duration=${sorted_times[$median_idx]}
            
            total_time=$((total_time + duration))
            successful_runs=$((successful_runs + 1))
            echo "    ✓ Completed in ${duration}ms"
        else
            echo -e "    ${RED}✗ Run failed${NC}"
        fi
    done
    
    if [[ $successful_runs -eq 0 ]]; then
        echo -e "${RED}❌ All benchmark runs failed for $benchmark_name${NC}"
        eval "$result_var=0"
        return 2
    fi
    
    # Calculate average time
    local avg_time=$((total_time / successful_runs))
    echo -e "${GREEN}📊 Average time: ${avg_time}ms${NC}"
    
    # Store result in the provided variable
    eval "$result_var=$avg_time"
    return 0
}

# Function to analyze performance against baseline
analyze_performance() {
    local benchmark_name="$1"
    local measured_time="$2"
    local baseline_time="$3"
    
    if [[ -z "$baseline_time" || "$baseline_time" -eq 0 ]]; then
        echo "No baseline available, setting current time as baseline"
        update_baseline "$benchmark_name" "$measured_time"
        return 0
    fi
    
    # Calculate percentage increase
    local increase_percent=$(( (measured_time - baseline_time) * 100 / baseline_time ))
    
    echo "Baseline: ${baseline_time}ms, Measured: ${measured_time}ms"
    
    if [[ $increase_percent -lt 0 ]]; then
        echo -e "${GREEN}🎉 Performance improved by $((0 - increase_percent))%${NC}"
        # Update baseline to new better performance
        update_baseline "$benchmark_name" "$measured_time"
        return 0
    elif [[ $increase_percent -le $WARNING_THRESHOLD ]]; then
        echo -e "${GREEN}✅ Performance within acceptable range (+${increase_percent}%)${NC}"
        return 0
    elif [[ $increase_percent -le $FAILURE_THRESHOLD ]]; then
        echo -e "${YELLOW}⚠️  Minor performance regression detected (+${increase_percent}%)${NC}"
        return 1
    else
        echo -e "${RED}❌ Critical performance regression detected (+${increase_percent}%)${NC}"
        return 2
    fi
}

# Function to log results
log_result() {
    local timestamp="$1"
    local commit="$2"
    local benchmark="$3"
    local measured="$4"
    local baseline="$5"
    local status="$6"
    
    # Create log file if it doesn't exist
    if [[ ! -f "$RESULTS_LOG" ]]; then
        echo "timestamp,commit,benchmark,measured_ms,baseline_ms,status" > "$RESULTS_LOG"
    fi
    
    echo "${timestamp},${commit},${benchmark},${measured},${baseline},${status}" >> "$RESULTS_LOG"
}

# Main execution
echo -e "${BLUE}=================================================================${NC}"
echo -e "${BLUE}           Orus Performance Regression Test${NC}"
echo -e "${BLUE}=================================================================${NC}"
echo ""
echo -e "${CYAN}ℹ️  Note: Performance baselines adjusted for CI environment variance${NC}"
echo -e "${CYAN}   Cold start optimization active - global dispatch table eliminates initial delays${NC}"
echo ""

# Get current git commit (if available)
COMMIT_HASH="unknown"
if command -v git >/dev/null 2>&1 && git rev-parse --git-dir >/dev/null 2>&1; then
    COMMIT_HASH=$(git rev-parse --short HEAD 2>/dev/null || echo "unknown")
fi

TIMESTAMP=$(date -u +"%Y-%m-%d %H:%M:%S UTC")
OVERALL_STATUS=0

# Define benchmarks to run
BENCHMARK_FILES=("arithmetic_benchmark.orus" "control_flow_benchmark.orus" "scope_management_benchmark.orus" "vm_optimization_benchmark.orus")
BENCHMARK_NAMES=("arithmetic" "control_flow" "scope_management" "vm_optimization")

# Run each benchmark
for i in "${!BENCHMARK_FILES[@]}"; do
    benchmark_file="${BENCHMARK_FILES[$i]}"
    benchmark_name="${BENCHMARK_NAMES[$i]}"
    
    echo -e "${YELLOW}Testing: $benchmark_name${NC}"
    echo "----------------------------------------"
    
    # Run the benchmark
    measured_time=""
    if run_benchmark "$benchmark_file" "$benchmark_name" 3 measured_time; then
        baseline_time=$(get_baseline "$benchmark_name")
        
        # Analyze performance
        if analyze_performance "$benchmark_name" "$measured_time" "$baseline_time"; then
            status=0
        else
            status=$?
            if [[ $status -gt $OVERALL_STATUS ]]; then
                OVERALL_STATUS=$status
            fi
        fi
        
        # Log the result
        log_result "$TIMESTAMP" "$COMMIT_HASH" "$benchmark_name" "$measured_time" "$baseline_time" "$status"
        
    else
        echo -e "${RED}❌ Failed to run $benchmark_name benchmark${NC}"
        OVERALL_STATUS=2
        log_result "$TIMESTAMP" "$COMMIT_HASH" "$benchmark_name" "FAILED" "$(get_baseline "$benchmark_name")" "2"
    fi
    
    echo ""
done

# Summary
echo -e "${BLUE}=================================================================${NC}"
echo -e "${BLUE}                    Test Summary${NC}"
echo -e "${BLUE}=================================================================${NC}"

case $OVERALL_STATUS in
    0)
        echo -e "${GREEN}✅ All performance tests passed${NC}"
        echo "No significant performance regressions detected."
        ;;
    1)
        echo -e "${YELLOW}⚠️  Performance warning detected${NC}"
        echo "Minor performance regression found, but within acceptable limits."
        echo "Consider investigating if this is expected."
        ;;
    2)
        echo -e "${RED}❌ Critical performance regression detected${NC}"
        echo "Performance has degraded beyond acceptable thresholds."
        echo "Please investigate and fix performance issues."
        ;;
esac

echo ""
echo "📊 Results logged to: $RESULTS_LOG"
echo "📋 Baselines stored in: $BASELINES_FILE"
echo "🔍 View recent results: tail -5 $RESULTS_LOG"
echo ""

exit $OVERALL_STATUS