# While Loop Optimization Plan for Orus

## Overview

This document outlines the comprehensive plan for implementing while loop optimizations in Orus, building upon the existing high-performance for loop optimization framework. The goal is to achieve the same level of optimization for while loops that currently makes Orus competitive with LuaJIT and faster than manually optimized code in other languages.

## Current State Analysis

### For Loop Optimizations (Already Implemented ✅)
- **Loop Invariant Code Motion (LICM)** - hoists invariant expressions outside loops
- **Loop unrolling** for constant iteration counts (≤8 iterations)
- **Strength reduction** - converts power-of-2 multiplications to bit shifts
- **Bounds elimination** for safe constant ranges
- **Expression replacement system** for LICM integration
- **Auto-mutable variable handling** during optimization
- **Performance results**: Outperforms LuaJIT, C (GCC -O3), and all manually optimized versions

### While Loop Current Implementation
- ✅ Basic while loop compilation with condition checking
- ✅ Break/continue statement support with jump patching
- ✅ Loop context management with proper scoping
- ❌ **NO optimization framework integration** - this is the key gap

### Architecture Foundation
- **Single-pass compiler design**: All optimizations must work within one forward pass
- **Zero-cost abstractions**: Optimizations add no runtime overhead
- **Conservative safety**: Only apply optimizations when 100% safe
- **Expression replacement system**: Allows LICM to work with single-pass compilation

## Plan for While Loop Optimization

### Phase 1: Extend Loop Analysis Framework

#### 1.1 Add While Loop Analysis to `analyzeLoopOptimized()`

**Current State:**
```c
// src/compiler/loop_optimization.c:350
if (node->type != NODE_FOR_RANGE) {
    return analysis;  // Only handles for loops
}
```

**Target Implementation:**
```c
static LoopAnalysis analyzeLoopOptimized(ASTNode* node) {
    LoopAnalysis analysis = {0};
    
    // Point to pre-allocated arrays
    analysis.invariants = g_optContext.invariants;
    analysis.reductions = g_optContext.reductions;
    
    if (node->type == NODE_FOR_RANGE) {
        // Existing for loop analysis...
    } else if (node->type == NODE_WHILE) {
        // NEW: While loop specific analysis
        analyzeWhileLoop(node, &analysis);
    } else {
        return analysis; // Unsupported loop type
    }
    
    return analysis;
}
```

#### 1.2 While Loop Specific Analysis (`analyzeWhileLoop()`)

**Implementation Strategy:**
```c
static void analyzeWhileLoop(ASTNode* node, LoopAnalysis* analysis) {
    // Pre-check for break/continue to avoid unnecessary work
    analysis->hasBreakContinue = hasBreakOrContinueOptimized(node->whileStmt.body);
    
    // Pattern-based analysis
    WhileLoopPattern pattern = detectWhileLoopPattern(node);
    
    switch (pattern) {
        case SIMPLE_COUNTING_LOOP:
            analyzeCountingLoop(node, analysis);
            break;
        case INFINITE_LOOP_WITH_BREAK:
            analyzeInfiniteLoop(node, analysis);
            break;
        case COMPLEX_CONDITION_LOOP:
            analyzeComplexConditionLoop(node, analysis);
            break;
        default:
            // General analysis for unknown patterns
            analyzeGeneralWhileLoop(node, analysis);
    }
    
    // Common analysis for all while loops
    if (node->whileStmt.body) {
        // Find invariant expressions (reuse existing infrastructure)
        findInvariantExpressionsOptimized(node->whileStmt.body, NULL, 
                                        analysis->invariants, &analysis->invariantCount);
        
        // Find strength reduction opportunities
        findStrengthReductionsOptimized(node->whileStmt.body, NULL,
                                      analysis->reductions, &analysis->reductionCount);
        
        analysis->canApplyLICM = analysis->invariantCount > 0;
        analysis->canStrengthReduce = analysis->reductionCount > 0;
    }
}
```

#### 1.3 While Loop Pattern Detection

