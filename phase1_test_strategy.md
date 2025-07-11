# Phase 1 Test Strategy: Type-Aware Expression Descriptors

## Overview
Phase 1 focuses on implementing the TypedExpDesc system for expression compilation while maintaining full backward compatibility. The goal is to replace the expression compilation engine while keeping all existing functionality working.

## What Phase 1 Should Support

### Core Expression Types (Must Work)
1. **Literals**: All basic literal types
   - Integer literals: `42`, `42i32`, `42i64`, `42u32`, `42u64`
   - Float literals: `3.14`, `3.14f64`
   - Boolean literals: `true`, `false`
   - String literals: `"hello"`
   - Nil literal: `nil`

2. **Binary Expressions**: All arithmetic and comparison operators
   - Arithmetic: `+`, `-`, `*`, `/`, `%`
   - Comparison: `<`, `<=`, `>`, `>=`, `==`, `!=`
   - Logical: `and`, `or`

3. **Unary Expressions**: Basic unary operators
   - Negation: `-expr`
   - Logical not: `not expr`

4. **Variable Access**: Reading variables
   - Local variables: `x`, `my_var`
   - Type-annotated variables: `x: i32`

5. **Variable Assignment**: Both declaration and assignment
   - Immutable: `x = 5`
   - Mutable: `mut x = 5`
   - Type-annotated: `x: i32 = 5`
   - Reassignment: `x = x + 1`

6. **Print Statements**: Basic output
   - Simple: `print(42)`
   - Multiple arguments: `print("Result:", x)`

### Type System Integration (Must Work)
1. **Type Inference**: Automatic type deduction
   - From literals: `x = 42` → `i32`
   - From arithmetic: `x = 1.0 + 2.0` → `f64`
   - Type promotion: `x = 1 + 2.0` → `f64`

2. **Type Checking**: Static type validation
   - Compatible operations: `i32 + i32`
   - Type promotion rules: `i32 + f64` → `f64`
   - Type annotations: `x: i32 = 42`

3. **Constant Folding**: Compile-time evaluation
   - Simple arithmetic: `1 + 2` → `3`
   - Type-aware folding: `1 + 2.0` → `3.0` (f64)
   - Complex expressions: `(1 + 2) * 3` → `9`

## Test Categories for Phase 1

### ✅ SHOULD WORK (Core Tests)
These tests should pass after Phase 1 implementation:

```bash
# Basic literals
tests/literals/literal.orus

# Simple expressions  
tests/expressions/binary.orus
tests/expressions/boolean.orus

# Variable operations
tests/variables/assignment.orus
tests/variables/vars.orus

# Type system basics
tests/types/i32/i32_basic.orus
tests/types/f64/f64_basic.orus
tests/types/u32/u32_basic.orus
tests/types/u64/u64_basic.orus
tests/types/i64/i64_basic.orus

# Type inference
tests/types/type_inference/arithmetic_inference_basic.orus
tests/types/type_inference/i32_arithmetic_inference.orus
tests/types/type_inference/f64_arithmetic_inference.orus

# Basic constant folding
tests/types/*/const_fold.orus
```

### ⚠️ SHOULD DEFER (Advanced Features)
These tests should be temporarily disabled/skipped in Phase 1:

```bash
# Control flow (implement in Phase 2)
tests/conditionals/
tests/control_flow/

# Functions (implement in Phase 2)
tests/functions/

# Advanced optimizations (implement in Phase 3)
tests/edge_cases/optimization/
tests/edge_cases/boundaries/

# Complex edge cases (implement in Phase 3)
tests/edge_cases/expression_edge_cases.orus
tests/edge_cases/numeric_edge_cases.orus

# Advanced type features (implement in Phase 2)
tests/types/advanced_type_inference.orus
tests/types/type_system_showcase.orus

# String operations (implement in Phase 2)
tests/formatting/

# Scoping and closures (implement in Phase 2)
tests/scope_analysis/
tests/variables/lexical_scoping*.orus
```

