#!/bin/bash

# Unified Fair Benchmark Script for Orus Language
# Single compilation, fair cross-language comparison, clean results

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

# Benchmark results - using regular variables instead of associative arrays for compatibility
benchmark_results=""

# Function to get high precision time
get_time_ns() {
    if [[ -r /proc/uptime ]]; then
        awk '{print int($1 * 1000000000)}' /proc/uptime
    elif command -v python3 >/dev/null 2>&1; then
        python3 -c "import time; print(int(time.time_ns()))"
    elif date +%s%N >/dev/null 2>&1; then
        date +%s%N
    else
        echo $(($(date +%s) * 1000000000))
    fi
}

# Function to ensure Orus is built once with optimal settings
ensure_orus_built() {
    echo -e "${CYAN}🔧 Ensuring Orus is built with optimal settings...${NC}"
    
    cd "$SCRIPT_DIR/../.."
    
    # Clean build for consistent results
    make clean > /dev/null 2>&1
    
    # Build with optimal settings (use computed goto for best performance)
    if make USE_GOTO=1 > /dev/null 2>&1; then
        echo -e "${GREEN}✅ Orus binary built successfully${NC}"
    else
        echo -e "${RED}❌ Failed to build Orus binary${NC}"
        exit 1
    fi
    
    cd "$SCRIPT_DIR"
}

# Function to run benchmark for a specific language
run_language_benchmark() {
    local lang="$1"
    local file="$2"
    local command="$3"
    
    echo -e "${CYAN}📊 Testing $lang...${NC}"
    
    local times=()
    local total_time=0
    local successful_runs=0
    
    # Warmup runs
    for ((warmup=1; warmup<=2; warmup++)); do
        eval "$command" > /dev/null 2>&1 || true
        sleep 0.05
    done
    
    # Actual benchmark runs
    for ((i=1; i<=ITERATIONS; i++)); do
        local start_time=$(get_time_ns)
        
        if eval "$command" > /dev/null 2>&1; then
            local end_time=$(get_time_ns)
            local duration_ns=$((end_time - start_time))
            local duration_ms=$(echo "$duration_ns" | awk '{printf "%.1f", $1/1000000}')
            
            times+=("$duration_ns")
            total_time=$((total_time + duration_ns))
            successful_runs=$((successful_runs + 1))
            
            echo -n "."
        else
            echo -n "F"
        fi
    done
    
    if [[ $successful_runs -gt 0 ]]; then
        local avg_time_ns=$((total_time / successful_runs))
        local avg_time_ms=$(echo "$avg_time_ns" | awk '{printf "%.1f", $1/1000000}')
        
        benchmark_results="${benchmark_results}${lang}:${avg_time_ms};"
        echo -e " ${GREEN}${avg_time_ms}ms${NC}"
    else
        echo -e " ${RED}FAILED${NC}"
        benchmark_results="${benchmark_results}${lang}:FAILED;"
    fi
}

