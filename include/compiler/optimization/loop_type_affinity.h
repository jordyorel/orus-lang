// Orus Language Project
// ---------------------------------------------------------------------------
// File: include/compiler/optimization/loop_type_affinity.h
// Description: Interface for the loop type affinity optimization pass that plans
//              persistent typed register usage across loop bodies.

#ifndef ORUS_LOOP_TYPE_AFFINITY_H
#define ORUS_LOOP_TYPE_AFFINITY_H

#include "compiler/optimization/optimizer.h"

OptimizationPassResult run_loop_type_affinity_pass(TypedASTNode* ast, OptimizationContext* ctx);

#endif // ORUS_LOOP_TYPE_AFFINITY_H
