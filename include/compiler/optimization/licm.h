/*
 * Orus Language Project
 * ---------------------------------------------------------------------------
 * File: include/compiler/optimization/licm.h
 * Author: Jordy Orel KONDA
 * Copyright (c) 2025 Jordy Orel KONDA
 * License: MIT License (see LICENSE file in the project root)
 * Description: Declares loop-invariant code motion utilities that hoist redundant
 *              computations out of loops.
 */

#ifndef LICM_H
#define LICM_H

#include "compiler/typed_ast.h"
#include "compiler/optimization/optimizer.h"
#include <stdbool.h>

typedef struct LICMStats {
    int invariants_hoisted;
    int loops_optimized;
    int guard_fusions;
    int redundant_guard_fusions;
    bool changed;
} LICMStats;

void init_licm_stats(LICMStats* stats);
bool apply_loop_invariant_code_motion(TypedASTNode* ast, OptimizationContext* opt_ctx);
void print_licm_statistics(const LICMStats* stats);

#endif // LICM_H
