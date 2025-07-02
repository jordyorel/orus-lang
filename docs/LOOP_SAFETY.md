# 🔒 Loop Safety Features in Orus

Orus includes comprehensive loop safety mechanisms to prevent infinite loops, detect programming errors at compile time, and ensure program reliability. This document describes the advanced loop safety features implemented in the Orus programming language.

## 🎯 Overview

The loop safety system consists of three main components:
1. **Compile-time Infinite Loop Detection** - Static analysis to catch obvious infinite loops
2. **Runtime Loop Guards** - Dynamic protection against runaway loops
3. **Direction Validation** - Compile-time checks for range syntax correctness

---

## 🔍 Compile-time Infinite Loop Detection

The Orus compiler performs static analysis to detect patterns that would result in infinite loops.

### Detected Patterns

#### Constant `true` Conditions
```orus
while true:        # ❌ Detected at compile time
    print("infinite")

while 1 == 1:      # ❌ Detected at compile time
    print("always true")
```

#### Impossible Range Directions
```orus
for i in 10..0:    # ❌ Forward range with backward bounds
    print(i)

for i in 0..10..-1: # ❌ Forward range with negative step
    print(i)
```

#### Zero Step Values
```orus
for i in 0..10..0:  # ❌ Zero step would never advance
    print(i)
```

### Exceptions (Not Detected as Infinite)

#### Loops with Break Statements
```orus
while true:        # ✅ Valid - contains break
    if some_condition:
        break
    print("controlled infinite")
```

#### Loops with Return Statements
```orus
fn search_forever():
    while true:    # ✅ Valid - contains return
        if found:
            return result
        continue_searching()
```

---

## ⚡ Runtime Loop Guards

Runtime guards protect against loops that cannot be proven safe at compile time but may still run indefinitely.

### How It Works

1. **Guard Initialization**: When a potentially unsafe loop is detected, the compiler inserts guard initialization code
2. **Iteration Counting**: Each loop iteration increments a counter
3. **Limit Checking**: When the counter exceeds a safety threshold, execution is halted

### Example Protection

```orus
// This loop cannot be proven safe at compile time
let x = 42
while x != 1:                    // Collatz conjecture - may be infinite
    if x % 2 == 0:
        x = x / 2
    else:
        x = 3 * x + 1
    // Runtime guard automatically inserted here
```

### Configuration

#### Default Iteration Limits

**Standard Configuration:**
- **Default limit**: 1,000,000 iterations (configurable)
- **Maximum capacity**: 4,294,967,295 iterations (4-byte limit)
- **Trigger threshold**: Loops with >10,000 static iterations or unknown bounds
- **Guard activation**: Automatic for potentially unsafe loops

#### How Loop Guards Work

**4-Byte Architecture:**
```c
// Compiler emits 4 bytes in little-endian format
uint32_t maxIterations = 1000000;  // Default: 1 million
// Supports range: 1 to 4,294,967,295 iterations
```

**Why 4 Bytes When Most Loops Are Small?**
- **Future-proofing**: As datasets grow, Orus scales with your needs
- **No artificial limits**: Prevents "magic number" constraints in algorithms  
- **Scientific computing**: Research and HPC applications need this capacity
- **Batch processing**: Large-scale data processing without arbitrary boundaries
- **VM consistency**: Keeps the guard system simple and uniform

**Runtime Protection:**
```orus
// Loops that trigger guards (automatically protected)
for i in 0..2000000:     // >10k iterations: guard enabled
    process_data(i)

// Loops that don't need guards (optimized)  
for i in 0..5000:        // <10k iterations: no guard overhead
    quick_operation(i)
```

#### Configuring Loop Limits

**Method 1: Compiler Configuration (Recommended)**

Update the default in `src/compiler/compiler.c`:
```c
// Location: analyzeLoopSafety() function
safety->maxIterations = 5000000;  // Set to 5 million iterations
```

**Method 2: Runtime Environment Variables (Future Enhancement)**
```bash
# Set custom loop limit
export ORUS_MAX_LOOP_ITERATIONS=2000000
./orus my_program.orus
```

**Method 3: Pragma Directives (Future Enhancement)**
```orus
#pragma loop_limit 10000000  // Set 10 million limit for this file

for i in 0..huge_dataset_size:
    intensive_computation(i)
```

#### Performance Characteristics

**Guard Overhead:**
- **Enabled**: ~2-5% performance impact per iteration
- **Disabled**: Zero overhead for small loops (<10k iterations)
- **Memory**: 2 registers per guarded loop (counter + limit)

**Optimization Strategy:**
```orus
// Automatically optimized (no guard)
for i in 0..1000:
    fast_operation(i)

// Automatically guarded (safety protection)  
for i in 0..1000000:
    safe_operation(i)  // Protected against infinite loops
```

---

## 🧭 Direction Validation

The compiler validates that range syntax uses appropriate step directions.

### Valid Patterns

```orus
// Forward ranges with positive steps
for i in 0..10..1:     # ✅ Standard forward iteration
    print(i)

for i in 0..10..2:     # ✅ Forward with step 2
    print(i)

// Backward ranges with negative steps  
for i in 10..0..-1:    # ✅ Standard backward iteration
    print(i)

for i in 10..0..-2:    # ✅ Backward with step 2
    print(i)

// Equal start and end
for i in 5..5..1:      # ✅ Single iteration or none
    print(i)
```

### Invalid Patterns

