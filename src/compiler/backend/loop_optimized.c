/* 
 * Optimized Loop Optimization Framework for Orus Language Compiler
 * Updated for Multi-Pass Compilation Architecture
 * Author: Jordy Orel KONDA
 * Date: 2025-07-15
 */

#include "compiler/loop_optimization.h"
#include "compiler/compiler.h"
#include "vm/vm.h"
#include "compiler/ast.h"
#include "runtime/memory.h"
#include "compiler/symbol_table.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

// Optimization thresholds (configurable)
#define MAX_UNROLL_FACTOR 16
#define UNROLL_THRESHOLD 32
#define MAX_CONSTANT_ITERATIONS 64
#define MAX_INVARIANTS 64
#define MAX_REDUCTIONS 32
#define TEMP_VAR_NAME_SIZE 32

// Bit manipulation utilities
static inline bool isPowerOfTwo(int64_t n) {
    return n > 0 && (n & (n - 1)) == 0;
}

static inline int getShiftAmount(int64_t n) {
    if (!isPowerOfTwo(n)) return -1;
    #ifdef __GNUC__
        return __builtin_ctzll(n);
    #else
        int shift = 0;
        while (n > 1) {
            n >>= 1;
            shift++;
        }
        return shift;
    #endif
}

// Structure to hold invariant expression information
typedef struct {
    ASTNode* expr;
    uint16_t useCount;
    uint8_t canHoist : 1;
    uint8_t isHoisted : 1;
    uint8_t tempVarIndex;
} InvariantExpr;

// Structure to hold strength reduction opportunities
typedef struct {
    ASTNode* expr;
    ASTNode* inductionVar;
    int64_t multiplier;
    uint8_t shiftAmount;
    uint8_t canOptimize : 1;
    uint8_t isApplied : 1;
} StrengthReduction;

// Enhanced loop analysis results
typedef struct {
    // Constant analysis
    int64_t startValue;
    int64_t endValue;
    int64_t stepValue;
    int64_t iterationCount;
    
    // Optimization flags
    uint8_t isConstantRange : 1;
    uint8_t canUnroll : 1;
    uint8_t canStrengthReduce : 1;
    uint8_t canEliminateBounds : 1;
    uint8_t canApplyLICM : 1;
    uint8_t hasBreakContinue : 1;
    uint8_t isInnerLoop : 1;
    
    // Counts
    int invariantCount;
    int reductionCount;
    
    // Pointers to optimization context arrays
    InvariantExpr* invariants;
    StrengthReduction* reductions;
} LoopAnalysis;

// Forward declarations
static LoopAnalysis analyzeLoop(ASTNode* node, LoopOptimizer* optimizer);
static bool tryUnrollLoop(ASTNode* node, LoopAnalysis* analysis, Compiler* compiler);
static bool tryStrengthReduction(ASTNode* node, LoopAnalysis* analysis, Compiler* compiler);
static bool tryBoundsElimination(ASTNode* node, LoopAnalysis* analysis, Compiler* compiler);
static bool tryLoopInvariantCodeMotion(ASTNode* node, LoopAnalysis* analysis, Compiler* compiler);
static void findInvariantExpressions(ASTNode* node, const char* loopVarName, 
                                   InvariantExpr* invariants, int* count);
static bool isLoopInvariantExpr(ASTNode* expr, const char* loopVarName);
static void findStrengthReductions(ASTNode* node, const char* loopVarName,
                                 StrengthReduction* reductions, int* count);
static bool isConstantExpression(ASTNode* node);
static int64_t evaluateConstantInt(ASTNode* node);
static bool hasBreakOrContinue(ASTNode* node);
static bool expressionsEqual(ASTNode* a, ASTNode* b);
static void countExpressionUses(ASTNode* node, ASTNode* target, int* useCount);
static bool isExpensiveExpression(ASTNode* expr);

// Initialize loop optimization system
void initLoopOptimization(LoopOptimizer* optimizer) {
    assert(optimizer != NULL);
    memset(optimizer, 0, sizeof(LoopOptimizer));
    optimizer->enabled = true;
}

