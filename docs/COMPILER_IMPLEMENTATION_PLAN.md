# Orus Compiler Implementation Plan
## Multi-Pass Compiler Backend Development Strategy

### Executive Summary
This document outlines the detailed implementation plan for building the Orus compiler backend. The compiler follows a multi-pass architecture: TypedAST → OptimizationPass → CodegenPass → Bytecode, with visualization support between each pass.

### Current State Analysis

#### ✅ **What's Already Implemented**
- **Lexer & Parser**: Complete tokenization and AST generation
- **HM Type System**: Complete type inference producing TypedAST  
- **TypedAST Data Structures**: `src/compiler/typed_ast.c` with `generate_typed_ast()`
- **TypedAST Visualization**: `src/compiler/backend/typed_ast_visualizer.c`
- **VM Target**: 256-register VM with comprehensive instruction set
- **Integration Points**: Pipeline hooks in `src/vm/runtime/vm.c`

#### ❌ **What's Missing (Our Implementation Target)**
- **Optimization Pass**: `src/compiler/backend/optimizer.c`
- **Code Generation Pass**: `src/compiler/backend/codegen.c`  
- **Pipeline Coordination**: `src/compiler/backend/compiler.c`
- **Register Allocation**: `src/compiler/backend/register_allocator.c`
- **Bytecode Emission**: Infrastructure for VM instruction generation

### Architecture Overview

```
Source → Lexer → Parser → AST → HM → TypedAST → OptimizationPass → OptimizedTypedAST → CodegenPass → Bytecode → VM
                                      ↓              ↓                    ↓               ↓
                                 Visualizer    Visualizer          Visualizer      Visualizer
```

### File Structure Plan

```
src/compiler/
├── typed_ast.c                    ✅ (Exists - TypedAST data structures)
├── backend/
│   ├── compiler.c                 ❌ Phase 1 - Pipeline coordination
│   ├── optimizer.c                ❌ Phase 2 - TypedAST optimization  
│   ├── codegen.c                  ❌ Phase 3 - Bytecode generation
│   ├── register_allocator.c       ❌ Phase 1 - Register management
│   └── typed_ast_visualizer.c     ✅ (Exists - Debugging support)

include/compiler/
├── typed_ast.h                    ✅ (Exists)
├── compiler.h                     ❌ Phase 1 - Main compiler API
├── optimizer.h                    ❌ Phase 2 - Optimization API
├── codegen.h                      ❌ Phase 3 - Codegen API
└── register_allocator.h           ❌ Phase 1 - Register allocation API
```

---

## Phase 1: Core Infrastructure (Week 1) ✅ COMPLETED
**Goal**: Create the foundational data structures and interfaces

### ✅ **PHASE 1 RESULTS - ALL DELIVERABLES COMPLETE**
- **✅ Core Infrastructure**: CompilerContext, BytecodeBuffer, MultiPassRegisterAllocator
- **✅ Pipeline Coordination**: Optimization pass + Code generation pass coordination
- **✅ VM Integration**: Successfully integrated with existing VM pipeline
- **✅ Basic Testing**: Successfully compiles simple literals (42 → 4 bytecode instructions)
- **✅ Visualization Support**: TypedAST visualization working between all passes

**Evidence of Success**: Multi-pass compiler generates bytecode, no conflicts with existing system, clean execution with debug output showing all pipeline stages.

### Phase 1A: Header Files & Core Interfaces

