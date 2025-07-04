# Orus Performance Testing & Benchmark Suite

This directory contains **comprehensive performance testing tools** for Orus, including cross-language benchmarks and automated regression testing.

## 🎯 Quick Start

### Performance Dashboard
```bash
./performance_dashboard.sh        # Quick status overview
```

### Automated Testing
```bash
./performance_regression_test.sh  # Regression testing with baselines
```

### High-Precision Benchmarking
```bash
./precise_benchmark.sh           # Detailed timing analysis
```

### Cross-Language Comparison
```bash
./run_all_benchmarks_fixed.sh   # Compare with Python, Node.js, Lua, Julia
```

## 📁 File Structure

### 🔧 Testing Tools
- **`performance_dashboard.sh`** - Performance status overview with trends
- **`performance_regression_test.sh`** - Automated regression testing system
- **`precise_benchmark.sh`** - High-precision timing with statistical analysis  
- **`run_all_benchmarks_fixed.sh`** - Cross-language benchmark comparison

### 📊 Benchmark Programs
- **`arithmetic_benchmark.orus`** - Arithmetic-heavy operations (1M+ iterations)
- **`control_flow_benchmark.orus`** - Control flow and conditional logic
- **`arithmetic_benchmark.{py,js,lua,jl}`** - Equivalent implementations for comparison
- **`control_flow_benchmark.{py,js,lua,jl}`** - Control flow implementations

### 📈 Performance Data
- **`performance_baselines.txt`** - Performance targets and thresholds
- **`performance_results.log`** - Historical test results with git commits

## 🚀 Current Performance Status

### Latest Results (Jul 2025)
| Benchmark | **Orus** | Target | Status |
|-----------|----------|--------|--------|
| **Arithmetic** | **28ms** ⚡ | 30ms | ✅ **Excellent** |
| **Control Flow** | **52ms** ⚡ | 55ms | ✅ **Excellent** |

### Performance vs Competition
| Language | Arithmetic | Control Flow |
|----------|------------|--------------|
| **Orus** | **28ms** | **52ms** |
| Python | 63ms | 86ms |
| Node.js | 38ms | 38ms |
| Lua | 17ms | 20ms |

*All measurements: M1 MacBook Pro, median of 5 runs*

## 🔍 Benchmark Details

### Arithmetic Benchmark Operations
1. **Addition Loop** - 1 million integer additions
2. **Mixed Arithmetic** - 100K floating point operations
3. **Integer Arithmetic** - Factorial calculations (1-19)
4. **Division/Modulo** - 10K division and modulo operations
5. **Floating Point** - 50K precision-sensitive operations

### Control Flow Benchmark Operations
1. **Conditional Logic** - Complex if/else chains
2. **Loop Constructs** - Various loop types and nesting
3. **Function Calls** - Call overhead testing
4. **Variable Scoping** - Scope resolution performance
5. **Pattern Matching** - Conditional branching

## 📊 Performance Testing Methodology

### Timing Accuracy
- **High-precision timing** using nanosecond resolution
- **Statistical analysis** with median of multiple runs
- **System isolation** to avoid measurement contamination
- **Multiple methodologies** for validation

### Regression Detection
- **Automated baselines** against documented targets
- **Warning thresholds** at 15% degradation
- **Failure thresholds** at 50% degradation
- **Historical tracking** with git commit correlation

### Cross-Language Fairness
- **Identical algorithms** across all languages
- **Same computational workload** for each test
- **Equivalent data structures** and operations
- **Fair timing methodology** for all languages

## 🔧 Advanced Usage

### Regression Testing Integration
```bash
# Run in CI/CD pipeline
./performance_regression_test.sh
exit_code=$?
if [ $exit_code -eq 2 ]; then
    echo "Critical performance regression detected!"
    exit 1
fi
```

### Custom Baseline Updates
```bash
# Update performance targets
echo "arithmetic,0.025,$(date)" > performance_baselines.txt
echo "control_flow,0.048,$(date)" >> performance_baselines.txt
```

### Historical Analysis
```bash
# View performance trends
tail -20 performance_results.log

# Compare specific commits
grep "abc1234" performance_results.log
grep "def5678" performance_results.log
```

## 📈 Performance Features Tested

### Compiler Optimizations
- ✅ **LICM** - Loop Invariant Code Motion
- ✅ **Register Allocation** - Efficient register usage
- ✅ **Constant Folding** - Compile-time evaluation
- ✅ **Type Specialization** - Type-specific opcodes

### Runtime Performance
- ✅ **Register-based VM** - Fewer memory accesses
- ✅ **Computed Goto** - Fast instruction dispatch
- ✅ **Memory Management** - Efficient GC with object pooling
- ✅ **Zero JIT Warmup** - Consistent performance from start

### Language Features
- ✅ **Type Inference** - Minimal type annotation overhead
- ✅ **Loop Constructs** - For/while loop optimization
- ✅ **Function Calls** - Efficient calling conventions
- ✅ **Variable Scoping** - Fast scope resolution

## 🛠️ Troubleshooting

### Common Issues

#### Inconsistent Results
**Solution**: Ensure system is not under heavy load during testing
```bash
# Check system load
top -l 1 | grep "CPU usage"
```

#### Missing Language Interpreters
**Solution**: Install required interpreters
```bash
# macOS
brew install python node lua julia

# Ubuntu
sudo apt-get install python3 nodejs lua5.3 julia
```

#### Performance Regression
**Solution**: Use regression testing tools
```bash
./performance_regression_test.sh  # Detailed analysis
./performance_dashboard.sh        # Quick overview
```

## 📚 Related Documentation

- **[Performance Testing Guide](../../docs/PERFORMANCE_TESTING_GUIDE.md)** - Comprehensive testing methodology
- **[Loop Safety Guide](../../docs/LOOP_SAFETY_GUIDE.md)** - Loop optimization and safety
- **[Architecture Guide](../../docs/VM_OPTIMIZATION.md)** - VM performance details

## 🎯 Performance Targets

### Current Baselines (Jul 2025)
- **Arithmetic**: ≤ 30ms (currently ~28ms) ✅
- **Control Flow**: ≤ 55ms (currently ~52ms) ✅

### Quality Standards
- **Measurement Precision**: ±1ms accuracy
- **Test Reliability**: <5% variance across runs
- **Regression Detection**: 15% warning, 50% failure
- **Documentation**: Complete performance methodology

---

**Orus delivers excellent performance with comprehensive testing infrastructure to maintain and improve it continuously.**