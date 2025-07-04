# 🔒 Loop Safety & Performance Guide

**Complete guide to Orus's progressive loop safety system - protecting against infinite loops while maximizing performance.**

---

## 🎯 Overview

Orus implements a **progressive loop safety system** that provides automatic protection against infinite loops while maintaining maximum performance for typical code. The system intelligently scales from zero-overhead execution for small loops to comprehensive safety monitoring for large-scale processing.

### Progressive Safety Levels

| **Iteration Count** | **Behavior** | **Message** | **User Override** |
|---------------------|--------------|-------------|-------------------|
| `< 100K` | ✅ **Fast, guard-free execution** | None | N/A |
| `100K–1M` | ✅ **Guarded silently** | No message | No need |
| `1M–10M` | ⚠️ **Warns at 1M** | Prints warning | No error yet |
| `> 10M` | ❌ **Stops by default** | Runtime error | ✅ Yes (via env var) |
| `Any` w/ `ORUS_MAX_LOOP_ITERATIONS=0` | ✅ **Unlimited** | None | Full trust granted |

---

## 🚀 Quick Examples

### ✅ Fast Execution (No Guards)
```orus
// Zero overhead - runs at maximum speed
for i in 0..50000:
    process_item(i)

mut count = 0
while count < 75000:
    count = count + 1
```

### 🛡️ Silent Protection (Guards Active)
```orus
// Protected, but no user-visible impact
for batch in 0..500000:  // 500K iterations
    process_batch(batch)
```

### ⚠️ Warning + Continuation
```orus
// Warns at 1M, continues to completion
mut total = 0
for i in 0..1500000:     // 1.5M iterations
    total = total + i
// Output: Warning: Loop has exceeded 1000000 iterations...
print("Total:", total)   // Completes successfully
```

### ❌ Safety Stop
```orus
// Stops at 10M iterations with clear error
for i in 0..20000000:    // 20M requested
    process_large_dataset(i)
// Error: Loop exceeded maximum iteration limit (10000000)
```

### 🔓 Unlimited Processing
```bash
# Override for specialized applications
ORUS_MAX_LOOP_ITERATIONS=0 ./orus massive_computation.orus
```

---

## 🔧 Configuration

### Environment Variables

#### `ORUS_LOOP_GUARD_THRESHOLD` (Default: 100,000)
Controls when guards are enabled:
```bash
# Enable guards at 50K iterations (more aggressive)
ORUS_LOOP_GUARD_THRESHOLD=50000 ./orus program.orus

# Enable guards at 500K iterations (more permissive)
ORUS_LOOP_GUARD_THRESHOLD=500000 ./orus program.orus
```

#### `ORUS_MAX_LOOP_ITERATIONS` (Default: 10,000,000)
Controls the hard stop limit:
```bash
# Unlimited loops (enterprise/scientific computing)
ORUS_MAX_LOOP_ITERATIONS=0 ./orus program.orus

# Custom 50M limit (large-scale data processing)
ORUS_MAX_LOOP_ITERATIONS=50000000 ./orus program.orus

# Conservative 1M limit (web applications)
ORUS_MAX_LOOP_ITERATIONS=1000000 ./orus program.orus
```

### Combined Configuration
```bash
# Custom setup for high-performance computing
ORUS_LOOP_GUARD_THRESHOLD=1000000 \
ORUS_MAX_LOOP_ITERATIONS=100000000 \
./orus scientific_simulation.orus
```

---

## 📊 Use Case Configurations

### 🌐 Web Applications (95% of use cases)
```bash
# Default settings - optimized for typical web workloads
./orus web_application.orus
```
- **Guard Threshold**: 100K iterations
- **Safety Limit**: 10M iterations
- **Typical Loops**: Request processing (1K-100K), data validation (100-10K)