**Pattern Types:**
```c
typedef enum {
    SIMPLE_COUNTING_LOOP,    // i < n; i++
    INFINITE_LOOP_WITH_BREAK, // while(true) with break
    COMPLEX_CONDITION_LOOP,   // Multiple conditions with &&/||
    UNKNOWN_PATTERN          // General case
} WhileLoopPattern;
```

**Simple Counting Loop Detection:**
```orus
// Pattern: Simple counting that can be unrolled
mut i = 0
while i < 5:
    // loop body
    i = i + 1
```

**Detection Algorithm:**
1. **Condition Analysis**: Look for `variable < constant` or `variable <= constant`
2. **Increment Detection**: Find `i = i + step` or `i += step` in loop body
3. **Constant Bounds**: Verify start value and bounds are compile-time constants
4. **Iteration Count**: Calculate total iterations if ≤ 8

### Phase 2: Implement While Loop Optimizations

#### 2.1 Loop Invariant Code Motion (LICM) for While Loops

**Implementation Strategy:**
- **Reuse existing LICM infrastructure** from `src/compiler/loop_optimization.c:505`
- **Extend condition analysis** to detect invariant parts of while conditions
- **Add while loop body traversal** to existing `findInvariantExpressionsOptimized()`

**Condition Invariant Detection:**
```c
static void analyzeWhileConditionInvariants(ASTNode* condition, 
                                          InvariantExpr* invariants, 
                                          int* count) {
    // Example: while (i < expensive_calculation() && flag)
    // expensive_calculation() can be hoisted if loop-invariant
    
    if (condition->type == NODE_BINARY) {
        if (isLoopInvariantExprOptimized(condition->binary.left, NULL)) {
            // Add to invariants for hoisting
        }
        if (isLoopInvariantExprOptimized(condition->binary.right, NULL)) {
            // Add to invariants for hoisting
        }
    }
}
```

#### 2.2 While Loop Unrolling (Limited Cases)

**Unrolling Criteria:**
- **Simple counting pattern**: `while (i < constant) { body; i++; }`
- **Small iteration count**: ≤ 8 iterations (same threshold as for loops)
- **No break/continue**: Must be safe to unroll
- **Constant bounds**: Start and end values must be compile-time constants

**Implementation:**
```c
static bool tryUnrollWhileLoopOptimized(ASTNode* node, LoopAnalysis* analysis, 
                                       Compiler* compiler) {
    if (!analysis->canUnroll || analysis->iterationCount <= 0) {
        return false;
    }
    
    // Enhanced safety checks
    if (analysis->iterationCount > MAX_UNROLL_FACTOR || analysis->hasBreakContinue) {
        return false;
    }
    
    // Detect induction variable and increment pattern
    InductionVariable inductionVar = detectInductionVariable(node);
    if (!inductionVar.isValid) {
        return false;
    }
    
    // Generate unrolled loop body (similar to for loop unrolling)
    for (int64_t i = 0; i < analysis->iterationCount; i++) {
        // Set induction variable to current iteration value
        setInductionVariableValue(&inductionVar, 
                                 inductionVar.startValue + i * inductionVar.step);
        
        // Compile the body with current variable value
        compiler->loopDepth++;
        compileNode(node->whileStmt.body, compiler);
        compiler->loopDepth--;
    }
    
    return true;
}
```

#### 2.3 Strength Reduction for While Loops

**Strategy:**
- **Reuse existing framework** from `findStrengthReductionsOptimized()`
- **Induction variable detection** specific to while loops
- **Pattern matching** for common optimization opportunities

**While Loop Strength Reduction:**
```c
static void findWhileLoopStrengthReductions(ASTNode* node, 
                                          StrengthReduction* reductions, 
                                          int* count) {
    // Detect induction variable from while loop pattern
    InductionVariable inductionVar = detectInductionVariable(node);
    
    if (inductionVar.isValid) {
        // Use existing strength reduction detection with induction variable name
        findStrengthReductionsOptimized(node->whileStmt.body, 
                                      inductionVar.name, 
                                      reductions, count);
    }
}
```

#### 2.4 Condition Optimization

**Optimization Types:**
1. **Constant condition folding**: `while (true)` → infinite loop optimization
2. **Condition caching**: Cache expensive condition results
3. **Boolean expression simplification**: Optimize complex conditions

