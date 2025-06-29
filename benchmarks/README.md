# Orus Language Benchmarks

This directory contains comprehensive performance benchmarks comparing Orus VM against Python and JavaScript.

## Files

### Benchmark Scripts
- `simple_benchmark.py` - Python vs Orus comparison
- `orus_vs_js_benchmark.py` - JavaScript vs Orus comparison  
- `orus_vs_lua_benchmark.py` - Lua vs Orus comparison
- `quick_bench.sh` - Quick launcher for all benchmark types

### Test Files
- `complex_expression.orus` - Complex expression test
- `test_150_ops_fixed.orus` - 150 operation chain test
- `test_500_ops_fixed.orus` - 500 operation chain test

### Result Files (Auto-generated, Git-ignored)
- `benchmark_results_python.json` - Python benchmark results
- `benchmark_results_js.json` - JavaScript benchmark results
- `benchmark_results_lua.json` - Lua benchmark results

## Usage

### Quick Start
```bash
./quick_bench.sh
```

Select from:
1. Quick test (5 iterations)
2. Standard test (20 iterations)  
3. Thorough test (50 iterations)
4. Orus vs Python comparison
5. Orus vs JavaScript comparison
6. Orus vs Lua comparison
7. Stress test
8. All benchmarks (comprehensive)
9. Compare all languages (Python + JavaScript + Lua)

### Individual Benchmarks

**Python Comparison:**
```bash
python3 simple_benchmark.py --iterations 30
```

**JavaScript Comparison:**
```bash
python3 orus_vs_js_benchmark.py --iterations 30
```

**Lua Comparison:**
```bash
python3 orus_vs_lua_benchmark.py --iterations 30
```
**Custom Output File:**
```bash
python3 simple_benchmark.py --output my_results.json
```

## Results Summary

### Performance Rankings
1. 🥇 **Orus VM** - Fastest (register-based architecture)
2. 🥈 **Lua** - ~1.3x slower (register-based, but interpreted)
3. 🥉 **Python** - ~7.5x slower (stack-based interpreter)
4. � **JavaScript** - ~12.4x slower (V8 JIT overhead for short scripts)

### Key Insights
- **Register-based VM** provides consistent performance advantages
- **Computed goto dispatch** eliminates switch statement overhead
- **Fast arithmetic operations** show significant gains over interpreted languages
- **V8 startup overhead** affects short-running JavaScript scripts
- **Orus binary execution** is extremely efficient with ~2ms average execution time

## Architecture Benefits

Orus VM optimizations:
- **Register-based execution** (vs stack-based in Python/JS)
- **Computed goto dispatch** (faster than switch statements)
- **Fast arithmetic** (no overflow checks in optimized builds)
- **Memory pooling** (reduced allocation overhead)
- **Optimized bytecode** (efficient instruction encoding)

## Requirements

- Built Orus binary (`../orus`)
- Python 3.6+
- Node.js (for JavaScript comparisons)
- Lua 5.4+ (for Lua comparisons)

## Git Integration

Result files are automatically ignored by git to prevent repository bloat:
- `benchmark_results_*.json` pattern is excluded
- Only source code and test files are tracked
- Results are generated fresh on each run

## Example Performance

```
🏆 ORUS VM PERFORMANCE COMPARISON SUMMARY
============================================================
🐍 Orus vs Python:    7.5x faster
🟨 Orus vs JavaScript: 12.4x faster
🌙 Orus vs Lua:       1.3x faster

⚡ EXECUTION TIME COMPARISON (Typical):
----------------------------------------
Orus:       ~2.2ms   (Baseline - fastest)
Lua:        ~2.9ms   (1.3x slower)
Python:     ~17.4ms  (7.9x slower)
JavaScript: ~27.8ms  (12.6x slower)

🏅 PERFORMANCE RANKING:
----------------------------------------
1. 🥇 Orus VM      - Fastest, optimized register-based execution
2. 🥈 Lua          - Fast scripting language with register-based VM
3. 🥉 Python       - Interpreted, stack-based VM
4. � JavaScript   - V8 JIT overhead for short-running scripts
```