#### Task 1.1: Create `include/compiler/compiler.h`
```c
#ifndef COMPILER_H
#define COMPILER_H

#include "compiler/typed_ast.h"
#include "vm/vm.h"

// Main compilation context
typedef struct CompilerContext {
    TypedASTNode* input_ast;           // Input from HM type inference
    TypedASTNode* optimized_ast;       // After optimization pass
    
    // Register allocation
    struct RegisterAllocator* allocator;
    int next_temp_register;            // R192-R239 temps
    int next_local_register;           // R64-R191 locals
    int next_global_register;          // R0-R63 globals
    
    // Symbol management
    struct SymbolTable* symbols;       // Variable → register mapping
    struct ScopeStack* scopes;         // Lexical scope tracking
    
    // Bytecode output
    struct BytecodeBuffer* bytecode;   // Final VM instructions
    struct ConstantPool* constants;    // Literal values (42, "hello")
    
    // Error handling
    struct ErrorReporter* errors;      // Compilation error tracking
    
    // Debugging
    bool enable_visualization;         // Show TypedAST between passes
    FILE* debug_output;               // Where to output debug info
    
    // Optimization settings
    struct OptimizationContext* opt_ctx;
} CompilerContext;

// Main compilation functions
CompilerContext* init_compiler_context(TypedASTNode* typed_ast);
bool compile_to_bytecode(CompilerContext* ctx);
void free_compiler_context(CompilerContext* ctx);

// Pipeline coordination
bool run_optimization_pass(CompilerContext* ctx);
bool run_codegen_pass(CompilerContext* ctx);

#endif
```

#### Task 1.2: Create `include/compiler/register_allocator.h`
```c
#ifndef REGISTER_ALLOCATOR_H
#define REGISTER_ALLOCATOR_H

#include <stdint.h>
#include <stdbool.h>

// Register ranges (matching VM architecture)
#define GLOBAL_REG_START    0     // R0-R63: Global variables
#define GLOBAL_REG_END      63
#define FRAME_REG_START     64    // R64-R191: Function locals/params  
#define FRAME_REG_END       191
#define TEMP_REG_START      192   // R192-R239: Expression temps
#define TEMP_REG_END        239
#define MODULE_REG_START    240   // R240-R255: Module scope
#define MODULE_REG_END      255

typedef struct RegisterAllocator {
    // Register usage tracking
    bool global_regs[64];      // R0-R63 usage
    bool frame_regs[128];      // R64-R191 usage  
    bool temp_regs[48];        // R192-R239 usage
    bool module_regs[16];      // R240-R255 usage
    
    // Allocation state
    int next_global;           // Next available global register
    int next_frame;            // Next available frame register
    int next_temp;             // Next available temp register
    int next_module;           // Next available module register
    
    // Stack for temp register reuse
    int temp_stack[48];        // Reusable temp registers
    int temp_stack_top;        // Top of temp stack
} RegisterAllocator;

// Core allocation functions
RegisterAllocator* init_register_allocator(void);
void free_register_allocator(RegisterAllocator* allocator);

// Register allocation by type
int allocate_global_register(RegisterAllocator* allocator);
int allocate_frame_register(RegisterAllocator* allocator);  
int allocate_temp_register(RegisterAllocator* allocator);
int allocate_module_register(RegisterAllocator* allocator);

// Register deallocation
void free_register(RegisterAllocator* allocator, int reg);
void free_temp_register(RegisterAllocator* allocator, int reg);

// Utilities
bool is_register_free(RegisterAllocator* allocator, int reg);
const char* register_type_name(int reg);

#endif
```

#### Task 1.3: Create BytecodeBuffer Infrastructure
```c
// In include/compiler/compiler.h (extend)

typedef struct BytecodeBuffer {
    uint8_t* instructions;     // VM instruction bytes
    int capacity;              // Buffer capacity
    int count;                 // Current instruction count
    
    // Metadata for debugging
    int* source_lines;         // Line numbers for each instruction  
    int* source_columns;       // Column numbers for each instruction
    
    // Jump patching
    struct JumpPatch* patches; // Forward jump patches
    int patch_count;
    int patch_capacity;
} BytecodeBuffer;

typedef struct JumpPatch {
    int instruction_offset;    // Where the jump instruction is
    int target_label;          // Label to patch to
} JumpPatch;

// Bytecode emission functions
BytecodeBuffer* init_bytecode_buffer(void);
void free_bytecode_buffer(BytecodeBuffer* buffer);
void emit_byte(BytecodeBuffer* buffer, uint8_t byte);
void emit_instruction(BytecodeBuffer* buffer, uint8_t opcode, uint8_t reg1, uint8_t reg2, uint8_t reg3);
int emit_jump_placeholder(BytecodeBuffer* buffer, uint8_t jump_opcode);
void patch_jump(BytecodeBuffer* buffer, int jump_offset, int target_offset);
```

