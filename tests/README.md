# Orus Test Suite

This directory contains the comprehensive test suite for the Orus programming language VM.

## Structure

```
tests/
├── types/          # Type system tests (i32, i64, u32, u64, f64, etc.)
├── conditionals/   # Conditional statements and expressions
├── control_flow/   # Loops, break, continue statements
├── loop_safety/    # 🔒 Loop safety system comprehensive tests
├── expressions/    # Expressions and operators
├── formatting/     # String formatting and output
├── literals/       # Literal values and constants
├── variables/      # Variable declarations and assignments
├── c/             # C-level VM implementation tests
├── benchmarks/    # Performance benchmarks (empty, moved to type-specific dirs)
├── edge_cases/    # Edge cases (empty, moved to category-specific dirs)
└── errors/        # Error tests (empty, moved to category-specific dirs)
```

## Organization Principles

Each test category follows a consistent structure:

### Standard Subdirectories
- **`basic/`** - Core functionality tests
- **`arithmetic/`** - Arithmetic and mathematical operations
- **`benchmarks/`** - Performance and optimization tests
- **`boundaries/`** - Edge cases with boundary conditions
- **`errors/`** - Error conditions and exception handling
- **`edge_cases/`** - Complex edge cases and unusual scenarios
- **`advanced/`** - Advanced patterns and complex usage
- **`integration/`** - Integration tests with other features

### Test Categories

#### Types (`types/`)
Comprehensive tests for all numeric types, type inference, and conversions.
- Individual type directories: `i32/`, `i64/`, `u32/`, `u64/`, `f64/`
- `type_inference/` - Automatic type inference tests
- `conversions/` - Type conversion tests

#### Conditionals (`conditionals/`)
Tests for if/else statements, ternary operators, and boolean expressions.

#### Control Flow (`control_flow/`)
Tests for loops (for, while), break/continue statements, and control flow patterns.

#### Loop Safety (`loop_safety/`) 🔒
Comprehensive tests for the progressive loop safety system:
- **Threshold Testing**: Edge cases around 100K, 1M, and 10M iteration boundaries
- **Environment Variables**: `ORUS_MAX_LOOP_ITERATIONS` and `ORUS_LOOP_GUARD_THRESHOLD` configuration
- **Loop Type Consistency**: For-loops vs while-loops behavior validation
- **Nested Loops**: Independent guard tracking and multiple warnings
- **Stress Testing**: Register allocation and very large iteration counts
- **Static vs Dynamic**: Compile-time vs runtime loop analysis validation

Run: `cd loop_safety && ./run_loop_safety_tests.sh`

#### Expressions (`expressions/`)
Tests for binary operations, boolean logic, comparison operators, and complex expressions.

#### Formatting (`formatting/`)
Tests for string formatting, output operations, and text manipulation.

#### Literals (`literals/`)
Tests for literal values: integers, floats, strings, booleans, and constants.

#### Variables (`variables/`)
Tests for variable declarations, assignments, mutability, and scoping.

#### C Tests (`c/`)
Low-level tests for the VM implementation written in C.

## Running Tests

Each category can be tested independently by running the appropriate Orus files:

```bash
# Run all type tests
./orus tests/types/i32/basic/*.orus

# Run conditional tests
./orus tests/conditionals/basic/*.orus

# Run control flow tests  
./orus tests/control_flow/basic/*.orus
```

## Test Naming Conventions

- Use descriptive names that indicate what is being tested
- Include the test type in the filename when helpful
- Group related tests with common prefixes
- Examples:
  - `i32_basic.orus` - Basic i32 functionality
  - `if_nested.orus` - Nested if statements
  - `while_basic.orus` - Basic while loop functionality

## Adding New Tests

When adding new tests:

1. Choose the appropriate category directory
2. Place in the most specific subdirectory that fits
3. Follow the naming conventions
4. Add documentation comments in the test file
5. Update the category README if adding new test patterns

## Categories Moved to Organized Structure

The following directories are now empty as their contents have been moved:
- `benchmarks/` → Moved to category-specific `benchmarks/` subdirectories
- `edge_cases/` → Moved to category-specific `edge_cases/` subdirectories  
- `errors/` → Moved to category-specific `errors/` subdirectories
