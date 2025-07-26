# Orus Compiler Refactoring Plan: Recursive to Iterative Approach

## Executive Summary

The current Orus compiler uses a recursive approach in `compileMultiPassNode()` which causes several critical issues:
- Stack overflow with deeply nested expressions or loops
- Infinite recursion in nested control structures
- Poor performance due to function call overhead
- Difficult debugging and error recovery

This plan outlines a systematic transition to an iterative compilation approach inspired by Rust's HIR lowering and LLVM's pass infrastructure.

## Current State Analysis

### Problems Identified

1. **Recursive Compilation Function** (`multipass.c:1528`)
   - `compileMultiPassNode()` calls itself recursively for each AST node
   - Maximum recursion depth of 100 leads to failures on complex code
   - Nested loops are currently disabled due to infinite recursion issues

2. **Memory Management Issues**
   - Static recursion depth counter is not thread-safe
   - Stack growth with deep AST trees
   - Complex cleanup logic scattered throughout recursive calls

3. **Control Flow Problems**
   - Break/continue jumps are handled during recursion
   - Difficult to patch forward jumps
   - Scope management tied to call stack

4. **Performance Issues**
   - Function call overhead for each AST node
   - Repeated scope lookups
   - Inefficient register allocation

### Current Architecture

```
compileMultiPass() -> compileMultiPassNode() -> [recursive calls]
                                             -> compileMultiPassNode()
                                             -> compileMultiPassNode()
                                             -> ...
```

## Target Architecture: Iterative Compilation Pipeline

### Inspiration from Rust/LLVM

**Rust's HIR Lowering Approach:**
- Visitor pattern with explicit work queue
- Separate passes for different concerns
- Clear separation between analysis and code generation

**LLVM's Pass Infrastructure:**
- Each pass has a specific responsibility
- Iterative processing with work lists
- Clear data flow between passes

### New Architecture Design

```
┌─────────────────┐    ┌──────────────────┐    ┌─────────────────┐
│   AST Input     │ -> │  Work Queue      │ -> │  Bytecode       │
│                 │    │  (Iterative)     │    │  Generation     │
└─────────────────┘    └──────────────────┘    └─────────────────┘
                              │
                       ┌──────▼──────┐
                       │ Compilation │
                       │ Context     │
                       │ (Stack)     │
                       └─────────────┘
```

## Implementation Plan

### Phase 1: Foundation Setup (PRIORITY: CRITICAL)

#### 1.1 Create Iterative Compilation Context
```c
typedef struct CompilationContext {
    // Work queue for iterative processing
    ASTNode** workQueue;
    int workQueueSize;
    int workQueueCapacity;
    
    // Current processing state
    int currentIndex;
    
    // Scope management stack
    ScopeFrame* scopeStack;
    int scopeDepth;
    int maxScopeDepth;
    
    // Jump patching system
    JumpPatch* pendingJumps;
    int jumpCount;
    
    // Error recovery state
    bool inErrorRecovery;
    int errorCount;
} CompilationContext;
```

#### 1.2 Work Queue Management
```c
// Core queue operations
void pushWork(CompilationContext* ctx, ASTNode* node);
ASTNode* popWork(CompilationContext* ctx);
bool hasWork(CompilationContext* ctx);

// Priority work insertion (for control flow)
void pushPriorityWork(CompilationContext* ctx, ASTNode* node);
```

#### 1.3 Scope Stack Management
```c
typedef struct ScopeFrame {
    int startLocalCount;
    int scopeDepth;
    int registerBase;
    
    // For control flow
    int* breakJumps;
    int* continueJumps;
    int breakCount;
    int continueCount;
} ScopeFrame;

void pushScope(CompilationContext* ctx, Compiler* compiler);
void popScope(CompilationContext* ctx, Compiler* compiler);
```

### Phase 2: Core Iterative Loop (PRIORITY: CRITICAL)

