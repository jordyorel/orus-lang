/* Author: Jordy Orel KONDA
 *
 * Loop Optimization Framework for Orus
 * 
 * Implements high-performance loop optimizations that work within the single-pass
 * compiler design. All optimizations are applied during the single forward pass
 * through the source code.
 * 
 * Key Design Principles:
 * - Single-pass only: No multiple passes or deferred analysis
 * - Immediate optimization: Apply optimizations as loops are compiled
 * - Zero-cost abstractions: Optimizations must not add runtime overhead
 * - Edge case safety: Comprehensive boundary condition handling
 */

#include "compiler/loop_optimization.h"
#include "compiler/compiler.h"
#include "vm/vm.h"
#include "compiler/ast.h"
#include "compiler/lexer.h"
#include "runtime/memory.h"
#include <string.h>
#include <stdio.h>

// External VM instance
extern VM vm;

// Maximum unroll factor for small loops
#define MAX_UNROLL_FACTOR 8

// Threshold for considering a loop "small" enough to unroll
#define UNROLL_THRESHOLD 16

// Maximum number of iterations for constant range unrolling
#define MAX_CONSTANT_ITERATIONS 32

// Structure to hold loop analysis results
typedef struct {
    bool isConstantRange;
    int64_t startValue;
    int64_t endValue;
    int64_t stepValue;
    int64_t iterationCount;
    bool canUnroll;
    bool canStrengthReduce;
    bool canEliminateBounds;
} LoopAnalysis;

// Forward declarations
static LoopAnalysis analyzeLoop(ASTNode* node);
static bool tryUnrollLoop(ASTNode* node, LoopAnalysis* analysis, Compiler* compiler);
static bool tryStrengthReduction(ASTNode* node, LoopAnalysis* analysis, Compiler* compiler);
static bool tryBoundsElimination(ASTNode* node, LoopAnalysis* analysis, Compiler* compiler);
static bool isConstantExpression(ASTNode* node);
static int64_t evaluateConstantInt(ASTNode* node);
static void emitUnrolledIteration(ASTNode* body, int64_t iterValue, Compiler* compiler, const char* loopVarName);
static bool hasBreakOrContinue(ASTNode* node);

// Declare compileNode function for use in loop unrolling
bool compileNode(ASTNode* node, Compiler* compiler);

// Initialize loop optimization system
void initLoopOptimization(LoopOptimizer* optimizer) {
    optimizer->unrollCount = 0;
    optimizer->strengthReductionCount = 0;
    optimizer->boundsEliminationCount = 0;
    optimizer->totalOptimizations = 0;
    optimizer->enabled = true;
}

// Main entry point for loop optimization
bool optimizeLoop(ASTNode* node, Compiler* compiler) {
    if (!compiler->optimizer.enabled) {
        return false; // Optimizations disabled
    }
    
    // Analyze loop characteristics
    LoopAnalysis analysis = analyzeLoop(node);
    
    // Debug output (can be enabled for debugging)
    // if (analysis.isConstantRange) {
    //     printf("DEBUG: Found constant range loop: %lld..%lld step %lld (%lld iterations)\n", 
    //            analysis.startValue, analysis.endValue, analysis.stepValue, analysis.iterationCount);
    // }
    
    bool optimized = false;
    
    // Try loop unrolling first (highest impact)
    if (analysis.canUnroll && tryUnrollLoop(node, &analysis, compiler)) {
        compiler->optimizer.unrollCount++;
        optimized = true;
    }
    
    // Try strength reduction (if not unrolled)
    if (!optimized && analysis.canStrengthReduce && 
        tryStrengthReduction(node, &analysis, compiler)) {
        compiler->optimizer.strengthReductionCount++;
        optimized = true;
    }
    
    // Try bounds checking elimination
    if (analysis.canEliminateBounds && 
        tryBoundsElimination(node, &analysis, compiler)) {
        compiler->optimizer.boundsEliminationCount++;
        optimized = true;
    }
    
    if (optimized) {
        compiler->optimizer.totalOptimizations++;
    }
    
    return optimized;
}

