#!/bin/bash

# Baseline Calibration Script for CI Environments
# Automatically determines appropriate performance baselines for the current environment

set -e

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ORUS_BINARY="$SCRIPT_DIR/../../orus"
BASELINES_FILE="$SCRIPT_DIR/performance_baselines.txt"
CALIBRATION_RUNS=10

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

echo -e "${CYAN}🎯 Calibrating performance baselines for current environment...${NC}"
echo ""

# Apply optimizations if available
if [[ -f "$SCRIPT_DIR/optimize_for_ci.sh" ]]; then
    echo -e "${BLUE}⚡ Applying system optimizations...${NC}"
    "$SCRIPT_DIR/optimize_for_ci.sh" >/dev/null 2>&1 || true
    source /tmp/performance_env.sh >/dev/null 2>&1 || true
fi

# Function to run calibration benchmark
calibrate_benchmark() {
    local benchmark_file="$1"
    local benchmark_name="$2"
    
    if [[ ! -f "$SCRIPT_DIR/$benchmark_file" ]]; then
        echo -e "${YELLOW}⚠️  Benchmark file $benchmark_file not found, skipping${NC}"
        return 1
    fi
    
    echo -e "${BLUE}📊 Calibrating $benchmark_name benchmark...${NC}"
    echo "  Running $CALIBRATION_RUNS iterations for accurate baseline..."
    
    local times=()
    local successful_runs=0
    
    # Extended warmup
    for ((warmup=1; warmup<=5; warmup++)); do
        "$ORUS_BINARY" "$SCRIPT_DIR/$benchmark_file" > /dev/null 2>&1 || true
    done
    sleep 1
    
    # Calibration runs
    for ((i=1; i<=CALIBRATION_RUNS; i++)); do
        echo -n "    Run $i/$CALIBRATION_RUNS... "
        
        # Multiple samples per run
        local run_times=()
        for ((sample=1; sample<=3; sample++)); do
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
                local duration=$(((end_time - start_time) / 1000000))
                run_times+=("$duration")
            fi
            [[ $sample -lt 3 ]] && sleep 0.05
        done
        
        if [[ ${#run_times[@]} -gt 0 ]]; then
            # Use median of samples
            IFS=$'\n' sorted_times=($(sort -n <<< "${run_times[*]}"))
            local median_idx=$(( ${#sorted_times[@]} / 2 ))
            local duration=${sorted_times[$median_idx]}
            
            times+=("$duration")
            successful_runs=$((successful_runs + 1))
            echo "${duration}ms"
        else
            echo -e "${RED}FAILED${NC}"
        fi
        
        # Brief pause between runs
        sleep 0.2
    done
    
    if [[ $successful_runs -lt 5 ]]; then
        echo -e "${RED}❌ Too few successful runs for reliable calibration${NC}"
        return 1
    fi
    
    # Calculate statistics
    IFS=$'\n' sorted_times=($(sort -n <<< "${times[*]}"))
    local count=${#sorted_times[@]}
    
    # Remove outliers (top and bottom 10%)
    local start_idx=$((count / 10))
    local end_idx=$((count - count / 10))
    local trimmed_times=("${sorted_times[@]:$start_idx:$((end_idx - start_idx))}")
    
    # Calculate trimmed mean
    local total=0
    for time in "${trimmed_times[@]}"; do
        total=$((total + time))
    done
    local baseline=$((total / ${#trimmed_times[@]}))
    
    # Calculate standard deviation
    local variance=0
    for time in "${trimmed_times[@]}"; do
        local diff=$((time - baseline))
        variance=$((variance + diff * diff))
    done
    variance=$((variance / ${#trimmed_times[@]}))
    local stddev=$(echo "sqrt($variance)" | bc -l 2>/dev/null || echo "0")
    local stddev_int=$(echo "$stddev" | awk '{printf "%.0f", $1}')
    
    # Add safety margin (2 standard deviations)
    local safe_baseline=$((baseline + 2 * stddev_int))
    
    # Calculate coefficient of variation
    local cv=0
    if [[ $baseline -gt 0 ]]; then
        cv=$(echo "scale=2; ($stddev_int * 100) / $baseline" | bc -l 2>/dev/null || echo "0")
    fi
    
    echo ""
    echo -e "${GREEN}📈 Calibration Results for $benchmark_name:${NC}"
    echo "    Successful runs: $successful_runs/$CALIBRATION_RUNS"
    echo "    Raw mean: ${baseline}ms"
    echo "    Standard deviation: ${stddev_int}ms"
    echo "    Coefficient of variation: ${cv}%"
    echo "    Recommended baseline: ${safe_baseline}ms (mean + 2σ)"
    
    # Consistency assessment
    if (( $(echo "$cv < 10.0" | bc -l 2>/dev/null || echo "0") )); then
        echo -e "    Consistency: ${GREEN}Excellent (CV < 10%)${NC}"
    elif (( $(echo "$cv < 20.0" | bc -l 2>/dev/null || echo "0") )); then
        echo -e "    Consistency: ${YELLOW}Good (CV < 20%)${NC}"
    else
        echo -e "    Consistency: ${RED}Poor (CV ≥ 20%)${NC}"
        echo -e "    ${YELLOW}⚠️  Consider running more calibration iterations${NC}"
    fi
    
    echo ""
    
    # Return the recommended baseline
    echo "$benchmark_name:$safe_baseline"
}

# System information
echo -e "${CYAN}🖥️  Environment Information:${NC}"
echo "  Platform: $(uname -s) $(uname -m)"
echo "  Kernel: $(uname -r)"
echo "  CPU cores: $(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 'unknown')"
echo "  Memory: $(free -h 2>/dev/null | awk '/^Mem:/ {print $2}' || echo 'unknown')"
if command -v git >/dev/null 2>&1 && git rev-parse --git-dir >/dev/null 2>&1; then
    echo "  Commit: $(git rev-parse --short HEAD 2>/dev/null || echo 'unknown')"
fi
echo ""

# Define benchmarks to calibrate
BENCHMARK_FILES=("arithmetic_benchmark.orus" "control_flow_benchmark.orus" "vm_optimization_benchmark.orus")
BENCHMARK_NAMES=("arithmetic" "control_flow" "vm_optimization")

# Run calibration for each benchmark
declare -A CALIBRATED_BASELINES

for i in "${!BENCHMARK_FILES[@]}"; do
    benchmark_file="${BENCHMARK_FILES[$i]}"
    benchmark_name="${BENCHMARK_NAMES[$i]}"
    
    if result=$(calibrate_benchmark "$benchmark_file" "$benchmark_name"); then
        baseline_value=$(echo "$result" | grep "^${benchmark_name}:" | cut -d':' -f2)
        if [[ -n "$baseline_value" ]]; then
            CALIBRATED_BASELINES["$benchmark_name"]="$baseline_value"
        fi
    fi
done

# Update baselines file
if [[ ${#CALIBRATED_BASELINES[@]} -gt 0 ]]; then
    echo -e "${CYAN}💾 Updating baselines file...${NC}"
    
    # Create backup
    cp "$BASELINES_FILE" "${BASELINES_FILE}.backup.$(date +%Y%m%d_%H%M%S)"
    
    # Update baselines
    for benchmark_name in "${!CALIBRATED_BASELINES[@]}"; do
        baseline_value="${CALIBRATED_BASELINES[$benchmark_name]}"
        
        if grep -q "^${benchmark_name}=" "$BASELINES_FILE"; then
            # Update existing baseline
            sed -i.tmp "s/^${benchmark_name}=.*/${benchmark_name}=${baseline_value}/" "$BASELINES_FILE"
            rm -f "${BASELINES_FILE}.tmp"
        else
            # Add new baseline
            echo "${benchmark_name}=${baseline_value}" >> "$BASELINES_FILE"
        fi
        
        echo "  ✓ ${benchmark_name}: ${baseline_value}ms"
    done
    
    echo ""
    echo -e "${GREEN}✅ Baseline calibration complete!${NC}"
    echo ""
    echo -e "${BLUE}📋 Summary of calibrated baselines:${NC}"
    for benchmark_name in "${!CALIBRATED_BASELINES[@]}"; do
        baseline_value="${CALIBRATED_BASELINES[$benchmark_name]}"
        echo "  • $benchmark_name: ${baseline_value}ms"
    done
    
    echo ""
    echo -e "${CYAN}💡 Recommendations:${NC}"
    echo "• Run this calibration script on each new CI environment"
    echo "• Re-calibrate after significant system or VM changes"
    echo "• Monitor performance over time and adjust baselines as needed"
    echo "• Consider using the precise_benchmark.sh for detailed analysis"
    
else
    echo -e "${RED}❌ No baselines could be calibrated${NC}"
    exit 1
fi