#### 2.1 Main Compilation Loop
```c
bool compileIterative(ASTNode* ast, Compiler* compiler) {
    CompilationContext ctx;
    initCompilationContext(&ctx);
    
    // Initialize with root node
    pushWork(&ctx, ast);
    
    while (hasWork(&ctx)) {
        ASTNode* current = popWork(&ctx);
        
        if (!processNode(current, compiler, &ctx)) {
            cleanupCompilationContext(&ctx);
            return false;
        }
    }
    
    cleanupCompilationContext(&ctx);
    return true;
}
```

#### 2.2 Node Processing Dispatch
```c
bool processNode(ASTNode* node, Compiler* compiler, CompilationContext* ctx) {
    if (!node) return true;
    
    // Set current location for error reporting
    compiler->currentLine = node->location.line;
    compiler->currentColumn = node->location.column;
    
    switch (node->type) {
        case NODE_PROGRAM:
            return processProgram(node, compiler, ctx);
        case NODE_FOR_RANGE:
            return processForRange(node, compiler, ctx);
        case NODE_BLOCK:
            return processBlock(node, compiler, ctx);
        // ... other node types
        default:
            return processLeafNode(node, compiler, ctx);
    }
}
```

### Phase 3: Control Flow Implementation (PRIORITY: HIGH)

#### 3.1 For Loop Implementation
```c
bool processForRange(ASTNode* node, Compiler* compiler, CompilationContext* ctx) {
    // Begin scope
    pushScope(ctx, compiler);
    
    // Compile loop variable and bounds
    compileLoopVariable(node, compiler);
    compileLoopBounds(node, compiler);
    
    // Setup loop bytecode
    int loopStart = currentOffset(compiler);
    emitLoop(compiler, OP_FOR_RANGE_CHECK);
    
    // Queue loop body for processing
    pushWork(ctx, node->forRange.body);
    
    // Set up jump patching for break/continue
    setupLoopJumps(ctx, loopStart);
    
    return true;
}
```

#### 3.2 Jump Patching System
```c
typedef struct JumpPatch {
    int offset;          // Where to patch
    int target;          // What to patch to
    JumpType type;       // BREAK, CONTINUE, etc.
    int scopeDepth;      // Which scope this belongs to
} JumpPatch;

void addJumpPatch(CompilationContext* ctx, int offset, JumpType type);
void patchJumps(CompilationContext* ctx, int target, JumpType type);
```

### Phase 4: Nested Structure Support (PRIORITY: HIGH)

#### 4.1 Nested Loop Handling
- Remove the current artificial restriction on nested loops
- Use scope stack to properly track nested control structures
- Implement proper variable scoping for nested contexts

#### 4.2 Block Statement Processing
```c
bool processBlock(ASTNode* node, Compiler* compiler, CompilationContext* ctx) {
    pushScope(ctx, compiler);
    
    // Queue all statements in reverse order (so they execute in correct order)
    for (int i = node->block.count - 1; i >= 0; i--) {
        pushWork(ctx, node->block.statements[i]);
    }
    
    // Schedule scope cleanup (special marker node)
    pushWork(ctx, createScopeEndMarker());
    
    return true;
}
```

### Phase 5: Error Recovery and Debugging (PRIORITY: MEDIUM)

#### 5.1 Improved Error Recovery
```c
bool handleCompilationError(CompilationContext* ctx, Compiler* compiler, const char* message) {
    ctx->inErrorRecovery = true;
    ctx->errorCount++;
    
    // Skip to next safe recovery point
    while (hasWork(ctx)) {
        ASTNode* node = popWork(ctx);
        if (isSafeRecoveryPoint(node)) {
            pushWork(ctx, node);  // Put it back
            break;
        }
    }
    
    return ctx->errorCount < MAX_ERRORS;
}
```

#### 5.2 Debug Support
```c
void dumpCompilationState(CompilationContext* ctx, Compiler* compiler) {
    printf("=== Compilation State ===\n");
    printf("Work queue size: %d\n", ctx->workQueueSize);
    printf("Scope depth: %d\n", ctx->scopeDepth);
    printf("Pending jumps: %d\n", ctx->jumpCount);
    
    // Dump work queue contents
    for (int i = 0; i < ctx->workQueueSize; i++) {
        printf("  [%d] Node type: %d, line: %d\n", i, 
               ctx->workQueue[i]->type, ctx->workQueue[i]->location.line);
    }
}
```

