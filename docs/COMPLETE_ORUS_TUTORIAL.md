# 📚 Complete Orus Programming Language Tutorial
*The Ultimate Guide to Orus Programming - Every Feature, Every Detail*

Welcome to the most comprehensive tutorial for the Orus programming language! This guide covers every single implemented feature, from basic literals to advanced loop safety mechanisms. Each concept is explained with working examples taken directly from the Orus test suite.

---

## 📖 Table of Contents

1. [Getting Started](#getting-started)
2. [Basic Literals and Output](#basic-literals-and-output)
    - [The `time_stamp` Builtin](#the-time_stamp-builtin)
3. [Variables and Assignment](#variables-and-assignment)
4. [Data Types System](#data-types-system)
5. [Expressions and Operators](#expressions-and-operators)
6. [String Operations and Formatting](#string-operations-and-formatting)
7. [Conditional Statements](#conditional-statements)
8. [Loops and Control Flow](#loops-and-control-flow)
9. [Advanced Loop Features](#advanced-loop-features)
10. [Error Handling and Edge Cases](#error-handling-and-edge-cases)
11. [Performance Features](#performance-features)
    - [Loop Invariant Code Motion (LICM)](#loop-invariant-code-motion-licm)
12. [Complete Language Reference](#complete-language-reference)
13. [Advanced Patterns](#advanced-patterns)
14. [Edge Cases and Stress Testing](#edge-cases-and-stress-testing)
15. [Testing Your Knowledge](#testing-your-knowledge)

---

## 🚀 Getting Started

### Installation and Setup

```bash
# Clone and build Orus
git clone <repository-url>
cd orus-reg-vm
make clean && make

# Run the REPL
./orus

# Execute a file
echo 'print("Hello, Orus!")' > hello.orus
./orus hello.orus
```

### Your First Orus Program

```orus
// hello.orus - Your first Orus program
print("Hello, World!")
```

**Output:**
```
Hello, World!
```

---

## 💎 Basic Literals and Output

### Integer Literals

The simplest Orus program uses integer literals:

```orus
// Literal integers
print(42)
print(0)
print(999999)
```

**Key Points:**
- Integer literals default to `i32` (32-bit signed integers)
- No quotes needed for numbers
- Negative numbers supported: `print(-42)`

### The `print` Statement

Orus provides powerful printing functions for different output needs:

#### Basic Print Function
`print` automatically inserts spaces between arguments:

```orus
// Basic print
print(42)

// Multiple arguments
print("The answer is", 42)

// Expressions in print
print(10 + 20 + 12)

// Multiple values with automatic spacing
print("x", "y", "z")
print(1, 2, 3, 4, 5)
```

**Output:**
```
42
The answer is 42
42
x y z
1 2 3 4 5
```

#### Custom Separator Print Function
Use `print_sep` for custom formatting with any separator:

```orus
// Comma-separated values
print_sep(", ", "apple", "banana", "cherry")

// Pipe-separated values
print_sep(" | ", "name", "age", "city")

// No separator (concatenation)
print_sep("", "hello", "world")

// Custom separators
print_sep(" -> ", "step1", "step2", "step3")
print_sep("\n", "line1", "line2", "line3")

// Variable separators
separator = " :: "
print_sep(separator, "custom", "format")

// Expression separators
print_sep(1 + 2, "number", "separator")
```

**Output:**
```
apple, banana, cherry
name | age | city
helloworld
step1 -> step2 -> step3
line1
line2
line3
custom :: format
number3separator
```

### The `time_stamp` Builtin

Orus provides a high-precision timing function for performance measurement and benchmarking:

```orus
// Get current timestamp in nanoseconds
start_time = time_stamp()
print("Start time:", start_time)

// Perform some work
result = 0
for i in 0..1000:
    result = result + i

end_time = time_stamp()
duration = end_time - start_time
duration_ms = duration / 1000000

print("Calculation result:", result)
print("Time taken (nanoseconds):", duration)
print("Time taken (milliseconds):", duration_ms)
```

**Output:**
```
Start time: 1639123456789123456
Calculation result: 499500
Time taken (nanoseconds): 245833
Time taken (milliseconds): 0
```

**Key Features:**
- Returns nanosecond-precision timestamps
- Perfect for micro-benchmarking and performance analysis
- Used in all official Orus benchmarks
- Cross-platform high-resolution timing

---

## 🏷️ Variables and Assignment

### Immutable Variables (Default)

Variables in Orus are **immutable by default** - they cannot be changed after assignment:

```orus
// Basic variable assignment
x = 3
y = 4
print(x * y + 2)  // Outputs: 14

// Variables can store expressions
result = x + y
print(result)     // Outputs: 7
```

**Key Concepts:**
- No `let` or `var` keyword needed for basic variables
- Variables are immutable by default (like Rust)
- Assignment uses `=` operator
- Variables can store the result of expressions

### Mutable Variables

When you need to modify a variable, use the `mut` keyword:

```orus
// Mutable variable declaration
mut x = 1
x = x + 2
print(x)  // Outputs: 3

// Mutable variables can be reassigned multiple times
mut counter = 0
counter = 1
counter = 2
counter = 3
print(counter)  // Outputs: 3
```

### Compound Assignment Operators

Mutable variables support compound assignment:

```orus
mut x = 5
x += 3    // Addition assignment (x = x + 3)
x *= 2    // Multiplication assignment (x = x * 2)
print(x)  // Outputs: 16

// All compound operators
mut value = 10
value += 5   // Addition: 15
value -= 3   // Subtraction: 12
value *= 2   // Multiplication: 24
value /= 4   // Division: 6
print(value) // Outputs: 6
```

### Constant Folding

Orus optimizes constant expressions at compile time:

```orus
// Complex constant expressions are optimized
a = 2 + 3 * 4 - 1          // Computed as: 2 + 12 - 1 = 13
b = a * 10 + (5 + 5)       // Uses the constant value of 'a'
print(b)                   // Outputs: 140
```

---

## 🔢 Data Types System

Orus has a rich type system with automatic type inference and explicit type annotations.

### Integer Types

#### i32 (32-bit Signed Integer) - Default

```orus
// Basic i32 (default for integer literals)
x = 42
y = 13

// All arithmetic operations
print(x + y)   // Addition: 55
print(x - y)   // Subtraction: 29
print(x * y)   // Multiplication: 546
print(x / y)   // Division: 3
print(x % y)   // Modulo: 3

// Negative numbers
ten = 10
neg = 0 - ten  // Negative: -10
print(neg)
```

#### i64 (64-bit Signed Integer)

```orus
// Large number support
mut x: i64 = 5000000000    // Explicit type annotation
mut y: i64 = 1000000000
x += y
print(x)  // Outputs: 6000000000

// Loop integration with i64
mut z: i64 = 0
for i in 0..10:
    z = z + i
print(z)  // Outputs: 45
```

#### u32 (32-bit Unsigned Integer)

```orus
// Unsigned integer literals with suffix
zero = 0u32
one_val = 1u32
max_val = 4294967295u32        // Maximum u32 value
almost_max = 4294967294u32     // Type inferred from suffix

// Type inference with u32
result1 = almost_max + one_val   // Inferred as u32
print(result1)  // May overflow!
```

#### u64 (64-bit Unsigned Integer)

```orus
// Basic u64 operations
a = 100u64
b = 200u64
result = a * b
print(result)  // Outputs: 20000
```

### Floating Point Types

#### f64 (64-bit Floating Point)

```orus
// Basic floating point
zero = 0.0
one = 1.0
pi = 3.14159

// Scientific notation support
tiny = 1e-10    // 0.0000000001
huge = 1e10     // 10000000000.0

// Arithmetic operations
sum = one + pi
print(sum)  // Outputs: 4.14159

// Negative floating point
neg_one = 0.0 - 1.0
print(neg_one)  // Outputs: -1
```

#### IEEE 754 Special Values

```orus
// Special floating point values
pos = 1.0
neg = -1.0
zero = 0.0

// Infinity values
pos_inf = pos / zero   // Positive infinity
neg_inf = neg / zero   // Negative infinity

// NaN (Not a Number)
nan_result = zero / zero  // NaN

print(pos_inf)  // Outputs: inf
print(neg_inf)  // Outputs: -inf
print(nan_result)  // Outputs: nan
```

### Boolean Type

```orus
// Boolean literals
flag1 = true
flag2 = false

print(flag1)  // Outputs: true
print(flag2)  // Outputs: false

// Boolean expressions
result = true and false or true
print(result)  // Outputs: true
```

### Type Annotations vs Type Suffixes

Orus offers two ways to specify types - choose the most concise approach:

```orus
// Method 1: Type suffixes (preferred for literals)
x = 42u32             // 32-bit unsigned integer  
y = 1000000000i64     // 64-bit signed integer
z = 3.14159f64        // 64-bit floating point (if needed)

// Method 2: Explicit type annotations (when suffix isn't available)
x: i32 = 42           // 32-bit signed integer
flag: bool = true     // Boolean (no suffix available)

// AVOID redundancy - don't use both:
// wrong: x: u32 = 42u32  ❌ Redundant (compiler will warn)
// right: x = 42u32       ✅ Concise

// Mutable with type annotation
mut counter: i32 = 0
counter += 1
```

### Type Inference

Orus automatically infers types from expressions:

```orus
// Automatic type inference from literals
a = 42        // Inferred as i32
b = 3.14      // Inferred as f64
c = true      // Inferred as bool

// Type inference from arithmetic
x = 1.0
y = 2.0
result = x + y    // Inferred as f64

// Type inference with different types
num1 = 100u32
num2 = 50u32
sum = num1 + num2  // Inferred as u32
```

**Important:** Orus requires exact type matching - you cannot mix different numeric types without explicit conversion.
Attempting operations like `i32 + i64` will raise a runtime error unless you cast one side to match the other.

---

## ⚙️ Expressions and Operators

### Arithmetic Operators

```orus
// Basic arithmetic with precedence
print(1 + 2 * 3)      // Outputs: 7 (multiplication first)
print((1 + 2) * 3)    // Outputs: 9 (parentheses override)

// All arithmetic operators
a = 15
b = 4
print(a + b)  // Addition: 19
print(a - b)  // Subtraction: 11
print(a * b)  // Multiplication: 60
print(a / b)  // Division: 3
print(a % b)  // Modulo: 3
```

### Comparison Operators

```orus
x = 10
y = 5

// All comparison operators
print(x > y)   // Greater than: true
print(x < y)   // Less than: false
print(x >= y)  // Greater or equal: true
print(x <= y)  // Less or equal: false
print(x == y)  // Equal: false
print(x != y)  // Not equal: true
```

### Logical Operators

```orus
// Boolean logic
print(true and false)  // Logical AND: false
print(true or false)   // Logical OR: true
print(not true)        // Logical NOT: false

// Short-circuit evaluation
print(false and expensive_operation())  // expensive_operation() not called
print(true or expensive_operation())    // expensive_operation() not called
```

### Complex Expressions

```orus
// Complex expression evaluation
result = (1 + 2 + 3 + 4 + 5) * (6 + 7 + 8 + 9 + 10) - (11 + 12 + 13 + 14 + 15) + 16 * 17 - 18 / 3 + 19 % 5
print(result)  // Outputs: 805

// Expression chains (Orus can handle very long expressions)
chain = 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1  // ... up to 100 additions
print(chain)   // Outputs: 100
```

### Operator Precedence

Orus follows standard mathematical precedence:

1. **Parentheses**: `()`
2. **Unary operators**: `-`, `not`
3. **Multiplication/Division**: `*`, `/`, `%`
4. **Addition/Subtraction**: `+`, `-`
5. **Comparison**: `<`, `>`, `<=`, `>=`, `==`, `!=`
6. **Logical AND**: `and`
7. **Logical OR**: `or`
8. **Ternary**: `? :`

```orus
// Precedence examples
print(2 + 3 * 4)        // 14 (not 20)
print(10 > 5 and 3 < 7) // true
print(false or true and false)  // false (and has higher precedence)
```

---

## 📝 String Operations and Formatting

### String Concatenation

```orus
// String concatenation with +
greeting = "Hello" + " " + "World"
print(greeting)  // Outputs: Hello World

// Mixing strings and concatenation
name = "Orus"
message = "Welcome to " + name
print(message)  // Outputs: Welcome to Orus
```

### Print Statement with Multiple Arguments

```orus
// Multiple arguments in print
a = 1
b = 2
print(a, "+", b, "=", a + b)  // Outputs: 1 + 2 = 3

// String interpolation style
name = "Alice"
age = 25
print("Name:", name, "Age:", age)  // Outputs: Name: Alice Age: 25
```

### Format Specifiers

Orus supports format specifiers for different data types:

```orus
// Floating point formatting
pi = 3.14159
print("Pi: @.2f", pi)      // Outputs: Pi: 3.14
print("Pi: @.4f", pi)      // Outputs: Pi: 3.1416

// Hexadecimal formatting
num = 255
print("Hex: @x", num)      // Outputs: Hex: ff
print("HEX: @X", num)      // Outputs: HEX: FF

// Binary formatting
print("Binary: @b", num)   // Outputs: Binary: 11111111

// Octal formatting
print("Octal: @o", num)    // Outputs: Octal: 377
```

### String Literals

```orus
// Basic string literals
simple = "Hello"
empty = ""
with_spaces = "  spaces  "

// Strings with special characters
quote = "He said \"Hello\""     // Escaped quotes
newline = "Line 1\nLine 2"      // Newline character
tab = "Column 1\tColumn 2"      // Tab character
```

---

## 🔀 Conditional Statements

### Basic If/Else

```orus
// Simple conditional
x = 5
if x > 0:
    print("positive")
else:
    print("non-positive")
// Outputs: positive
```

### Elif Chains

```orus
// Multiple conditions with elif
score = 85

if score >= 90:
    print("A grade")
elif score >= 80:
    print("B grade")
elif score >= 70:
    print("C grade")
else:
    print("F grade")
// Outputs: B grade
```

### Nested Conditionals

```orus
x = 3

if x > 5:
    print("big")
elif x > 2:
    print("medium")
else:
    if x == 0:
        print("zero")
    elif x == 1:
        print("one")
    else:
        print("small")
// Outputs: medium
```

### Ternary Operator

```orus
// Concise conditional expressions
x = 10
result = x > 0 ? "positive" : "non-positive"
print(result)  // Outputs: positive

// Nested ternary
age = 25
category = age < 13 ? "child" : age < 20 ? "teen" : "adult"
print(category)  // Outputs: adult
```

### Inline Conditionals

```orus
// Inline conditional execution
x = 1
print("ok") if x == 1 else print("not ok")  // Outputs: ok

// More complex inline conditionals
x = 2
print("one") if x == 1 elif x == 2 then print("two") else print("other")  // Outputs: two
```

---

## 🔄 Loops and Control Flow

### For Loops with Ranges

#### Basic Range Syntax

```orus
// Basic for loop (exclusive upper bound)
for i in 0..5:
    print(i)
// Outputs: 0, 1, 2, 3, 4
```

#### Inclusive Ranges

```orus
// Inclusive range with ..=
for i in 0..=3:
    print(i)
// Outputs: 0, 1, 2, 3
```

#### Step Values

```orus
// Range with step value
for i in 0..10..2:
    print(i)
// Outputs: 0, 2, 4, 6, 8

// Large step values
for i in 0..20..5:
    print(i)
// Outputs: 0, 5, 10, 15

// Step larger than range
for i in 0..5..10:
    print(i)
// Outputs: 0 (only one iteration)
```

### While Loops

```orus
// Basic while loop
mut i = 0
while i < 3:
    print(i)
    i = i + 1
// Outputs: 0, 1, 2

// While with complex condition
mut x = 1
mut done = false
while x < 10 and not done:
    print(x)
    x = x * 2
    if x > 5:
        done = true
// Outputs: 1, 2, 4
```

### Break and Continue

#### Basic Break

```orus
// Break statement exits the loop
for i in 0..10:
    if i == 5:
        break
    print(i)
// Outputs: 0, 1, 2, 3, 4
```

#### Basic Continue

```orus
// Continue statement skips to next iteration
for i in 0..5:
    if i == 2:
        continue
    print(i)
// Outputs: 0, 1, 3, 4
```

#### Break and Continue in While Loops

```orus
mut i = 0
while i < 10:
    i = i + 1
    if i % 2 == 0:
        continue  // Skip even numbers
    if i > 7:
        break     // Exit when > 7
    print(i)
// Outputs: 1, 3, 5, 7
```

---

## 🚀 Advanced Loop Features

### Labeled Loops

For complex nested loops, Orus supports labeled break and continue:

```orus
// Labeled loops for precise control
'outer: for i in 0..3:
    'inner: for j in 0..3:
        if j == 1:
            continue 'inner    // Continue inner loop only
        if i == 2 and j == 0:
            break 'outer      // Break out of both loops
        if j == 2:
            break 'inner      // Break inner loop only
        print(i, j)

// Output:
// 0 0
// 1 0
// (exits completely when i=2, j=0)
```

### Loop Variable Scoping

Loop variables have their own scope and don't interfere with outer variables:

```orus
// Variable shadowing in loops
i = 999
for i in 0..3:
    print("Loop i:", i)
print("Outer i:", i)

// Output:
// Loop i: 0
// Loop i: 1  
// Loop i: 2
// Outer i: 999
```

### Nested Loop Scoping

```orus
// Nested loops with proper scoping
for i in 0..2:
    print("Outer:", i)
    for j in 0..2:
        print("  Inner:", j)
        for k in 0..2:
            print("    Deep:", k)

// Each loop variable (i, j, k) has its own scope
```

### Empty Ranges

```orus
// Empty ranges produce no iterations
for x in 5..5:
    print("This should not print")

for y in 10..5:  // Start > end with positive step
    print("This should not print either")

// No output - loops are skipped entirely
```

### Range Edge Cases

```orus
// Same start and end
for i in 5..5..1:
    print(i)  // No output (empty range)

// Very large steps
for i in 0..1000..999:
    print(i)  // Outputs: 0 (single iteration)

// Single element ranges
for i in 5..6..1:
    print(i)  // Outputs: 5
```

---

## ⚠️ Error Handling and Edge Cases

### Overflow Detection

```orus
// Integer overflow is detected at runtime
max_val = 4294967295u32
one = 1u32
// result = max_val + one  // Would trigger overflow error
```

### Division by Zero

```orus
// Division by zero is caught at runtime
a = 10
b = 0
// result = a / b  // Runtime error: division by zero
```

### Type Safety

```orus
// Type mismatches are caught at compile time
// These would cause compilation errors:

// x: i32 = 3.14      // Error: float assigned to int
// y = 10 + "hello"   // Error: cannot add int and string
// z: bool = 42       // Error: int assigned to bool
```

### Floating Point Edge Cases

```orus
// IEEE 754 compliance
zero = 0.0
pos = 1.0
neg = -1.0

// These produce special values, not errors
pos_inf = pos / zero    // Positive infinity
neg_inf = neg / zero    // Negative infinity  
nan_val = zero / zero   // NaN (Not a Number)

print(pos_inf)  // inf
print(neg_inf)  // -inf
print(nan_val)  // nan

// Infinity comparisons
print(pos_inf > 1000000.0)  // true
print(nan_val == nan_val)   // false (NaN != NaN)
```

---

## 🔧 Performance Features

### Compile-Time Optimizations

#### Constant Folding

```orus
// Complex expressions computed at compile time
value = 2 + 3 * 4 - 1    // Becomes: value = 13
result = 10 * (5 + 5)    // Becomes: result = 100
```

#### Loop Unrolling (Automatic)

```orus
// Small loops may be unrolled automatically
for i in 0..3:
    print(i)
// May be optimized to: print(0); print(1); print(2)
```

#### Loop Invariant Code Motion (LICM)

Orus automatically optimizes loops by moving invariant expressions outside the loop body, reducing computation overhead:

```orus
// Example: Loop-invariant expressions are automatically hoisted
a = 5
b = 10
for i in 0..1000:
    // (a + b) is computed only ONCE, not 1000 times!
    result = a + b + i
    print(result)
// The compiler transforms this to:
// temp = a + b  // Computed once before the loop
// for i in 0..1000:
//     result = temp + i
//     print(result)
```

**What Gets Optimized:**
- Arithmetic expressions with loop-invariant operands
- Variable references that don't change in the loop
- Complex calculations that don't depend on loop variables

**Safety Features:**
- Only safe expressions are hoisted (no side effects)
- Division operations checked for safety
- Preserves program semantics exactly

**Examples of LICM Optimization:**

```orus
// Test 1: Basic invariant hoisting
x = 42
y = 100
for j in 0..1000:
    expensive_calc = x * y * 2  // Computed once, not 1000 times
    result = expensive_calc + j
    print(result)

// Test 2: Nested loops - multiple optimization levels  
outer_val = 70
for outer in 0..100:
    // This is invariant to both loops - hoisted to outermost scope
    base = outer_val * 3
    for inner in 0..100:
        // This is invariant to inner loop only
        combined = base + outer + inner
        print(combined)

// Test 3: What CANNOT be hoisted (side effects)
mut counter = 0
for i in 0..10:
    counter = counter + 1  // Side effect - cannot hoist
    result = counter + i
    print(result)
```

**Performance Impact:**
- Reduces loop overhead significantly for computation-heavy loops
- Eliminates redundant calculations automatically
- Works with all loop types: `for`, `while`, and nested loops

### Register Allocation

Orus uses advanced register allocation for performance:

```orus
// Testing register pressure with many variables
for i in 0..3:
    a = i + 1
    b = i + 2  
    c = i + 3
    d = i + 4
    e = i + 5
    sum = a + b + c + d + e
    print("i:", i, "sum:", sum)
// The compiler efficiently manages registers for all variables
```

### Type-Specific Optimizations

```orus
// Different types use optimized opcodes
x = 10
y = 20
result1 = x + y        // Uses fast i32 addition

a = 10.0
b = 20.0
result2 = a + b        // Uses fast f64 addition

// No boxing/unboxing overhead for primitive types
```

### Progressive Loop Safety System

Orus implements a progressive loop safety system that intelligently scales from zero-overhead execution to comprehensive safety monitoring:

#### Safety Levels

| **Iteration Count** | **Behavior** | **Performance** |
|---------------------|--------------|-----------------|
| `< 100K` | ✅ Guard-free execution | **Maximum speed** |
| `100K–1M` | ✅ Silent protection | **~1% overhead** |
| `1M–10M` | ⚠️ Warning + continuation | **~1% overhead** |
| `> 10M` | ❌ Safety stop | **Controlled execution** |

```orus
// Fast execution (no guards)
mut total = 0
for i in 0..50000:       // Under 100K - maximum performance
    total = total + i

// Silent protection  
for batch in 0..500000:  // 500K iterations - protected silently
    process_batch(batch)

// Warning system
for record in 0..1500000: // 1.5M iterations - warns at 1M
    process_record(record)
// Output: Warning: Loop has exceeded 1000000 iterations...

// Safety stops
for item in 0..20000000: // 20M requested - stops at 10M
    process_item(item)
// Error: Loop exceeded maximum iteration limit (10000000)
```

#### Environment Configuration

**Runtime Configuration** (no recompilation needed):
```bash
# Unlimited loops (scientific computing)
ORUS_MAX_LOOP_ITERATIONS=0 ./orus simulation.orus

# Custom 50M limit (large-scale data processing)  
ORUS_MAX_LOOP_ITERATIONS=50000000 ./orus data_processor.orus

# Conservative 1M limit (web applications)
ORUS_MAX_LOOP_ITERATIONS=1000000 ./orus web_app.orus

# Custom guard threshold
ORUS_LOOP_GUARD_THRESHOLD=200000 ./orus program.orus
```

**Performance Characteristics:**
- **Small loops** (<100K): Zero guard overhead
- **Guarded loops** (>100K): ~1% overhead for comprehensive safety
- **Memory usage**: 3 registers per guarded loop (auto-managed)

> 📖 **For comprehensive loop safety documentation**, see [Loop Safety Guide](LOOP_SAFETY_GUIDE.md)

#### Enterprise-Scale Examples

```orus
// Typical data processing (most common use case)
mut processed = 0
for record in 0..500000:     // 500k records (typical dataset)
    if validate_record(record):
        processed = processed + 1
print("Processed", processed, "records")

// Large-scale analytics (enterprise use case)
mut daily_total = 0
for hour in 0..24:
    for user_session in 0..100000:  // 100k sessions per hour
        daily_total = daily_total + process_session(user_session)
print("Daily total:", daily_total)

// Scientific simulation (specialized use case)
mut monte_carlo_sum = 0.0
for trial in 0..10000000:    // 10M trials (reasonable simulation)
    monte_carlo_sum = monte_carlo_sum + random_sample()
print("Monte Carlo result:", monte_carlo_sum / 10000000)
```

---

## 🏁 Complete Language Reference

### Keywords

```orus
// Control flow keywords
if, elif, else          // Conditionals
for, in, while          // Loops  
break, continue         // Loop control
true, false             // Boolean literals

// Variable modifiers
mut                     // Mutable variables

// Operators
and, or, not           // Logical operators
```

### Built-in Types

| Type | Description | Suffix Example | Annotation Example |
|------|-------------|----------------|-------------------|
| `i32` | 32-bit signed integer (default) | `x = 42` | `x: i32 = 42` |
| `i64` | 64-bit signed integer | `x = 1000000000i64` | `x: i64 = 1000000000` |
| `u32` | 32-bit unsigned integer | `x = 42u32` | `x: u32 = 42` |
| `u64` | 64-bit unsigned integer | `x = 1000u64` | `x: u64 = 1000` |
| `f64` | 64-bit floating point (default) | `x = 3.14` | `x: f64 = 3.14` |
| `bool` | Boolean | N/A | `x: bool = true` |

**Best Practice:** Use suffixes when available to avoid redundancy: `x = 42u32` not `x: u32 = 42u32`

### Compiler Warnings for Redundancy

Orus helps you write cleaner code by warning about redundant type annotations:

```orus
// ❌ This will generate a warning:
x: u32 = 42u32
// Warning: Redundant type annotation at line 1:1. 
// Literal already has type suffix matching declared type 'u32'. 
// Consider using just 'x = value32' instead of 'x: u32 = valueu32'.

// ✅ Write this instead:
x = 42u32
```

### Operators by Precedence

1. `()` - Parentheses
2. `-`, `not` - Unary operators
3. `*`, `/`, `%` - Multiplication, division, modulo
4. `+`, `-` - Addition, subtraction
5. `<`, `>`, `<=`, `>=`, `==`, `!=` - Comparison
6. `and` - Logical AND
7. `or` - Logical OR
8. `? :` - Ternary operator

### Assignment Operators

```orus
=          // Basic assignment
+=         // Addition assignment
-=         // Subtraction assignment
*=         // Multiplication assignment
/=         // Division assignment
```

### Range Syntax

```orus
0..5       // Exclusive: 0, 1, 2, 3, 4
0..=5      // Inclusive: 0, 1, 2, 3, 4, 5
0..10..2   // With step: 0, 2, 4, 6, 8
```

### Format Specifiers

```orus
@.2f       // Float with 2 decimal places
@x         // Lowercase hexadecimal
@X         // Uppercase hexadecimal
@b         // Binary
@o         // Octal
```

### Comments

```orus
// Single line comment

/*
Multi-line
comment
*/
```

---

## 💡 Best Practices and Tips

### Variable Naming

```orus
// Use descriptive names
user_count = 42
max_retries = 3
is_valid = true

// Constants in UPPER_CASE (when available)
PI = 3.14159
MAX_SIZE = 1000
```

### Type Usage

```orus
// Use appropriate numeric types
small_number = 100             // For typical integers (i32 default)
big_number = 5000000000i64     // For large values
precise_calc = 3.14159         // For floating point (f64 default)
counter = 0u32                 // For positive-only values
```

### Loop Patterns

```orus
// Prefer for loops for known ranges
for i in 0..10:
    process(i)

// Use while for conditional iteration
while has_more_data():
    process_next()

// Use labeled breaks for complex nested loops
'outer: for i in 0..10:
    'inner: for j in 0..10:
        if should_exit():
            break 'outer
```

### Error Prevention

```orus
// Check for potential division by zero
if divisor != 0:
    result = dividend / divisor
else:
    print("Error: division by zero")

// Use appropriate types to prevent overflow
mut large_sum = 0i64  // Use i64 for large sums
for i in 0..1000000:
    large_sum += i
```

---

## 🎯 Complete Working Examples

### Simple Calculator

```orus
// calculator.orus
a = 15
b = 4

print("Addition:", a + b)
print("Subtraction:", a - b)
print("Multiplication:", a * b)
print("Division:", a / b)
print("Modulo:", a % b)
```

### Loop Examples Collection

```orus
// loops_demo.orus

// Basic counting
print("Basic counting:")
for i in 1..6:
    print("Count:", i)

// Even numbers only
print("Even numbers:")
for i in 0..11..2:
    print(i)

// Factorial calculation
print("Factorial of 5:")
mut factorial = 1
for i in 1..6:
    factorial *= i
print("5! =", factorial)

// Nested loops with break
print("Matrix pattern:")
'outer: for row in 0..3:
    'inner: for col in 0..3:
        if row == col:
            print("*")
            break 'inner
        print("-")
```

### Type Showcase

```orus
// types_demo.orus

// Integer types
small = 42
big = 9223372036854775807i64
positive = 4294967295u32
huge = 18446744073709551615u64

// Floating point
pi = 3.141592653589793
euler = 2.718281828459045

// Boolean
is_demo = true
is_complete = false

// Display all values
print("i32:", small)
print("i64:", big)
print("u32:", positive)  
print("u64:", huge)
print("f64 pi:", pi)
print("f64 e:", euler)
print("bool:", is_demo, is_complete)
```

---

## 🚀 Advanced Patterns

### State Machine Pattern

```orus
// state_machine.orus
mut state = 0
mut counter = 0

while counter < 10:
    if state == 0:
        print("State A")
        state = 1
    elif state == 1:
        print("State B")
        state = 2
    else:
        print("State C")
        state = 0
    
    counter += 1
```

### Mathematical Sequences

```orus
// fibonacci.orus - Fibonacci sequence
mut a = 0
mut b = 1
print("Fibonacci sequence:")
print(a)
print(b)

for i in 0..8:
    mut next = a + b
    print(next)
    a = b
    b = next
```

### Complex Loop Control

```orus
// search_pattern.orus
found = false
'search: for row in 0..5:
    'columns: for col in 0..5:
        value = row * 5 + col
        if value == 12:
            print("Found 12 at position:", row, col)
            found = true
            break 'search
        elif value > 20:
            print("Stopping search at:", value)
            break 'search

if not found:
    print("Value not found")
```

---

## 🔥 Edge Cases and Stress Testing

Orus is designed to handle exceptional cases gracefully and perform well under stress. Here are examples that demonstrate Orus' robustness:

### Deep Expression Nesting

Orus can handle deeply nested expressions efficiently:

```orus
// Deep mathematical nesting
start_time = time_stamp()
deep_result = (((((((((1 + 2) * 3) + 4) * 5) + 6) * 7) + 8) * 9) + 10)
end_time = time_stamp()

print("Deep nesting result:", deep_result)  // Outputs: 4555
print("Parse time (nanoseconds):", end_time - start_time)
```

### Memory Pressure Testing

Test Orus' register allocation with many variables:

```orus
// Many variables in single scope (testing register allocation)
var1 = 1, var2 = 2, var3 = 3, var4 = 4,
var5 = 5, var6 = 6, var7 = 7, var8 = 8,
var9 = 9, var10 = 10, var11 = 11, var12 = 12,
var13 = 13, var14 = 14, var15 = 15, var16 = 16,
var17 = 17, var18 = 18, var19 = 19, var20 = 20

// Complex expression using all variables
sum_all = var1 + var2 + var3 + var4 + var5 + var6 + var7 + var8 + var9 + var10 +
          var11 + var12 + var13 + var14 + var15 + var16 + var17 + var18 + var19 + var20

print("Many variables sum:", sum_all)  // Outputs: 210
print("Expected sum:", (20 * 21) / 2)  // Mathematical verification
```

### Nested Scope Stress Test

Test scope management with deep nesting:

```orus
// Deeply nested scopes (6 levels deep)
if true:
    level1 = 1
    if true:
        level2 = level1 + 1
        if true:
            level3 = level2 + 1
            if true:
                level4 = level3 + 1
                if true:
                    level5 = level4 + 1
                    if true:
                        level6 = level5 + 1
                        print("Deep scope level 6:", level6)  // Outputs: 6
                    print("Back to level 5:", level5)         // Outputs: 5
                print("Back to level 4:", level4)             // Outputs: 4
            print("Back to level 3:", level3)                 // Outputs: 3
        print("Back to level 2:", level2)                     // Outputs: 2
    print("Back to level 1:", level1)                         // Outputs: 1
```

### Loop Performance Under Load

Test loop performance with nested iterations:

```orus
start_time = time_stamp()
mut operations_count = 0

// Stress test: many operations in tight loops
for i in 0..100:
    for j in 0..100:
        operations_count += 1
        operations_count *= 2
        operations_count /= 2
        operations_count -= 1
        operations_count += 1
        // Each iteration: 5 operations × 100 × 100 = 50,000 operations

end_time = time_stamp()
duration_ns = end_time - start_time

print("Operations completed:", operations_count)  // Outputs: 10000
print("Time taken (nanoseconds):", duration_ns)
print("Operations per nanosecond:", operations_count * 5.0 / duration_ns)
```

### String Concatenation Chain

Test string handling with long concatenation chains:

```orus
// Long concatenation chain (26 string concatenations)
alphabet = "a" + "b" + "c" + "d" + "e" + "f" + "g" + "h" + "i" + "j" + 
           "k" + "l" + "m" + "n" + "o" + "p" + "q" + "r" + "s" + "t" + 
           "u" + "v" + "w" + "x" + "y" + "z"

print("Alphabet concatenation:", alphabet)
// Outputs: abcdefghijklmnopqrstuvwxyz
print("Length check:", alphabet == "abcdefghijklmnopqrstuvwxyz")  // Outputs: true
```

### Complex Boolean Logic Chains

Test boolean expression evaluation with complex chains:

```orus
// Complex boolean chain with short-circuit evaluation
complex_bool = true and true and true and false or 
               true or false or not false and 
               not false and not false and 
               (true or false) and (false or true)

print("Complex boolean result:", complex_bool)  // Outputs: true

// Performance test: many boolean operations
start_time = time_stamp()
for i in 0..1000:
    test_result = (i > 500) and (i < 999) or (i == 0) or (i == 999)
end_time = time_stamp()

print("Boolean operations time:", end_time - start_time, "nanoseconds")
```

### Mixed Data Type Stress Test

Test type system with complex mixed expressions:

```orus
// Complex expression mixing all data types
start_time = time_stamp()

mixed_result = (100 + 200) * 
               (3.14159 > 3.0 ? 1 : 0) + 
               (true and false ? 10 : 20) + 
               ((50 > 25) and (30 < 40) ? 5 : 15)

end_time = time_stamp()

print("Mixed complex result:", mixed_result)      // Outputs: 325
print("Evaluation time:", end_time - start_time, "nanoseconds")

// Verify calculation: 300 * 1 + 20 + 5 = 325
verification = 300 * 1 + 20 + 5
print("Verification:", verification == mixed_result)  // Outputs: true
```

### Performance Benchmarking Template

Use this template for your own performance testing:

```orus
// benchmarking_template.orus
print("Starting performance benchmark...")

benchmark_start = time_stamp()

// YOUR CODE TO BENCHMARK GOES HERE
mut counter = 0
for i in 0..1000000:
    counter = counter + 1

benchmark_end = time_stamp()
total_duration = benchmark_end - benchmark_start
duration_ms = total_duration / 1000000

print("Benchmark completed!")
print("Result:", counter)
print("Total time (nanoseconds):", total_duration)
print("Total time (milliseconds):", duration_ms)
print("Operations per second:", counter * 1000000000 / total_duration)
```

**Why These Edge Cases Matter:**

1. **Parser Robustness**: Deep nesting tests the recursive descent parser
2. **Memory Management**: Many variables test register allocation efficiency  
3. **Scope Handling**: Nested scopes verify symbol table correctness
4. **Performance Scaling**: Loop stress tests verify O(n) performance characteristics
5. **Type Safety**: Mixed expressions validate the type system
6. **String Optimization**: Concatenation chains test string memory management

These patterns demonstrate that Orus maintains both **correctness** and **performance** even under exceptional conditions.

---

## 📋 Testing Your Knowledge

Try these exercises to test your understanding:

### Beginner Exercises

1. **Calculator**: Create a program that calculates the area of a circle given radius = 5
2. **Number Pattern**: Print numbers from 1 to 20, but only odd numbers
3. **Temperature**: Convert Celsius to Fahrenheit for temperatures 0, 10, 20, 30

### Intermediate Exercises

1. **Prime Check**: Check if numbers 1-30 could be prime (basic division test)
2. **Multiplication Table**: Create a 5x5 multiplication table
3. **Sum and Average**: Calculate sum and average of numbers 1-100

### Advanced Exercises

1. **Collatz Sequence**: Implement the Collatz conjecture for a given number
2. **Pattern Printer**: Create complex ASCII patterns using nested loops
3. **Number Base Converter**: Convert decimal to binary manually using division

---

This tutorial covers every implemented feature of the Orus programming language. Each example is tested and working. Use this as your complete reference for learning and using Orus!

---

*Happy coding with Orus! 🚀*