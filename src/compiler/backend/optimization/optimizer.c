#include "compiler/optimization/optimizer.h"
#include "compiler/optimization/constantfold.h"
#include "runtime/memory.h"
#include <stdio.h>
#include <stdlib.h>

// ===== OPTIMIZATION COORDINATOR =====
// Orchestrates high-level AST optimizations
// Delegates to specific optimization algorithms

OptimizationContext* init_optimization_context(void) {
    OptimizationContext* ctx = malloc(sizeof(OptimizationContext));
    if (!ctx) return NULL;
    
    // Enable primary optimization (constant folding)
    ctx->enable_constant_folding = true;
    ctx->enable_dead_code_elimination = false; // Future phase
    ctx->enable_common_subexpression = false;  // Future phase
    
    // Initialize analysis structures (for future advanced features)
    ctx->constants = NULL;
    ctx->usage = NULL;
    ctx->expressions = NULL;
    
    // Initialize statistics
    ctx->optimizations_applied = 0;
    ctx->nodes_eliminated = 0;
    ctx->constants_folded = 0;
    ctx->binary_expressions_folded = 0;
    
    // Enable detailed logging
    ctx->verbose_output = true;
    
    return ctx;
}

void free_optimization_context(OptimizationContext* ctx) {
    if (!ctx) return;
    
    // TODO: Free analysis structures when implemented
    // free_constant_table(ctx->constants);
    // free_usage_analysis(ctx->usage);
    // free_expression_cache(ctx->expressions);
    
    free(ctx);
}

TypedASTNode* optimize_typed_ast(TypedASTNode* input_ast, OptimizationContext* ctx) {
    if (!input_ast || !ctx) return input_ast;
    
    printf("[OPTIMIZER] 🚀 Starting production-grade optimization passes...\n");
    
    // Phase 1: Constant Folding
    if (ctx->enable_constant_folding) {
        if (!apply_constant_folding(input_ast, ctx)) {
            printf("[OPTIMIZER] ❌ Constant folding failed\n");
            return input_ast;
        }
    }
    
    // Phase 2: Dead Code Elimination (Future)
    if (ctx->enable_dead_code_elimination) {
        printf("[OPTIMIZER] Dead code elimination not yet implemented\n");
    }
    
    // Phase 3: Common Subexpression Elimination (Future)
    if (ctx->enable_common_subexpression) {
        printf("[OPTIMIZER] Common subexpression elimination not yet implemented\n");
    }
    
    printf("[OPTIMIZER] ✅ Production-grade optimization passes completed\n");
    return input_ast; // Return optimized AST (same reference, modified in-place)
}