#!/bin/bash
# Comprehensive benchmark runner for Orus language
# Runs all benchmarks: Orus native, cross-language comparisons, and VM optimizations

set -e

# Use bash 4+ features if available, otherwise fallback
if [[ ${BASH_VERSION%%.*} -ge 4 ]]; then
    HAS_ASSOC_ARRAYS=true
else
    HAS_ASSOC_ARRAYS=false
fi

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Check if we're in the right directory
if [[ ! -f "../../orus" ]]; then
    echo -e "${RED}Error: Orus interpreter not found. Run 'make' from the project root first.${NC}"
    exit 1
fi

ORUS_BINARY="../../orus"

# Arrays to store benchmark results (using temp files for compatibility)
TEMP_DIR=$(mktemp -d)
BENCHMARK_NAMES=("arithmetic" "control_flow" "scope_management" "scoop_management")  # Exclude vm_optimization from cross-language comparison
ORUS_ONLY_BENCHMARKS=("vm_optimization")       # Orus-specific benchmarks
LANGUAGES_TESTED=()

# Helper functions for storing/retrieving results
store_benchmark_time() {
    local lang="$1"
    local benchmark="$2" 
    local time="$3"
    echo "$time" > "$TEMP_DIR/${lang}_${benchmark}_time"
}

get_benchmark_time() {
    local lang="$1"
    local benchmark="$2"
    local file="$TEMP_DIR/${lang}_${benchmark}_time"
    if [[ -f "$file" ]]; then
        cat "$file"
    else
        echo ""
    fi
}

store_benchmark_status() {
    local lang="$1"
    local benchmark="$2"
    local status="$3"
    echo "$status" > "$TEMP_DIR/${lang}_${benchmark}_status"
}

get_benchmark_status() {
    local lang="$1"
    local benchmark="$2"
    local file="$TEMP_DIR/${lang}_${benchmark}_status"
    if [[ -f "$file" ]]; then
        cat "$file"
    else
        echo ""
    fi
}

echo -e "${CYAN}=================================================================${NC}"
echo -e "${CYAN}                Orus Language Benchmark Suite${NC}"
echo -e "${CYAN}=================================================================${NC}"
echo ""

# Function to check if a command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Function to run a benchmark with timing and result collection
run_benchmark() {
    local name="$1"
    local command="$2"
    local language="$3"
    local benchmark_type="$4"
    
    echo -e "${YELLOW}Running: $name${NC}"
    echo "Command: $command"
    echo "----------------------------------------"
    
    # Capture timing and output
    local start_time=$(date +%s%N 2>/dev/null || date +%s)
    local output_file=$(mktemp)
    local success=true
    
    # Try to run command with better error handling
    set +e  # Temporarily disable error exit
    eval "$command" > "$output_file" 2>&1
    local exit_code=$?
    set -e  # Re-enable error exit
    
    if [ $exit_code -eq 0 ]; then
        local end_time=$(date +%s%N 2>/dev/null || date +%s)
        
        # Calculate duration (handle both nanosecond and second precision)
        if [[ "$start_time" == *N* ]] || [[ "$end_time" == *N* ]]; then
            # Fallback to seconds if nanosecond precision not available
            local duration=$(( (end_time - start_time) * 1000 ))
        else
            local duration=$(( (end_time - start_time) / 1000000 )) # Convert to milliseconds
        fi
        
        store_benchmark_time "$language" "$benchmark_type" "$duration"
        store_benchmark_status "$language" "$benchmark_type" "SUCCESS"
        
        echo -e "${GREEN}✓ $name completed successfully in ${duration}ms${NC}"
        
        # Show last few lines of output for context
        echo "Output:"
        tail -n 5 "$output_file"
    else
        local end_time=$(date +%s%N 2>/dev/null || date +%s)
        
        if [[ "$start_time" == *N* ]] || [[ "$end_time" == *N* ]]; then
            local duration=$(( (end_time - start_time) * 1000 ))
        else
            local duration=$(( (end_time - start_time) / 1000000 ))
        fi
        
        store_benchmark_time "$language" "$benchmark_type" "$duration"
        store_benchmark_status "$language" "$benchmark_type" "FAILED"
        
        echo -e "${RED}✗ $name failed after ${duration}ms (exit code: $exit_code)${NC}"
        echo "Error output:"
        tail -n 10 "$output_file"
        
        # Check if this was a segfault
        if [ $exit_code -eq 139 ] || [ $exit_code -eq 11 ]; then
            echo -e "${RED}Detected segmentation fault (exit code $exit_code)${NC}"
        fi
        success=false
    fi
    
    rm -f "$output_file"
    echo ""
    return $([ "$success" = true ] && echo 0 || echo 1)
}

# Start benchmarking
echo -e "${BLUE}=== Orus Native Benchmarks ===${NC}"

# Add Orus to tested languages
LANGUAGES_TESTED+=("orus")

# 1. Arithmetic Benchmark
run_benchmark "Orus Arithmetic Benchmark" "$ORUS_BINARY arithmetic_benchmark.orus" "orus" "arithmetic"

