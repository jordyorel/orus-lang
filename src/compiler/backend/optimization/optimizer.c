//  Orus Language Project
//  ---------------------------------------------------------------------------
//  File: src/compiler/backend/optimization/optimizer.c
//  Author: Jordy Orel KONDA
//  Copyright (c) 2025 Jordy Orel KONDA
//  License: MIT License (see LICENSE file in the project root)
//  Description: Runs orchestrated optimization passes over compiled functions before final emission.

#include "compiler/optimization/optimizer.h"
#include "compiler/optimization/constantfold.h"
#include "compiler/optimization/loop_type_affinity.h"
#include "compiler/optimization/loop_type_residency.h"
#include "runtime/memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Disable all debug output for clean program execution
#define OPTIMIZER_DEBUG 0
#if OPTIMIZER_DEBUG == 0
#define printf(...) ((void)0)
#endif

// ===== OPTIMIZATION COORDINATOR =====
// Orchestrates high-level AST optimizations
// Delegates to specific optimization algorithms

typedef struct OptimizationPassRegistration {
    const char* name;
    bool enabled;
    OptimizationPassFunction fn;
} OptimizationPassRegistration;

static bool ensure_pass_capacity(OptimizationContext* ctx) {
    if (ctx->pass_count < ctx->pass_capacity) {
        return true;
    }

    size_t new_capacity = ctx->pass_capacity == 0 ? 4 : ctx->pass_capacity * 2;
    OptimizationPass* resized = realloc(ctx->passes, new_capacity * sizeof(OptimizationPass));
    if (!resized) {
        return false;
    }

    ctx->passes = resized;
    ctx->pass_capacity = new_capacity;
    return true;
}

static bool register_pass(OptimizationContext* ctx, const OptimizationPassRegistration* registration) {
    if (!ensure_pass_capacity(ctx)) {
        return false;
    }

    OptimizationPass* slot = &ctx->passes[ctx->pass_count++];
    slot->name = registration->name;
    slot->enabled = registration->enabled;
    slot->run = registration->fn;
    return true;
}

static OptimizationPass* find_pass(OptimizationContext* ctx, const char* name) {
    if (!ctx || !name) {
        return NULL;
    }

    for (size_t i = 0; i < ctx->pass_count; ++i) {
        if (strcmp(ctx->passes[i].name, name) == 0) {
            return &ctx->passes[i];
        }
    }

    return NULL;
}

static OptimizationPassResult run_constant_folding_pass(TypedASTNode* ast, OptimizationContext* ctx) {
    (void)ctx;
    OptimizationPassResult result = {0};
    ConstantFoldContext fold_ctx;
    init_constant_fold_context(&fold_ctx);

    bool success = apply_constant_folding(ast, &fold_ctx);
    result.success = success;

    if (success) {
        result.optimizations_applied = fold_ctx.optimizations_applied;
        result.constants_folded = fold_ctx.constants_folded;
        result.binary_expressions_folded = fold_ctx.binary_expressions_folded;
        result.nodes_eliminated = fold_ctx.nodes_eliminated;
    }

    if (!success) {
        printf("[OPTIMIZER] ❌ Constant folding failed\n");
    }

    return result;
}

static OptimizationPassResult run_not_implemented_pass(TypedASTNode* ast, OptimizationContext* ctx, const char* name) {
    (void)ast;
    (void)ctx;
    (void)name;
    OptimizationPassResult result = {0};
    result.success = true;
    printf("[OPTIMIZER] %s not yet implemented\n", name);
    return result;
}

static OptimizationPassResult run_dead_code_elimination_pass(TypedASTNode* ast, OptimizationContext* ctx) {
    return run_not_implemented_pass(ast, ctx, "Dead code elimination");
}

static OptimizationPassResult run_common_subexpression_pass(TypedASTNode* ast, OptimizationContext* ctx) {
    return run_not_implemented_pass(ast, ctx, "Common subexpression elimination");
}

