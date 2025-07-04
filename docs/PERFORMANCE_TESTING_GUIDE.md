# Orus Performance Testing Guide

This guide provides comprehensive instructions for measuring, monitoring, and improving Orus performance.

## 📊 Overview

Orus performance testing ensures that the language maintains excellent execution speed and provides early detection of performance regressions.

### Current Performance Targets
- **Arithmetic Benchmark**: ≤ 30ms (current: ~28ms)
- **Control Flow Benchmark**: ≤ 55ms (current: ~52ms)
- **Memory Usage**: Efficient GC with minimal overhead
- **Startup Time**: < 5ms for basic programs

## 🔧 Testing Tools

### 1. Automated Regression Testing
```bash
cd tests/benchmarks
./performance_regression_test.sh
```

**Features:**
- Automated baseline comparison
- Multiple run statistical analysis
- Pass/Warn/Fail thresholds
- Historical logging
- Git commit tracking

### 2. High-Precision Benchmarking
```bash
cd tests/benchmarks
./precise_benchmark.sh
```

**Features:**
- Nanosecond-resolution timing
- Multiple measurement methodologies
- Statistical analysis (median, min, max)
- Comparison with documented targets

### 3. Cross-Language Benchmarking
```bash
cd tests/benchmarks
./run_all_benchmarks_fixed.sh
```

**Features:**
- Comparison with Python, Node.js, Lua, Julia
- Accurate timing methodology
- Multiple runs for stability
- Winner detection

## 📈 Measurement Methodology

### ✅ Correct Timing Approaches

#### System Time Command (Recommended)
```bash
time ./orus program.orus
```
- Most accurate for total execution time
- Includes process overhead
- Standard across systems

#### High-Resolution Timer (For Precision)
```bash
start_ns=$(date +%s%N)
./orus program.orus >/dev/null 2>&1
end_ns=$(date +%s%N)
duration=$((end_ns - start_ns))
```
- Nanosecond precision
- Excludes I/O overhead
- Best for micro-benchmarks

### ❌ Problematic Timing Approaches

#### Date + BC Calculator (Avoid)
```bash
# DON'T USE THIS
start_time=$(date +%s.%N)
./orus program.orus
end_time=$(date +%s.%N)
time=$(echo "$end_time - $start_time" | bc -l)
```
**Problems:**
- Process overhead included in measurement
- BC calculator precision limitations
- Inconsistent across systems
- Can inflate results by 3000%+

### Statistical Best Practices

1. **Multiple Runs**: Always run ≥5 iterations
2. **Median Selection**: Use median to avoid outliers
3. **Warm-up**: Include 1-2 warm-up runs for consistency
4. **Environment**: Test in consistent environment
5. **System Load**: Avoid high system load during testing

## 🎯 Performance Baselines

### Current Baselines (Jul 2025)
```
arithmetic: 0.028s (30ms tolerance)
control_flow: 0.052s (55ms tolerance)
```

### Threshold Configuration
- **Warning**: +15% over baseline
- **Failure**: +50% over baseline
- **Excellent**: Under baseline

### Updating Baselines
```bash
# Update baseline file manually
echo "arithmetic,0.025,$(date)" > tests/benchmarks/performance_baselines.txt
echo "control_flow,0.048,$(date)" >> tests/benchmarks/performance_baselines.txt
```

## 🚨 Performance Regression Detection

### Automated Testing Integration

#### Pre-commit Hook
```bash
#!/bin/bash
cd tests/benchmarks
./performance_regression_test.sh
if [ $? -ne 0 ]; then
    echo "❌ Performance regression detected!"
    echo "Run './performance_regression_test.sh' for details"
    exit 1
fi
```

#### CI/CD Integration
```yaml
# GitHub Actions example
- name: Performance Tests
  run: |
    cd tests/benchmarks
    ./performance_regression_test.sh
    if [ $? -eq 2 ]; then
      echo "::error::Critical performance regression detected"
      exit 1
    elif [ $? -eq 1 ]; then
      echo "::warning::Performance degradation detected"
    fi
```

### Manual Investigation

#### Performance Profiling
```bash
# CPU profiling with perf (Linux)
perf record ./orus benchmark.orus
perf report

# Memory profiling with valgrind
valgrind --tool=massif ./orus benchmark.orus

# Basic timing breakdown
time -v ./orus benchmark.orus
```

