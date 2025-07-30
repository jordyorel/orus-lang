# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Development Commands

The Orus project uses a Makefile-based build system with multiple build profiles. Always check the makefile for the most current build options.

### Build Commands
```bash
# Development build with debugging (creates orus_debug)
make debug

# Production build with optimizations (creates orus)
make release

# Profiling build with instrumentation (creates orus_profiling)
make profiling

# Clean all builds
make clean

# Show all available build options
make help
```

### Testing Commands
```bash
# Run comprehensive test suite
make test

# Run unit tests for compiler components
make unit-test

# Run CI test with warnings as errors
make ci-test

# Run performance benchmarks
make benchmark

# Run static analysis (requires cppcheck and clang)
make analyze
```

### Running Programs
```bash
# Interactive REPL
./orus_debug

# Run a program file
./orus_debug path/to/program.orus

# For release builds
./orus path/to/program.orus
```

## Architecture Overview

Orus is a register-based programming language with a multi-pass compiler architecture:

### Core Components
1. **Lexer** (`src/compiler/frontend/lexer.c`) - Tokenization with 105+ token types
2. **Parser** (`src/compiler/frontend/parser.c`) - Precedence-climbing parser producing AST
3. **Type System** (`src/type/`) - Hindley-Milner type inference with typed AST output
4. **Compiler** (`src/compiler/backend/`) - Multi-pass compilation (optimization + codegen)
5. **VM** (`src/vm/`) - 256-register virtual machine with 135+ opcodes

### Compilation Pipeline
```
Source → Lexer → Parser → AST → HM Type Inference → TypedAST
                                                      ↓
                                          Optimization Pass
                                                      ↓
                                            Code Generation → Bytecode → VM
```

### Register Architecture
- **R0-R63**: Global registers (module-level variables)
- **R64-R191**: Frame registers (function locals and parameters)  
- **R192-R239**: Temporary registers (expression evaluation)
- **R240-R255**: Module registers (imports/exports)

## Code Structure

### Frontend (Language Processing)
- `src/compiler/frontend/lexer.c` - Comprehensive tokenization
- `src/compiler/frontend/parser.c` - AST generation
- `include/compiler/ast.h` - AST node definitions

### Backend (Compilation)
- `src/compiler/backend/compiler.c` - Main compilation coordination
- `src/compiler/backend/optimization/` - Optimization passes (constant folding, peephole)
- `src/compiler/backend/codegen/` - Bytecode generation
- `src/compiler/symbol_table.c` - Variable to register mapping
- `src/compiler/typed_ast.c` - Typed AST handling

### Type System
- `src/type/type_inference.c` - Hindley-Milner type inference
- `src/type/type_representation.c` - Type system implementation
- `include/type/type.h` - Type definitions

### VM Implementation
- `src/vm/core/` - Core VM execution engine
- `src/vm/dispatch/` - Instruction dispatch (switch/goto variants)
- `src/vm/handlers/` - Opcode handlers (arithmetic, control flow, memory)
- `src/vm/operations/` - Type-specific operations
- `src/vm/runtime/` - Runtime services and built-ins

### Error Handling
- `src/errors/infrastructure/` - Error reporting infrastructure
- `src/errors/features/` - Feature-specific error types (type, variable, control flow)
- Rust-like error messages with source location information

## Testing

### Test Structure
Tests are organized by feature in the `tests/` directory:
- `tests/expressions/` - Arithmetic, boolean, comparison tests
- `tests/types/` - Type system and casting tests
- `tests/variables/` - Variable declaration and scoping tests
- `tests/strings/` - String operations and escape sequences
- `tests/comments/` - Comment handling tests

### Running Specific Tests
```bash
# Run tests for a specific feature
./orus_debug tests/expressions/arithmetic_basic.orus
./orus_debug tests/types/i32/i32_basic.orus

# Integration tests
./scripts/run_integration_tests.sh
```

### Test Expectations
- Files in `type_safety_fails/` directories are expected to fail (type errors)
- Division by zero tests are expected to fail with runtime errors
- All other tests should pass successfully

## Performance Characteristics

Orus targets high performance:
- **Compilation**: >10,000 lines/second across all passes
- **Runtime**: Competitive with LuaJIT, 1.7x faster than Python, 2.3x faster than JavaScript
- **Memory**: Efficient register-based execution with minimal boxing

## Common Development Tasks

### Adding New Language Features
1. Update lexer with new tokens (if needed)
2. Extend parser to handle new syntax
3. Add AST node types in `include/compiler/ast.h`
4. Implement type inference rules
5. Add compilation logic in backend
6. Create comprehensive tests

### Debugging Compilation Issues
1. Use debug build: `make debug`
2. Enable trace output in VM configuration
3. Check typed AST visualization output
4. Verify register allocation with debug output

### Performance Analysis
```bash
# Build with profiling
make profiling

# Run with profiling
./orus_profiling program.orus

# Analyze profile output
gprof orus_profiling gmon.out
```

## Important Implementation Notes

- The VM uses computed goto dispatch on supported architectures for performance
- Type inference produces a typed AST that guides optimization and code generation
- The compiler is designed for separation of concerns: optimization and codegen are distinct passes
- Register allocation is hierarchical with spilling support for unlimited parameters
- Error messages follow Rust conventions for clarity and actionability

## Module System

Orus supports a module system with import/export capabilities:
- Module registers (R240-R255) handle imports/exports
- Each module has its own namespace and register space
- Module loading supports both disk-based and embedded modules

When working on the module system, pay attention to:
- Module register allocation and management
- Import/export symbol resolution
- Module dependency resolution and loading order