**Implementation:**
```c
static bool optimizeWhileCondition(ASTNode* condition, Compiler* compiler) {
    if (isConstantExpression(condition)) {
        // Constant condition optimization
        int64_t conditionValue = evaluateConstantInt(condition);
        if (conditionValue) {
            // while(true) - infinite loop
            return optimizeInfiniteLoop(compiler);
        } else {
            // while(false) - dead code elimination
            return optimizeDeadLoop(compiler);
        }
    }
    
    // Complex condition optimization
    return optimizeComplexCondition(condition, compiler);
}
```

### Phase 3: Integration with Existing Optimization System

#### 3.1 Modify Main Optimization Entry Point

**Current For Loop Integration (compiler.c:1197):**
```c
case NODE_FOR_RANGE: {
    // Try loop optimization first
    if (optimizeLoop(node, compiler)) {
        // Loop was optimized (e.g., unrolled), no need for regular compilation
        return true;
    }
    // Regular for loop compilation...
}
```

**Target While Loop Integration:**
```c
case NODE_WHILE: {
    // NEW: Try while loop optimization first
    if (optimizeLoop(node, compiler)) {
        // Optimization applied, but still need regular compilation for LICM replacements
        enableLICMReplacements();
    }
    
    // Enter loop context (existing code)
    LoopContext loopCtx;
    // ... existing while loop compilation ...
    
    // Disable LICM replacements after loop
    disableLICMReplacements();
    return true;
}
```

#### 3.2 Optimization Framework Updates

**Update `optimizeLoop()` Function:**
```c
bool optimizeLoop(ASTNode* node, Compiler* compiler) {
    if (!compiler || !node || !compiler->optimizer.enabled) {
        return false;
    }
    
    // Support both for loops and while loops
    if (node->type != NODE_FOR_RANGE && node->type != NODE_WHILE) {
        return false;
    }
    
    // Reset optimization context for this loop
    g_optContext.invariantCount = 0;
    g_optContext.reductionCount = 0;
    
    // Analyze loop characteristics (now supports while loops)
    LoopAnalysis analysis = analyzeLoopOptimized(node);
    
    // Apply optimizations in order of potential impact...
    // (existing optimization application logic)
}
```

### Phase 4: While Loop Optimization Patterns

#### 4.1 Pattern-Based Optimizations

**Simple Counting Loop (High Priority):**
```orus
// Input code
mut i = 0
while i < 5:
    mut x = i * 2
    mut y = x + 1
    print("Value:", y)
    i = i + 1

// Optimized to (unrolled):
// i = 0: x = 0, y = 1, print("Value:", 1)
// i = 1: x = 2, y = 3, print("Value:", 3)
// i = 2: x = 4, y = 5, print("Value:", 5)
// i = 3: x = 6, y = 7, print("Value:", 7)
// i = 4: x = 8, y = 9, print("Value:", 9)
```

**Infinite Loop with Break (Medium Priority):**
```orus
// Input code
while true:
    x = expensive_calculation()  // Loop invariant
    if x > threshold:
        break
    process(x)

// Optimized to (LICM applied):
temp_var = expensive_calculation()  // Hoisted
while true:
    if temp_var > threshold:
        break
    process(temp_var)
```

**Complex Condition (Medium Priority):**
```orus
// Input code
while i < n and flag and expensive_function():
    // body that doesn't modify n, flag, or expensive_function() inputs
    i = i + 1

// Optimized to (condition hoisting):
temp_flag = flag
temp_expensive = expensive_function()
while i < n and temp_flag and temp_expensive:
    // body
    i = i + 1
```

### Phase 5: Performance and Safety

#### 5.1 Safety Constraints

**Conservative Approach:**
- **Only optimize when 100% safe**: No correctness compromises
- **Break/continue detection**: Initially skip optimization if present
- **Variable mutation analysis**: Ensure induction variable detection is accurate
- **Scope safety**: Maintain proper variable scoping during optimization

