# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build and Development Commands

### Essential Commands
```bash
# Build the Orus interpreter (includes both switch and goto dispatchers)
make clean && make

# Run comprehensive test suite with color-coded output (100+ tests)
make test

# Run cross-language performance benchmarks (vs Python, Node.js, Lua)
make benchmark

# Start REPL for interactive development
./orus

# Execute Orus source files
./orus filename.orus

# Clean build artifacts
make clean
```

### Test Categories
The test suite is organized into comprehensive categories with specific behaviors:
- **Basic tests**: Must pass (expressions, variables, literals)
- **Type system tests**: Type inference, annotations, casting, string conversions
- **Type safety tests**: Expected to fail with friendly error messages (in `type_safety_fails/`)
- **Division by zero tests**: Expected to fail (runtime safety)
- **Edge cases**: Boundary conditions and arithmetic limits

## High-Level Architecture

### Core Design Principles
- **Register-based VM**: 256 registers, outperforms Python by 7x, Node.js by 11x
- **Single-pass compilation**: Fast compilation for development speed
- **Performance-first**: Zero-cost abstractions, arena allocation, computed-goto dispatch
- **Type safety**: Static typing with inference, explicit casting required
- **Friendly error messages**: Mentor-like guidance following Rust/Elm patterns

### Key Components

#### VM Core (`src/vm/`)
- **Dual dispatch system**: Switch-based and computed-goto implementations compiled together
- **vm.c**: Main VM runtime with register management
- **vm_dispatch_*.c**: Specialized dispatch implementations for performance
- **vm_arithmetic.c, vm_typed_ops.c**: Type-specific operations with comprehensive casting
- **Memory management**: Mark-and-sweep GC with object pooling

#### Compiler Pipeline (`src/compiler/`)
- **Single-pass design**: lexer.c → parser.c → compiler.c → bytecode
- **parser.c**: Precedence climbing parser with AST generation
- **compiler.c**: Bytecode emission with comprehensive type checking and casting support
- **symbol_table.c**: Variable tracking and scope management

#### Type System (`src/type/`)
- **type_representation.c**: Arena-allocated type objects with advanced features
- **Phase 5 Complete**: Comprehensive casting rules with string conversion support
- **Type inference**: Smart inference for assignments and expressions
- **Cast validation**: Prevents unsafe conversions (e.g., string → numeric)

#### Error Reporting (`src/errors/`)
- **Feature-based organization**: Modular error system by language feature
- **Core infrastructure**: `src/errors/core/error_base.c` for registry and management
- **Type errors**: `src/errors/features/type_errors.c` with friendly, helpful messages
- **Friendly format**: Rust/Elm-inspired error messages with guidance and context

### Memory Architecture
- **Arena allocation**: For predictable lifetime objects (types, AST nodes)
- **Object pooling**: For frequently allocated/deallocated objects
- **String ropes**: O(1) concatenation with ASCII fast paths

## Current Implementation Status

### ✅ Complete Features
- **VM foundation**: 256 registers, computed-goto dispatch, comprehensive arithmetic
- **Complete lexer**: All language constructs with literal suffixes (i64, u32, f64)
- **Robust parser**: Precedence climbing, binary expressions, type annotations
- **Type system**: Phase 5 complete with comprehensive casting rules
- **String conversions**: All types can cast to string, string cannot cast to others
- **Variable system**: Declarations with type inference and annotation support
- **Error reporting**: Feature-based modular system with friendly messages
- **Memory management**: GC with object pooling and arena allocation
- **Comprehensive testing**: 100+ tests including type error validation

### ✅ Recent Major Achievements (Phase 5)
- **Comprehensive casting system**: All numeric ↔ numeric, bool ↔ numeric, all → string
- **String conversion rules**: `42 as string` ✅, `"42" as i32` ❌ with helpful errors
- **Type error improvements**: Friendly, mentor-like error messages
- **Modular error architecture**: Feature-based organization for maintainability
- **Enhanced test coverage**: Dedicated type error tests with expected failure validation

### ❌ Missing Critical Features
- Control flow statements (`if`, `while`, `for`)
- Function definitions and calls (`fn name:`)
- Arrays and collections
- Module system and imports
- Pattern matching and enums

## Development Guidelines

### Performance Requirements
- **Zero-cost abstractions**: Every abstraction must compile to optimal code
- **Arena allocation**: Use for objects with predictable lifetimes
- **Single-pass compilation**: Maintain fast compilation speeds
- **Type safety**: Explicit casting required, no implicit conversions

### Before Implementing Features
1. **Read documentation**: Check `docs/MISSING.md` for current roadmap
2. **Study existing patterns**: Follow established code conventions
3. **Performance first**: Every change must maintain or improve performance
4. **Test thoroughly**: Add tests for new features in appropriate categories
5. **Error handling**: Use the new modular error system for feature-specific errors