### 📈 Data Processing (4% of use cases)
```bash
# Higher limits for batch processing
ORUS_MAX_LOOP_ITERATIONS=50000000 ./orus data_processor.orus
```
- **Guard Threshold**: 100K iterations (unchanged)
- **Safety Limit**: 50M iterations
- **Typical Loops**: CSV processing (10K-1M rows), database operations (1K-10M records)

### 🔬 Scientific Computing (1% of use cases)
```bash
# Unlimited for research and simulation
ORUS_MAX_LOOP_ITERATIONS=0 ./orus simulation.orus
```
- **Guard Threshold**: 100K iterations (warning still useful)
- **Safety Limit**: Unlimited
- **Typical Loops**: Monte Carlo simulations (1M-1B iterations), numerical analysis

### 🛡️ Security-Critical Systems
```bash
# Conservative limits for embedded/safety systems
ORUS_LOOP_GUARD_THRESHOLD=10000 \
ORUS_MAX_LOOP_ITERATIONS=100000 \
./orus embedded_system.orus
```
- **Guard Threshold**: 10K iterations (early detection)
- **Safety Limit**: 100K iterations (strict bounds)
- **Typical Loops**: Real-time processing (60-10K iterations), sensor data (100-1K samples)

---

## ⚡ Performance Characteristics

### Overhead Analysis
| **Loop Size** | **Guard Overhead** | **Memory Usage** | **Performance** |
|---------------|-------------------|------------------|-----------------|
| `< 100K` | **0%** | 0 extra registers | **Maximum speed** |
| `100K-1M` | **~1%** | 3 registers per loop | **Near-optimal** |
| `1M-10M` | **~1%** + warning I/O | 3 registers per loop | **High performance** |
| `> 10M` | **~1%** until stop | 3 registers per loop | **Controlled execution** |

### Memory Usage
- **Small loops**: Zero memory overhead
- **Guarded loops**: 3 consecutive registers per loop
  - Register N: Current iteration count
  - Register N+1: Warning threshold (1M)
  - Register N+2: Maximum iterations (10M or custom)

### CPU Impact
- **Guard check**: ~2 additional instructions per iteration
- **Warning check**: Happens only once at 1M iterations
- **I/O impact**: Warning message printed to stderr once per loop

---

## 🔍 Compile-Time Safety Features

In addition to runtime guards, Orus performs static analysis to catch obvious infinite loops at compile time.

### Detected Patterns

#### Constant `true` Conditions
```orus
while true:        # ❌ Detected at compile time
    print("infinite")

while 1 == 1:      # ❌ Detected at compile time  
    print("always true")
```

#### Invalid Range Directions
```orus
for i in 10..0:    # ❌ Forward range with backward bounds
    print(i)

for i in 0..10..-1: # ❌ Forward range with negative step
    print(i)

for i in 0..10..0:  # ❌ Zero step would never advance
    print(i)
```

#### Exception: Loops with Break Statements
```orus
while true:        # ✅ Valid - contains break
    if some_condition:
        break
    process_data()
```

---

## 🧪 Testing Your Configuration

### Test Script Template
```orus
// test_loop_config.orus
print("Testing loop safety configuration...")

// Test 1: Under guard threshold (should be fast)
mut sum1 = 0
for i in 0..50000:
    sum1 = sum1 + i
print("50K test: completed")

// Test 2: Above threshold, under warning (should be silent)
mut sum2 = 0  
for i in 0..500000:
    sum2 = sum2 + i
print("500K test: completed")

// Test 3: Above warning threshold (should warn but complete)
mut sum3 = 0
for i in 0..1500000:
    sum3 = sum3 + i
print("1.5M test: completed")

print("Configuration test complete!")
```

### Expected Output (Default Configuration)
```
Testing loop safety configuration...
50K test: completed
500K test: completed
Warning: Loop has exceeded 1000000 iterations and is now being monitored for safety.
Set ORUS_MAX_LOOP_ITERATIONS=0 to disable loop limits.
1.5M test: completed
Configuration test complete!
```