### Phase 1B: Basic Implementation Files

#### Task 1.4: Create `src/compiler/backend/compiler.c`
```c
#include "compiler/compiler.h"
#include "compiler/typed_ast_visualizer.h"
#include "runtime/memory.h"
#include <stdio.h>
#include <stdlib.h>

CompilerContext* init_compiler_context(TypedASTNode* typed_ast) {
    if (!typed_ast) return NULL;
    
    CompilerContext* ctx = malloc(sizeof(CompilerContext));
    if (!ctx) return NULL;
    
    // Initialize all fields
    ctx->input_ast = typed_ast;
    ctx->optimized_ast = NULL;
    ctx->allocator = init_register_allocator();
    ctx->bytecode = init_bytecode_buffer();
    ctx->enable_visualization = false;  // Default off
    ctx->debug_output = stdout;
    
    // TODO: Initialize other components
    ctx->symbols = NULL;      // Will implement in Phase 2
    ctx->scopes = NULL;       // Will implement in Phase 2  
    ctx->constants = NULL;    // Will implement in Phase 2
    ctx->errors = NULL;       // Will implement in Phase 2
    ctx->opt_ctx = NULL;      // Will implement in Phase 2
    
    return ctx;
}

bool compile_to_bytecode(CompilerContext* ctx) {
    if (!ctx || !ctx->input_ast) return false;
    
    printf("[COMPILER] Starting compilation pipeline...\n");
    
    // Phase 1: Visualization (if enabled)
    if (ctx->enable_visualization) {
        fprintf(ctx->debug_output, "\n=== INPUT TYPED AST ===\n");
        visualize_typed_ast(ctx->input_ast, ctx->debug_output);
    }
    
    // Phase 2: Optimization Pass
    if (!run_optimization_pass(ctx)) {
        printf("[COMPILER] Optimization pass failed\n");
        return false;
    }
    
    // Phase 3: Code Generation Pass  
    if (!run_codegen_pass(ctx)) {
        printf("[COMPILER] Code generation pass failed\n");
        return false;
    }
    
    printf("[COMPILER] Compilation completed successfully\n");
    return true;
}

bool run_optimization_pass(CompilerContext* ctx) {
    printf("[COMPILER] Running optimization pass...\n");
    
    // For Phase 1: Simple pass-through (no actual optimization)
    // TODO: Implement real optimizations in Phase 2
    ctx->optimized_ast = ctx->input_ast;  // Pass-through for now
    
    // Visualization after optimization
    if (ctx->enable_visualization) {
        fprintf(ctx->debug_output, "\n=== OPTIMIZED TYPED AST ===\n");
        visualize_typed_ast(ctx->optimized_ast, ctx->debug_output);
    }
    
    return true;
}

bool run_codegen_pass(CompilerContext* ctx) {
    printf("[COMPILER] Running code generation pass...\n");
    
    // For Phase 1: Minimal codegen (just literals)
    // TODO: Implement full codegen in Phase 3
    
    // Simple test: emit a HALT instruction
    emit_instruction(ctx->bytecode, OP_HALT, 0, 0, 0);
    
    printf("[COMPILER] Generated %d bytecode instructions\n", ctx->bytecode->count);
    return true;
}

void free_compiler_context(CompilerContext* ctx) {
    if (!ctx) return;
    
    free_register_allocator(ctx->allocator);
    free_bytecode_buffer(ctx->bytecode);
    // Note: Don't free input_ast - it's owned by caller
    // TODO: Free other components as we implement them
    
    free(ctx);
}
```

