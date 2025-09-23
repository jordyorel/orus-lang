#ifndef OPTIMIZER_H
#define OPTIMIZER_H

#include "compiler/typed_ast.h"
#include <stdbool.h>

// Forward declarations for future implementation 
typedef struct ConstantTable ConstantTable;
typedef struct UsageAnalysis UsageAnalysis;
typedef struct ExpressionCache ExpressionCache;

typedef struct OptimizationContext {
    // Optimization flags
    bool enable_constant_folding;       // Fold 2+3 → 5
    bool enable_dead_code_elimination;  // Remove unused variables
    bool enable_common_subexpression;   // Eliminate duplicate expressions
    bool enable_loop_invariant_code_motion; // Hoist loop-invariant declarations
    
    // Analysis results (TODO: implement in advanced phases)
    ConstantTable* constants;          // Known constant values
    UsageAnalysis* usage;              // Variable usage tracking  
    ExpressionCache* expressions;      // Common expressions
    
    // Statistics for reporting
    int optimizations_applied;
    int nodes_eliminated;
    int constants_folded;
    int binary_expressions_folded;      // Specific to constant folding
    int loop_invariants_hoisted;        // LICM statistics
    int loops_optimized;                // Number of loops transformed
    int licm_guard_fusions;             // Count of guards hoisted + fused
    int licm_redundant_guard_fusions;   // Count of redundant guard rewrites

    // Debug information
    bool verbose_output;               // Enable detailed optimization logging
} OptimizationContext;

// Core optimization functions
OptimizationContext* init_optimization_context(void);
TypedASTNode* optimize_typed_ast(TypedASTNode* input, OptimizationContext* ctx);
void free_optimization_context(OptimizationContext* ctx);

// Individual optimization passes
TypedASTNode* constant_folding_pass(TypedASTNode* node, OptimizationContext* ctx);
TypedASTNode* dead_code_elimination_pass(TypedASTNode* node, OptimizationContext* ctx);

// Utility functions for optimization
bool is_constant_literal(TypedASTNode* node);
Value evaluate_constant_binary(const char* op, Value left, Value right);
TypedASTNode* create_constant_typed_node(Value value, Type* type);

// Statistics and reporting
void print_optimization_stats(OptimizationContext* ctx);
void reset_optimization_stats(OptimizationContext* ctx);

#endif // OPTIMIZER_H