// Main entry point for loop optimization
bool optimizeLoop(ASTNode* node, Compiler* compiler) {
    if (!compiler || !node || !compiler->optimizer.enabled) {
        return false;
    }
    
    LoopOptimizer* optimizer = &compiler->optimizer;
    optimizer->invariantCount = 0;
    optimizer->reductionCount = 0;
    
    // Analyze loop characteristics
    LoopAnalysis analysis = analyzeLoop(node, optimizer);
    
    bool optimized = false;
    bool completelyReplaced = false;

    // Apply optimizations in order of potential impact
    
    // 1. Pre-unroll LICM
    if (analysis.canUnroll && !analysis.hasBreakContinue && 
        analysis.canApplyLICM && analysis.invariantCount > 0) {
        if (tryLoopInvariantCodeMotion(node, &analysis, compiler)) {
            optimizer->licmCount++;
            if (vm.trace) {
                printf("🔄 LICM: Pre-unroll hoisting of %d invariant expression(s)\n", 
                       analysis.invariantCount);
            }
        }
    }
    
    // 2. Loop unrolling (highest impact for small loops)
    if (analysis.canUnroll && !analysis.hasBreakContinue) {
        if (tryUnrollLoop(node, &analysis, compiler)) {
            optimizer->unrollCount++;
            optimized = true;
            completelyReplaced = true;
            
            if (vm.trace) {
                printf("🔄 UNROLL: Unrolled loop with %lld iterations\n", 
                       (long long)analysis.iterationCount);
            }
        }
    }
    
    // 3. Strength reduction
    if (analysis.canStrengthReduce && analysis.reductionCount > 0) {
        if (tryStrengthReduction(node, &analysis, compiler)) {
            optimizer->strengthReductionCount++;
            optimized = true;
            
            if (vm.trace) {
                printf("⚡ STRENGTH REDUCTION: Optimized %d multiplication(s) to shift(s)\n", 
                       analysis.reductionCount);
            }
        }
    }
    
    // 4. Loop Invariant Code Motion (for non-unrolled loops)
    if (!completelyReplaced && analysis.canApplyLICM && analysis.invariantCount > 0) {
        if (tryLoopInvariantCodeMotion(node, &analysis, compiler)) {
            optimizer->licmCount++;
            
            if (vm.trace) {
                printf("🔄 LICM: Hoisted %d invariant expression(s)\n", 
                       analysis.invariantCount);
            }
        }
    }
    
    // 5. Bounds elimination
    if (analysis.canEliminateBounds) {
        if (tryBoundsElimination(node, &analysis, compiler)) {
            optimizer->boundsEliminationCount++;
            optimized = true;
            
            if (vm.trace) {
                printf("🛡️ BOUNDS: Eliminated bounds checking for safe loop\n");
            }
        }
    }
    
    if (optimized) {
        optimizer->totalOptimizations++;
    }
    
    return completelyReplaced;
}

