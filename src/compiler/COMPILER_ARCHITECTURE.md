# Orus Compiler Architecture: Shared Frontend + Specialized Backends

## Overview

This document outlines the architectural approach for Orus's compiler to achieve **safety, accuracy, and VM optimization** through a unified frontend with specialized backends.

## Current Problem

Orus currently has two separate compilers (single-pass and multi-pass) with different feature support:

```
┌─────────────────┐    ┌─────────────────┐
│  Single-Pass    │    │   Multi-Pass    │
│                 │    │                 │
│ • Basic exprs   │    │ • Basic exprs   │
│ • All features  │    │ • Missing cast  │  ← PROBLEM!
│ • No optimiz.   │    │ • Missing 'and' │
└─────────────────┘    └─────────────────┘
```

This leads to:
- ❌ Feature gaps between compilation modes
- ❌ Maintenance burden (two codebases)
- ❌ Inconsistent behavior
- ❌ Testing complexity

## Recommended Solution: Shared Frontend + Specialized Backends

Following the **LLVM model** but tailored for Orus's register-based VM:

```
┌─────────────────────────────────────────────────────────────┐
│                    SHARED FRONTEND                          │
│  ┌─────────────┐  ┌──────────────┐  ┌─────────────────┐   │
│  │   Parser    │→ │ Semantic     │→ │ Type Checker    │   │
│  │             │  │ Analysis     │  │ & Validation    │   │
│  └─────────────┘  └──────────────┘  └─────────────────┘   │
└─────────────────────────────────────────────────────────────┘
                              │
                    ┌─────────▼─────────┐
                    │  COMMON AST + IR  │
                    │  • Type info      │
                    │  • Safety checks  │
                    │  • Register hints │
                    └─────────┬─────────┘
                              │
        ┌─────────────────────┼─────────────────────┐
        │                     │                     │
┌───────▼────────┐   ┌────────▼────────┐   ┌──────▼──────┐
│ FAST BACKEND   │   │ OPTIMIZED       │   │ HYBRID      │
│                │   │ BACKEND         │   │ BACKEND     │
│ • Direct emit  │   │ • Loop analysis │   │ • Smart     │
│ • Simple codegen│   │ • Register opt │   │   dispatch  │
│ • Fast compile │   │ • LICM/DCE      │   │ • Best of   │
│ • Development  │   │ • Production    │   │   both      │
└────────────────┘   └─────────────────┘   └─────────────┘
```

## Benefits

### **1. Safety & Accuracy** 
- ✅ **Single source of truth** for language semantics
- ✅ **Unified type checking** - no discrepancies between modes
- ✅ **Consistent error reporting** across all backends
- ✅ **Same AST validation** for all compilation paths

### **2. VM Optimization Power**
- ✅ **Register allocation hints** in shared IR
- ✅ **Backend-specific optimizations** for register VM
- ✅ **Hot path detection** can switch backends dynamically
- ✅ **Profile-guided optimization** using VM runtime data

### **3. Implementation Benefits**
- ✅ **Single place** to add new language features
- ✅ **Easier testing** - validate semantics once
- ✅ **Performance flexibility** - choose backend per function
- ✅ **Future-proof** for advanced optimizations

## Implementation Phases

### **Phase 1: Shared Expression Layer** ⬅️ **STARTING HERE**

Create a unified expression analysis and compilation system:

#### Core Data Structures
```c
typedef struct {
    ASTNode* node;
    ValueType inferredType;
    bool isConstant;
    RegisterHint preferredReg;
    SafetyFlags flags;
} TypedExpression;

typedef enum {
    BACKEND_FAST,        // Single-pass equivalent
    BACKEND_OPTIMIZED,   // Multi-pass equivalent  
    BACKEND_HYBRID       // Smart selection
} Backend;
```

#### Shared Analysis Pass
```c
// Single validation/typing pass
TypedExpression* analyzeExpression(ASTNode* node, Compiler* compiler) {
    TypedExpression* typed = malloc(sizeof(TypedExpression));
    typed->node = node;
    typed->inferredType = inferType(node, compiler);
    typed->isConstant = isConstantExpression(node);
    typed->preferredReg = suggestRegister(node, compiler);
    typed->flags = validateSafety(node, compiler);
    return typed;
}
```

#### Backend-Specific Code Generation
```c
// Backend-specific code generation
int compileTypedExpression(TypedExpression* expr, Compiler* compiler, Backend backend) {
    switch (backend) {
        case BACKEND_FAST:
            return compileFast(expr, compiler);
        case BACKEND_OPTIMIZED:
            return compileOptimized(expr, compiler);
        case BACKEND_HYBRID:
            return compileHybrid(expr, compiler);
    }
}
```

#### Implementation Tasks for Phase 1 ✅ **COMPLETED**
1. ✅ Create `TypedExpression` structure and framework
2. ✅ Implement shared expression analysis foundation  
3. ✅ Add support for missing expressions in multi-pass:
   - ✅ `NODE_CAST` (for `as` operator) - **WORKING**
   - ✅ `NODE_LOGICAL_AND` (for `and` operator) - **WORKING**
   - ✅ `NODE_LOGICAL_OR` (for `or` operator) - **WORKING**
4. ✅ Fix multipass compiler feature gaps
5. ✅ Add comprehensive tests - **all critical tests passing**
6. ✅ **BONUS**: Fixed infinite loop bug in nested control flow

#### Phase 1 Results ✅
- **Feature Parity Achieved**: Both compilers support same expression types
- **Critical Bug Fixed**: Nested loops with break/continue now work correctly  
- **Tests Passing**: `advanced_range_syntax.orus`, `loop_variable_scoping_nested.orus`
- **Architecture Ready**: Foundation laid for smart backend selection

