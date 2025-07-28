#include "compiler/optimizer.h"
#include "compiler/typed_ast.h"
#include "runtime/memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ===== OPTIMIZATION CONTEXT MANAGEMENT =====

OptimizationContext* init_optimization_context(void) {
    OptimizationContext* ctx = malloc(sizeof(OptimizationContext));
    if (!ctx) return NULL;
    
    // Re-enable optimizations with safer implementation
    ctx->enable_constant_folding = true;
    ctx->enable_dead_code_elimination = false; // TODO: Implement in later phase
    ctx->enable_common_subexpression = false;  // TODO: Implement in later phase
    
    // Initialize analysis structures (TODO: implement advanced features)
    ctx->constants = NULL;
    ctx->usage = NULL;
    ctx->expressions = NULL;
    
    // Initialize statistics
    ctx->optimizations_applied = 0;
    ctx->nodes_eliminated = 0;
    ctx->constants_folded = 0;
    ctx->binary_expressions_folded = 0;
    
    // Enable debug output
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

void reset_optimization_stats(OptimizationContext* ctx) {
    if (!ctx) return;
    
    ctx->optimizations_applied = 0;
    ctx->nodes_eliminated = 0;
    ctx->constants_folded = 0;
    ctx->binary_expressions_folded = 0;
}

void print_optimization_stats(OptimizationContext* ctx) {
    if (!ctx) return;
    
    printf("\n=== OPTIMIZATION STATISTICS ===\n");
    printf("Total optimizations applied: %d\n", ctx->optimizations_applied);
    printf("Constants folded: %d\n", ctx->constants_folded);
    printf("Binary expressions folded: %d\n", ctx->binary_expressions_folded);
    printf("Nodes eliminated: %d\n", ctx->nodes_eliminated);
    printf("===============================\n\n");
}

// ===== UTILITY FUNCTIONS =====

bool is_constant_literal(TypedASTNode* node) {
    if (!node || !node->original) return false;
    return node->original->type == NODE_LITERAL;
}

Value evaluate_constant_binary(const char* op, Value left, Value right) {
    Value result;
    result.type = VAL_I32; // Default to i32 for simplicity
    
    if (!op) {
        result.as.i32 = 0;
        return result;
    }
    
    // Handle integer arithmetic (assuming both operands are i32)
    if (left.type == VAL_I32 && right.type == VAL_I32) {
        if (strcmp(op, "+") == 0) {
            result.as.i32 = left.as.i32 + right.as.i32;
        }
        else if (strcmp(op, "-") == 0) {
            result.as.i32 = left.as.i32 - right.as.i32;
        }
        else if (strcmp(op, "*") == 0) {
            result.as.i32 = left.as.i32 * right.as.i32;
        }
        else if (strcmp(op, "/") == 0) {
            result.as.i32 = (right.as.i32 != 0) ? left.as.i32 / right.as.i32 : 0;
        }
        else {
            result.as.i32 = 0; // Unknown operator
        }
    }
    // TODO: Add support for other types (f64, string concatenation, etc.)
    else {
        result.as.i32 = 0; // Unsupported types
    }
    
    return result;
}

TypedASTNode* create_constant_typed_node(Value value, Type* type) {
    // SAFER APPROACH: Instead of creating entirely new nodes, 
    // we'll return NULL here and modify the original node in-place
    // This avoids complex memory management issues during cleanup
    
    (void)value; // Suppress unused parameter warning
    (void)type;  // Suppress unused parameter warning
    
    // TODO: Implement in-place transformation instead
    return NULL;
}

// ===== CONSTANT FOLDING OPTIMIZATION =====

TypedASTNode* constant_folding_pass(TypedASTNode* node, OptimizationContext* ctx) {
    if (!node || !ctx) return node;
    
    // Handle binary expressions for constant folding
    if (node->original->type == NODE_BINARY) {
        if (ctx->verbose_output) {
            printf("[OPTIMIZER] Analyzing binary expression: %s\n", 
                   node->original->binary.op ? node->original->binary.op : "unknown");
        }
        
        // TEMPORARY: Skip actual optimization to avoid hanging issue
        // TODO: Fix the in-place transformation that causes hanging
        if (ctx->verbose_output) {
            printf("[OPTIMIZER] Skipping binary optimization (temporary fix)\n");
        }
        
        // Just recursively process children without optimization
        if (node->typed.binary.left) {
            node->typed.binary.left = constant_folding_pass(node->typed.binary.left, ctx);
        }
        if (node->typed.binary.right) {
            node->typed.binary.right = constant_folding_pass(node->typed.binary.right, ctx);
        }
        
        return node;
    }
    
    // Recursively process other node types
    switch (node->original->type) {
        case NODE_PROGRAM:
            if (node->typed.program.declarations) {
                for (int i = 0; i < node->typed.program.count; i++) {
                    node->typed.program.declarations[i] = 
                        constant_folding_pass(node->typed.program.declarations[i], ctx);
                }
            }
            break;
            
        case NODE_VAR_DECL:
            if (node->typed.varDecl.initializer) {
                node->typed.varDecl.initializer = 
                    constant_folding_pass(node->typed.varDecl.initializer, ctx);
            }
            break;
            
        case NODE_ASSIGN:
            if (node->typed.assign.value) {
                node->typed.assign.value = 
                    constant_folding_pass(node->typed.assign.value, ctx);
            }
            break;
            
        default:
            // For other node types, no optimization yet
            break;
    }
    
    return node;
}

// ===== DEAD CODE ELIMINATION (PLACEHOLDER) =====

TypedASTNode* dead_code_elimination_pass(TypedASTNode* node, OptimizationContext* ctx) {
    // TODO: Implement in advanced phase
    (void)ctx; // Suppress unused parameter warning
    return node;
}

// ===== MAIN OPTIMIZATION ENTRY POINT =====

TypedASTNode* optimize_typed_ast(TypedASTNode* input, OptimizationContext* ctx) {
    if (!input || !ctx) return input;
    
    printf("[OPTIMIZER] 🚀 Starting optimization passes...\n");
    
    // Reset statistics for this optimization run
    reset_optimization_stats(ctx);
    
    TypedASTNode* optimized = input;
    
    // Phase 1: Constant Folding
    if (ctx->enable_constant_folding) {
        printf("[OPTIMIZER] Running constant folding pass...\n");
        optimized = constant_folding_pass(optimized, ctx);
    }
    
    // Phase 2: Dead Code Elimination (TODO: implement)
    if (ctx->enable_dead_code_elimination) {
        printf("[OPTIMIZER] Running dead code elimination pass...\n");
        optimized = dead_code_elimination_pass(optimized, ctx);
    }
    
    // Print optimization results
    print_optimization_stats(ctx);
    
    printf("[OPTIMIZER] ✅ Optimization passes completed\n");
    
    return optimized;
}