# 🔧 Loop Guard Configuration Guide

This guide explains how to configure Orus loop iteration limits for different use cases.

## 📊 Current Loop Guard System

### Default Configuration

- **Default limit**: 1,000,000 iterations
- **Maximum capacity**: 4,294,967,295 iterations (4-byte limit)
- **Trigger threshold**: Loops with >10,000 static iterations or unknown bounds
- **Architecture**: 4-byte little-endian encoding

### When Guards Activate

```orus
// ✅ No guard (optimized) - small loops
for i in 0..5000:
    quick_operation(i)

// 🛡️ Guard enabled (protected) - large loops  
for i in 0..50000:
    protected_operation(i)

// 🛡️ Guard enabled (protected) - unknown bounds
start = get_start()
end = get_end()
for i in start..end:
    dynamic_operation(i)
```

## 🛠️ Configuration Methods

### Method 1: Compiler Source Modification (Current)

**Location**: `src/compiler/compiler.c`
**Function**: `analyzeLoopSafety()`
**Line**: ~2212

```c
// Original:
safety->maxIterations = 1000000; // Default safety limit

// Examples of custom limits:
safety->maxIterations = 5000000;    // 5 million (high-performance)
safety->maxIterations = 10000000;   // 10 million (data processing)
safety->maxIterations = 100000000;  // 100 million (scientific computing)
safety->maxIterations = 2000000000; // 2 billion (maximum practical)
```

**Rebuild Required:**
```bash
make clean && make
```

### Method 2: Per-Application Limits

For different applications, you can maintain separate builds with different limits:

```bash
# Create specialized builds
mkdir builds
cp -r src builds/high-performance-src
cd builds/high-performance-src

# Edit compiler.c for specific use case
sed -i 's/maxIterations = 1000000/maxIterations = 50000000/' compiler/compiler.c

# Build specialized version
make clean && make
mv orus ../../orus-high-performance
```

### Method 3: Conditional Configuration (Advanced)

Add environment-based configuration:

```c
// In analyzeLoopSafety() function:
const char* limit_env = getenv("ORUS_MAX_ITERATIONS");
if (limit_env) {
    safety->maxIterations = (uint32_t)strtoul(limit_env, NULL, 10);
} else {
    safety->maxIterations = 1000000; // Default
}
```

**Usage:**
```bash
export ORUS_MAX_ITERATIONS=10000000
./orus large_data_program.orus
```

## 📋 Use Case Recommendations

### 🌐 Web Applications & APIs (95% of use cases)
```c
safety->maxIterations = 1000000;    // 1 million (default)
```
**Typical loops:** Request processing (1K-100K), data validation (100-10K), template rendering
**Examples:** User dashboards, REST APIs, content management systems

### 📊 Data Processing & Analytics (4% of use cases)  
```c
safety->maxIterations = 10000000;   // 10 million
```
**Typical loops:** CSV processing (10K-1M rows), database operations (1K-10M records), log analysis
**Examples:** ETL pipelines, business intelligence, reporting systems

### 🚀 High-Performance Computing (1% of use cases)
```c
safety->maxIterations = 100000000;  // 100 million
```
**Typical loops:** Scientific simulations (1M-100M iterations), numerical analysis, ML training
**Examples:** Weather modeling, financial risk analysis, computer graphics

### 🔬 Research & Extreme Computing (<0.1% of use cases)
```c
safety->maxIterations = 1000000000; // 1 billion
```
**Typical loops:** Particle physics simulations, genome analysis, large-scale ML training
**Examples:** CERN data analysis, protein folding, LLM training

### 🛡️ Security-Critical & Embedded Systems
```c
safety->maxIterations = 100000;     // 100 thousand
```
**Typical loops:** Real-time processing (60-10K iterations), sensor data (100-1K samples)
**Examples:** IoT devices, automotive systems, medical devices

### 🎮 Game Development
```c
safety->maxIterations = 500000;     // 500 thousand
```
**Typical loops:** Frame updates (60 FPS), particle systems (1K-100K particles), AI pathfinding
**Examples:** Physics engines, rendering pipelines, game logic

### 📊 **Reality Check: When Do You Actually Need Billions?**

**Billion+ iteration loops are extremely rare** and usually indicate:

1. **❌ Algorithm inefficiency** - O(n²) or worse complexity that should be optimized
2. **❌ Missing batching** - Processing that should be chunked or streamed  
3. **❌ Wrong tool** - Data that should be in a database or specialized system
4. **✅ Legitimate scientific computing** - Physics simulations, molecular dynamics

**Common Misconceptions:**
```orus
// ❌ WRONG: Brute force approach  
for i in 0..1000000000:
    if is_solution(i):
        return i

// ✅ BETTER: Algorithmic approach
return binary_search(0, 1000000000, is_solution)

// ❌ WRONG: Processing all data in memory
for record in 0..billion_records:
    process(load_record(record))

// ✅ BETTER: Streaming/batching
for batch in data_stream(batch_size=10000):
    process_batch(batch)
```

**When Billions Are Legitimate:**
- Monte Carlo simulations (random sampling)
- Particle physics computations  
- Cryptographic key generation
- Large-scale machine learning training
- Molecular dynamics simulations

## ⚡ Performance Impact

### Guard Overhead by Loop Size

| Loop Size | Guard Status | Overhead | Memory Usage |
|-----------|-------------|----------|--------------|
| <10k iterations | Disabled | 0% | 0 registers |
| 10k-1M iterations | Enabled | ~2% | 2 registers |
| 1M-100M iterations | Enabled | ~3% | 2 registers |
| >100M iterations | Enabled | ~5% | 2 registers |

### Benchmark Example

```orus
// Performance test
start_time = get_time()

mut total = 0
for i in 0..10000000:  // 10 million iterations
    total = total + i

end_time = get_time()
print("Time:", end_time - start_time, "ms")
print("Total:", total)
```

**Expected Results:**
- **Without guards**: ~50ms
- **With guards**: ~52ms (~4% overhead)

## 🧪 Testing Your Configuration

### Basic Functionality Test

```orus
// Test 1: Small loop (should be fast)
for i in 0..1000:
    print(i)

// Test 2: Medium loop (should work with guards)
mut sum = 0
for i in 0..100000:
    sum = sum + i
print("Sum:", sum)

// Test 3: Large loop (tests your limit)
mut large_sum = 0
for i in 0..5000000:  // Adjust based on your limit
    large_sum = large_sum + 1
print("Large sum:", large_sum)
```

### Stress Test

```orus
// Stress test: approach your configured limit
mut stress_total = 0
for i in 0..999999999:  // Just under 1 billion
    stress_total = stress_total + 1
    if i % 100000000 == 0:
        print("Progress:", i)
print("Stress test complete:", stress_total)
```

## 🔍 Troubleshooting

### Common Issues

**Problem**: "Loop exceeded maximum iteration limit"
**Solution**: Increase `maxIterations` or optimize your algorithm

**Problem**: Poor performance with large loops
**Solution**: Consider if you really need guards; maybe reduce the threshold

**Problem**: Memory usage too high
**Solution**: Guards use 2 registers per loop; consider nested loop optimization

### Debug Information

Add debug output to see guard activation:

```c
// In compiler.c, when guard is enabled:
if (safetyInfo.staticIterationCount > 10000) {
    printf("DEBUG: Guard enabled for %d iterations, limit=%u\n", 
           safetyInfo.staticIterationCount, safetyInfo.maxIterations);
}
```

## 🚀 Future Enhancements

### Planned Features

1. **Runtime Configuration**: Environment variables and command-line flags
2. **Pragma Directives**: Per-file or per-loop limits
3. **Adaptive Limits**: Dynamic adjustment based on system resources
4. **Profile-Guided Optimization**: Automatic limit tuning based on usage patterns

### Proposed Syntax

```orus
// Future pragma support
#pragma loop_limit 50000000

// Future per-loop syntax
for i in 0..huge_number @limit(10000000):
    intensive_operation(i)

// Future adaptive syntax
for i in 0..dataset_size @adaptive:
    smart_operation(i)
```

---

## 📚 Related Documentation

- [LOOP_SAFETY.md](LOOP_SAFETY.md) - Complete loop safety documentation
- [COMPLETE_ORUS_TUTORIAL.md](COMPLETE_ORUS_TUTORIAL.md) - Full language tutorial
- [VM_OPTIMIZATION.md](VM_OPTIMIZATION.md) - VM performance details

---

*Configure responsibly! Higher limits provide more flexibility but reduce safety margins.*