# 2. Control Flow Benchmark
run_benchmark "Orus Control Flow Benchmark" "$ORUS_BINARY control_flow_benchmark.orus" "orus" "control_flow"

# 3. Scope Management Benchmark
run_benchmark "Orus Scope Management Benchmark" "$ORUS_BINARY scope_management_benchmark.orus" "orus" "scope_management"

# 4. VM Optimization Benchmark
run_benchmark "Orus VM Optimization Benchmark" "$ORUS_BINARY vm_optimization_benchmark.orus" "orus" "vm_optimization"

echo -e "${BLUE}=== Cross-Language Benchmark Comparisons ===${NC}"

# Python benchmarks (if available)
if command_exists python3; then
    echo -e "${CYAN}Python 3 available - running comparison benchmarks${NC}"
    LANGUAGES_TESTED+=("python")
    run_benchmark "Python Arithmetic Benchmark" "python3 arithmetic_benchmark.py" "python" "arithmetic"
    run_benchmark "Python Control Flow Benchmark" "python3 control_flow_benchmark.py" "python" "control_flow"
    run_benchmark "Python Scope Management Benchmark" "python3 scope_management_benchmark.py" "python" "scope_management"
else
    echo -e "${YELLOW}Python 3 not available - skipping Python benchmarks${NC}"
fi

# Node.js benchmarks (if available)
if command_exists node; then
    echo -e "${CYAN}Node.js available - running comparison benchmarks${NC}"
    LANGUAGES_TESTED+=("javascript")
    run_benchmark "JavaScript Arithmetic Benchmark" "node arithmetic_benchmark.js" "javascript" "arithmetic"
    run_benchmark "JavaScript Control Flow Benchmark" "node control_flow_benchmark.js" "javascript" "control_flow"
    run_benchmark "JavaScript Scope Management Benchmark" "node scope_management_benchmark.js" "javascript" "scope_management"
else
    echo -e "${YELLOW}Node.js not available - skipping JavaScript benchmarks${NC}"
fi

# Lua benchmarks (if available)
if command_exists lua; then
    echo -e "${CYAN}Lua available - running comparison benchmarks${NC}"
    LANGUAGES_TESTED+=("lua")
    run_benchmark "Lua Arithmetic Benchmark" "lua arithmetic_benchmark.lua" "lua" "arithmetic"
    run_benchmark "Lua Control Flow Benchmark" "lua control_flow_benchmark.lua" "lua" "control_flow"
    run_benchmark "Lua Scope Management Benchmark" "lua scope_management_benchmark.lua" "lua" "scope_management"
    run_benchmark "Lua Scoop Management Benchmark" "lua scoop_management_benchmark.lua" "lua" "scoop_management"
else
    echo -e "${YELLOW}Lua not available - skipping Lua benchmarks${NC}"
fi

echo -e "${CYAN}=================================================================${NC}"
echo -e "${GREEN}              Benchmark Suite Complete!${NC}"
echo -e "${CYAN}=================================================================${NC}"
echo ""

# Performance Analysis and Classification
echo -e "${BLUE}=== Performance Analysis & Language Classification ===${NC}"
echo ""

# Function to classify performance
classify_performance() {
    local time_ms=$1
    if [ "$time_ms" -le 100 ]; then
        echo "Excellent"
    elif [ "$time_ms" -le 500 ]; then
        echo "Very Good"
    elif [ "$time_ms" -le 1000 ]; then
        echo "Good"
    elif [ "$time_ms" -le 3000 ]; then
        echo "Fair"
    elif [ "$time_ms" -le 10000 ]; then
        echo "Poor"
    else
        echo "Very Poor"
    fi
}

# Function to get performance color
get_performance_color() {
    local classification="$1"
    case "$classification" in
        "Excellent") echo "$GREEN" ;;
        "Very Good") echo "$GREEN" ;;
        "Good") echo "$YELLOW" ;;
        "Fair") echo "$YELLOW" ;;
        "Poor") echo "$RED" ;;
        "Very Poor") echo "$RED" ;;
        *) echo "$NC" ;;
    esac
}

# Function to capitalize first letter (for compatibility)
capitalize() {
    local str="$1"
    echo "$(echo ${str:0:1} | tr '[:lower:]' '[:upper:]')${str:1}"
}

