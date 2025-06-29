#!/bin/bash

# Orus Language Benchmark Runner
# Comprehensive benchmarking script for the Orus interpreter

set -e  # Exit on any error

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
ORUS_BINARY="$PROJECT_ROOT/orus"
RESULTS_DIR="$SCRIPT_DIR/results"
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
ITERATIONS=${ITERATIONS:-20}

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Helper functions
log() {
    echo -e "${BLUE}[$(date +'%H:%M:%S')]${NC} $1"
}

success() {
    echo -e "${GREEN}✓${NC} $1"
}

warning() {
    echo -e "${YELLOW}⚠${NC} $1"
}

error() {
    echo -e "${RED}✗${NC} $1" >&2
}

# Check dependencies
check_dependencies() {
    log "Checking dependencies..."
    
    # Check if Orus binary exists
    if [[ ! -f "$ORUS_BINARY" ]]; then
        error "Orus binary not found at $ORUS_BINARY"
        echo "Please run 'make' in the project root to build the interpreter"
        exit 1
    fi
    
    # Check if Python3 is available
    if ! command -v python3 &> /dev/null; then
        error "Python3 is required for benchmarking"
        exit 1
    fi
    
    # Check if required Python modules are available
    if ! python3 -c "import time, statistics, json, subprocess" &> /dev/null; then
        error "Required Python modules not available"
        exit 1
    fi
    
    success "All dependencies satisfied"
}

# Build project if needed
build_project() {
    log "Ensuring Orus is built..."
    cd "$PROJECT_ROOT"
    
    if [[ ! -f "$ORUS_BINARY" ]] || [[ src/ -nt "$ORUS_BINARY" ]]; then
        log "Building Orus interpreter..."
        make clean && make
        success "Build complete"
    else
        success "Orus binary is up to date"
    fi
    
    cd "$SCRIPT_DIR"
}

# Create results directory
setup_results_dir() {
    mkdir -p "$RESULTS_DIR"
    log "Results will be saved to: $RESULTS_DIR"
}

# Run a single benchmark
run_benchmark() {
    local test_file="$1"
    local test_name="$2"
    local output_file="$3"
    
    log "Running benchmark: $test_name"
    
    if [[ ! -f "$test_file" ]]; then
        error "Test file not found: $test_file"
        return 1
    fi
    
    # Run the benchmark using Python script
    python3 - <<EOF > "$output_file"
import subprocess
import time
import statistics
import json
import sys

def benchmark_orus(file_path, iterations):
    """Benchmark Orus interpreter"""
    times = []
    cmd = ['$ORUS_BINARY', file_path]
    
    for i in range(iterations):
        try:
            start = time.perf_counter()
            result = subprocess.run(cmd, 
                                  stdout=subprocess.PIPE, 
                                  stderr=subprocess.PIPE, 
                                  timeout=30,
                                  check=True)
            end = time.perf_counter()
            times.append(end - start)
        except subprocess.TimeoutExpired:
            print(f"Timeout on iteration {i+1}", file=sys.stderr)
            continue
        except subprocess.CalledProcessError as e:
            print(f"Error on iteration {i+1}: {e}", file=sys.stderr)
            continue
    
    if not times:
        return None
        
    return {
        'avg': statistics.mean(times),
        'min': min(times),
        'max': max(times),
        'median': statistics.median(times),
        'stddev': statistics.stdev(times) if len(times) > 1 else 0,
        'successful_runs': len(times),
        'total_runs': iterations
    }

# Run the benchmark
result = benchmark_orus('$test_file', $ITERATIONS)

if result:
    output = {
        'test_name': '$test_name',
        'test_file': '$test_file',
        'timestamp': '$(date -Iseconds)',
        'iterations': $ITERATIONS,
        'system_info': {
            'os': '$(uname -s)',
            'arch': '$(uname -m)',
            'hostname': '$(hostname)'
        },
        'results': result
    }
    print(json.dumps(output, indent=2))
else:
    print('{"error": "All benchmark runs failed"}')
EOF

    if [[ $? -eq 0 ]]; then
        success "Completed: $test_name"
    else
        error "Failed: $test_name"
        return 1
    fi
}

# Run comparison benchmark (Orus vs Python)
run_comparison_benchmark() {
    log "Running Orus vs Python comparison benchmark..."
    
    local output_file="$RESULTS_DIR/comparison_${TIMESTAMP}.json"
    
    if [[ -f "benchmark_complex.py" ]]; then
        python3 benchmark_complex.py > "$output_file"
        success "Comparison benchmark completed"
        
        # Extract and display results
        python3 - <<EOF
import json

with open('$output_file', 'r') as f:
    data = json.load(f)

orus_avg = data['orus']['avg']
python_avg = data['python']['avg']
speedup = python_avg / orus_avg

print(f"\\n${GREEN}Performance Comparison Results:${NC}")
print(f"  Orus average:    {orus_avg:.6f}s")
print(f"  Python average:  {python_avg:.6f}s")
print(f"  Speedup:         {speedup:.2f}x faster")
EOF
    else
        warning "benchmark_complex.py not found, skipping comparison"
    fi
}

