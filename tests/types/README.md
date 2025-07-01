# Type Tests

This directory contains all type-related tests organized by type and test category.

## Structure

```
types/
├── i32/           # 32-bit signed integer tests
├── i64/           # 64-bit signed integer tests  
├── u32/           # 32-bit unsigned integer tests
├── u64/           # 64-bit unsigned integer tests
├── f64/           # 64-bit floating point tests
├── type_inference/ # Type inference tests
└── conversions/   # Type conversion tests
```

Each type directory contains:
- `basic/` - Basic functionality tests
- `arithmetic/` - Arithmetic operation tests
- `boundaries/` - Boundary condition tests (min/max values)
- `errors/` - Error condition tests (overflow, division by zero, etc.)
- `benchmarks/` - Performance benchmark tests

## Test Categories

### Basic Tests
Core functionality and simple operations for each type.

### Arithmetic Tests
Comprehensive testing of arithmetic operations (+, -, *, /, %).

### Boundary Tests
Testing edge cases like maximum values, minimum values, and boundary conditions.

### Error Tests
Testing error conditions like:
- Division by zero
- Overflow/underflow
- Type mismatches

### Benchmark Tests
Performance tests for comparing type operations.

### Type Inference Tests
Testing the compiler's ability to infer types automatically.

### Conversion Tests
Testing explicit and implicit type conversions between different numeric types.