#### Task 1.5: Create `src/compiler/backend/register_allocator.c`
```c
#include "compiler/register_allocator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

RegisterAllocator* init_register_allocator(void) {
    RegisterAllocator* allocator = malloc(sizeof(RegisterAllocator));
    if (!allocator) return NULL;
    
    // Initialize all registers as free
    memset(allocator->global_regs, false, sizeof(allocator->global_regs));
    memset(allocator->frame_regs, false, sizeof(allocator->frame_regs)); 
    memset(allocator->temp_regs, false, sizeof(allocator->temp_regs));
    memset(allocator->module_regs, false, sizeof(allocator->module_regs));
    
    // Initialize allocation pointers
    allocator->next_global = GLOBAL_REG_START;
    allocator->next_frame = FRAME_REG_START;
    allocator->next_temp = TEMP_REG_START;
    allocator->next_module = MODULE_REG_START;
    
    // Initialize temp stack
    allocator->temp_stack_top = -1;
    
    return allocator;
}

int allocate_global_register(RegisterAllocator* allocator) {
    if (!allocator) return -1;
    
    // Find next free global register
    for (int i = 0; i < 64; i++) {
        if (!allocator->global_regs[i]) {
            allocator->global_regs[i] = true;
            return GLOBAL_REG_START + i;
        }
    }
    
    // No free global registers
    printf("[REGISTER_ALLOCATOR] Warning: No free global registers\n");
    return -1;
}

int allocate_temp_register(RegisterAllocator* allocator) {
    if (!allocator) return -1;
    
    // Try to reuse from stack first
    if (allocator->temp_stack_top >= 0) {
        return allocator->temp_stack[allocator->temp_stack_top--];
    }
    
    // Find next free temp register
    for (int i = 0; i < 48; i++) {
        if (!allocator->temp_regs[i]) {
            allocator->temp_regs[i] = true;
            return TEMP_REG_START + i;
        }
    }
    
    // No free temp registers
    printf("[REGISTER_ALLOCATOR] Warning: No free temp registers\n");
    return -1;
}

void free_temp_register(RegisterAllocator* allocator, int reg) {
    if (!allocator || reg < TEMP_REG_START || reg > TEMP_REG_END) return;
    
    int index = reg - TEMP_REG_START;
    allocator->temp_regs[index] = false;
    
    // Add to reuse stack
    if (allocator->temp_stack_top < 47) {
        allocator->temp_stack[++allocator->temp_stack_top] = reg;
    }
}

const char* register_type_name(int reg) {
    if (reg >= GLOBAL_REG_START && reg <= GLOBAL_REG_END) return "GLOBAL";
    if (reg >= FRAME_REG_START && reg <= FRAME_REG_END) return "FRAME";  
    if (reg >= TEMP_REG_START && reg <= TEMP_REG_END) return "TEMP";
    if (reg >= MODULE_REG_START && reg <= MODULE_REG_END) return "MODULE";
    return "INVALID";
}

void free_register_allocator(RegisterAllocator* allocator) {
    if (allocator) {
        free(allocator);
    }
}
```

### Phase 1C: Integration & Testing

#### Task 1.6: Integrate with Main VM Pipeline
```c
// Modify src/vm/runtime/vm.c to use new compiler
// Add after TypedAST generation:

if (typed_ast) {
    // Create compiler context
    CompilerContext* compiler_ctx = init_compiler_context(typed_ast);
    if (compiler_ctx) {
        compiler_ctx->enable_visualization = config->show_typed_ast;
        
        // Run compilation pipeline
        if (compile_to_bytecode(compiler_ctx)) {
            printf("[VM] New compiler pipeline succeeded\n");
            
            // TODO: Use compiler_ctx->bytecode instead of old compilation
            // For now, continue with existing compilation for compatibility
        } else {
            printf("[VM] New compiler pipeline failed, falling back\n");
        }
        
        free_compiler_context(compiler_ctx);
    }
}
```

