# Orus Language Benchmark Suite

This directory contains comprehensive benchmarking tools for the Orus programming language interpreter.

## Quick Start

```bash
# Easy interactive mode
./quick_bench.sh

# Or run specific benchmarks directly
./run_benchmarks.sh all                    # Run all benchmarks
./advanced_benchmark.py --comparison       # Compare with Python
```

## Available Scripts

### 1. `quick_bench.sh` - Interactive Benchmark Runner
The easiest way to run benchmarks with a simple menu interface.

```bash
./quick_bench.sh
```

### 2. `run_benchmarks.sh` - Comprehensive Shell Script
Full-featured benchmark runner with extensive options.

```bash
# Basic usage
./run_benchmarks.sh all              # Run all benchmarks
./run_benchmarks.sh comparison       # Orus vs Python only
./run_benchmarks.sh single test.orus # Single test file

# Advanced options
./run_benchmarks.sh -i 50 all       # 50 iterations per test
./run_benchmarks.sh -o ./my_results  # Custom output directory
./run_benchmarks.sh --help          # Show all options
```

### 3. `advanced_benchmark.py` - Python Benchmark Suite
Advanced benchmarking with statistical analysis and stress testing.

```bash
# Basic benchmarks
./advanced_benchmark.py --all                    # All tests
./advanced_benchmark.py --iterations 50          # Custom iterations
./advanced_benchmark.py --test-files test1.orus test2.orus # Specific files

# Special benchmarks
./advanced_benchmark.py --comparison             # Orus vs Python
./advanced_benchmark.py --stress-test            # Performance under load
./advanced_benchmark.py --output-dir ./results   # Custom output location
```

### 4. `benchmark_complex.py` - Legacy Comparison Tool
Simple Orus vs Python comparison (legacy).

```bash
python3 benchmark_complex.py
```

## Test Files

- `complex_expression.orus` - Complex arithmetic expression
- `test_150_ops_fixed.orus` - Chain of 150 arithmetic operations
- `test_500_ops_fixed.orus` - Chain of 500 arithmetic operations

## Output Files

Results are saved in the `results/` directory with timestamps:

- `benchmark_results_YYYYMMDD_HHMMSS.json` - Detailed benchmark data
- `benchmark_report_YYYYMMDD_HHMMSS.md` - Human-readable report
- `comparison_YYYYMMDD_HHMMSS.json` - Orus vs Python comparison
- `stress_test_YYYYMMDD_HHMMSS.json` - Stress test results
- `summary_YYYYMMDD_HHMMSS.md` - Overall summary report

## Environment Variables

- `ITERATIONS` - Number of benchmark iterations (default: 20)

## Examples

### Run Quick Performance Check
```bash
./quick_bench.sh
# Select option 1 for a fast 5-iteration test
```

### Comprehensive Performance Analysis
```bash
./run_benchmarks.sh -i 100 all
```

### Compare Orus vs Python Performance
```bash
./advanced_benchmark.py --comparison --iterations 50
```

### Stress Test Performance
```bash
./advanced_benchmark.py --stress-test
```

### Custom Test with Specific Files
```bash
./advanced_benchmark.py --test-files complex_expression.orus test_150_ops_fixed.orus --iterations 30
```

## Understanding Results

### Key Metrics
- **Average time**: Mean execution time across all iterations
- **Min/Max time**: Fastest and slowest execution times
- **Median time**: Middle value when times are sorted
- **Standard deviation**: Measure of timing consistency
- **Operations/second**: Estimated throughput (for arithmetic tests)
- **Speedup**: Performance multiplier compared to Python

### Interpreting Performance
- **Sub-millisecond times**: Excellent performance
- **1-10ms times**: Good performance for complex operations
- **>10ms times**: May indicate performance issues
- **High std deviation**: Inconsistent performance
- **Low success rate**: Stability issues

### Sample Result
```json
{
  "test_name": "Complex Arithmetic",
  "avg_time": 0.000708,
  "min_time": 0.000582,
  "max_time": 0.001001,
  "operations_per_second": 14124,
  "speedup": 18.32
}
```

This shows Orus executing in ~0.7ms on average, with 18.3x speedup over Python.

## Troubleshooting

### Build Issues
```bash
cd ..
make clean && make
cd benchmarks
```

### Permission Issues
```bash
chmod +x *.sh *.py
```

### Missing Dependencies
- Requires Python 3.6+
- Requires compiled Orus binary (`../orus`)
- Requires standard Unix tools (bash, time, etc.)

## Adding New Tests

1. Create a `.orus` file with your test case
2. Add it to the benchmarks directory
3. Run with: `./advanced_benchmark.py --test-files your_test.orus`

## Performance Tips

- Run benchmarks when system is idle
- Use higher iteration counts for more accurate results
- Compare results across different system configurations
- Monitor for thermal throttling on laptops during stress tests

## Benchmark Data Analysis

The JSON output files can be processed with additional tools:

```python
import json
import matplotlib.pyplot as plt

# Load benchmark results
with open('results/benchmark_results_*.json') as f:
    data = json.load(f)

# Plot performance trends
for result in data['results']:
    print(f"{result['test_name']}: {result['avg_time']:.6f}s")
```