static bool ensure_loop_residency_capacity(OptimizationContext* ctx, size_t required) {
    if (!ctx) {
        return false;
    }

    if (ctx->loop_residency_capacity >= required) {
        return true;
    }

    size_t new_capacity = ctx->loop_residency_capacity == 0 ? 8 : ctx->loop_residency_capacity;
    while (new_capacity < required) {
        new_capacity *= 2;
    }

    LoopTypeResidencyPlan* resized = realloc(ctx->loop_residency_plans,
                                             new_capacity * sizeof(LoopTypeResidencyPlan));
    if (!resized) {
        return false;
    }

    ctx->loop_residency_plans = resized;
    ctx->loop_residency_capacity = new_capacity;
    return true;
}

int optimization_add_loop_residency_plan(OptimizationContext* ctx, const LoopTypeResidencyPlan* plan) {
    if (!ctx || !plan) {
        return -1;
    }

    if (!ensure_loop_residency_capacity(ctx, ctx->loop_residency_count + 1)) {
        return -1;
    }

    LoopTypeResidencyPlan* slot = &ctx->loop_residency_plans[ctx->loop_residency_count];
    *slot = *plan;
    ctx->loop_residency_count++;
    return (int)(ctx->loop_residency_count - 1);
}

const LoopTypeResidencyPlan* optimization_find_loop_residency_plan(const OptimizationContext* ctx,
                                                                   const TypedASTNode* loop_node) {
    if (!ctx || !loop_node || ctx->loop_residency_count == 0) {
        return NULL;
    }

    for (size_t i = 0; i < ctx->loop_residency_count; ++i) {
        const LoopTypeResidencyPlan* plan = &ctx->loop_residency_plans[i];
        if (plan->loop_node == loop_node) {
            return plan;
        }
    }

    return NULL;
}

void optimization_clear_loop_residency_plans(OptimizationContext* ctx) {
    if (!ctx) {
        return;
    }

    ctx->loop_residency_count = 0;
}

static bool ensure_loop_affinity_capacity(OptimizationContext* ctx, size_t required) {
    if (!ctx) {
        return false;
    }

    if (ctx->loop_affinity_capacity >= required) {
        return true;
    }

    size_t new_capacity = ctx->loop_affinity_capacity == 0 ? 8 : ctx->loop_affinity_capacity;
    while (new_capacity < required) {
        new_capacity *= 2;
    }

    LoopTypeAffinityBinding* resized = realloc(ctx->loop_affinity_bindings,
                                               new_capacity * sizeof(LoopTypeAffinityBinding));
    if (!resized) {
        return false;
    }

    ctx->loop_affinity_bindings = resized;
    ctx->loop_affinity_capacity = new_capacity;
    return true;
}

int optimization_add_loop_affinity(OptimizationContext* ctx, const LoopTypeAffinityBinding* binding) {
    if (!ctx || !binding) {
        return -1;
    }

    if (!ensure_loop_affinity_capacity(ctx, ctx->loop_affinity_count + 1)) {
        return -1;
    }

    LoopTypeAffinityBinding* slot = &ctx->loop_affinity_bindings[ctx->loop_affinity_count];
    *slot = *binding;
    ctx->loop_affinity_count++;
    return (int)(ctx->loop_affinity_count - 1);
}

const LoopTypeAffinityBinding* optimization_find_loop_affinity(const OptimizationContext* ctx,
                                                               const TypedASTNode* loop_node) {
    if (!ctx || !loop_node || ctx->loop_affinity_count == 0) {
        return NULL;
    }

    for (size_t i = 0; i < ctx->loop_affinity_count; ++i) {
        const LoopTypeAffinityBinding* binding = &ctx->loop_affinity_bindings[i];
        if (binding->loop_node == loop_node) {
            return binding;
        }
    }

    return NULL;
}

void optimization_clear_loop_affinities(OptimizationContext* ctx) {
    if (!ctx) {
        return;
    }

    ctx->loop_affinity_count = 0;
}

