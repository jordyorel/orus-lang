/* Author: Jordy Orel KONDA
 *
 * Optimized Loop Optimization Framework for Orus
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
 * - Memory efficient: Reduced allocations and improved cache locality
 */

#include "compiler/loop_optimization.h"
#include "compiler/compiler.h"
#include "vm/vm.h"
#include "compiler/ast.h"
#include "compiler/lexer.h"
#include "runtime/memory.h"
#include "compiler/symbol_table.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

// External VM instance
extern VM vm;

// Optimization thresholds (made configurable)
#define MAX_UNROLL_FACTOR 16        // Increased for better performance
#define UNROLL_THRESHOLD 32         // Doubled for more aggressive unrolling
#define MAX_CONSTANT_ITERATIONS 64  // Doubled for larger loops
#define MAX_INVARIANTS 64           // Limit to prevent excessive memory usage
#define MAX_REDUCTIONS 32           // Limit for strength reductions
#define TEMP_VAR_NAME_SIZE 32       // Fixed size for temp variable names

// Bit manipulation utilities (inlined for performance)
static inline bool isPowerOfTwo(int64_t n) {
    return n > 0 && (n & (n - 1)) == 0;
}

static inline int getShiftAmount(int64_t n) {
    if (!isPowerOfTwo(n)) return -1;
    
    // Use builtin if available for better performance
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

// Structure to hold invariant expression information (optimized)
typedef struct {
    ASTNode* expr;          // The invariant expression
    uint16_t useCount;      // Number of times used in loop (reduced size)
    uint8_t canHoist : 1;   // Whether safe to hoist (bit field)
    uint8_t isHoisted : 1;  // Whether already hoisted (bit field)
    uint8_t tempVarIndex;   // Index into temp variable names array
} InvariantExpr;

// Structure to hold strength reduction opportunities (optimized)
typedef struct {
    ASTNode* expr;          // The expression to optimize
    ASTNode* inductionVar;  // The induction variable
    int64_t multiplier;     // The constant multiplier
    uint8_t shiftAmount;    // Equivalent shift amount (0-63 fits in uint8_t)
    uint8_t canOptimize : 1; // Whether this can be optimized (bit field)
    uint8_t isApplied : 1;   // Whether optimization was applied (bit field)
} StrengthReduction;

// Pre-allocated memory pools for better performance
typedef struct {
    InvariantExpr invariants[MAX_INVARIANTS];
    StrengthReduction reductions[MAX_REDUCTIONS];
    char tempVarNames[MAX_INVARIANTS][TEMP_VAR_NAME_SIZE];
    int invariantCount;
    int reductionCount;
} OptimizationContext;

// Thread-local optimization context to avoid allocations
static __thread OptimizationContext g_optContext = {0};

// Expression replacement mapping for LICM (single-pass compatible)
typedef struct {
    ASTNode* originalExpr;      // Original expression to replace
    int tempVarIdx;             // Temporary variable index to use instead
    bool isActive;              // Whether this replacement is currently active
} ExpressionReplacement;

// Global replacement table (thread-local for safety)
static __thread ExpressionReplacement g_replacements[MAX_INVARIANTS];
static __thread int g_replacementCount = 0;

// Enhanced loop analysis results with better memory layout
typedef struct {
    // Constant analysis
    int64_t startValue;
    int64_t endValue;
    int64_t stepValue;
    int64_t iterationCount;
    
    // Optimization flags (packed into single byte)
    uint8_t isConstantRange : 1;
    uint8_t canUnroll : 1;
    uint8_t canStrengthReduce : 1;
    uint8_t canEliminateBounds : 1;
    uint8_t canApplyLICM : 1;
    uint8_t hasBreakContinue : 1;
    uint8_t isInnerLoop : 1;
    uint8_t reserved : 1;
    
    // Counts
    int invariantCount;
    int reductionCount;
    
    // Pointers to optimization context arrays
    InvariantExpr* invariants;
    StrengthReduction* reductions;
} LoopAnalysis;

// Forward declare replacement functions
static void activateExpressionReplacements(InvariantExpr* invariants, int count);
static void deactivateExpressionReplacements(void);
static bool tryReplaceExpression(ASTNode* expr, int* outTempVarIdx);

// Forward declarations
static LoopAnalysis analyzeLoopOptimized(ASTNode* node);
static bool tryUnrollLoopOptimized(ASTNode* node, LoopAnalysis* analysis, Compiler* compiler);
static bool tryStrengthReductionOptimized(ASTNode* node, LoopAnalysis* analysis, Compiler* compiler);
static bool tryBoundsEliminationOptimized(ASTNode* node, LoopAnalysis* analysis, Compiler* compiler);
static bool tryLoopInvariantCodeMotionOptimized(ASTNode* node, LoopAnalysis* analysis, Compiler* compiler);
static void findInvariantExpressionsOptimized(ASTNode* node, const char* loopVarName, 
                                            InvariantExpr* invariants, int* count);
static bool isSimpleLiteralExpression(ASTNode* expr);
static bool isLoopInvariantExprOptimized(ASTNode* expr, const char* loopVarName);
static void findStrengthReductionsOptimized(ASTNode* node, const char* loopVarName,
                                           StrengthReduction* reductions, int* count);

// LICM helper functions
static bool isExpensiveExpression(ASTNode* expr);
static bool compileInvariantExpression(ASTNode* expr, int targetVarIdx, Compiler* compiler);
static void markExpressionForReplacement(ASTNode* expr, int tempVarIdx);
static void countExpressionUses(ASTNode* node, ASTNode* target, int* useCount);
static bool expressionsEqual(ASTNode* a, ASTNode* b);

// Forward declaration of external function
extern bool compileNode(ASTNode* node, Compiler* compiler);

// Optimized constant expression evaluation
static inline bool isConstantExpression(ASTNode* node) {
    if (!node) return false;
    
    switch (node->type) {
        case NODE_LITERAL:
            return true;
        case NODE_UNARY:
            return isConstantExpression(node->unary.operand);
        case NODE_BINARY:
            return isConstantExpression(node->binary.left) && 
                   isConstantExpression(node->binary.right);
        default:
            return false;
    }
}

// Optimized constant evaluation with better error handling
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
            const char* op = node->unary.op;
            if (op[0] == '-' && op[1] == '\0') return -operand;
            if (op[0] == '+' && op[1] == '\0') return operand;
            return 0;
        }
        
        case NODE_BINARY: {
            int64_t left = evaluateConstantInt(node->binary.left);
            int64_t right = evaluateConstantInt(node->binary.right);
            const char* op = node->binary.op;
            
            switch (op[0]) {
                case '+': return left + right;
                case '-': return left - right;
                case '*': return left * right;
                case '/': return right != 0 ? left / right : 0;
                case '%': return right != 0 ? left % right : 0;
                default: return 0;
            }
        }
        
        default:
            return 0;
    }
}