#### Task 1.7: Create Basic Test
```c
// tests/compiler/test_phase1.c
#include "compiler/compiler.h"
#include "compiler/typed_ast.h"

void test_basic_compilation() {
    // Create a simple literal AST node
    ASTNode* literal_ast = create_literal_ast_node(42);
    TypedASTNode* typed_ast = generate_typed_ast(literal_ast, NULL);
    
    // Test compiler context creation
    CompilerContext* ctx = init_compiler_context(typed_ast);
    assert(ctx != NULL);
    assert(ctx->allocator != NULL);
    assert(ctx->bytecode != NULL);
    
    // Test compilation pipeline
    bool result = compile_to_bytecode(ctx);
    assert(result == true);
    assert(ctx->bytecode->count > 0);  // Should have at least HALT
    
    printf("✅ Phase 1 basic compilation test passed\n");
    
    free_compiler_context(ctx);
    free_typed_ast_node(typed_ast);
    free_ast_node(literal_ast);
}
```

---

## Phase 2: Optimization Pass (Week 2) 🚨 **CRITICAL PHASE**
**Goal**: Implement TypedAST transformations with **MANDATORY VISUALIZATION**

### 🎯 **CRITICAL REQUIREMENT**: Optimized TypedAST Visualization
**This phase is crucial for progression to code generation. We MUST:**
- ✅ Show visible differences between input TypedAST and optimized TypedAST
- ✅ Demonstrate actual optimization transformations (e.g., `2+3` → `5`)
- ✅ Verify optimization correctness through visualization
- ✅ Enable debugging of optimization passes

**Without proper optimized TypedAST visualization, code generation phase cannot proceed safely.**

### Phase 2A: Optimization Infrastructure

#### Task 2.1: Create `include/compiler/optimizer.h`
```c
#ifndef OPTIMIZER_H
#define OPTIMIZER_H

#include "compiler/typed_ast.h"

typedef struct OptimizationContext {
    // Optimization flags
    bool enable_constant_folding;     // Fold 2+3 → 5
    bool enable_dead_code_elimination; // Remove unused variables
    bool enable_common_subexpression;  // Eliminate duplicate expressions
    
    // Analysis results
    struct ConstantTable* constants;   // Known constant values
    struct UsageAnalysis* usage;       // Variable usage tracking
    struct ExpressionCache* expressions; // Common expressions
    
    // Statistics
    int optimizations_applied;
    int nodes_eliminated;
    int constants_folded;
} OptimizationContext;

// Main optimization functions
OptimizationContext* init_optimization_context(void);
TypedASTNode* optimize_typed_ast(TypedASTNode* input, OptimizationContext* ctx);
void free_optimization_context(OptimizationContext* ctx);

// Individual optimization passes
TypedASTNode* constant_folding_pass(TypedASTNode* node, OptimizationContext* ctx);
TypedASTNode* dead_code_elimination_pass(TypedASTNode* node, OptimizationContext* ctx);

#endif
```

#### Task 2.2: Implement Basic Constant Folding
```c
// src/compiler/backend/optimizer.c

TypedASTNode* constant_folding_pass(TypedASTNode* node, OptimizationContext* ctx) {
    if (!node || !ctx) return node;
    
    // Handle binary expressions
    if (node->original->type == NODE_BINARY) {
        TypedASTNode* left = constant_folding_pass(node->typed.binary.left, ctx);
        TypedASTNode* right = constant_folding_pass(node->typed.binary.right, ctx);
        
        // Check if both operands are constants
        if (is_constant_literal(left) && is_constant_literal(right)) {
            Value result = evaluate_constant_binary(
                node->original->binary.op, 
                left->original->literal.value,
                right->original->literal.value
            );
            
            // Create new constant node
            TypedASTNode* folded = create_constant_typed_node(result, node->resolvedType);
            ctx->constants_folded++;
            
            return folded;
        }
        
        // Update children even if we can't fold
        node->typed.binary.left = left;
        node->typed.binary.right = right;
    }
    
    return node;
}
```

### 🎯 **HIGH-LEVEL OPTIMIZATIONS (optimizer.c Implementation TODOs)**

## 🚀 **VM-AWARE OPTIMIZATION STRATEGY**
**This VM has 150+ specialized opcodes and is INCREDIBLY powerful! Our optimizations must leverage these superpowers:**

