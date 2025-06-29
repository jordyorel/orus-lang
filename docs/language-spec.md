# Orus Language Specification

## Version 0.1.0

This document defines the syntax and semantics of the Orus programming language.

## Table of Contents

1. [Lexical Structure](#lexical-structure)
2. [Types](#types)
3. [Expressions](#expressions)
4. [Statements](#statements)
5. [Functions](#functions)
6. [Control Flow](#control-flow)
7. [Modules](#modules)
8. [Error Handling](#error-handling)

## Lexical Structure

### Comments

```orus
// Single-line comment

/*
Multi-line
comment
*/
```

### Identifiers

Identifiers start with a letter or underscore, followed by letters, digits, or underscores:

```orus
identifier
_private
value42
MY_CONSTANT
```

### Keywords

```
and     break   catch   continue  else    elif    false
fn      for     if      import    let     not     or
pub     return  struct  true      try     type    var
while
```

### Literals

#### Integer Literals

```orus
42          // i32
42i64       // i64
42u32       // u32
42u64       // u64
0x2A        // hexadecimal
0o52        // octal
0b101010    // binary
```

#### Floating-Point Literals

```orus
3.14
3.14f64
1e10
1.5e-4
```

#### String Literals

```orus
"Hello, World!"
"String with \"quotes\""
"String with \n newlines"
```

#### Boolean Literals

```orus
true
false
```

## Types

### Primitive Types

- `i32`: 32-bit signed integer
- `i64`: 64-bit signed integer
- `u32`: 32-bit unsigned integer
- `u64`: 64-bit unsigned integer
- `f64`: 64-bit floating-point
- `bool`: Boolean (true/false)
- `string`: UTF-8 string

### Array Types

```orus
let numbers: [i32] = [1, 2, 3, 4, 5];
let matrix: [[f64]] = [[1.0, 2.0], [3.0, 4.0]];
```

### Function Types

```orus
let add: fn(i32, i32) -> i32 = fn(a, b) { return a + b; };
```

### Struct Types

```orus
struct Point {
    x: f64,
    y: f64
}
```

### Generic Types

```orus
struct Array<T> {
    data: [T],
    length: i32
}
```

## Expressions

### Literals

```orus
42          // integer
3.14        // float
"hello"     // string
true        // boolean
[1, 2, 3]   // array
```

### Binary Expressions

#### Arithmetic

```orus
a + b       // addition
a - b       // subtraction
a * b       // multiplication
a / b       // division
a % b       // modulo
```

#### Comparison

```orus
a == b      // equality
a != b      // inequality
a < b       // less than
a <= b      // less than or equal
a > b       // greater than
a >= b      // greater than or equal
```

#### Logical

```orus
a and b     // logical AND
a or b      // logical OR
not a       // logical NOT
```

#### Bitwise

```orus
a & b       // bitwise AND
a | b       // bitwise OR
a ^ b       // bitwise XOR
a << b      // left shift
a >> b      // right shift
~a          // bitwise NOT
```

### Unary Expressions

```orus
-a          // negation
not a       // logical NOT
~a          // bitwise NOT
```

### Function Calls

```orus
function_name(arg1, arg2)
object.method(arg1, arg2)
```

### Array Indexing

```orus
array[index]
matrix[row][col]
```

### Range Expressions

```orus
0..10       // range from 0 to 9
0..=10      // range from 0 to 10 (inclusive)
```

## Statements

### Variable Declarations

```orus
let x: i32 = 42;          // immutable variable
var y: i32 = 42;          // mutable variable
let z = 42;               // type inference
```

### Assignments

```orus
x = 42;                   // simple assignment
x += 5;                   // compound assignment
array[0] = value;         // indexed assignment
```

### Expression Statements

```orus
print("Hello, World!");
calculate();
```

### Block Statements

```orus
{
    let x = 42;
    print(x);
}
```

## Functions

### Function Declarations

```orus
fn add(a: i32, b: i32) -> i32 {
    return a + b;
}
```

### Generic Functions

```orus
fn identity<T>(value: T) -> T {
    return value;
}
```

### Public Functions

```orus
pub fn public_function() {
    // visible to other modules
}
```

## Control Flow

### If Statements

```orus
if condition {
    // then branch
} else if other_condition {
    // elif branch
} else {
    // else branch
}
```

### While Loops

```orus
while condition {
    // loop body
}
```

### For Loops

```orus
// Range-based for loop
for i in 0..10 {
    print(i);
}

// Array iteration
for item in array {
    print(item);
}

// C-style for loop
for i = 0; i < 10; i += 1 {
    print(i);
}
```

### Break and Continue

```orus
for i in 0..10 {
    if i == 5 {
        continue;  // skip to next iteration
    }
    if i == 8 {
        break;     // exit loop
    }
    print(i);
}
```

## Modules

### Import Statements

```orus
import math;                    // import entire module
import math.{sin, cos};         // import specific functions
import math as m;               // import with alias
```

### Export Declarations

```orus
pub fn public_function() { }    // public function
pub struct Point { x: f64, y: f64 }  // public struct

fn private_function() { }       // private (default)
```

## Error Handling

### Try-Catch

```orus
try {
    risky_operation();
} catch error {
    print("Error occurred: " + error);
}
```

### Throwing Errors

```orus
fn divide(a: f64, b: f64) -> f64 {
    if b == 0.0 {
        throw "Division by zero";
    }
    return a / b;
}
```

## Operator Precedence

From highest to lowest precedence:

1. Primary: `()`, `[]`, `.`
2. Unary: `not`, `-`, `~`
3. Multiplicative: `*`, `/`, `%`
4. Additive: `+`, `-`
5. Shift: `<<`, `>>`
6. Bitwise AND: `&`
7. Bitwise XOR: `^`
8. Bitwise OR: `|`
9. Comparison: `<`, `<=`, `>`, `>=`
10. Equality: `==`, `!=`
11. Logical AND: `and`
12. Logical OR: `or`
13. Assignment: `=`, `+=`, `-=`, etc.

## Type System

### Type Inference

The compiler can infer types in many contexts:

```orus
let x = 42;           // inferred as i32
let y = 3.14;         // inferred as f64
let z = [1, 2, 3];    // inferred as [i32]
```

### Type Conversions

Explicit type conversions using `as`:

```orus
let x: i64 = 42i32 as i64;
let y: f64 = 42 as f64;
```

### Generic Constraints

```orus
fn compare<T: Comparable>(a: T, b: T) -> bool {
    return a < b;
}
```

## Memory Management

### Automatic Memory Management

Orus uses garbage collection for automatic memory management. Objects are automatically freed when no longer reachable.

### Object Lifecycle

1. Objects are allocated on the heap
2. References are tracked automatically
3. Garbage collector runs periodically
4. Unreachable objects are freed

## Standard Library

### Built-in Functions

- `print(value)`: Print a value to stdout
- `len(array)`: Get array length
- `type(value)`: Get type information

### Math Functions

- `abs(x)`: Absolute value
- `sqrt(x)`: Square root
- `sin(x)`, `cos(x)`, `tan(x)`: Trigonometric functions

### String Functions

- `concat(a, b)`: Concatenate strings
- `substring(str, start, length)`: Extract substring
- `to_string(value)`: Convert to string

## Future Features

Features planned for future versions:

- Pattern matching
- Async/await
- Traits/interfaces
- More collection types
- Standard library expansion
- Package manager

---

This specification is subject to change as the language evolves.