### **Phase 2: Smart Backend Selection**

Implement intelligent backend selection based on code characteristics:

```c
Backend chooseBackend(ASTNode* node, CompilerContext* ctx) {
    if (ctx->isDebugMode) return BACKEND_FAST;
    if (isHotPath(node, ctx->profile)) return BACKEND_OPTIMIZED;
    if (hasComplexLoops(node)) return BACKEND_OPTIMIZED;
    if (hasSimpleExpressions(node)) return BACKEND_FAST;
    return BACKEND_HYBRID;
}
```

### **Phase 3: VM-Aware Optimizations**

Add register-VM specific optimizations:

```c
// Register-specific optimizations
typedef struct {
    int liveRegisters[256];
    int spillCost[256];
    bool isLoopVariable[256];
    RegisterPressure pressure;
} RegisterState;

int optimizeForRegisterVM(TypedExpression* expr, RegisterState* state) {
    // Choose optimal registers based on VM characteristics
    // Minimize register pressure for 256-register VM
    // Optimize for computed-goto dispatch
    // Consider register allocation across expression boundaries
}
```

### **Phase 4: Advanced Features**

- Profile-guided optimization
- Hot path detection and recompilation
- Cross-function register allocation
- Advanced loop optimizations

## Real-World Example: How This Helps

**Current Problem:**
```orus
// Works in single-pass
x = 42 as string

// Fails in multi-pass  
// "Unsupported expression type"
```

**With Shared Frontend:**
```c
// Single analysis phase
TypedExpression* castExpr = analyzeExpression(castNode, compiler);
// ✅ Type validation: i32 → string (valid)
// ✅ Inferred type: string
// ✅ Register hint: prefer string registers
// ✅ Safety: casting rules validated

// Both backends support it
int reg1 = compileTypedExpression(castExpr, compiler, BACKEND_FAST);
int reg2 = compileTypedExpression(castExpr, compiler, BACKEND_OPTIMIZED);
```

## Performance Benefits for Orus VM

1. **Register Allocation**: Shared analysis provides optimal register usage hints
2. **Instruction Selection**: VM-aware backends choose best opcodes for each case
3. **Hot Path Optimization**: Runtime profiling guides backend selection
4. **Memory Layout**: Consistent object layout across compilation modes
5. **Dispatch Optimization**: Optimize for computed-goto dispatch in VM

## Migration Timeline

- **Week 1**: Phase 1 - Shared expression analysis layer
- **Week 2**: Phase 1 - Unify type checking and validation  
- **Week 3**: Phase 2 - Backend selection logic
- **Week 4**: Phase 3 - VM-specific optimizations

## Files to Modify

### Phase 1 Files:
- `src/compiler/shared_expression.c` (new)
- `src/compiler/shared_expression.h` (new)
- `src/compiler/singlepass.c` (modify to use shared layer)
- `src/compiler/multipass.c` (modify to use shared layer)
- `include/compiler/expression_analysis.h` (new)

### Testing:
- Add tests for `as` operator in both compilation modes
- Add tests for `and`/`or` operators in both modes
- Ensure feature parity between backends

---

**Status**: Phase 1 implementation starting
**Goal**: Unified expression compilation with full feature parity between backends



# Compiler Feature Separation Specification

## Single-Pass Compiler (singlepass.c)
**Purpose**: Handle ONLY the simplest constructs for maximum compilation speed

### ✅ SUPPORTED Features:
- `NODE_LITERAL` - Basic literals (numbers, strings, booleans)
- `NODE_IDENTIFIER` - Simple variable references
- `NODE_BINARY` - Basic arithmetic (+, -, *, /, %, ==, !=, <, >, <=, >=)
- `NODE_ASSIGN` - Simple variable assignments
- `NODE_VAR_DECL` - Simple variable declarations
- `NODE_PRINT` - Simple print statements
- `NODE_BLOCK` - Simple statement blocks
- `NODE_IF` - Simple if/else (ONLY single level, no nesting)
- `NODE_FOR_RANGE` - Simple for loops (ONLY single level, NO break/continue)
- `NODE_WHILE` - Simple while loops (ONLY single level, NO break/continue)
- `NODE_PROGRAM` - Program root
- `NODE_TIME_STAMP` - Time operations

### ❌ NOT SUPPORTED (Must redirect to multi-pass):
- `NODE_BREAK` - Break statements
- `NODE_CONTINUE` - Continue statements  
- `NODE_FUNCTION` - Function definitions
- `NODE_CALL` - Function calls
- `NODE_RETURN` - Return statements
- Any nested loops
- Any nested if/else
- Complex expressions
- Type casting (`as` expressions)

## Multi-Pass Compiler (multipass.c) 
**Purpose**: Handle ALL complex constructs with proper analysis

### ✅ SUPPORTED Features:
- ALL features that single-pass supports
- `NODE_BREAK` - Break statements with proper scoping
- `NODE_CONTINUE` - Continue statements with proper scoping
- `NODE_FUNCTION` - Function definitions
- `NODE_CALL` - Function calls
- `NODE_RETURN` - Return statements
- Nested loops of any depth
- Nested if/else statements
- Complex expressions and type casting
- Advanced scoping and variable lifetime management

## Routing Logic in hybride_compiler.c

The hybrid compiler will analyze the AST and route to:
- **Single-pass**: Only if NO complex features are present
- **Multi-pass**: If ANY complex feature is detected

This ensures clean separation and prevents the state corruption we discovered.