#### Bytecode Analysis
```bash
# Debug bytecode generation
ORUS_DEBUG=1 ./orus benchmark.orus

# Compare bytecode between versions
git show HEAD~1:benchmark.orus > /tmp/old_benchmark.orus
./orus -d benchmark.orus > current_bytecode.txt
./orus -d /tmp/old_benchmark.orus > old_bytecode.txt
diff current_bytecode.txt old_bytecode.txt
```

## 📝 Writing Performance Tests

### Test Structure
```orus
// performance_test_template.orus

// Warm-up phase (optional)
for i in 0..1000:
    // Simple operation

// Main benchmark
mut total = 0
for i in 0..1000000:  // Sufficient iterations for timing
    total = total + i * 2 - 1

print(total)  // Prevent optimization
```

### Benchmark Categories

#### 1. Arithmetic Operations
- Mathematical computations
- Loop-heavy calculations
- Type conversions
- Floating-point operations

#### 2. Control Flow
- Conditional branches
- Loop constructs
- Function calls
- Pattern matching

#### 3. Memory Operations
- Variable allocation
- Array operations
- String manipulation
- Garbage collection stress

#### 4. I/O Operations
- File reading/writing
- Print operations
- Network operations (future)

### Performance Optimization Guidelines

#### Compiler Optimizations
- **LICM**: Loop Invariant Code Motion
- **Register Allocation**: Efficient register usage
- **Constant Folding**: Compile-time evaluation
- **Dead Code Elimination**: Remove unused code

#### Runtime Optimizations
- **Computed Goto**: Fast dispatch
- **Register-based VM**: Fewer memory accesses
- **Object Pooling**: Reduce allocation overhead
- **Mark-and-Sweep GC**: Efficient memory management

## 🔍 Troubleshooting Performance Issues

### Common Performance Problems

#### 1. Loop Performance
**Symptoms**: Slow iteration-heavy code
**Investigation**:
```bash
# Check for loop guards (should be removed)
grep -r "LOOP_GUARD" src/

# Verify LICM optimization
ORUS_DEBUG=1 ./orus loop_test.orus | grep LICM
```

#### 2. Memory Leaks
**Symptoms**: Increasing memory usage
**Investigation**:
```bash
valgrind --leak-check=full ./orus program.orus
```

#### 3. Compilation Overhead
**Symptoms**: Slow startup times
**Investigation**:
```bash
time ./orus -c "print(42)"  # Should be < 5ms
```

### Performance Recovery Checklist

- [ ] Run `./performance_regression_test.sh`
- [ ] Check recent commits for performance impact
- [ ] Profile with `perf` or `valgrind`
- [ ] Compare bytecode generation
- [ ] Verify optimization flags in Makefile
- [ ] Test with different optimization levels
- [ ] Check for memory leaks
- [ ] Validate benchmark methodology

## 📊 Performance Reporting

### Test Results Format
```
Date: 2025-07-04
Commit: abc1234
Environment: M1 MacBook Pro, macOS 14

Benchmark Results:
- Arithmetic: 28ms (Target: 30ms) ✅ PASS
- Control Flow: 52ms (Target: 55ms) ✅ PASS

Overall Status: EXCELLENT
```

### Historical Tracking
```bash
# View performance trends
tail -20 tests/benchmarks/performance_results.log

# Generate performance graph (if available)
python3 tools/plot_performance.py tests/benchmarks/performance_results.log
```

## 🎯 Best Practices Summary

1. **Always measure before optimizing**
2. **Use multiple measurement methods for validation**
3. **Test on representative workloads**
4. **Monitor performance continuously**
5. **Document performance changes**
6. **Set realistic performance targets**
7. **Automate regression detection**
8. **Profile before making changes**

## 📚 Additional Resources

- [Orus Architecture Documentation](VM_OPTIMIZATION.md)
- [Loop Safety and Performance](LOOP_SAFETY_GUIDE.md)
- [Benchmark Results](../tests/benchmarks/README.md)
- [Memory Management Guide](../docs/MEMORY_MANAGEMENT.md)

---

**Remember**: Performance testing is most effective when integrated into the development workflow. Run tests frequently and investigate regressions immediately.