### 🔄 PARTIALLY SUPPORTED (Needs Adaptation)
These tests may need modification to work with Phase 1:

```bash
# Complex expressions (may need simplification)
tests/expressions/complex_expr.orus
tests/expressions/chain100.orus

# Mixed type operations (verify type promotion works)
tests/types/type_inference/mixed_type_inference.orus

# Variable shadowing (basic cases should work)
tests/variables/symbol_table_shadowing.orus
```

## Test Execution Strategy

### Phase 1 Test Suite
Create a Phase 1-specific test runner:

```bash
#!/bin/bash
# phase1_test_runner.sh

echo "Running Phase 1 Test Suite..."

# Core expression tests
echo "Testing literals..."
./orus tests/literals/literal.orus

echo "Testing binary expressions..."
./orus tests/expressions/binary.orus

echo "Testing variables..."
./orus tests/variables/assignment.orus
./orus tests/variables/vars.orus

echo "Testing type system..."
./orus tests/types/i32/i32_basic.orus
./orus tests/types/f64/f64_basic.orus
./orus tests/types/type_inference/arithmetic_inference_basic.orus

echo "Testing constant folding..."
./orus tests/types/i32/const_fold.orus || echo "SKIP: const_fold not implemented yet"

echo "Phase 1 tests completed"
```

### Regression Testing
Keep a separate regression test suite that runs the full test suite:

```bash
#!/bin/bash
# regression_test_runner.sh

echo "Running full regression test suite..."

# Run ALL tests to ensure no regressions
for test_file in $(find tests -name "*.orus" | sort); do
    echo "Testing $test_file..."
    if ./orus "$test_file" > /dev/null 2>&1; then
        echo "✅ PASS: $test_file"
    else
        echo "❌ FAIL: $test_file"
        echo "  Error: $(./orus "$test_file" 2>&1 | head -1)"
    fi
done
```

## Implementation Approach

### Step 1: Create Feature Flags
```c
// compiler.h
#define PHASE1_TYPED_EXPRESSIONS 1

// compiler.c
#if PHASE1_TYPED_EXPRESSIONS
    // Use new TypedExpDesc system
    int result = compile_typed_expression_to_register(node, compiler);
#else
    // Use old system
    int result = compileExpressionToRegister(node, compiler);
#endif
```

### Step 2: Implement Dual Code Paths
```c
// Wrapper function for backward compatibility
int compileExpressionToRegister(ASTNode* node, Compiler* compiler) {
#if PHASE1_TYPED_EXPRESSIONS
    return compile_typed_expression_to_register(node, compiler);
#else
    return compileExpressionToRegister_old(node, compiler);
#endif
}
```

### Step 3: Gradual Migration
1. Implement TypedExpDesc system alongside existing code
2. Test with Phase 1 test suite
3. Gradually enable more features
4. Run regression tests to ensure no breakage

## Success Criteria

### Phase 1 Complete When:
1. ✅ All Phase 1 core tests pass
2. ✅ No regressions in existing functionality
3. ✅ Type inference works correctly
4. ✅ Constant folding improvements are visible
5. ✅ Performance is maintained or improved

### Quality Gates:
- [ ] All literal expressions work
- [ ] All binary expressions work  
- [ ] All variable operations work
- [ ] Type inference is preserved
- [ ] Constant folding works
- [ ] No memory leaks introduced
- [ ] Compilation speed maintained

## Test Organization

### Create Phase 1 Test Directory
```
tests/phase1/
├── core_expressions.orus    # Essential expression tests
├── type_inference.orus      # Basic type inference
├── constant_folding.orus    # Compile-time evaluation
├── variable_operations.orus # Variable assignment/access
└── regression_minimal.orus  # Minimal regression test
```

### Modified Test Runner
```bash
# Run only Phase 1 tests during development
make test-phase1

# Run full regression tests before committing
make test-regression
```

This strategy ensures that Phase 1 delivers a solid foundation while maintaining full backward compatibility and providing clear success criteria.