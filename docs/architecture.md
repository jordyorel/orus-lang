# Orus Language Architecture

## Overview

The Orus programming language is implemented as a register-based virtual machine with a multi-stage compilation pipeline. This document describes the overall architecture and design decisions.

## Core Components

### 1. Lexical Analysis (`src/compiler/lexer.c`)

The lexer converts source code into a stream of tokens. It handles:
- Keywords and identifiers
- Literals (numbers, strings, booleans)
- Operators and punctuation
- Comments and whitespace

### 2. Syntax Analysis (`src/compiler/parser.c`)

The parser builds an Abstract Syntax Tree (AST) from tokens using a recursive descent parser with Pratt parsing for expressions. It supports:
- Precedence-driven expression parsing
- Error recovery and reporting
- Type annotations
- Generic parameters

### 3. Abstract Syntax Tree (`src/core/ast.c`)

The AST represents the program structure with typed nodes for:
- Expressions (binary, unary, calls, literals)
- Statements (declarations, control flow, blocks)
- Types (primitives, arrays, functions, generics)

### 4. Compilation (`src/compiler/compiler.c`)

The compiler transforms the AST into register-based bytecode:
- Register allocation and management
- Type checking and inference
- Code generation for all constructs
- Optimization passes

### 5. Virtual Machine (`src/vm/vm.c`)

The register-based VM executes bytecode with:
- 256 registers per frame
- Direct register addressing
- Built-in type system
- Memory management with GC
- Exception handling

## Register-Based VM Design

### Why Register-Based?

Unlike stack-based VMs, register-based VMs offer:

1. **Fewer Instructions**: Complex expressions require fewer bytecode instructions
2. **Better Performance**: Reduced instruction dispatch overhead
3. **Optimization Opportunities**: Register allocation enables advanced optimizations
4. **Clearer Model**: More intuitive mapping to modern CPU architectures

### Register Allocation

The compiler uses a simple register allocator:
- Linear scan allocation for local variables
- Temporary registers for expression evaluation
- Register reuse for dead values
- Spilling to memory when registers are exhausted

### Instruction Set

Instructions operate directly on registers:

```
OP_LOAD_CONST R1, #42        # Load constant 42 into R1
OP_ADD_I32_R  R2, R1, R0     # R2 = R1 + R0 (32-bit integer add)
OP_PRINT_R    R2             # Print value in R2
```

## Type System

### Static Typing

Orus uses static typing with:
- Primitive types: `i32`, `i64`, `u32`, `u64`, `f64`, `bool`, `string`
- Composite types: arrays, structs, functions
- Generic types with constraints
- Type inference where possible

### Type Checking

Type checking occurs during compilation:
- Expression type propagation
- Function signature validation
- Generic instantiation
- Implicit conversions

## Memory Management

### Object Model

All heap objects share a common header:
```c
struct Obj {
    struct Obj* next;    // GC linked list
    bool isMarked;       // Mark bit for GC
};
```

### Garbage Collection

Mark-and-sweep garbage collector:
1. Mark phase: traverse from roots
2. Sweep phase: free unmarked objects
3. Compaction: optional memory defragmentation

## Error Handling

### Compile-Time Errors

- Syntax errors with precise location
- Type errors with helpful messages
- Semantic errors (undefined variables, etc.)

### Runtime Errors

- Type mismatches
- Division by zero
- Index out of bounds
- Stack overflow

### Exception Handling

Built-in try/catch mechanism:
```orus
try {
    risky_operation();
} catch error {
    print("Error: " + error);
}
```

## Module System

### Import/Export

Modules can export functions and types:
```orus
// math.orus
pub fn add(a: i32, b: i32) -> i32 { return a + b; }

// main.orus
import math;
let result = math.add(1, 2);
```

### Module Loading

- Static linking at compile time
- Dynamic loading for plugins
- Circular dependency detection

## Optimization Opportunities

### Current

- Register allocation
- Constant folding
- Dead code elimination

### Future

- Inline caching for dynamic dispatch
- Just-in-time compilation
- Profile-guided optimization
- Advanced register allocation

## Performance Characteristics

### Benchmarks

Register-based VM shows ~2-3x performance improvement over equivalent stack-based implementation for expression-heavy code.

### Memory Usage

- Lower instruction count reduces memory usage
- Register allocation reduces temporary allocations
- GC overhead comparable to other managed languages

## Extensibility

### Native Functions

Easy FFI for calling C functions:
```c
static Value native_sqrt(int argCount, Value* args) {
    if (argCount != 1 || !IS_F64(args[0])) {
        return ERROR_VAL("sqrt requires one number argument");
    }
    return F64_VAL(sqrt(AS_F64(args[0])));
}
```

### Custom Types

Support for user-defined types and operators through the type system.

## Future Directions

1. **JIT Compilation**: Hot path compilation to native code
2. **Parallel GC**: Concurrent garbage collection
3. **SIMD Support**: Vector operations for numeric computing
4. **Debugging Tools**: Interactive debugger and profiler
5. **IDE Integration**: Language server protocol support