OptimizationContext* init_optimization_context(void) {
    OptimizationContext* ctx = malloc(sizeof(OptimizationContext));
    if (!ctx) return NULL;

    ctx->passes = NULL;
    ctx->pass_count = 0;
    ctx->pass_capacity = 0;

    ctx->constants = NULL;
    ctx->usage = NULL;
    ctx->expressions = NULL;

    ctx->optimizations_applied = 0;
    ctx->nodes_eliminated = 0;
    ctx->constants_folded = 0;
    ctx->binary_expressions_folded = 0;

    ctx->verbose_output = true;

    ctx->loop_residency_plans = NULL;
    ctx->loop_residency_count = 0;
    ctx->loop_residency_capacity = 0;
    ctx->loop_affinity_bindings = NULL;
    ctx->loop_affinity_count = 0;
    ctx->loop_affinity_capacity = 0;

    const OptimizationPassRegistration registrations[] = {
        {"Constant Folding", true, run_constant_folding_pass},
        {"Loop Type Affinity", true, run_loop_type_affinity_pass},
        {"Loop Type Residency", true, run_loop_type_residency_pass},
        {"Dead Code Elimination", false, run_dead_code_elimination_pass},
        {"Common Subexpression Elimination", false, run_common_subexpression_pass},
    };

    size_t registration_count = sizeof(registrations) / sizeof(registrations[0]);
    for (size_t i = 0; i < registration_count; ++i) {
        if (!register_pass(ctx, &registrations[i])) {
            free_optimization_context(ctx);
            return NULL;
        }
    }

    return ctx;
}

void free_optimization_context(OptimizationContext* ctx) {
    if (!ctx) return;

    // TODO: Free analysis structures when implemented
    // free_constant_table(ctx->constants);
    // free_usage_analysis(ctx->usage);
    // free_expression_cache(ctx->expressions);

    free(ctx->passes);
    free(ctx->loop_residency_plans);
    free(ctx->loop_affinity_bindings);
    free(ctx);
}

bool set_optimization_pass_enabled(OptimizationContext* ctx, const char* name, bool enabled) {
    OptimizationPass* pass = find_pass(ctx, name);
    if (!pass) {
        return false;
    }

    pass->enabled = enabled;
    return true;
}

bool toggle_optimization_pass(OptimizationContext* ctx, const char* name) {
    OptimizationPass* pass = find_pass(ctx, name);
    if (!pass) {
        return false;
    }

    pass->enabled = !pass->enabled;
    return true;
}

bool is_optimization_pass_enabled(OptimizationContext* ctx, const char* name) {
    OptimizationPass* pass = find_pass(ctx, name);
    if (!pass) {
        return false;
    }

    return pass->enabled;
}

TypedASTNode* optimize_typed_ast(TypedASTNode* input_ast, OptimizationContext* ctx) {
    if (!input_ast || !ctx) return input_ast;

    printf("[OPTIMIZER] 🚀 Starting production-grade optimization passes...\n");

    for (size_t i = 0; i < ctx->pass_count; ++i) {
        OptimizationPass* pass = &ctx->passes[i];
        if (!pass->enabled) {
            continue;
        }

        printf("[OPTIMIZER] ▶ Running pass: %s\n", pass->name);
        OptimizationPassResult result = pass->run(input_ast, ctx);
        if (!result.success) {
            printf("[OPTIMIZER] ❌ Pass failed: %s\n", pass->name);
            continue;
        }

        ctx->optimizations_applied += result.optimizations_applied;
        ctx->nodes_eliminated += result.nodes_eliminated;
        ctx->constants_folded += result.constants_folded;
        ctx->binary_expressions_folded += result.binary_expressions_folded;
    }

    printf("[OPTIMIZER] ✅ Production-grade optimization passes completed\n");
    return input_ast; // Return optimized AST (same reference, modified in-place)
}
