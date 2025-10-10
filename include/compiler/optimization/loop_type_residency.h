#ifndef LOOP_TYPE_RESIDENCY_H
#define LOOP_TYPE_RESIDENCY_H

#include "compiler/optimization/optimizer.h"

OptimizationPassResult run_loop_type_residency_pass(TypedASTNode* ast, OptimizationContext* ctx);

#endif // LOOP_TYPE_RESIDENCY_H