# Display results by benchmark type
for benchmark_type in "${BENCHMARK_NAMES[@]}"; do
    benchmark_title=$(capitalize "$benchmark_type")
    echo -e "${CYAN}=== ${benchmark_title} Benchmark Results ===${NC}"
    
    # Find fastest time for ranking
    fastest_time=999999
    fastest_lang=""
    
    for lang in "${LANGUAGES_TESTED[@]}"; do
        time_ms=$(get_benchmark_time "$lang" "$benchmark_type")
        if [[ -n "$time_ms" ]]; then
            if [ "$time_ms" -lt "$fastest_time" ]; then
                fastest_time=$time_ms
                fastest_lang=$lang
            fi
        fi
    done
    
    # Display results sorted by performance
    declare -a sorted_results
    for lang in "${LANGUAGES_TESTED[@]}"; do
        time_ms=$(get_benchmark_time "$lang" "$benchmark_type")
        status=$(get_benchmark_status "$lang" "$benchmark_type")
        
        if [[ -n "$time_ms" && "$status" == "SUCCESS" ]]; then
            classification=$(classify_performance $time_ms)
            color=$(get_performance_color "$classification")
            
            # Calculate relative performance
            if [ "$time_ms" -eq "$fastest_time" ]; then
                relative="(fastest)"
            else
                # Simple arithmetic for compatibility
                if command -v bc > /dev/null 2>&1; then
                    multiplier=$(echo "scale=2; $time_ms / $fastest_time" | bc -l 2>/dev/null)
                else
                    multiplier=$(awk "BEGIN {printf \"%.2f\", $time_ms / $fastest_time}")
                fi
                relative="(${multiplier}x slower)"
            fi
            
            sorted_results+=("$time_ms:$lang:$classification:$color:$relative")
        elif [[ "$status" == "FAILED" ]]; then
            sorted_results+=("999999:$lang:Failed:$RED:(failed)")
        fi
    done
    
    # Sort and display
    IFS=$'\n' sorted_results=($(sort -n <<< "${sorted_results[*]}"))
    
    rank=1
    for result in "${sorted_results[@]}"; do
        IFS=':' read -r time_ms lang classification color relative <<< "$result"
        lang_title=$(capitalize "$lang")
        if [ "$time_ms" != "999999" ]; then
            echo -e "  ${rank}. ${color}${lang_title}${NC}: ${time_ms}ms - ${color}${classification}${NC} ${relative}"
        else
            echo -e "  ${rank}. ${color}${lang_title}${NC}: ${color}${classification}${NC} ${relative}"
        fi
        ((rank++))
    done
    echo ""
done

# Display Orus-specific benchmarks separately
echo -e "${CYAN}=== Orus-Specific Performance Benchmarks ===${NC}"
for benchmark_type in "${ORUS_ONLY_BENCHMARKS[@]}"; do
    benchmark_title=$(capitalize "$benchmark_type")
    time_ms=$(get_benchmark_time "orus" "$benchmark_type")
    status=$(get_benchmark_status "orus" "$benchmark_type")
    
    if [[ -n "$time_ms" && "$status" == "SUCCESS" ]]; then
        classification=$(classify_performance $time_ms)
        color=$(get_performance_color "$classification")
        echo -e "  ${benchmark_title}: ${color}${time_ms}ms - ${classification}${NC}"
        echo -e "    → Tests Orus VM optimizations and internal performance"
    fi
done
echo ""

# Overall language classification
echo -e "${CYAN}=== Overall Language Performance Classification ===${NC}"

# Calculate averages for each language
declare -a overall_results
for lang in "${LANGUAGES_TESTED[@]}"; do
    total_time=0
    success_count=0
    
    for benchmark_type in "${BENCHMARK_NAMES[@]}"; do
        time_ms=$(get_benchmark_time "$lang" "$benchmark_type")
        status=$(get_benchmark_status "$lang" "$benchmark_type")
        
        if [[ -n "$time_ms" && "$status" == "SUCCESS" ]]; then
            total_time=$((total_time + time_ms))
            success_count=$((success_count + 1))
        fi
    done
    
    if [ "$success_count" -gt 0 ]; then
        avg_time=$((total_time / success_count))
        classification=$(classify_performance $avg_time)
        color=$(get_performance_color "$classification")
        overall_results+=("$avg_time:$lang:$classification:$color:$success_count")
    fi
done

# Sort languages by average performance
IFS=$'\n' overall_results=($(sort -n <<< "${overall_results[*]}"))

echo "Ranked by average performance across all benchmarks:"
echo ""

rank=1
for result in "${overall_results[@]}"; do
    IFS=':' read -r avg_time lang classification color success_count <<< "$result"
    total_benchmarks=${#BENCHMARK_NAMES[@]}
    lang_title=$(capitalize "$lang")
    
    echo -e "  ${rank}. ${color}${lang_title}${NC}: ${avg_time}ms average - ${color}${classification}${NC}"
    echo -e "      Completed: ${success_count}/${total_benchmarks} benchmarks"
    
    # Add performance notes
    case "$classification" in
        "Excellent"|"Very Good")
            echo -e "      ${GREEN}Performance: Production-ready for high-performance applications${NC}"
            ;;
        "Good"|"Fair")
            echo -e "      ${YELLOW}Performance: Suitable for most applications${NC}"
            ;;
        "Poor"|"Very Poor")
            echo -e "      ${RED}Performance: May need optimization for performance-critical applications${NC}"
            ;;
    esac
    echo ""
    ((rank++))
done

echo -e "${BLUE}Performance Notes:${NC}"
echo "• Cross-language rankings based on arithmetic + control flow benchmarks only"
echo "• VM optimization benchmark excluded from overall ranking (Orus-specific)"
echo "• Orus benchmarks test core VM performance and internal optimizations"
echo "• Performance classifications: Excellent (≤100ms), Very Good (≤500ms), Good (≤1s), Fair (≤3s), Poor (≤10s), Very Poor (>10s)"
echo ""

# Cleanup temporary files
rm -rf "$TEMP_DIR"