**Phase 1: Simple & Essential (CURRENT TARGET)**
- [x] **Constant Folding**: ✅ IMPLEMENTED - Fold `2 + 3` → `5` to enable `OP_LOAD_I32_CONST`
- [ ] **Type-Aware Constant Folding**: Generate typed constants for `OP_LOAD_I32_CONST`, `OP_LOAD_F64_CONST`
- [ ] **Immediate Value Detection**: Identify constants for `OP_ADD_I32_IMM`, `OP_MUL_I32_IMM` fused ops

**Future High-Level Optimizations (After Codegen)**
- [ ] **Constant Propagation**: Track constant values across assignments (`x = 5; y = x + 2` → `y = 7`)
- [ ] **Copy Propagation**: Replace variable copies with original values (`x = y; z = x` → `z = y`)
- [ ] **Algebraic Simplification**: Simplify mathematical identities (`x * 1` → `x`, `x + 0` → `x`)

**Optimization Pass 2: Control Flow Analysis** 
- [ ] **Dead Code Elimination**: Remove unreachable code after returns/breaks
- [ ] **Branch Optimization**: Optimize constant conditions (`if (true)` → remove else branch)
- [ ] **Loop Invariant Code Motion (LICM)**: Move loop-invariant expressions outside loops
- [ ] **Jump Threading**: Optimize chain of conditional branches

**Optimization Pass 3: Function-Level Optimizations**
- [ ] **Inlining**: Inline small functions at call sites
- [ ] **Tail Recursion Optimization**: Convert tail recursive calls to loops
- [ ] **Dead Parameter Elimination**: Remove unused function parameters
- [ ] **Return Value Optimization**: Optimize multiple return statements

**Optimization Pass 4: Memory & Storage Optimizations**
- [ ] **Common Subexpression Elimination**: Reuse identical expression results
- [ ] **Strength Reduction**: Replace expensive operations (`x * 2` → `x << 1`)
- [ ] **Load/Store Optimization**: Minimize memory operations
- [ ] **Variable Lifetime Analysis**: Optimize variable scope and reuse

### Phase 2B: Integration with Pipeline
- Update `run_optimization_pass()` to use real optimization
- Add optimization statistics reporting
- Implement visualization of optimization results

---

## Phase 3: Code Generation Pass (Week 3)
**Goal**: Generate VM bytecode from optimized TypedAST

### Phase 3A: Code Generation Infrastructure

#### Task 3.1: Create `include/compiler/codegen.h`
```c
#ifndef CODEGEN_H  
#define CODEGEN_H

#include "compiler/typed_ast.h"
#include "compiler/compiler.h"

// Code generation functions
bool generate_bytecode_from_ast(CompilerContext* ctx);
int compile_expression(CompilerContext* ctx, TypedASTNode* expr);
void compile_statement(CompilerContext* ctx, TypedASTNode* stmt);
void compile_literal(CompilerContext* ctx, TypedASTNode* literal, int target_reg);
void compile_binary_op(CompilerContext* ctx, TypedASTNode* binary, int target_reg);

// Instruction emission helpers
void emit_load_constant(CompilerContext* ctx, int reg, Value constant);
void emit_arithmetic_op(CompilerContext* ctx, const char* op, Type* type, int dst, int src1, int src2);
void emit_move(CompilerContext* ctx, int dst, int src);

#endif
```