// Analyze loop characteristics for optimization opportunities
static LoopAnalysis analyzeLoop(ASTNode* node) {
    LoopAnalysis analysis = {0};
    
    // Only analyze for loops with range syntax
    if (node->type != NODE_FOR_RANGE) {
        return analysis;
    }
    
    // Check if start, end, and step are constant expressions
    bool startConstant = isConstantExpression(node->forRange.start);
    bool endConstant = isConstantExpression(node->forRange.end);
    bool stepConstant = !node->forRange.step || isConstantExpression(node->forRange.step);
    
    if (startConstant && endConstant && stepConstant) {
        analysis.isConstantRange = true;
        analysis.startValue = evaluateConstantInt(node->forRange.start);
        analysis.endValue = evaluateConstantInt(node->forRange.end);
        analysis.stepValue = node->forRange.step ? 
            evaluateConstantInt(node->forRange.step) : 1;
        
        // Calculate iteration count
        if (analysis.stepValue > 0 && analysis.endValue > analysis.startValue) {
            analysis.iterationCount = 
                (analysis.endValue - analysis.startValue + analysis.stepValue - 1) / analysis.stepValue;
        } else if (analysis.stepValue < 0 && analysis.endValue < analysis.startValue) {
            analysis.iterationCount = 
                (analysis.startValue - analysis.endValue + (-analysis.stepValue) - 1) / (-analysis.stepValue);
        } else {
            analysis.iterationCount = 0; // Empty range
        }
        
        // Determine optimization opportunities
        analysis.canUnroll = analysis.iterationCount > 0 && 
                           analysis.iterationCount <= MAX_CONSTANT_ITERATIONS;
        analysis.canStrengthReduce = analysis.stepValue != 1 && 
                                   (analysis.stepValue & (analysis.stepValue - 1)) == 0; // Power of 2
        analysis.canEliminateBounds = analysis.iterationCount > 0; // Known safe range
    }
    
    return analysis;
}

// Try to unroll a small constant loop
static bool tryUnrollLoop(ASTNode* node, LoopAnalysis* analysis, Compiler* compiler) {
    if (!analysis->canUnroll || analysis->iterationCount <= 0) {
        return false;
    }
    
    // Safety check: don't unroll very large loops
    if (analysis->iterationCount > MAX_UNROLL_FACTOR) {
        return false;
    }
    
    // Safety check: don't unroll loops with break/continue statements
    if (hasBreakOrContinue(node->forRange.body)) {
        return false;
    }
    
    // Generate unrolled loop body
    int64_t current = analysis->startValue;
    for (int64_t i = 0; i < analysis->iterationCount; i++) {
        emitUnrolledIteration(node->forRange.body, current, compiler, node->forRange.varName);
        current += analysis->stepValue;
    }
    
    return true;
}

// Try strength reduction optimization
static bool tryStrengthReduction(ASTNode* node, LoopAnalysis* analysis, Compiler* compiler) {
    (void)node; (void)compiler; // Suppress unused warnings
    
    if (!analysis->canStrengthReduce) {
        return false;
    }
    
    // For power-of-2 steps, we could optimize multiplication operations
    // within the loop body to use bit shifts instead
    // This would require analyzing the loop body for patterns like:
    // - i * 2 -> i << 1
    // - i * 4 -> i << 2
    // - i * 8 -> i << 3
    
    // For now, we just mark that strength reduction is possible
    // A full implementation would require pattern matching in the loop body
    return false; // Not fully implemented yet
}

// Try bounds checking elimination
static bool tryBoundsElimination(ASTNode* node, LoopAnalysis* analysis, Compiler* compiler) {
    (void)node; (void)compiler; // Suppress unused warnings
    
    if (!analysis->canEliminateBounds) {
        return false;
    }
    
    // For constant range loops, we can eliminate bounds checking in array accesses
    // if we can prove that the loop variable is always within valid bounds
    // This would require:
    // 1. Analyzing array access patterns in the loop body
    // 2. Proving that loop variable + offset is always within array bounds
    // 3. Emitting array access opcodes without bounds checking
    
    // Mark that bounds checking can be eliminated for this loop
    // This would be used by array access operations within the loop
    return false; // Not fully implemented yet
}

// Check if an expression is a compile-time constant
static bool isConstantExpression(ASTNode* node) {
    if (!node) return false;
    
    switch (node->type) {
        case NODE_LITERAL:
            return true;
            
        case NODE_UNARY:
            return isConstantExpression(node->unary.operand);
            
        case NODE_BINARY: {
            return isConstantExpression(node->binary.left) && 
                   isConstantExpression(node->binary.right);
        }
        
        default:
            return false;
    }
}

// Evaluate a constant integer expression
static int64_t evaluateConstantInt(ASTNode* node) {
    if (!node) return 0;
    
    switch (node->type) {
        case NODE_LITERAL: {
            Value val = node->literal.value;
            if (IS_I32(val)) return AS_I32(val);
            if (IS_I64(val)) return AS_I64(val);
            if (IS_U32(val)) return (int64_t)AS_U32(val);
            if (IS_U64(val)) return (int64_t)AS_U64(val);
            return 0;
        }
        
        case NODE_UNARY: {
            int64_t operand = evaluateConstantInt(node->unary.operand);
            if (strcmp(node->unary.op, "-") == 0) {
                return -operand;
            } else if (strcmp(node->unary.op, "+") == 0) {
                return operand;
            }
            return 0;
        }
        
        case NODE_BINARY: {
            int64_t left = evaluateConstantInt(node->binary.left);
            int64_t right = evaluateConstantInt(node->binary.right);
            
            if (strcmp(node->binary.op, "+") == 0) {
                return left + right;
            } else if (strcmp(node->binary.op, "-") == 0) {
                return left - right;
            } else if (strcmp(node->binary.op, "*") == 0) {
                return left * right;
            } else if (strcmp(node->binary.op, "/") == 0) {
                return right != 0 ? left / right : 0;
            } else if (strcmp(node->binary.op, "%") == 0) {
                return right != 0 ? left % right : 0;
            }
            return 0;
        }
        
        default:
            return 0;
    }
}