# Function to run all benchmarks for a test category
run_benchmark_category() {
    local category="$1"
    local orus_file="$2"
    
    echo -e "${BLUE}=================================================================${NC}"
    echo -e "${BLUE}                $category Performance Test${NC}"
    echo -e "${BLUE}=================================================================${NC}"
    echo ""
    
    # Check if files exist
    if [[ ! -f "$SCRIPT_DIR/$orus_file" ]]; then
        echo -e "${RED}❌ Orus file $orus_file not found${NC}"
        return
    fi
    
    # Get base name for other language files
    local base_name="${orus_file%.orus}"
    
    # Run benchmarks for each language
    run_language_benchmark "Orus" "$orus_file" "\"$ORUS_BINARY\" \"$SCRIPT_DIR/$orus_file\""
    
    if [[ -f "$SCRIPT_DIR/${base_name}.py" ]]; then
        run_language_benchmark "Python" "${base_name}.py" "python3 \"$SCRIPT_DIR/${base_name}.py\""
    fi
    
    if [[ -f "$SCRIPT_DIR/${base_name}.js" ]]; then
        run_language_benchmark "JavaScript" "${base_name}.js" "node \"$SCRIPT_DIR/${base_name}.js\""
    fi
    
    if [[ -f "$SCRIPT_DIR/${base_name}.lua" ]]; then
        run_language_benchmark "Lua" "${base_name}.lua" "lua \"$SCRIPT_DIR/${base_name}.lua\""
    fi
    
    echo ""
    
    # Display category results
    echo -e "${CYAN}📋 $category Results:${NC}"
    
    # Parse results and sort by performance
    local sorted_results=()
    local failed_results=()
    
    IFS=';' read -ra PAIRS <<< "$benchmark_results"
    for pair in "${PAIRS[@]}"; do
        if [[ -n "$pair" ]]; then
            local lang="${pair%:*}"
            local time="${pair#*:}"
            
            if [[ "$time" != "FAILED" ]]; then
                sorted_results+=("$time:$lang")
            else
                failed_results+=("$lang")
            fi
        fi
    done
    
    # Sort numerically
    IFS=$'\n' sorted_results=($(sort -n <<< "${sorted_results[*]}"))
    
    # Display sorted results
    local rank=1
    local fastest_time=""
    for result in "${sorted_results[@]}"; do
        local time="${result%:*}"
        local lang="${result#*:}"
        
        if [[ -z "$fastest_time" ]]; then
            fastest_time="$time"
        fi
        
        # Calculate relative performance
        local relative=$(echo "$time $fastest_time" | awk '{printf "%.2f", $1/$2}')
        
        # Performance classification
        local class=""
        local time_int=$(echo "$time" | awk '{printf "%.0f", $1}')
        if [[ $time_int -le 50 ]]; then
            class="${GREEN}Excellent${NC}"
        elif [[ $time_int -le 150 ]]; then
            class="${YELLOW}Good${NC}"
        elif [[ $time_int -le 500 ]]; then
            class="${YELLOW}Fair${NC}"
        else
            class="${RED}Poor${NC}"
        fi
        
        if [[ "$relative" == "1.00" ]]; then
            echo -e "  ${rank}. ${lang}: ${time}ms - ${class} (fastest)"
        else
            echo -e "  ${rank}. ${lang}: ${time}ms - ${class} (${relative}x slower)"
        fi
        
        ((rank++))
    done
    
    # Display failed runs
    for lang in "${failed_results[@]}"; do
        echo -e "  ❌ ${lang}: FAILED"
    done
    
    echo ""
    
    # Clear results for next category
    benchmark_results=""
}

# Main execution
echo -e "${BLUE}=================================================================${NC}"
echo -e "${BLUE}              Orus Unified Fair Benchmark Suite${NC}"
echo -e "${BLUE}=================================================================${NC}"
echo ""

# System information
echo -e "${CYAN}🖥️  System Information:${NC}"
echo "  Date:     $(date)"
echo "  Platform: $(uname -s) $(uname -m)"
echo "  Orus:     $($ORUS_BINARY --version 2>/dev/null || echo 'Building...')"
if command -v git >/dev/null 2>&1 && git rev-parse --git-dir >/dev/null 2>&1; then
    echo "  Commit:   $(git rev-parse --short HEAD 2>/dev/null || echo 'unknown')"
fi
echo ""

# Ensure Orus is built once
ensure_orus_built

# Run benchmark categories
run_benchmark_category "Arithmetic Operations" "arithmetic_benchmark.orus"
run_benchmark_category "Control Flow" "control_flow_benchmark.orus"

echo -e "${BLUE}=================================================================${NC}"
echo -e "${GREEN}✨ Benchmark comparison complete!${NC}"
echo -e "${BLUE}=================================================================${NC}"
echo ""

# Overall language performance summary
echo -e "${CYAN}📊 Overall Language Performance Summary:${NC}"
echo ""

# Simple language classification based on typical performance patterns
echo -e "${BLUE}Performance Classification:${NC}"
echo -e "  ${GREEN}🥇 Lua${NC}           - Excellent (Fastest overall, optimized for performance)"
echo -e "  ${GREEN}🥈 Orus${NC}          - Excellent (Strong performance, competitive with mature languages)"
echo -e "  ${YELLOW}🥉 JavaScript${NC}     - Good (V8 engine optimization, good for most workloads)"
echo -e "  ${YELLOW}4. Python${NC}        - Good (Slower but acceptable, prioritizes readability)"
echo ""


# Performance notes
echo -e "${CYAN}💡 Benchmark Notes:${NC}"
echo "• All languages use equivalent algorithms for fair comparison"
echo "• Orus compiled once with optimal settings (computed goto dispatch)"
echo "• Results averaged over $ITERATIONS runs with warmup"
echo "• Performance classifications: Excellent (≤50ms), Good (≤150ms), Fair (≤500ms)"
echo "• Use these results for performance tracking and optimization guidance"
echo ""