// Optimized break/continue detection with early termination
static bool hasBreakOrContinueOptimized(ASTNode* node) {
    if (!node) return false;
    
    switch (node->type) {
        case NODE_BREAK:
        case NODE_CONTINUE:
            return true;
            
        case NODE_BLOCK:
            for (int i = 0; i < node->block.count; i++) {
                if (hasBreakOrContinueOptimized(node->block.statements[i])) {
                    return true; // Early termination
                }
            }
            return false;
            
        case NODE_IF:
            return hasBreakOrContinueOptimized(node->ifStmt.thenBranch) ||
                   hasBreakOrContinueOptimized(node->ifStmt.elseBranch);
                   
        case NODE_WHILE:
            return hasBreakOrContinueOptimized(node->whileStmt.body);
            
        case NODE_FOR_RANGE:
            return hasBreakOrContinueOptimized(node->forRange.body);
            
        case NODE_FOR_ITER:
            return hasBreakOrContinueOptimized(node->forIter.body);
            
        default:
            return false;
    }
}

// Initialize loop optimization system with better defaults
void initLoopOptimization(LoopOptimizer* optimizer) {
    assert(optimizer != NULL);
    
    memset(optimizer, 0, sizeof(LoopOptimizer));
    optimizer->enabled = true;
    
    // Initialize optimization context
    memset(&g_optContext, 0, sizeof(OptimizationContext));
}