# Run all available benchmarks
run_all_benchmarks() {
    log "Starting comprehensive benchmark suite..."
    
    # Array of test files and their descriptions
    local tests=(
        "complex_expression.orus:Complex Arithmetic"
        "test_150_ops_fixed.orus:150 Operations Chain"
        "test_500_ops_fixed.orus:500 Operations Chain"
    )
    
    # Run each test
    for test_entry in "${tests[@]}"; do
        IFS=':' read -r test_file test_name <<< "$test_entry"
        
        if [[ -f "$test_file" ]]; then
            local output_file="$RESULTS_DIR/${test_name// /_}_${TIMESTAMP}.json"
            run_benchmark "$test_file" "$test_name" "$output_file"
        else
            warning "Test file not found: $test_file"
        fi
    done
    
    # Run comparison benchmark
    run_comparison_benchmark
}

# Generate summary report
generate_summary() {
    log "Generating benchmark summary..."
    
    local summary_file="$RESULTS_DIR/summary_${TIMESTAMP}.md"
    
    cat > "$summary_file" <<EOF
# Orus Language Benchmark Report

**Generated:** $(date)
**System:** $(uname -s) $(uname -m)
**Hostname:** $(hostname)
**Iterations per test:** $ITERATIONS

## Results Summary

EOF

    # Process each result file
    for result_file in "$RESULTS_DIR"/*_${TIMESTAMP}.json; do
        if [[ -f "$result_file" ]]; then
            python3 - <<EOF >> "$summary_file"
import json
import os

try:
    with open('$result_file', 'r') as f:
        data = json.load(f)
    
    if 'results' in data:
        r = data['results']
        print(f"### {data['test_name']}")
        print(f"- **Average time:** {r['avg']:.6f}s")
        print(f"- **Min time:** {r['min']:.6f}s")
        print(f"- **Max time:** {r['max']:.6f}s")
        print(f"- **Median time:** {r['median']:.6f}s")
        print(f"- **Std deviation:** {r['stddev']:.6f}s")
        print(f"- **Success rate:** {r['successful_runs']}/{r['total_runs']} ({r['successful_runs']/r['total_runs']*100:.1f}%)")
        print()
    elif 'orus' in data and 'python' in data:
        orus_avg = data['orus']['avg']
        python_avg = data['python']['avg']
        speedup = python_avg / orus_avg
        print(f"### Orus vs Python Comparison")
        print(f"- **Orus average:** {orus_avg:.6f}s")
        print(f"- **Python average:** {python_avg:.6f}s")
        print(f"- **Speedup:** {speedup:.2f}x faster")
        print()
except Exception as e:
    print(f"Error processing {os.path.basename('$result_file')}: {e}")
EOF
        fi
    done
    
    success "Summary report generated: $summary_file"
}

# Show usage information
show_usage() {
    cat <<EOF
Orus Language Benchmark Runner

Usage: $0 [OPTIONS] [COMMAND]

Commands:
    all         Run all benchmarks (default)
    comparison  Run Orus vs Python comparison only
    single FILE Run benchmark on single file
    list        List available test files

Options:
    -i, --iterations N    Number of iterations per test (default: 20)
    -o, --output DIR      Output directory for results (default: ./results)
    -v, --verbose         Verbose output
    -h, --help           Show this help

Environment Variables:
    ITERATIONS           Number of benchmark iterations (default: 20)

Examples:
    $0                                    # Run all benchmarks
    $0 -i 50 all                         # Run all with 50 iterations
    $0 single complex_expression.orus    # Run single test
    $0 comparison                        # Compare with Python only

EOF
}

# List available test files
list_tests() {
    log "Available test files:"
    for file in *.orus; do
        if [[ -f "$file" ]]; then
            echo "  - $file"
        fi
    done
}

# Main script logic
main() {
    local command="all"
    
    # Parse command line arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            -i|--iterations)
                ITERATIONS="$2"
                shift 2
                ;;
            -o|--output)
                RESULTS_DIR="$2"
                shift 2
                ;;
            -v|--verbose)
                set -x
                shift
                ;;
            -h|--help)
                show_usage
                exit 0
                ;;
            all|comparison|list)
                command="$1"
                shift
                ;;
            single)
                command="single"
                test_file="$2"
                shift 2
                ;;
            *)
                error "Unknown option: $1"
                show_usage
                exit 1
                ;;
        esac
    done
    
    # Execute based on command
    case $command in
        all)
            check_dependencies
            build_project
            setup_results_dir
            run_all_benchmarks
            generate_summary
            success "All benchmarks completed! Results in: $RESULTS_DIR"
            ;;
        comparison)
            check_dependencies
            build_project
            setup_results_dir
            run_comparison_benchmark
            ;;
        single)
            if [[ -z "$test_file" ]]; then
                error "Please specify a test file"
                exit 1
            fi
            check_dependencies
            build_project
            setup_results_dir
            run_benchmark "$test_file" "$(basename "$test_file" .orus)" "$RESULTS_DIR/single_${TIMESTAMP}.json"
            ;;
        list)
            list_tests
            ;;
    esac
}

# Change to benchmark directory
cd "$SCRIPT_DIR"

# Run main function
main "$@"