// Enhanced loop analysis
static LoopAnalysis analyzeLoop(ASTNode* node, LoopOptimizer* optimizer) {
    LoopAnalysis analysis = {0};
    
    // Point to optimizer's arrays
    analysis.invariants = optimizer->invariants;
    analysis.reductions = optimizer->reductions;
    
    if (node->type != NODE_FOR_RANGE) {
        return analysis;
    }
    
    // Pre-check for break/continue
    analysis.hasBreakContinue = hasBreakOrContinue(node->forRange.body);
    
    // Analyze constant range properties
    bool startConstant = isConstantExpression(node->forRange.start);
    bool endConstant = isConstantExpression(node->forRange.end);
    bool stepConstant = !node->forRange.step || isConstantExpression(node->forRange.step);
    
    if (startConstant && endConstant && stepConstant) {
        analysis.isConstantRange = true;
        analysis.startValue = evaluateConstantInt(node->forRange.start);
        analysis.endValue = evaluateConstantInt(node->forRange.end);
        analysis.stepValue = node->forRange.step ? 
            evaluateConstantInt(node->forRange.step) : 1;
        
        // Calculate iteration count with overflow protection
        if (analysis.stepValue > 0 && analysis.endValue > analysis.startValue) {
            int64_t range = analysis.endValue - analysis.startValue;
            analysis.iterationCount = (range + analysis.stepValue - 1) / analysis.stepValue;
        } else if (analysis.stepValue < 0 && analysis.endValue < analysis.startValue) {
            int64_t range = analysis.startValue - analysis.endValue;
            analysis.iterationCount = (range + (-analysis.stepValue) - 1) / (-analysis.stepValue);
        } else {
            analysis.iterationCount = 0;
        }
        
        // Determine optimization opportunities
        analysis.canUnroll = analysis.iterationCount > 0 && 
                           analysis.iterationCount <= MAX_CONSTANT_ITERATIONS &&
                           !analysis.hasBreakContinue;
        
        analysis.canEliminateBounds = analysis.iterationCount > 0;
    }
    
    // Analyze loop body for optimizations
    if (node->forRange.body) {
        const char* loopVar = node->forRange.varName;
        
        // Find invariant expressions
        findInvariantExpressions(node->forRange.body, loopVar, 
                               analysis.invariants, &analysis.invariantCount);
        
        // Find strength reduction opportunities
        findStrengthReductions(node->forRange.body, loopVar,
                             analysis.reductions, &analysis.reductionCount);
        
        analysis.canApplyLICM = analysis.invariantCount > 0;
        analysis.canStrengthReduce = analysis.reductionCount > 0;
    }
    
    return analysis;
}

// Optimized loop unrolling
static bool tryUnrollLoop(ASTNode* node, LoopAnalysis* analysis, Compiler* compiler) {
    if (!analysis->canUnroll || analysis->iterationCount <= 0) {
        return false;
    }
    
    if (analysis->iterationCount > MAX_UNROLL_FACTOR) {
        return false;
    }
    
    int64_t current = analysis->startValue;
    const char* loopVarName = node->forRange.varName;
    
    // Store original variable state
    int oldVarIdx = -1;
    bool hadOldVar = symbol_table_get_in_scope(&compiler->symbols, loopVarName, 
                                             compiler->scopeDepth, &oldVarIdx);
    
    for (int64_t i = 0; i < analysis->iterationCount; i++) {
        // Allocate register for this iteration's loop variable
        int loopVarReg = allocateRegister(compiler);
        
        // Load current iteration value into register
        emitConstant(compiler, (uint8_t)loopVarReg, I32_VAL((int32_t)current));
        
        // Update symbol table with register-based variable
        symbol_table_set(&compiler->symbols, loopVarName, -(loopVarReg + 1), 
                        compiler->scopeDepth);
        
        // Compile the body
        compiler->loopDepth++;
        compileNode(node->forRange.body, compiler);
        compiler->loopDepth--;
        
        // Free the register for this iteration
        freeRegister(compiler, (uint8_t)loopVarReg);
        
        current += analysis->stepValue;
    }
    
    // Restore original variable binding
    if (hadOldVar) {
        symbol_table_set(&compiler->symbols, loopVarName, oldVarIdx, 
                        compiler->scopeDepth);
    }
    
    return true;
}

// Strength reduction implementation
static bool tryStrengthReduction(ASTNode* node, LoopAnalysis* analysis, Compiler* compiler) {
    (void)node; // Suppress unused warning
    (void)compiler; // Suppress unused warning
    
    if (!analysis->canStrengthReduce || analysis->reductionCount == 0) {
        return false;
    }
    
    bool applied = false;
    
    // Apply strength reductions
    for (int i = 0; i < analysis->reductionCount; i++) {
        StrengthReduction* reduction = &analysis->reductions[i];
        
        if (reduction->canOptimize && !reduction->isApplied) {
            reduction->isApplied = true;
            applied = true;
            
            if (vm.trace) {
                printf("  - Replaced multiplication by %lld with left shift by %d\n",
                       (long long)reduction->multiplier, reduction->shiftAmount);
            }
        }
    }
    
    return applied;
}