```orus
// Direction mismatches
for i in 0..10..-1:    # ❌ Forward range, backward step
    print(i)

for i in 10..0..1:     # ❌ Backward range, forward step
    print(i)

// Zero steps
for i in 0..10..0:     # ❌ Zero step - no progress
    print(i)
```

---

## 🔧 Advanced Range Syntax

Orus supports a rich range syntax with customizable step values: `start..end..step`

### Syntax Examples

```orus
// Basic ranges (step defaults to 1)
for i in 0..5:         // Equivalent to 0..5..1
    print(i)          // Prints: 0, 1, 2, 3, 4

// Custom positive steps
for i in 0..10..2:
    print(i)          // Prints: 0, 2, 4, 6, 8

for i in 0..100..25:
    print(i)          // Prints: 0, 25, 50, 75

// Negative steps for countdown
for i in 10..0..-1:
    print(i)          // Prints: 10, 9, 8, 7, 6, 5, 4, 3, 2, 1

for i in 10..0..-2:
    print(i)          // Prints: 10, 8, 6, 4, 2

// Large steps
for i in 0..1000..100:
    print(i)          // Prints: 0, 100, 200, ..., 900

// Single element ranges
for i in 5..6..1:
    print(i)          // Prints: 5

// Empty ranges (no output)
for i in 5..0..1:     // Start > end with positive step
    print(i)          // Prints nothing

for i in 0..5..-1:    // Start < end with negative step  
    print(i)          // Prints nothing
```

---

## 🏗️ Implementation Details

### Compiler Integration

The loop safety features are integrated into the compilation process:

1. **AST Analysis**: During compilation, loop nodes are analyzed for safety patterns
2. **Static Evaluation**: Constant expressions in loop conditions are evaluated
3. **Guard Insertion**: Runtime guards are automatically inserted for risky loops
4. **Direction Checking**: Range syntax is validated for logical consistency

### VM Support

The virtual machine includes specific opcodes for runtime loop safety:

- `OP_LOOP_GUARD_INIT`: Initialize iteration counter and limit
- `OP_LOOP_GUARD_CHECK`: Check iteration count and halt if exceeded

### Data Structures

```c
// Loop safety analysis information
typedef struct {
    bool isInfinite;              // True if detected as infinite
    bool hasBreakOrReturn;        // True if contains escape mechanisms
    bool hasVariableCondition;    // True if condition depends on variables
    int maxIterations;            // Runtime iteration limit
    int staticIterationCount;     // Compile-time computed count (-1 if unknown)
} LoopSafetyInfo;
```

---

## 🧪 Testing

Comprehensive test suites verify the loop safety features:

### Test Categories

1. **Infinite Loop Detection**: Tests for compile-time detection of obvious infinite loops
2. **Range Validation**: Tests for direction validation and step checking
3. **Runtime Guards**: Tests for runtime protection mechanisms
4. **Integration**: Complex scenarios combining multiple safety features

### Test Files

- `tests/edge_cases/infinite_loop_detection.orus`
- `tests/edge_cases/range_direction_validation.orus`
- `tests/edge_cases/runtime_loop_guards.orus`
- `tests/control_flow/advanced_range_syntax.orus`
- `tests/control_flow/loop_safety_integration.orus`

---

## ⚠️ Error Messages

Clear error messages help developers understand and fix loop safety issues:

```
Error: Infinite loop detected - condition is always true
  --> example.orus:5:1
   |
 5 | while true:
   | ^^^^^^^^^^^ this condition never changes

Error: Invalid range direction - negative step with forward range  
  --> example.orus:8:5
   |
 8 | for i in 0..10..-1:
   |          ^^^^^^^^^^ step direction conflicts with range direction

Runtime Error: Loop exceeded maximum iteration limit (1000000)
  --> example.orus:12:1
   |
12 | while complex_condition():
   | ^^^^^^^^^^^^^^^^^^^^^^^^^ loop guard triggered
```

---

## 🎓 Best Practices

### Writing Safe Loops

1. **Use explicit break conditions**: Always ensure loops have clear termination
2. **Validate input ranges**: Check bounds before creating dynamic ranges
3. **Test with edge cases**: Verify behavior with boundary values
4. **Use appropriate step sizes**: Choose steps that make mathematical sense

### Performance Considerations

1. **Guard overhead**: Runtime guards add minimal overhead (~1-2% for most loops)
2. **Compile-time optimization**: Static analysis eliminates guards when possible
3. **Nested loop limits**: Deep nesting may trigger conservative iteration limits

### Debugging Tips

1. **Enable trace mode**: Use `--trace` flag to see loop guard operations
2. **Check step direction**: Ensure range direction matches intended iteration
3. **Review iteration counts**: Large loops may need explicit safety measures

---

## 🔮 Future Enhancements

Planned improvements to the loop safety system:

1. **Configurable limits**: User-defined iteration thresholds
2. **Advanced analysis**: More sophisticated infinite loop detection
3. **Performance profiling**: Loop performance metrics and optimization hints
4. **IDE integration**: Real-time safety warnings in development environments

---

## 📚 References

- [LANGUAGE.md](LANGUAGE.md) - Complete Orus language documentation
- [VM_OPTIMIZATION.md](VM_OPTIMIZATION.md) - Virtual machine optimization details
- [IMPLEMENTATION_GUIDE.md](IMPLEMENTATION_GUIDE.md) - Implementation details

---

*This documentation covers Orus v0.2.0+ loop safety features. For older versions, some features may not be available.*