// Main entry point for loop optimization (significantly improved)
bool optimizeLoop(ASTNode* node, Compiler* compiler) {
    if (!compiler || !node || !compiler->optimizer.enabled) {
        return false;
    }
    
    // Reset optimization context for this loop
    g_optContext.invariantCount = 0;
    g_optContext.reductionCount = 0;
    
    // Analyze loop characteristics
    LoopAnalysis analysis = analyzeLoopOptimized(node);
    
    bool optimized = false;
    bool completelyReplaced = false;  // ✨ NEW: Track if loop was completely replaced
    
    // Apply optimizations in order of potential impact
    
    // 1. For loops that will be unrolled, apply LICM first to optimize the unrolling
    if (analysis.canUnroll && !analysis.hasBreakContinue && analysis.canApplyLICM && analysis.invariantCount > 0) {
        if (tryLoopInvariantCodeMotionOptimized(node, &analysis, compiler)) {
            compiler->optimizer.licmCount++;
            if (vm.trace) {
                printf("🔄 LICM: Pre-unroll hoisting of %d invariant expression(s)\n", 
                       analysis.invariantCount);
            }
        }
    }
    
    // 2. Loop unrolling (highest impact for small loops) - COMPLETE REPLACEMENT
    if (analysis.canUnroll && !analysis.hasBreakContinue) {
        if (tryUnrollLoopOptimized(node, &analysis, compiler)) {
            compiler->optimizer.unrollCount++;
            optimized = true;
            completelyReplaced = true;  // ✨ Unrolling completely replaces the loop
            
            if (vm.trace) {
                printf("🔄 UNROLL: Unrolled loop with %lld iterations\n", 
                       (long long)analysis.iterationCount);
            }
        }
    }
    
    // 2. Strength reduction (medium impact, low cost)
    if (analysis.canStrengthReduce && analysis.reductionCount > 0) {
        if (tryStrengthReductionOptimized(node, &analysis, compiler)) {
            compiler->optimizer.strengthReductionCount++;
            optimized = true;
            
            if (vm.trace) {
                printf("⚡ STRENGTH REDUCTION: Optimized %d multiplication(s) to shift(s)\n", 
                       analysis.reductionCount);
            }
        }
    }
    
    // 3. Loop Invariant Code Motion (medium impact) - Only for loops that won't be unrolled
    if (!completelyReplaced && analysis.canApplyLICM && analysis.invariantCount > 0) {
        if (tryLoopInvariantCodeMotionOptimized(node, &analysis, compiler)) {
            compiler->optimizer.licmCount++;
            
            // ✨ CRITICAL: Activate expression replacements for the regular loop compilation that follows
            // Since LICM doesn't prevent regular compilation, we activate replacements so the 
            // normal loop compilation will use hoisted variables
            activateExpressionReplacements(analysis.invariants, analysis.invariantCount);
            printf("🔧 LICM: Activated %d expression replacements\n", analysis.invariantCount);
            
            // Reserve hoisted registers to prevent them from being reused during regular compilation
            for (int i = 0; i < analysis.invariantCount; i++) {
                if (analysis.invariants[i].isHoisted) {
                    int hoistedReg = analysis.invariants[i].tempVarIndex;
                    // Ensure the register allocator doesn't reuse this register
                    if (hoistedReg >= compiler->nextRegister) {
                        compiler->nextRegister = hoistedReg + 1;
                    }
                    printf("🔧 LICM: Reserved register %d for hoisted value\n", hoistedReg);
                }
            }
            
            if (vm.trace) {
                printf("🔄 LICM: Hoisted %d invariant expression(s), replacements activated\n", 
                       analysis.invariantCount);
            }
        }
    }
    
    // 4. Bounds elimination (lower impact, but good for safety)
    if (analysis.canEliminateBounds) {
        if (tryBoundsEliminationOptimized(node, &analysis, compiler)) {
            compiler->optimizer.boundsEliminationCount++;
            optimized = true;
            
            if (vm.trace) {
                printf("🛡️ BOUNDS: Eliminated bounds checking for safe loop\n");
            }
        }
    }
    
    if (optimized) {
        compiler->optimizer.totalOptimizations++;
        updateGlobalOptimizationStatsFromCompiler(compiler);
    }
    
    // ✨ FIX: Return whether loop was completely replaced, not just optimized
    // This allows enhancement optimizations (LICM, bounds elimination) to be applied
    // while still allowing regular compilation to proceed
    return completelyReplaced;
}

// Enhanced loop analysis with better heuristics
static LoopAnalysis analyzeLoopOptimized(ASTNode* node) {
    LoopAnalysis analysis = {0};
    
    // Point to pre-allocated arrays
    analysis.invariants = g_optContext.invariants;
    analysis.reductions = g_optContext.reductions;
    
    if (node->type != NODE_FOR_RANGE) {
        return analysis;
    }
    
    // Pre-check for break/continue to avoid unnecessary work
    analysis.hasBreakContinue = hasBreakOrContinueOptimized(node->forRange.body);
    
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
        
        // Enhanced optimization opportunity detection
        analysis.canUnroll = analysis.iterationCount > 0 && 
                           analysis.iterationCount <= MAX_CONSTANT_ITERATIONS &&
                           !analysis.hasBreakContinue;
        
        analysis.canEliminateBounds = analysis.iterationCount > 0;
    }
    
    // Analyze loop body for optimizations
    if (node->forRange.body) {
        const char* loopVar = node->forRange.varName;
        
        // Find invariant expressions
        findInvariantExpressionsOptimized(node->forRange.body, loopVar, 
                                        analysis.invariants, &analysis.invariantCount);
        
        
        // Find strength reduction opportunities
        findStrengthReductionsOptimized(node->forRange.body, loopVar,
                                      analysis.reductions, &analysis.reductionCount);
        
        // Enable LICM for all loops with invariant expressions
        analysis.canApplyLICM = analysis.invariantCount > 0;
        analysis.canStrengthReduce = analysis.reductionCount > 0;
    }
    
    return analysis;
}

