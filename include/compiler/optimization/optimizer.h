/*
 * Orus Language Project
 * ---------------------------------------------------------------------------
 * File: include/compiler/optimization/optimizer.h
 * Author: Jordy Orel KONDA
 * Copyright (c) 2025 Jordy Orel KONDA
 * License: MIT License (see LICENSE file in the project root)
 * Description: Coordinator interface for invoking backend optimization passes on Orus
 *              bytecode.
 */

#ifndef OPTIMIZER_H
#define OPTIMIZER_H

#include "compiler/typed_ast.h"
#include <stdbool.h>
#include <stddef.h>

// Forward declarations for future implementation
typedef struct ConstantTable ConstantTable;
typedef struct UsageAnalysis UsageAnalysis;
typedef struct ExpressionCache ExpressionCache;

typedef struct OptimizationContext OptimizationContext;

typedef struct OptimizationPassResult {
    bool success;
    int optimizations_applied;
    int nodes_eliminated;
    int constants_folded;
    int binary_expressions_folded;
} OptimizationPassResult;

typedef OptimizationPassResult (*OptimizationPassFunction)(TypedASTNode* node, OptimizationContext* ctx);

typedef struct OptimizationPass {
    const char* name;
    bool enabled;
    OptimizationPassFunction run;
} OptimizationPass;

struct OptimizationContext {
    OptimizationPass* passes;
    size_t pass_count;
    size_t pass_capacity;

    // Analysis results (TODO: implement in advanced phases)
    ConstantTable* constants;          // Known constant values
    UsageAnalysis* usage;              // Variable usage tracking
    ExpressionCache* expressions;      // Common expressions

    // Statistics for reporting
    int optimizations_applied;
    int nodes_eliminated;
    int constants_folded;
    int binary_expressions_folded;      // Specific to constant folding

    // Debug information
    bool verbose_output;               // Enable detailed optimization logging
};

// Core optimization functions
OptimizationContext* init_optimization_context(void);
TypedASTNode* optimize_typed_ast(TypedASTNode* input, OptimizationContext* ctx);
void free_optimization_context(OptimizationContext* ctx);

// Pass management helpers
bool set_optimization_pass_enabled(OptimizationContext* ctx, const char* name, bool enabled);
bool toggle_optimization_pass(OptimizationContext* ctx, const char* name);
bool is_optimization_pass_enabled(OptimizationContext* ctx, const char* name);

// Utility functions for optimization
bool is_constant_literal(TypedASTNode* node);
Value evaluate_constant_binary(const char* op, Value left, Value right);
TypedASTNode* create_constant_typed_node(Value value, Type* type);

// Statistics and reporting
void print_optimization_stats(OptimizationContext* ctx);
void reset_optimization_stats(OptimizationContext* ctx);

#endif // OPTIMIZER_H