**Safety Checks:**
```c
static bool isWhileLoopSafeToOptimize(ASTNode* node, LoopAnalysis* analysis) {
    // Check for break/continue statements
    if (analysis->hasBreakContinue) {
        return false;  // Conservative: skip optimization
    }
    
    // Check for complex control flow
    if (hasComplexControlFlow(node->whileStmt.body)) {
        return false;
    }
    
    // Check for function calls that might have side effects
    if (hasSideEffectingCalls(node->whileStmt.body)) {
        return false;  // Conservative approach
    }
    
    return true;
}
```

#### 5.2 Performance Integration

**Benchmarking Framework:**
```c
// Add while loop benchmarks to existing test suite
// tests/benchmarks/while_loop_benchmark.orus
// tests/benchmarks/while_loop_benchmark_optimized.orus

// Performance targets (based on for loop results):
// - Competitive with LuaJIT (19.7ms)
// - Faster than manually optimized C (20.0ms)
// - Significantly faster than Python (32.6ms) and JavaScript (42.9ms)
```

**Statistics Tracking:**
```c
// Add while loop specific counters to LoopOptimizer struct
typedef struct {
    bool enabled;
    int unrollCount;
    int strengthReductionCount;
    int boundsEliminationCount;
    int licmCount;
    int whileLoopOptimizations;  // NEW: While loop specific counter
    int totalOptimizations;
} LoopOptimizer;
```

**Tracing Support:**
```c
// Add while loop optimization tracing (similar to for loops)
if (vm.trace) {
    printf("🔄 WHILE UNROLL: Unrolled while loop with %lld iterations\n", 
           (long long)analysis.iterationCount);
    printf("🔄 WHILE LICM: Hoisted %d invariant expression(s)\n", 
           analysis.invariantCount);
}
```

## Implementation Priority

### Immediate (Week 1)
1. **Extend `analyzeLoopOptimized()`** to handle `NODE_WHILE`
2. **Add LICM support for while loops** (reuse existing infrastructure)
3. **Update `optimizeLoop()` main entry point**
4. **Basic while loop pattern detection**

### Short-term (Week 2)
5. **Implement simple while loop unrolling** for counting patterns
6. **Add while loop optimization to compiler integration**
7. **Create comprehensive test cases**
8. **Add optimization statistics tracking**

### Medium-term (Week 3-4)
9. **Advanced pattern recognition** for induction variables
10. **Condition optimization** and caching
11. **Performance benchmarking** and validation
12. **Documentation and examples**

## Key Architecture Decisions

### 1. Maximum Code Reuse
- **Leverage existing infrastructure**: Reuse LICM, strength reduction, and expression replacement systems
- **Extend rather than duplicate**: Add while loop support to existing functions
- **Maintain consistency**: Use same optimization thresholds and safety constraints

### 2. Single-Pass Compatibility
- **No lookahead**: All analysis must work within single forward pass
- **Immediate optimization**: Apply optimizations as loops are encountered
- **Expression replacement**: Use existing system for LICM integration

### 3. Conservative Safety
- **Correctness first**: Never compromise program correctness
- **Gradual enhancement**: Start with simple patterns, expand over time
- **Extensive testing**: Comprehensive test coverage for all optimization patterns

### 4. Performance Focus
- **Competitive targets**: Match for loop optimization performance
- **Measurable impact**: Focus on optimizations with real-world benefits
- **Zero overhead**: Optimizations must not add runtime cost

## Expected Outcomes

### Performance Targets
- **Match for loop performance**: Achieve same optimization effectiveness
- **Competitive with JIT**: Target performance similar to LuaJIT
- **Beat manual optimization**: Outperform manually optimized code in other languages
- **Maintain compilation speed**: No significant impact on compilation time

### Feature Completeness
- **Pattern coverage**: Handle common while loop patterns effectively
- **Optimization breadth**: Apply all applicable optimizations (LICM, unrolling, strength reduction)
- **Safety guarantees**: Maintain correctness in all cases
- **Integration quality**: Seamless integration with existing optimization framework

This plan builds systematically on the excellent foundation already established for for loop optimization, ensuring that while loops receive the same level of high-performance treatment that makes Orus competitive with the fastest runtime systems available.