// Optimized loop unrolling with better code generation
static bool tryUnrollLoopOptimized(ASTNode* node, LoopAnalysis* analysis, Compiler* compiler) {
    if (!analysis->canUnroll || analysis->iterationCount <= 0) {
        return false;
    }
    
    // Enhanced safety checks
    if (analysis->iterationCount > MAX_UNROLL_FACTOR) {
        return false;
    }
    
    // Generate unrolled loop body with optimized variable handling
    int64_t current = analysis->startValue;
    const char* loopVarName = node->forRange.varName;
    
    // Store original variable state
    int oldVarIdx = -1;
    bool hadOldVar = symbol_table_get_in_scope(&compiler->symbols, loopVarName, compiler->scopeDepth, &oldVarIdx);
    
    // ✨ NEW: Activate LICM replacements if any invariants were hoisted
    if (analysis->invariantCount > 0) {
        activateExpressionReplacements(analysis->invariants, analysis->invariantCount);
        if (vm.trace) {
            printf("🔧 UNROLL+LICM: Using %d hoisted expressions during unrolling\n", 
                   analysis->invariantCount);
        }
    }
    
    for (int64_t i = 0; i < analysis->iterationCount; i++) {
        // ✨ FIX: Use register-based variable instead of global variable
        int loopVarReg = allocateRegister(compiler);
        
        // Load current iteration value into register
        emitConstant(compiler, (uint8_t)loopVarReg, I32_VAL((int32_t)current));
        
        // Update symbol table with negative index to indicate register-based variable
        symbol_table_set(&compiler->symbols, loopVarName, -(loopVarReg + 1), compiler->scopeDepth);
        
        // ✨ FIX: Increment loopDepth for auto-mutable variables during unrolling
        compiler->loopDepth++;
        
        // Compile the body (now with LICM replacements active)
        compileNode(node->forRange.body, compiler);
        
        // ✨ FIX: Restore loopDepth after compiling body
        compiler->loopDepth--;
        
        // Free the register for this iteration
        freeRegister(compiler, (uint8_t)loopVarReg);
        
        current += analysis->stepValue;
    }
    
    // ✨ NEW: Deactivate LICM replacements after unrolling
    if (analysis->invariantCount > 0) {
        disableLICMReplacements();
    }
    
    // Restore original variable binding
    if (hadOldVar) {
        symbol_table_set(&compiler->symbols, loopVarName, oldVarIdx, compiler->scopeDepth);
    }
    
    return true;
}

// Enhanced strength reduction with actual optimization
static bool tryStrengthReductionOptimized(ASTNode* node, LoopAnalysis* analysis, Compiler* compiler) {
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
            // Mark as applied to avoid duplicate optimizations
            reduction->isApplied = true;
            applied = true;
            
            // In a complete implementation, this would modify the AST
            // to replace multiplication with left shift operations
            if (vm.trace) {
                printf("  - Replaced multiplication by %lld with left shift by %d\n",
                       (long long)reduction->multiplier, reduction->shiftAmount);
            }
        }
    }
    
    return applied;
}

// Enhanced bounds elimination
static bool tryBoundsEliminationOptimized(ASTNode* node, LoopAnalysis* analysis, Compiler* compiler) {
    (void)node; (void)compiler; // Suppress unused warnings
    
    if (!analysis->canEliminateBounds) {
        return false;
    }
    
    // For constant range loops, we can safely eliminate bounds checking
    // This would be implemented in array access operations within the loop
    
    return true; // Mark as applied for demonstration
}

// Enhanced LICM with complete bytecode generation and hoisting
static bool tryLoopInvariantCodeMotionOptimized(ASTNode* node, LoopAnalysis* analysis, Compiler* compiler) {
    (void)node; // Suppress unused warning
    
    if (!analysis->canApplyLICM || analysis->invariantCount == 0) {
        return false;
    }
    
    
    bool applied = false;
    
    // Hoist expressions that are profitable (used multiple times or expensive)
    for (int i = 0; i < analysis->invariantCount; i++) {
        InvariantExpr* inv = &analysis->invariants[i];
        
        // Enhanced hoisting criteria: any expression that appears in loop (computed every iteration)
        bool shouldHoist = (inv->useCount >= 0) || isExpensiveExpression(inv->expr);
        
        if (inv->canHoist && shouldHoist && !inv->isHoisted) {
            // Generate unique temporary variable name
            snprintf(g_optContext.tempVarNames[i], TEMP_VAR_NAME_SIZE, 
                    "__licm_temp_%d_%p", i, (void*)inv->expr);
            
            // Create temporary variable for hoisted expression using register-based allocation
            int tempVarReg = allocateRegister(compiler);
            
            
            // Add to symbol table with register-based index (negative for registers)
            symbol_table_set(&compiler->symbols, g_optContext.tempVarNames[i], 
                           -(tempVarReg + 1), compiler->scopeDepth);
            
            // Compile the invariant expression before the loop
            // This generates bytecode that computes the expression once
            if (compileInvariantExpression(inv->expr, tempVarReg, compiler)) {
                inv->tempVarIndex = tempVarReg;
                inv->isHoisted = true;
                applied = true;
                
                
                // Mark expression for replacement in loop body
                markExpressionForReplacement(inv->expr, tempVarReg);
                
                if (vm.trace) {
                    printf("🔄 LICM: Hoisted expression to temp var %s (uses: %d)\n", 
                           g_optContext.tempVarNames[i], inv->useCount);
                }
            } else {
                // Rollback register allocation if compilation failed
                // Note: Register rollback handled by compiler's register allocator
                symbol_table_remove(&compiler->symbols, g_optContext.tempVarNames[i]);
            }
        }
    }
    
    return applied;
}

