#ifndef CONSTANTFOLD_H
#define CONSTANTFOLD_H

#include "compiler/typed_ast.h"
#include "compiler/optimization/optimizer.h"

// Constant folding algorithm implementation
// Transforms binary expressions with literal operands into single literals
// Example: Binary(2, +, 3) → Literal(5)

typedef struct ConstantFoldContext {
    int optimizations_applied;
    int constants_folded;
    int binary_expressions_folded;
    int nodes_eliminated;
} ConstantFoldContext;

// Main constant folding function
bool apply_constant_folding(TypedASTNode* ast, OptimizationContext* opt_ctx);
bool apply_constant_folding_recursive(TypedASTNode* ast);

// Individual folding functions
bool fold_binary_expression(TypedASTNode* node);
bool fold_unary_expression(TypedASTNode* node);
void fold_ast_node_directly(ASTNode* node);
bool fold_arithmetic_operation(TypedASTNode* node);
bool fold_comparison_operation(TypedASTNode* node);

// Helper functions
bool is_foldable_binary(TypedASTNode* node);
Value evaluate_binary_operation(Value left, const char* op, Value right);
bool has_overflow(Value left, const char* op, Value right);

// Statistics and reporting
void init_constant_fold_context(ConstantFoldContext* ctx);
void print_constant_fold_statistics(ConstantFoldContext* ctx);

#endif // CONSTANTFOLD_H