// Emit an unrolled iteration of the loop body
static void emitUnrolledIteration(ASTNode* body, int64_t iterValue, Compiler* compiler, const char* loopVarName) {
    // For single-pass compilation, we need to emit the loop variable as a constant
    // before compiling the body, so references to it will resolve to the constant
    
    if (body) {
        // Create a temporary variable with the loop variable name set to the constant value
        // This allows the body to reference the loop variable correctly
        
        // Store the constant value in a global variable slot
        // This is a simplified approach - a full implementation would need proper scoping
        int varIdx = vm.variableCount++;
        ObjString* varName = allocateString(loopVarName, (int)strlen(loopVarName));
        vm.variableNames[varIdx].name = varName;
        vm.variableNames[varIdx].length = varName->length;
        vm.globals[varIdx] = I32_VAL((int32_t)iterValue);
        
        // Add to symbol table temporarily
        int oldVarIdx = -1;
        bool hadOldVar = symbol_table_get(&compiler->symbols, loopVarName, &oldVarIdx);
        symbol_table_set(&compiler->symbols, loopVarName, varIdx);
        
        // Set type for the loop variable
        vm.globalTypes[varIdx] = getPrimitiveType(TYPE_I32);
        
        // Compile the body with the loop variable set to the constant
        compileNode(body, compiler);
        
        // Restore the old variable binding if it existed
        if (hadOldVar) {
            symbol_table_set(&compiler->symbols, loopVarName, oldVarIdx);
        }
        // Note: In a full implementation, we'd need proper cleanup of temporary variables
    }
}

// Get optimization statistics
LoopOptimizationStats getLoopOptimizationStats(LoopOptimizer* optimizer) {
    LoopOptimizationStats stats = {0};
    stats.unrollCount = optimizer->unrollCount;
    stats.strengthReductionCount = optimizer->strengthReductionCount;
    stats.boundsEliminationCount = optimizer->boundsEliminationCount;
    stats.totalOptimizations = optimizer->totalOptimizations;
    return stats;
}

// Print optimization statistics
void printLoopOptimizationStats(LoopOptimizer* optimizer) {
    LoopOptimizationStats stats = getLoopOptimizationStats(optimizer);
    
    printf("Loop Optimization Statistics:\n");
    printf("  Unrolled loops: %d\n", stats.unrollCount);
    printf("  Strength reductions: %d\n", stats.strengthReductionCount);
    printf("  Bounds eliminations: %d\n", stats.boundsEliminationCount);
    printf("  Total optimizations: %d\n", stats.totalOptimizations);
}

// Enable or disable loop optimizations
void setLoopOptimizationEnabled(LoopOptimizer* optimizer, bool enabled) {
    optimizer->enabled = enabled;
}

// Reset optimization counters
void resetLoopOptimizationStats(LoopOptimizer* optimizer) {
    optimizer->unrollCount = 0;
    optimizer->strengthReductionCount = 0;
    optimizer->boundsEliminationCount = 0;
    optimizer->totalOptimizations = 0;
}

// Check if a node contains break or continue statements
static bool hasBreakOrContinue(ASTNode* node) {
    if (!node) return false;
    
    switch (node->type) {
        case NODE_BREAK:
        case NODE_CONTINUE:
            return true;
            
        case NODE_BLOCK:
            for (int i = 0; i < node->block.count; i++) {
                if (hasBreakOrContinue(node->block.statements[i])) {
                    return true;
                }
            }
            return false;
            
        case NODE_IF:
            return hasBreakOrContinue(node->ifStmt.thenBranch) ||
                   hasBreakOrContinue(node->ifStmt.elseBranch);
                   
        case NODE_WHILE:
            return hasBreakOrContinue(node->whileStmt.body);
            
        case NODE_FOR_RANGE:
            return hasBreakOrContinue(node->forRange.body);
            
        case NODE_FOR_ITER:
            return hasBreakOrContinue(node->forIter.body);
            
        // For other node types, we assume they don't contain break/continue
        default:
            return false;
    }
}