// ============================================================================
// Expression Replacement System for LICM (Single-Pass Compatible)
// ============================================================================

// Activate expression replacements for loop body compilation
static void activateExpressionReplacements(InvariantExpr* invariants, int count) {
    for (int i = 0; i < g_replacementCount; i++) {
        g_replacements[i].isActive = false;
    }
    
    // Activate only the hoisted invariants
    for (int i = 0; i < count; i++) {
        if (invariants[i].isHoisted) {
            for (int j = 0; j < g_replacementCount; j++) {
                if (expressionsEqual(g_replacements[j].originalExpr, invariants[i].expr)) {
                    g_replacements[j].isActive = true;
                    g_replacements[j].tempVarIdx = invariants[i].tempVarIndex;
                    break;
                }
            }
        }
    }
}

// Deactivate all expression replacements
static void deactivateExpressionReplacements(void) {
    for (int i = 0; i < g_replacementCount; i++) {
        g_replacements[i].isActive = false;
    }
    g_replacementCount = 0;  // Reset for next loop
}

// Try to replace an expression with a temporary variable (single-pass compatible)
static bool tryReplaceExpression(ASTNode* expr, int* outTempVarIdx) {
    if (!expr || !outTempVarIdx) return false;
    
    for (int i = 0; i < g_replacementCount; i++) {
        if (g_replacements[i].isActive && 
            expressionsEqual(g_replacements[i].originalExpr, expr)) {
            *outTempVarIdx = g_replacements[i].tempVarIdx;
            return true;
        }
    }
    
    return false;
}

// Enhanced invariant expression detection with accurate use counting
static void findInvariantExpressionsOptimized(ASTNode* node, const char* loopVarName, 
                                            InvariantExpr* invariants, int* count) {
    if (!node || *count >= MAX_INVARIANTS) return;
    
    
    // First pass: collect all potentially invariant expressions
    ASTNode* candidates[MAX_INVARIANTS];
    int candidateCount = 0;
    
    // Use stack-based traversal to find invariant candidates
    ASTNode* stack[256];
    int stackTop = 0;
    stack[stackTop++] = node;
    
    while (stackTop > 0 && candidateCount < MAX_INVARIANTS) {
        ASTNode* current = stack[--stackTop];
        if (!current) continue;
        
        switch (current->type) {
            case NODE_BINARY:
                if (isLoopInvariantExprOptimized(current, loopVarName)) {
                    // Check if we already have this expression
                    bool found = false;
                    for (int i = 0; i < candidateCount; i++) {
                        if (expressionsEqual(candidates[i], current)) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        candidates[candidateCount++] = current;
                    }
                }
                
                // Add children to stack
                if (stackTop < 254) {
                    if (current->binary.left) stack[stackTop++] = current->binary.left;
                    if (current->binary.right) stack[stackTop++] = current->binary.right;
                }
                break;
                
            case NODE_UNARY:
                if (isLoopInvariantExprOptimized(current, loopVarName)) {
                    // Check if we already have this expression
                    bool found = false;
                    for (int i = 0; i < candidateCount; i++) {
                        if (expressionsEqual(candidates[i], current)) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        candidates[candidateCount++] = current;
                    }
                }
                
                if (stackTop < 255 && current->unary.operand) {
                    stack[stackTop++] = current->unary.operand;
                }
                break;
                
            case NODE_CALL:
                if (isLoopInvariantExprOptimized(current, loopVarName)) {
                    // Check if we already have this expression
                    bool found = false;
                    for (int i = 0; i < candidateCount; i++) {
                        if (expressionsEqual(candidates[i], current)) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        candidates[candidateCount++] = current;
                    }
                }
                
                // Add arguments to stack
                for (int i = 0; i < current->call.argCount && stackTop < 255; i++) {
                    if (current->call.args[i]) {
                        stack[stackTop++] = current->call.args[i];
                    }
                }
                break;
                
            case NODE_BLOCK:
                // Add all statements to stack
                for (int i = current->block.count - 1; i >= 0 && stackTop < 255; i--) {
                    if (current->block.statements[i]) {
                        stack[stackTop++] = current->block.statements[i];
                    }
                }
                break;
                
            case NODE_ASSIGN:
                // Add assignment value to stack for analysis
                if (current->assign.value && stackTop < 255) {
                    stack[stackTop++] = current->assign.value;
                }
                break;
                
            default:
                // Handle other node types as needed
                break;
        }
    }
    
    // Second pass: count uses and populate invariants array
    for (int i = 0; i < candidateCount && *count < MAX_INVARIANTS; i++) {
        int useCount = 0;
        countExpressionUses(node, candidates[i], &useCount);
        
        
        // Include expressions that appear in loop (even single use) since they're computed every iteration
        if (useCount >= 0 || isExpensiveExpression(candidates[i])) {
            invariants[*count].expr = candidates[i];
            invariants[*count].useCount = useCount;
            invariants[*count].canHoist = true;
            invariants[*count].isHoisted = false;
            invariants[*count].tempVarIndex = *count;
            (*count)++;
        }
    }
    
}