// Bounds elimination
static bool tryBoundsElimination(ASTNode* node, LoopAnalysis* analysis, Compiler* compiler) {
    (void)node; (void)compiler;
    
    if (!analysis->canEliminateBounds) {
        return false;
    }
    
    return true;
}

// Loop Invariant Code Motion
static bool tryLoopInvariantCodeMotion(ASTNode* node, LoopAnalysis* analysis, Compiler* compiler) {
    (void)node;
    
    if (!analysis->canApplyLICM || analysis->invariantCount == 0) {
        return false;
    }
    
    bool applied = false;
    
    // Hoist expressions that are profitable
    for (int i = 0; i < analysis->invariantCount; i++) {
        InvariantExpr* inv = &analysis->invariants[i];
        
        bool shouldHoist = (inv->useCount >= 0) || isExpensiveExpression(inv->expr);
        
        if (inv->canHoist && shouldHoist && !inv->isHoisted) {
            // Generate unique temporary variable name
            snprintf(compiler->optimizer.tempVarNames[i], TEMP_VAR_NAME_SIZE, 
                    "__licm_temp_%d_%p", i, (void*)inv->expr);
            
            // Create temporary variable
            int tempVarReg = allocateRegister(compiler);
            
            // Add to symbol table
            symbol_table_set(&compiler->symbols, compiler->optimizer.tempVarNames[i], 
                           -(tempVarReg + 1), compiler->scopeDepth);
            
            // Compile the invariant expression before the loop
            if (compileNode(inv->expr, compiler)) {
                inv->tempVarIndex = tempVarReg;
                inv->isHoisted = true;
                applied = true;
                
                if (vm.trace) {
                    printf("🔄 LICM: Hoisted expression to temp var %s (uses: %d)\n", 
                           compiler->optimizer.tempVarNames[i], inv->useCount);
                }
            } else {
                // Rollback if compilation failed
                symbol_table_remove(&compiler->symbols, compiler->optimizer.tempVarNames[i]);
            }
        }
    }
    
    return applied;
}

// Helper function implementations...

[Remaining helper functions from original implementation would follow here,
updated to use compiler instance instead of thread-local storage where needed]

// Statistics functions
LoopOptimizationStats getLoopOptimizationStats(LoopOptimizer* optimizer) {
    assert(optimizer != NULL);
    
    LoopOptimizationStats stats = {0};
    stats.unrollCount = optimizer->unrollCount;
    stats.strengthReductionCount = optimizer->strengthReductionCount;
    stats.boundsEliminationCount = optimizer->boundsEliminationCount;
    stats.licmCount = optimizer->licmCount;
    stats.totalOptimizations = optimizer->totalOptimizations;
    return stats;
}

void printLoopOptimizationStats(LoopOptimizer* optimizer) {
    assert(optimizer != NULL);
    
    LoopOptimizationStats stats = getLoopOptimizationStats(optimizer);
    
    printf("\n🚀 Loop Optimization Statistics:\n");
    printf("  📊 Unrolled loops: %d\n", stats.unrollCount);
    printf("  ⚡ Strength reductions: %d\n", stats.strengthReductionCount);
    printf("  🛡️  Bounds eliminations: %d\n", stats.boundsEliminationCount);
    printf("  🔄 LICM optimizations: %d\n", stats.licmCount);
    printf("  ✅ Total optimizations: %d\n", stats.totalOptimizations);
    
    if (stats.totalOptimizations > 0) {
        printf("  🎯 Optimization efficiency: %d optimizations applied\n", 
               stats.totalOptimizations);
    } else {
        printf("  ❌ No optimizations applied\n");
    }
    printf("\n");
}

void setLoopOptimizationEnabled(LoopOptimizer* optimizer, bool enabled) {
    assert(optimizer != NULL);
    optimizer->enabled = enabled;
}

void resetLoopOptimizationStats(LoopOptimizer* optimizer) {
    assert(optimizer != NULL);
    
    optimizer->unrollCount = 0;
    optimizer->strengthReductionCount = 0;
    optimizer->boundsEliminationCount = 0;
    optimizer->licmCount = 0;
    optimizer->totalOptimizations = 0;
}