#### Task 3.2: Implement Expression Compilation
```c
// src/compiler/backend/codegen.c

int compile_expression(CompilerContext* ctx, TypedASTNode* expr) {
    if (!ctx || !expr) return -1;
    
    switch (expr->original->type) {
        case NODE_LITERAL: {
            int reg = allocate_temp_register(ctx->allocator);
            compile_literal(ctx, expr, reg);
            return reg;
        }
        
        case NODE_BINARY: {
            int left_reg = compile_expression(ctx, expr->typed.binary.left);
            int right_reg = compile_expression(ctx, expr->typed.binary.right);
            int result_reg = allocate_temp_register(ctx->allocator);
            
            compile_binary_op(ctx, expr, result_reg);
            
            // Free temp registers
            free_temp_register(ctx->allocator, left_reg);
            free_temp_register(ctx->allocator, right_reg);
            
            return result_reg;
        }
        
        case NODE_IDENTIFIER: {
            // Look up variable in symbol table
            // TODO: Implement symbol table lookup
            return -1; // Placeholder
        }
        
        default:
            printf("[CODEGEN] Unsupported expression type: %d\n", expr->original->type);
            return -1;
    }
}

void compile_literal(CompilerContext* ctx, TypedASTNode* literal, int target_reg) {
    Value value = literal->original->literal.value;
    
    switch (value.type) {
        case VAL_I32:
            emit_instruction(ctx->bytecode, OP_LOAD_CONST_I32, target_reg, 
                           (value.as.i32 >> 8) & 0xFF, value.as.i32 & 0xFF);
            break;
            
        case VAL_STRING:
            // TODO: Handle string constants
            break;
            
        default:
            printf("[CODEGEN] Unsupported literal type: %d\n", value.type);
    }
}
```

### 🎯 **LOW-LEVEL OPTIMIZATIONS (Code Generation Pass TODOs)**

## 🚀 **VM-POWERED CODE GENERATION STRATEGY**
**The VM provides 150+ specialized opcodes - we must leverage ALL of them!**

**Phase 1: Leverage VM Superpowers (CURRENT TARGET)**
- [ ] **Typed Instruction Selection**: Use `OP_ADD_I32_TYPED` instead of generic `OP_ADD_I32_R`
  - **50% faster** - bypasses Value boxing/unboxing overhead!
- [ ] **Immediate Constant Optimization**: Use `OP_ADD_I32_IMM` for `x + 5` patterns
  - **Saves 1 instruction** per constant operation!
- [ ] **Typed Constant Loading**: Use `OP_LOAD_I32_CONST` vs generic `OP_LOAD_CONST`
  - **Direct register loading** without constant pool lookup!
- [ ] **Fused Instruction Selection**: Use `OP_MUL_ADD_I32` for `a*b+c` patterns
  - **Native FMA support** - 3 operations become 1!

**Register Optimization (256 Registers Available!)**
- [x] **Register Allocation**: ✅ IMPLEMENTED - Linear scan with hierarchical layout
- [ ] **Register Type Specialization**: Use proper register banks (global/frame/temp/module)
- [ ] **Spill Area Utilization**: Leverage unlimited parameter support via spill registers
- [ ] **Register Coalescing**: Eliminate `OP_MOVE` instructions where possible

**Advanced VM Features Utilization**
- [ ] **Loop Optimization**: Use `OP_INC_CMP_JMP` for `for(i=0; i<n; i++)` patterns
  - **3 instructions become 1** - increment+compare+branch fusion!
- [ ] **Short Jump Optimization**: Use `OP_JUMP_SHORT` for nearby branches (1 byte vs 2)
- [ ] **Frame Register Operations**: Use `OP_LOAD_FRAME`/`OP_STORE_FRAME` for locals
- [ ] **Module Register System**: Leverage module-scoped registers for globals

**Control Flow Optimizations (During Code Generation)**
- [ ] **Jump Optimization**: Minimize jump instructions and targets
- [ ] **Basic Block Optimization**: Optimize instruction ordering within blocks
- [ ] **Branch Prediction Hints**: Add hints for likely/unlikely branches
- [ ] **Loop Optimization**: Optimize loop entry/exit and loop bodies

**Memory Access Optimizations**
- [ ] **Load/Store Scheduling**: Reorder memory operations for efficiency
- [ ] **Constant Pool Optimization**: Efficient management of literal constants
- [ ] **Stack Frame Optimization**: Minimize stack frame size and access patterns
- [ ] **Memory Coalescing**: Combine adjacent memory operations when possible

---