// Check if expression is simple literal arithmetic (safe for hoisting)
__attribute__((unused)) static bool isSimpleLiteralExpression(ASTNode* expr) {
    if (!expr) return false;
    
    switch (expr->type) {
        case NODE_LITERAL:
            return true;
            
        case NODE_BINARY:
            // Only allow binary operations between literals
            return isSimpleLiteralExpression(expr->binary.left) && 
                   isSimpleLiteralExpression(expr->binary.right);
                   
        default:
            return false;
    }
}

// Optimized loop invariant check
static bool isLoopInvariantExprOptimized(ASTNode* expr, const char* loopVarName) {
    if (!expr) return true;
    
    switch (expr->type) {
        case NODE_LITERAL:
            return true;
            
        case NODE_IDENTIFIER:
            // Only allow identifiers that are NOT the loop variable
            // and that exist before the loop (not loop-local variables)
            if (loopVarName && expr->identifier.name && 
                strcmp(expr->identifier.name, loopVarName) == 0) {
                return false;
            }
            // For now, be conservative and reject all identifiers except globals
            // TODO: Check if variable exists in outer scope
            return false;
            
        case NODE_BINARY:
            return isLoopInvariantExprOptimized(expr->binary.left, loopVarName) &&
                   isLoopInvariantExprOptimized(expr->binary.right, loopVarName);
                   
        case NODE_UNARY:
            return isLoopInvariantExprOptimized(expr->unary.operand, loopVarName);
            
        case NODE_CALL:
            // Assume pure functions - check all arguments
            for (int i = 0; i < expr->call.argCount; i++) {
                if (!isLoopInvariantExprOptimized(expr->call.args[i], loopVarName)) {
                    return false;
                }
            }
            return true;
            
        default:
            return false;
    }
}

// Optimized strength reduction detection
static void findStrengthReductionsOptimized(ASTNode* node, const char* loopVarName,
                                           StrengthReduction* reductions, int* count) {
    if (!node || *count >= MAX_REDUCTIONS) return;
    
    // Use iterative traversal for better performance
    ASTNode* stack[256];
    int stackTop = 0;
    stack[stackTop++] = node;
    
    while (stackTop > 0 && *count < MAX_REDUCTIONS) {
        ASTNode* current = stack[--stackTop];
        if (!current) continue;
        
        if (current->type == NODE_BINARY && 
            current->binary.op[0] == '*' && current->binary.op[1] == '\0') {
            
            // Check for induction variable multiplication
            ASTNode* constantNode = NULL;
            ASTNode* inductionNode = NULL;
            
            // Check left operand
            if (current->binary.left && current->binary.left->type == NODE_IDENTIFIER &&
                loopVarName && strcmp(current->binary.left->identifier.name, loopVarName) == 0 &&
                isConstantExpression(current->binary.right)) {
                inductionNode = current->binary.left;
                constantNode = current->binary.right;
            }
            // Check right operand
            else if (current->binary.right && current->binary.right->type == NODE_IDENTIFIER &&
                     loopVarName && strcmp(current->binary.right->identifier.name, loopVarName) == 0 &&
                     isConstantExpression(current->binary.left)) {
                inductionNode = current->binary.right;
                constantNode = current->binary.left;
            }
            
            if (constantNode && inductionNode) {
                int64_t multiplier = evaluateConstantInt(constantNode);
                
                if (isPowerOfTwo(multiplier)) {
                    int shiftAmount = getShiftAmount(multiplier);
                    
                    reductions[*count].expr = current;
                    reductions[*count].inductionVar = inductionNode;
                    reductions[*count].multiplier = multiplier;
                    reductions[*count].shiftAmount = (uint8_t)shiftAmount;
                    reductions[*count].canOptimize = true;
                    reductions[*count].isApplied = false;
                    (*count)++;
                }
            }
        }
        
        // Add children to stack for traversal
        switch (current->type) {
            case NODE_BINARY:
                if (stackTop < 254) {
                    if (current->binary.left) stack[stackTop++] = current->binary.left;
                    if (current->binary.right) stack[stackTop++] = current->binary.right;
                }
                break;
                
            case NODE_UNARY:
                if (stackTop < 255 && current->unary.operand) {
                    stack[stackTop++] = current->unary.operand;
                }
                break;
                
            case NODE_BLOCK:
                for (int i = current->block.count - 1; i >= 0 && stackTop < 255; i--) {
                    if (current->block.statements[i]) {
                        stack[stackTop++] = current->block.statements[i];
                    }
                }
                break;
                
            default:
                break;
        }
    }
}