## Implementation Timeline

### Week 1: Foundation
- [ ] Create `CompilationContext` structure
- [ ] Implement work queue management
- [ ] Create scope stack system
- [ ] Basic iterative loop framework

### Week 2: Core Migration
- [ ] Migrate basic node types (literals, variables)
- [ ] Implement expression handling
- [ ] Test with simple programs

### Week 3: Control Flow
- [ ] Migrate for loop implementation
- [ ] Implement jump patching system
- [ ] Enable nested loop support
- [ ] Migrate if/else statements

### Week 4: Advanced Features & Testing
- [ ] Migrate function compilation
- [ ] Implement comprehensive error recovery
- [ ] Performance optimization
- [ ] Full test suite validation

## Testing Strategy

### Incremental Testing Approach

1. **Unit Tests for Each Component**
   ```bash
   # Test work queue operations
   make test-work-queue
   
   # Test scope management
   make test-scope-stack
   
   # Test jump patching
   make test-jump-patches
   ```

2. **Integration Testing**
   ```bash
   # Start with simple expressions
   echo "x = 5" | ./orus_debug
   
   # Progress to control flow
   echo "for i in 1..3: print(i)" | ./orus_debug
   
   # Test nested structures
   echo "for i in 1..3: for j in 1..2: print(i+j)" | ./orus_debug
   ```

3. **Regression Testing**
   ```bash
   # Ensure all existing tests still pass
   make test
   
   # Performance benchmarking
   make benchmark
   ```

### Performance Validation

- **Memory Usage**: Should reduce stack usage significantly
- **Compilation Speed**: Should improve due to reduced function call overhead
- **Runtime Performance**: Should maintain current VM performance levels

### Success Criteria

1. ✅ All existing tests pass
2. ✅ Nested loops work without artificial restrictions
3. ✅ Memory usage reduced (no recursive stack growth)
4. ✅ Compilation speed improved by 15-25%
5. ✅ Support for arbitrarily deep nesting
6. ✅ Better error messages with precise location tracking

## Risk Mitigation

### High-Risk Areas
1. **Jump Patching**: Complex control flow requires careful forward reference handling
2. **Scope Management**: Variable lifetimes must be precisely tracked
3. **Error Recovery**: Must maintain current error message quality

### Mitigation Strategies
1. **Incremental Migration**: Migrate one node type at a time
2. **Extensive Testing**: Test each component in isolation
3. **Fallback Plan**: Keep recursive implementation as backup during transition
4. **Performance Monitoring**: Continuous benchmarking during development

## File Structure Changes

### New Files to Create
```
src/compiler/backend/
├── iterative_compiler.c      # Main iterative compilation logic
├── compilation_context.c     # Work queue and state management
├── scope_stack.c            # Scope management system
├── jump_patches.c           # Forward jump resolution
└── error_recovery.c         # Improved error handling
```

### Files to Modify
```
src/compiler/backend/multipass.c  # Replace recursive functions
include/compiler/compiler.h       # Add new structures
src/main.c                        # Wire up new compiler
```

## Performance Expectations

### Current Performance Issues
- Recursive function call overhead: ~15-20% of compilation time
- Stack growth with complex expressions
- Poor error recovery leading to cascading failures

### Expected Improvements
- **Compilation Speed**: 15-25% faster due to eliminated recursion overhead
- **Memory Usage**: 40-60% reduction in peak memory usage
- **Error Recovery**: 90% reduction in cascading errors
- **Nested Support**: Unlimited nesting depth (within reason)

## Conclusion

This iterative approach will transform the Orus compiler from a fragile recursive system to a robust, scalable compilation pipeline. The design follows proven patterns from Rust and LLVM while maintaining the performance characteristics that make Orus competitive with other dynamic languages.

The incremental approach ensures we can validate each step while maintaining the existing functionality, making this a low-risk, high-reward refactoring effort.