---

## 🚨 Error Messages & Troubleshooting

### Warning Message
```
Warning: Loop has exceeded 1000000 iterations and is now being monitored for safety.
Set ORUS_MAX_LOOP_ITERATIONS=0 to disable loop limits.
```
**Meaning**: Your loop has exceeded 1M iterations but will continue running up to the safety limit.
**Action**: Usually none required - this is just informational.

### Error Message
```
Runtime Error: Loop exceeded maximum iteration limit (10000000).
Set ORUS_MAX_LOOP_ITERATIONS=0 for unlimited loops.
```
**Meaning**: Your loop hit the hard safety limit and was terminated.
**Solutions**:
1. Fix infinite loop bug in your code
2. Increase limit: `ORUS_MAX_LOOP_ITERATIONS=50000000`
3. Disable limits: `ORUS_MAX_LOOP_ITERATIONS=0`

### Debugging Large Loops
```orus
// Add progress indicators for large loops
mut processed = 0
for item in 0..5000000:
    process_item(item)
    processed = processed + 1
    
    // Progress indicator every 100K items
    if processed % 100000 == 0:
        print("Processed:", processed, "items")
```

---

## 🏗️ Technical Implementation

### Architecture Overview
- **Compile-time**: Static analysis identifies loop iteration counts when possible
- **Runtime**: Dynamic guards monitor loops that exceed thresholds
- **Register allocation**: Automatic management of guard registers
- **Bytecode**: `OP_LOOP_GUARD_INIT` and `OP_LOOP_GUARD_CHECK` opcodes

### Loop Classification
1. **Static loops**: `for i in 0..1000` - iteration count known at compile time
2. **Dynamic loops**: `for i in 0..user_input` - count determined at runtime
3. **Conditional loops**: `while condition` - termination depends on runtime state

### Guard Emission Logic
```
if (loop_iterations < 0 OR loop_iterations > GUARD_THRESHOLD):
    emit_guard_initialization()
    emit_guard_checks()
else:
    emit_optimized_loop()
```

### Memory Safety
- Guards use safe register allocation with bounds checking
- Maximum capacity: 4,294,967,295 iterations (32-bit counter)
- Automatic overflow detection and handling

---

## 📚 Best Practices

### ✅ Recommended Patterns
```orus
// Good: Clear iteration bounds
for i in 0..records.length:
    process_record(records[i])

// Good: Progress indicators for large loops
for batch in 0..large_dataset_size:
    process_batch(batch)
    if batch % 10000 == 0:
        print(f"Progress: {batch}/{large_dataset_size}")

// Good: Environment-aware configuration
// Run with ORUS_MAX_LOOP_ITERATIONS=0 for production
```

### ❌ Patterns to Avoid
```orus
// Avoid: Unclear termination conditions
while some_complex_condition():
    modify_state()  // May accidentally create infinite loop

// Avoid: Very large fixed loops without progress indicators
for i in 0..100000000:  // 100M iterations with no feedback
    silent_processing(i)
```

### 🔧 Configuration Guidelines
- **Development**: Use default settings for early bug detection
- **Testing**: Test with various `ORUS_MAX_LOOP_ITERATIONS` values
- **Production**: Set appropriate limits based on expected workload
- **Debugging**: Use `ORUS_MAX_LOOP_ITERATIONS=0` to bypass limits temporarily

---

## 🎓 Summary

Orus's progressive loop safety system provides:

- ⚡ **Zero overhead** for typical small loops
- 🛡️ **Automatic protection** against runaway loops  
- ⚠️ **Early warning** at 1M iterations
- 🚨 **Safety stops** at configurable limits
- 🔧 **Full configurability** for specialized use cases
- 📊 **Clear feedback** with helpful error messages

This design ensures that Orus programs are both **safe by default** and **capable of high-performance computing** when configured appropriately.