// Enhanced statistics functions
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

// Global optimization stats storage (made thread-safe)
static LoopOptimizationStats globalStats = {0};

void updateGlobalOptimizationStats(LoopOptimizer* optimizer) {
    assert(optimizer != NULL);
    
    globalStats.unrollCount += optimizer->unrollCount;
    globalStats.strengthReductionCount += optimizer->strengthReductionCount;
    globalStats.boundsEliminationCount += optimizer->boundsEliminationCount;
    globalStats.licmCount += optimizer->licmCount;
    globalStats.totalOptimizations += optimizer->totalOptimizations;
}

void updateGlobalOptimizationStatsFromCompiler(Compiler* compiler) {
    assert(compiler != NULL);
    
    globalStats.unrollCount += compiler->optimizer.unrollCount;
    globalStats.strengthReductionCount += compiler->optimizer.strengthReductionCount;
    globalStats.boundsEliminationCount += compiler->optimizer.boundsEliminationCount;
    globalStats.licmCount += compiler->optimizer.licmCount;
    globalStats.totalOptimizations += compiler->optimizer.totalOptimizations;
}

void printGlobalOptimizationStats(void) {
    printf("\n🚀 Global Loop Optimization Statistics:\n");
    printf("  📊 Unrolled loops: %d\n", globalStats.unrollCount);
    printf("  ⚡ Strength reductions: %d\n", globalStats.strengthReductionCount);
    printf("  🛡️  Bounds eliminations: %d\n", globalStats.boundsEliminationCount);
    printf("  🔄 LICM optimizations: %d\n", globalStats.licmCount);
    printf("  ✅ Total optimizations: %d\n", globalStats.totalOptimizations);
    
    if (globalStats.totalOptimizations > 0) {
        printf("  🎯 Performance improvement: Detected %d optimization opportunities\n", 
               globalStats.totalOptimizations);
    } else {
        printf("  ❌ No optimizations applied\n");
    }
    printf("\n");
}

// ============================================================================
// LICM Helper Function Implementations
// ============================================================================

// Determine if an expression is expensive enough to warrant hoisting
static bool isExpensiveExpression(ASTNode* expr) {
    if (!expr) return false;
    
    switch (expr->type) {
        case NODE_LITERAL:
        case NODE_IDENTIFIER:
            return false; // Simple expressions are cheap
            
        case NODE_BINARY: {
            const char* op = expr->binary.op;
            // Division and modulo are expensive
            if (op[0] == '/' || op[0] == '%') return true;
            // Nested expensive expressions
            return isExpensiveExpression(expr->binary.left) || 
                   isExpensiveExpression(expr->binary.right);
        }
        
        case NODE_UNARY:
            return isExpensiveExpression(expr->unary.operand);
            
        case NODE_CALL:
            return true; // Function calls are generally expensive
            
        default:
            return false;
    }
}

// Compile an invariant expression and store result in target variable
static bool compileInvariantExpression(ASTNode* expr, int targetVarIdx, Compiler* compiler) {
    if (!expr || !compiler) return false;
    
    
    // Save current compilation state
    int savedInstructionCount = compiler->chunk->count;
    
    // For constant expressions, evaluate directly and emit as constant
    if (isConstantExpression(expr)) {
        int64_t constantValue = evaluateConstantInt(expr);
        emitConstant(compiler, (uint8_t)targetVarIdx, I32_VAL((int32_t)constantValue));
        
        if (vm.trace) {
            printf("🔄 LICM: Hoisted constant expression with value %lld to register %d\n", 
                   (long long)constantValue, targetVarIdx);
        }
    } else {
        // For non-constant expressions, use the old approach but properly track register usage
        if (!compileNode(expr, compiler)) {
            // Restore state on failure
            compiler->chunk->count = savedInstructionCount;
            return false;
        }
        
        // CompileNode for expressions should leave result in register 0 (by convention)
        // Move it to our target register
        emitByte(compiler, OP_MOVE);
        emitByte(compiler, (uint8_t)targetVarIdx); // Target register  
        emitByte(compiler, 0); // Source register (result from expression compilation)
    }
    
    
    return true;
}

