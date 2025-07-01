# Cross-Language Arithmetic Benchmark Suite

This directory contains **universal arithmetic benchmarks** designed to compare Orus performance against other popular languages (Python, JavaScript, Lua).

## Philosophy: One Benchmark, Multiple Languages

Instead of scattered type-specific tests, we use **one comprehensive arithmetic benchmark** implemented equivalently in each language for direct performance comparison.

## Files

### Core Benchmarks
- **`arithmetic_benchmark.orus`** - Orus implementation
- **`arithmetic_benchmark.py`** - Python 3 implementation  
- **`arithmetic_benchmark.js`** - Node.js implementation
- **`arithmetic_benchmark.lua`** - Lua implementation

### Execution
- **`run_arithmetic_benchmark.sh`** - Automated cross-language runner

## What Gets Tested

Each benchmark performs **identical operations**:

1. **Addition Loop** - 1 million iterations of integer addition
2. **Mixed Arithmetic** - 100K iterations of floating point operations (add, multiply, divide, subtract)
3. **Integer Arithmetic** - Factorial calculation (1-19)
4. **Division/Modulo** - 10K iterations of division and modulo operations
5. **Floating Point Precision** - 50K iterations of precision-sensitive operations

## Running Benchmarks

### From Root Makefile (Recommended)

```bash
# Run all languages side-by-side
make benchmark

# Run Orus only (fast)
make benchmark-orus

# See all options
make help
```

### Direct Execution

```bash
# Run cross-language comparison
cd tests/benchmarks
./run_arithmetic_benchmark.sh

# Run individual language
./orus arithmetic_benchmark.orus
python3 arithmetic_benchmark.py
node arithmetic_benchmark.js
lua arithmetic_benchmark.lua
```

## Sample Output

```
========================================================
Universal Arithmetic Benchmark: Cross-Language Testing
========================================================

=== Orus ===
Running: ../../orus arithmetic_benchmark.orus
Results:
499999500000
6.9146e+217
109641728
34611628
2.53092e+23

Timing:
real    0m0.032s
user    0m0.028s
sys     0m0.002s

=== Python 3 ===
Running: python3 arithmetic_benchmark.py
Results:
499999500000
6.914599446192004e+217
121645100408832000
34611628
2.5309178438242257e+23

Timing:
Python execution time: 0.036069 seconds
real    0m0.055s
user    0m0.047s
sys     0m0.005s
========================================================
```

## Performance Analysis

The benchmarks enable direct comparison of:
- **Execution Time**: Total runtime across languages
- **Arithmetic Performance**: Raw computational speed
- **Memory Usage**: System resource consumption
- **Precision Handling**: Floating point accuracy

## Design Benefits

### Unified Testing
- ✅ **Same operations** across all languages
- ✅ **Comparable results** for validation
- ✅ **Equivalent workloads** for fair comparison
- ✅ **Automated execution** via Makefile

### Maintainability
- ✅ **One file per language** (not scattered)
- ✅ **Single test suite** to maintain
- ✅ **Easy to add new languages**
- ✅ **Consistent test structure**

### Integration
- ✅ **Makefile integration** for CI/CD
- ✅ **Professional output** with timing
- ✅ **Error checking** for missing interpreters
- ✅ **Documentation** and help system

This approach provides meaningful performance comparisons while maintaining simplicity and avoiding the complexity of managing dozens of scattered benchmark files.