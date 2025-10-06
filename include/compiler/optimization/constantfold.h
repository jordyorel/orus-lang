// Orus Language Project
// ---------------------------------------------------------------------------
// File: include/compiler/optimization/constantfold.h
// Author: Jordy Orel KONDA
// Copyright (c) 2025 Jordy Orel KONDA
// License: MIT License (see LICENSE file in the project root)
// Description: Declares constant folding optimizations applied to the typed AST and emitted bytecode.


#ifndef CONSTANTFOLD_H
#define CONSTANTFOLD_H

#include "compiler/typed_ast.h"

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
bool apply_constant_folding(TypedASTNode* ast, ConstantFoldContext* ctx);
bool apply_constant_folding_recursive(TypedASTNode* ast, ConstantFoldContext* ctx);

// Individual folding functions
bool fold_binary_expression(TypedASTNode* node, ConstantFoldContext* ctx);
bool fold_unary_expression(TypedASTNode* node, ConstantFoldContext* ctx);
void fold_ast_node_directly(ASTNode* node, ConstantFoldContext* ctx);

// Helper functions
bool is_foldable_binary(TypedASTNode* node);
Value evaluate_binary_operation(Value left, const char* op, Value right);
bool has_overflow(Value left, const char* op, Value right);

// Statistics and reporting
void init_constant_fold_context(ConstantFoldContext* ctx);
void print_constant_fold_statistics(ConstantFoldContext* ctx);

#endif // CONSTANTFOLD_H