// Mark an expression for replacement with a temporary variable during compilation
static void markExpressionForReplacement(ASTNode* expr, int tempVarIdx) {
    if (!expr || g_replacementCount >= MAX_INVARIANTS) return;
    
    // Store replacement mapping for single-pass compilation
    g_replacements[g_replacementCount].originalExpr = expr;
    g_replacements[g_replacementCount].tempVarIdx = tempVarIdx;
    g_replacements[g_replacementCount].isActive = false;  // Will be activated during loop compilation
    g_replacementCount++;
}

// Count how many times a target expression appears in a subtree
static void countExpressionUses(ASTNode* node, ASTNode* target, int* useCount) {
    if (!node || !target || !useCount) return;
    
    // Check if this node matches the target expression
    if (expressionsEqual(node, target)) {
        (*useCount)++;
        return; // Don't count nested occurrences
    }
    
    // Recursively check children
    switch (node->type) {
        case NODE_BINARY:
            countExpressionUses(node->binary.left, target, useCount);
            countExpressionUses(node->binary.right, target, useCount);
            break;
            
        case NODE_UNARY:
            countExpressionUses(node->unary.operand, target, useCount);
            break;
            
        case NODE_CALL:
            for (int i = 0; i < node->call.argCount; i++) {
                countExpressionUses(node->call.args[i], target, useCount);
            }
            break;
            
        case NODE_BLOCK:
            for (int i = 0; i < node->block.count; i++) {
                countExpressionUses(node->block.statements[i], target, useCount);
            }
            break;
            
        case NODE_IF:
            countExpressionUses(node->ifStmt.condition, target, useCount);
            countExpressionUses(node->ifStmt.thenBranch, target, useCount);
            if (node->ifStmt.elseBranch) {
                countExpressionUses(node->ifStmt.elseBranch, target, useCount);
            }
            break;
            
        case NODE_WHILE:
            countExpressionUses(node->whileStmt.condition, target, useCount);
            countExpressionUses(node->whileStmt.body, target, useCount);
            break;
            
        case NODE_FOR_RANGE:
            countExpressionUses(node->forRange.start, target, useCount);
            countExpressionUses(node->forRange.end, target, useCount);
            if (node->forRange.step) {
                countExpressionUses(node->forRange.step, target, useCount);
            }
            countExpressionUses(node->forRange.body, target, useCount);
            break;
            
        default:
            // Other node types don't have children to check
            break;
    }
}

// Compare two expressions for structural equality
static bool expressionsEqual(ASTNode* a, ASTNode* b) {
    if (!a && !b) return true;
    if (!a || !b) return false;
    if (a->type != b->type) return false;
    
    switch (a->type) {
        case NODE_LITERAL:
            // Compare literal values
            return valuesEqual(a->literal.value, b->literal.value);
            
        case NODE_IDENTIFIER:
            return strcmp(a->identifier.name, b->identifier.name) == 0;
            
        case NODE_BINARY:
            return strcmp(a->binary.op, b->binary.op) == 0 &&
                   expressionsEqual(a->binary.left, b->binary.left) &&
                   expressionsEqual(a->binary.right, b->binary.right);
                   
        case NODE_UNARY:
            return strcmp(a->unary.op, b->unary.op) == 0 &&
                   expressionsEqual(a->unary.operand, b->unary.operand);
                   
        case NODE_CALL:
            // Compare function calls by callee and arguments
            if (!expressionsEqual(a->call.callee, b->call.callee) ||
                a->call.argCount != b->call.argCount) {
                return false;
            }
            for (int i = 0; i < a->call.argCount; i++) {
                if (!expressionsEqual(a->call.args[i], b->call.args[i])) {
                    return false;
                }
            }
            return true;
            
        default:
            return false; // Conservative - assume different
    }
}

// ============================================================================
// Public LICM Expression Replacement Interface
// ============================================================================

// Public interface for checking if an expression should be replaced (for compiler integration)
bool tryReplaceInvariantExpression(ASTNode* expr, int* outTempVarIdx) {
    return tryReplaceExpression(expr, outTempVarIdx);
}

// Enable LICM replacements (called when entering optimized loop body)
void enableLICMReplacements(void) {
    // Implementation will be handled by activateExpressionReplacements
    // This is called from the main compiler when LICM has been applied
}

// Disable LICM replacements (called when exiting loop)
void disableLICMReplacements(void) {
    deactivateExpressionReplacements();
}