# Orus Language Benchmarks

This directory contains a simple benchmarking setup to compare Orus vs Python performance.

## Files

- **`simple_benchmark.py`** - Python script for comprehensive Orus vs Python benchmarking
- **`quick_bench.sh`** - Shell script for easy benchmark execution
- **`*.orus`** - Orus test files for benchmarking

## Quick Start

### Option 1: Use the shell script (recommended)
```bash
cd benchmarks
./quick_bench.sh
```

### Option 2: Run Python benchmark directly
```bash
cd benchmarks
./simple_benchmark.py --quick
```

## Test Files

- `complex_expression.orus` - Complex arithmetic expressions
- `test_150_ops_fixed.orus` - Chain of 150 arithmetic operations
- `test_500_ops_fixed.orus` - Chain of 500 arithmetic operations

## Requirements

- Built Orus binary (`../orus`)
- Python 3.6+

## Example Results

Typical performance shows Orus is ~7x faster than Python for arithmetic operations.