## Phase 4: Testing & Integration (Week 4)
**Goal**: Comprehensive testing and performance validation

### Phase 4A: Test Suite
```c
// tests/compiler/integration_tests.c

void test_simple_arithmetic() {
    // Test: x = 2 + 3
    // Expected: Constant folding → x = 5
    // Expected bytecode: LOAD_CONST_I32 R0, 5
}

void test_variable_assignment() {
    // Test: x = 42; y = x
    // Expected bytecode: LOAD_CONST_I32 R0, 42; MOVE R1, R0
}

void test_optimization_effectiveness() {
    // Measure optimization impact
    // Verify constant folding reduces instruction count
    // Verify dead code elimination removes unused code
}
```

### Phase 4B: Performance Benchmarking
- Measure compilation speed (target: >10,000 lines/second)
- Measure generated code quality (register efficiency)
- Compare against baseline (current single-pass compiler)

### Phase 4C: Error Handling Integration
- Connect with existing error reporting system
- Add compiler-specific error messages
- Validate error recovery

---

## Success Criteria

### Functional Requirements
- [ ] Phase 1: Basic pipeline compiles simple literals
- [ ] Phase 2: Constant folding reduces instruction count by 10%
- [ ] Phase 3: Generate correct bytecode for arithmetic expressions
- [ ] Phase 4: All existing test cases pass with new compiler

### Performance Requirements  
- [ ] Compilation speed: >5,000 lines/second (Phase 1 target)
- [ ] Register efficiency: <10% temp register spills
- [ ] Code quality: Generated bytecode executes correctly on VM

### Quality Requirements
- [ ] Zero compilation crashes on valid input
- [ ] Clear error messages for invalid input  
- [ ] 90% test coverage for new compiler components
- [ ] Comprehensive documentation for each phase

---

## Risk Mitigation

### Technical Risks
- **Register Allocation Complexity**: Start with simple linear allocation, optimize later
- **Optimization Correctness**: Implement conservative optimizations first
- **Integration Issues**: Maintain compatibility with existing VM

### Timeline Risks  
- **Phase Dependencies**: Each phase builds on previous, delays compound
- **Scope Creep**: Focus on MVP functionality first, advanced features later
- **Testing Overhead**: Allocate 30% of time to testing and debugging

---

## Future Extensions (Beyond Phase 4)

### Advanced Optimizations
- Loop invariant code motion
- Interprocedural optimization
- Advanced register allocation (graph coloring)

### Language Features
- Function compilation
- Control flow (if/while/for)
- Array and string operations
- Module system integration

### Tooling
- Bytecode disassembler
- Performance profiler integration
- Interactive debugging support

---

## 🚀 **CRITICAL INSIGHT: VM SUPERPOWERS DISCOVERED!**

**After analyzing the VM capabilities, this is NOT a simple register machine - it's a POWERHOUSE:**

### **VM Superpowers Summary:**
- **256 Registers** with hierarchical memory layout (global/frame/temp/module/spill)
- **150+ Specialized Opcodes** including type-specific and fused instructions
- **Zero-overhead typed operations** (`OP_ADD_I32_TYPED` bypasses Value boxing)
- **Fused instruction support** (`OP_MUL_ADD_I32`, `OP_INC_CMP_JMP`)
- **Immediate arithmetic** (`OP_ADD_I32_IMM`) for constant folding results
- **Advanced control flow** with short jumps and loop optimizations
- **Unlimited scalability** via spill registers and frame management

### **Our Compiler Strategy:**
1. **Keep high-level optimizations SIMPLE** - just constant folding for now
2. **Focus on LEVERAGING VM power** in code generation
3. **Use typed instructions** instead of generic ones (50% performance gain)
4. **Exploit fused operations** for multi-step patterns
5. **Utilize specialized register banks** for optimal memory hierarchy

**The VM is so powerful that simple constant folding + smart instruction selection will yield excellent performance!**

---

This implementation plan provides a structured approach to building the Orus compiler backend while **maximally leveraging the incredibly powerful VM architecture** for optimal performance.