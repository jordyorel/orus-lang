# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build and Development Commands

### Essential Commands
```bash
# Build the Orus interpreter (includes both switch and goto dispatchers)
make clean && make

# Run comprehensive test suite with color-coded output
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
The test suite is organized into categories with specific behaviors:
- **Basic tests**: Must pass (expressions, variables, literals)
- **Type safety tests**: Expected to fail (in `type_safety_fails/`)
- **Division by zero tests**: Expected to fail (runtime safety)
- **Edge cases**: Boundary conditions and arithmetic limits

## High-Level Architecture

### Core Design Principles
- **Register-based VM**: 256 registers, outperforms Python by 7x, Node.js by 11x
- **Single-pass compilation**: Fast compilation for development speed
- **Performance-first**: Zero-cost abstractions, arena allocation, computed-goto dispatch
- **Type safety**: Static typing with inference, explicit casting required

### Key Components

#### VM Core (`src/vm/`)
- **Dual dispatch system**: Switch-based and computed-goto implementations compiled together
- **vm.c**: Main VM runtime with register management
- **vm_dispatch_*.c**: Specialized dispatch implementations for performance
- **vm_arithmetic.c, vm_typed_ops.c**: Type-specific operations
- **Memory management**: Mark-and-sweep GC with object pooling

#### Compiler Pipeline (`src/compiler/`)
- **Single-pass design**: lexer.c → parser.c → compiler.c → bytecode
- **parser.c**: Precedence climbing parser with AST generation
- **compiler.c**: Bytecode emission with type-aware code generation
- **symbol_table.c**: Variable tracking and scope management

#### Type System (`src/type/`)
- **type_representation.c**: Arena-allocated type objects with advanced features
- **type_inference.c**: Hindley-Milner style inference for literals and expressions
- **Current focus**: Implementing phases 3-4 (compiler type resolution, binary operation rules)

### Memory Architecture
- **Arena allocation**: For predictable lifetime objects (types, AST nodes)
- **Object pooling**: For frequently allocated/deallocated objects
- **String ropes**: O(1) concatenation with ASCII fast paths

## Current Implementation Status

### ✅ Complete Features
- VM foundation with 256 registers and computed-goto dispatch
- Complete lexer with all language constructs
- Basic parser with precedence climbing and binary expressions
- Variable declarations with type inference (`x = 10` → i32)
- Memory management with GC and object pooling
- Comprehensive test framework with 100+ tests

### 🔄 In Progress (Phases 3-4)
- **Phase 3**: Compiler type resolution in variable declarations
- **Phase 4**: Binary operation type checking with explicit casting rules
- Type-aware bytecode generation

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

### Type System Rules
```orus
# ✅ Valid: Type inference from literals
x = 10        # inferred as i32
y = 3.14      # inferred as f64
z = 10i64     # explicit suffix

# ✅ Valid: Explicit type annotations
a: u32 = 42   # annotation overrides inference

# ❌ Invalid: Cross-type operations without casting
x = 10
y = 2.5
z = x + y     # Error: Cannot perform operation between i32 and f64

# ✅ Valid: Explicit casting required
z = (x as f64) + y  # Explicit cast makes it valid
```

### Testing Strategy
```bash
# Run specific test categories
make test 2>&1 | grep "Expression Tests"    # Basic expressions
make test 2>&1 | grep "Type System Tests"   # Type checking
make test 2>&1 | grep "Expected to Fail"    # Type safety validation

# Performance testing
make benchmark  # Cross-language comparisons
```

## Code Organization Patterns

### File Naming Conventions
- **VM operations**: `vm_*.c` (vm_arithmetic.c, vm_typed_ops.c)
- **Compiler phases**: Named by function (lexer.c, parser.c, compiler.c)
- **Tests**: Organized by feature category in `tests/`

### Include Structure
- **Public API**: Headers in `include/` with clear separation
- **VM internals**: `src/vm/vm_internal.h` for implementation details
- **Type system**: `include/type.h` for type definitions and operations

### Error Handling
- **Compile-time**: Type errors with line/column information
- **Runtime**: Structured error reporting with error codes
- **Test validation**: Expected failures for type safety enforcement

## Performance Optimization Notes

### Hot Paths
- **Dispatch loop**: Optimized with computed-goto when available
- **Arithmetic operations**: Type-specific opcodes for common operations
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
- **Type inference**: `src/type/type_inference.c`
- **Test runner**: Built into Makefile with comprehensive categories

### Debug and Development
- **Build with debug**: `CFLAGS` includes `-g` by default
- **Verbose testing**: Makefile provides detailed test output with colors
- **Performance tracking**: Integrated benchmarking against other languages