### Type System Rules (Phase 5 Complete)
```orus
# ✅ Valid: Type inference from literals
x = 10        # inferred as i32
y = 3.14      # inferred as f64
z = 10i64     # explicit suffix

# ✅ Valid: Explicit type annotations
a: u32 = 42   # annotation overrides inference

# ✅ Valid: Comprehensive casting system
num = 42
text = num as string     # All types → string
flag = 1 as bool        # Numeric → bool
bigger = num as i64     # Size conversions

# ❌ Invalid: Cross-type operations without casting
x = 10
y = 2.5
z = x + y     # Error: Can't mix these number types directly

# ✅ Valid: Explicit casting required
z = (x as f64) + y  # Explicit cast makes it valid

# ❌ Invalid: String to other type conversions
text = "42"
num = text as i32   # Error: String conversions are not allowed
```

### Error Message Quality
All errors follow a friendly, helpful format:
```
-- TYPE MISMATCH: This value isn't what we expected ------ file.orus:4:1

  4 | x: i32 = "hello"
    | ^^^^^^ this is a `string`, but `i32` was expected
    |
    = this is a `string`, but `i32` was expected
    = help: You can convert between types using conversion functions if appropriate.
    = note: Different types can't be mixed directly for safety reasons.
```

### Testing Strategy
```bash
# Run specific test categories
make test 2>&1 | grep "Type System Tests"      # Type checking and conversions
make test 2>&1 | grep "Type Safety Tests"      # Expected failures with friendly errors
make test 2>&1 | grep "Expression Tests"       # Basic expressions

# Test type error messages specifically
./orus tests/type_safety_fails/type_mismatch_string_to_int.orus
./orus tests/type_safety_fails/mixed_arithmetic_int_float.orus

# Test valid conversions
./orus tests/types/valid_string_conversions.orus
./orus tests/types/valid_numeric_conversions.orus

# Performance testing
make benchmark  # Cross-language comparisons
```

## Code Organization Patterns

### File Naming Conventions
- **VM operations**: `vm_*.c` (vm_arithmetic.c, vm_typed_ops.c)
- **Compiler phases**: Named by function (lexer.c, parser.c, compiler.c)
- **Error handling**: `src/errors/features/[feature]_errors.c`
- **Tests**: Organized by feature category in `tests/`

### Include Structure
- **Public API**: Headers in `include/` with clear separation
- **VM internals**: `src/vm/vm_internal.h` for implementation details
- **Type system**: `include/type.h` for type definitions and operations
- **Error system**: `include/errors/` for modular error handling

### Error Handling (New Modular System)
- **Feature-based**: Each language feature has its own error module
- **Type errors**: `src/errors/features/type_errors.c` with specialized functions
- **Friendly format**: Mentor-like messages with help and context
- **Easy expansion**: Simple pattern for adding new feature error modules

```c
// Use feature-specific error functions
report_type_mismatch(location, expected_type, found_type);
report_mixed_arithmetic(location, left_type, right_type);
report_invalid_cast(location, target_type, source_type);
```

## Performance Optimization Notes

### Hot Paths
- **Dispatch loop**: Optimized with computed-goto when available
- **Arithmetic operations**: Type-specific opcodes for common operations
- **Casting system**: Efficient runtime type conversions
- **Memory allocation**: Arena-based for predictable patterns

### Benchmarking
- Cross-language comparisons included in `tests/benchmarks/`
- Target: 10x faster than Python, 12x faster than JavaScript
- Current: 7x Python, 11x JavaScript (within target range)

## Quick Reference

### Common File Locations
- **Main interpreter**: `src/main.c`
- **REPL**: `src/repl.c`
- **Core VM**: `src/vm/vm.c`
- **Type system**: `src/type/type_representation.c`
- **Type errors**: `src/errors/features/type_errors.c`
- **Test runner**: Built into Makefile with comprehensive categories

### Recent Improvements
- **Type system**: Phase 5 casting rules complete
- **Error reporting**: Feature-based modular organization
- **Test coverage**: Comprehensive type error validation
- **String conversions**: Complete implementation with safety rules
- **User experience**: Friendly, helpful error messages

### Debug and Development
- **Build with debug**: `CFLAGS` includes `-g` by default
- **Verbose testing**: Makefile provides detailed test output with colors
- **Error validation**: Tests verify both success and failure cases
- **Performance tracking**: Integrated benchmarking against other languages

### Adding New Features
1. **Error handling**: Create feature-specific error module in `src/errors/features/`
2. **Testing**: Add both success and failure test cases
3. **Documentation**: Update relevant docs in `docs/`
4. **Performance**: Ensure no regression in benchmarks

## Type System Phases Status
- **Phase 1**: ✅ Basic type representation
- **Phase 2**: ✅ Literal type inference  
- **Phase 3**: ✅ Variable type resolution
- **Phase 4**: ✅ Binary operation type checking
- **Phase 5**: ✅ Comprehensive casting rules with string conversion support
- **Next**: Control flow and function call type checking