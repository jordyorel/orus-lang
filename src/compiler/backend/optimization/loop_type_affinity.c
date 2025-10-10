//  Orus Language Project
//  ---------------------------------------------------------------------------
//  File: src/compiler/backend/optimization/loop_type_affinity.c
//  Description: Analyzes typed loops to plan persistent typed register usage
//               ahead of bytecode emission.

#include "compiler/optimization/loop_type_affinity.h"

#include "type/type.h"

#include <stdbool.h>
#include <string.h>

typedef struct {
    OptimizationContext* ctx;
    int loop_depth;
    int recorded;
} LoopTypeAffinityState;

static const Type* resolve_type(const Type* type) {
    if (!type) {
        return NULL;
    }
    return prune((Type*)type);
}

static bool type_supports_typed_registers(const Type* type) {
    const Type* resolved = resolve_type(type);
    if (!resolved) {
        return false;
    }

    switch (resolved->kind) {
        case TYPE_I32:
        case TYPE_I64:
        case TYPE_U32:
        case TYPE_U64:
        case TYPE_F64:
        case TYPE_BOOL:
            return true;
        default:
            return false;
    }
}

static bool ast_literal_to_double(const ASTNode* node, double* out_value) {
    if (!node || !out_value || node->type != NODE_LITERAL) {
        return false;
    }

    const Value* value = &node->literal.value;
    switch (value->type) {
        case VAL_I32:
            *out_value = (double)value->as.i32;
            return true;
        case VAL_I64:
            *out_value = (double)value->as.i64;
            return true;
        case VAL_U32:
            *out_value = (double)value->as.u32;
            return true;
        case VAL_U64:
            *out_value = (double)value->as.u64;
            return true;
        case VAL_F64:
            *out_value = value->as.f64;
            return true;
        case VAL_NUMBER:
            *out_value = value->as.number;
            return true;
        default:
            break;
    }

    return false;
}

static bool typed_node_constant_number(const TypedASTNode* node, double* out_value) {
    if (!out_value) {
        return false;
    }

    if (!node) {
        *out_value = 1.0;
        return true;
    }

    ASTNode* ast = node->original;
    if (!ast) {
        return false;
    }

    if (ast->type == NODE_LITERAL) {
        return ast_literal_to_double(ast, out_value);
    }

    if (ast->type == NODE_UNARY && ast->unary.op && strcmp(ast->unary.op, "-") == 0) {
        ASTNode* operand = ast->unary.operand;
        double operand_value = 0.0;
        if (operand && ast_literal_to_double(operand, &operand_value)) {
            *out_value = -operand_value;
            return true;
        }
    }

    return false;
}

static bool is_effectively_constant(const TypedASTNode* node) {
    if (!node) {
        return true;
    }
    if (node->isConstant) {
        return true;
    }
    if (node->original && node->original->type == NODE_LITERAL) {
        return true;
    }
    return false;
}

static bool record_for_range_binding(LoopTypeAffinityState* state, TypedASTNode* loop) {
    if (!state || !state->ctx || !loop) {
        return false;
    }

    const TypedASTNode* start_node = loop->typed.forRange.start;
    const TypedASTNode* end_node = loop->typed.forRange.end;
    const TypedASTNode* step_node = loop->typed.forRange.step;

    const Type* start_type = start_node ? start_node->resolvedType : NULL;
    const Type* end_type = end_node ? end_node->resolvedType : NULL;
    const Type* step_type = step_node ? step_node->resolvedType : NULL;

    const Type* candidate_type = start_type ? start_type : end_type;
    if (!candidate_type && step_type) {
        candidate_type = step_type;
    }

    bool prefer_typed = type_supports_typed_registers(candidate_type);
    bool numeric_start = type_supports_typed_registers(start_type);
    bool numeric_end = type_supports_typed_registers(end_type);
    bool numeric_step = type_supports_typed_registers(step_type);
    bool numeric_bounds = numeric_start && numeric_end && prefer_typed;

    bool constant_start = is_effectively_constant(start_node);
    bool constant_end = is_effectively_constant(end_node);
    bool constant_step = is_effectively_constant(step_node);

    double step_value = 0.0;
    bool has_step_value = typed_node_constant_number(step_node, &step_value);
    bool step_positive = false;
    bool step_negative = false;

    if (!step_node) {
        // Default step is +1
        step_positive = true;
        constant_step = true;
    } else if (has_step_value) {
        step_positive = step_value > 0.0;
        step_negative = step_value < 0.0;
    }

    LoopTypeAffinityBinding binding = (LoopTypeAffinityBinding){0};
    binding.loop_node = loop;
    binding.loop_variable_type = candidate_type;
    binding.start_type = start_type;
    binding.end_type = end_type;
    binding.step_type = step_type;
    binding.start_prefers_typed = numeric_start;
    binding.end_prefers_typed = numeric_end;
    binding.step_prefers_typed = numeric_step;
    binding.start_requires_residency = numeric_start && !constant_start;
    binding.end_requires_residency = numeric_end && !constant_end;
    binding.step_requires_residency = numeric_step && !constant_step;
    binding.prefer_typed_registers = prefer_typed;
    binding.proven_numeric_bounds = numeric_bounds;
    binding.has_constant_start = constant_start;
    binding.has_constant_end = constant_end;
    binding.has_constant_step = constant_step;
    binding.step_is_positive = step_positive;
    binding.step_is_negative = step_negative;
    binding.is_inclusive = loop->typed.forRange.inclusive;
    binding.is_range_loop = true;
    binding.is_iterator_loop = false;
    binding.loop_depth = state->loop_depth;

    int index = optimization_add_loop_affinity(state->ctx, &binding);
    if (index >= 0) {
        loop->preferTypedRegister = prefer_typed;
        loop->requiresLoopResidency = prefer_typed && numeric_bounds;
        loop->loopBindingId = index;
        state->recorded += 1;
    }

    return true;
}

static bool record_while_binding(LoopTypeAffinityState* state, TypedASTNode* loop) {
    if (!state || !state->ctx || !loop) {
        return false;
    }

    TypedASTNode* condition = loop->typed.whileStmt.condition;
    if (!condition || !condition->original) {
        return false;
    }

    if (condition->original->type != NODE_BINARY) {
        return false;
    }

    const char* op = condition->original->binary.op;
    if (!op) {
        return false;
    }

    bool is_comparison = (strcmp(op, "<") == 0) || (strcmp(op, "<=") == 0) ||
                         (strcmp(op, ">") == 0) || (strcmp(op, ">=") == 0);
    if (!is_comparison) {
        return false;
    }

    const TypedASTNode* left = condition->typed.binary.left;
    const TypedASTNode* right = condition->typed.binary.right;

    const Type* left_type = left ? left->resolvedType : NULL;
    const Type* right_type = right ? right->resolvedType : NULL;

    bool left_numeric = type_supports_typed_registers(left_type);
    bool right_numeric = type_supports_typed_registers(right_type);
    bool guard_numeric = left_numeric && right_numeric;

    bool left_constant = is_effectively_constant(left);
    bool right_constant = is_effectively_constant(right);

    const Type* candidate_type = NULL;
    if (left_numeric) {
        candidate_type = left_type;
    } else if (right_numeric) {
        candidate_type = right_type;
    }

    LoopTypeAffinityBinding binding = (LoopTypeAffinityBinding){0};
    binding.loop_node = loop;
    binding.loop_variable_type = candidate_type;
    binding.start_type = left_type;
    binding.end_type = right_type;
    binding.step_type = NULL;
    binding.prefer_typed_registers = guard_numeric;
    binding.proven_numeric_bounds = guard_numeric && (left_constant || right_constant);
    binding.has_constant_start = left_constant;
    binding.has_constant_end = right_constant;
    binding.has_constant_step = true;
    binding.step_is_positive = false;
    binding.step_is_negative = false;
    binding.is_inclusive = (strcmp(op, "<=") == 0) || (strcmp(op, ">=") == 0);
    binding.is_range_loop = false;
    binding.is_iterator_loop = false;
    binding.is_while_loop = true;
    binding.loop_depth = state->loop_depth;
    binding.guard_left = left;
    binding.guard_right = right;
    binding.guard_left_type = left_type;
    binding.guard_right_type = right_type;
    binding.guard_operator = op;
    binding.start_prefers_typed = left_numeric;
    binding.end_prefers_typed = right_numeric;
    binding.start_requires_residency = left_numeric && !left_constant;
    binding.end_requires_residency = right_numeric && !right_constant;
    binding.guard_prefers_typed = guard_numeric;
    binding.guard_is_numeric = guard_numeric;
    binding.guard_left_is_constant = left_constant;
    binding.guard_right_is_constant = right_constant;
    binding.guard_left_prefers_typed = left_numeric;
    binding.guard_right_prefers_typed = right_numeric;
    binding.guard_left_requires_residency = left_numeric && !left_constant;
    binding.guard_right_requires_residency = right_numeric && !right_constant;

    int index = optimization_add_loop_affinity(state->ctx, &binding);
    if (index >= 0) {
        loop->preferTypedRegister = guard_numeric;
        loop->requiresLoopResidency = guard_numeric;
        loop->loopBindingId = index;
        state->recorded += 1;
    }

    return true;
}

static bool loop_type_affinity_pre_visit(TypedASTNode* node, void* user_data) {
    LoopTypeAffinityState* state = (LoopTypeAffinityState*)user_data;
    if (!state || !node || !node->original) {
        return true;
    }

    switch (node->original->type) {
        case NODE_FOR_RANGE:
            record_for_range_binding(state, node);
            state->loop_depth += 1;
            break;
        case NODE_FOR_ITER:
        case NODE_WHILE:
            record_while_binding(state, node);
            state->loop_depth += 1;
            break;
        default:
            break;
    }

    return true;
}

static bool loop_type_affinity_post_visit(TypedASTNode* node, void* user_data) {
    LoopTypeAffinityState* state = (LoopTypeAffinityState*)user_data;
    if (!state || !node || !node->original) {
        return true;
    }

    switch (node->original->type) {
        case NODE_FOR_RANGE:
        case NODE_FOR_ITER:
        case NODE_WHILE:
            if (state->loop_depth > 0) {
                state->loop_depth -= 1;
            }
            break;
        default:
            break;
    }

    return true;
}

OptimizationPassResult run_loop_type_affinity_pass(TypedASTNode* ast, OptimizationContext* ctx) {
    OptimizationPassResult result = {0};
    result.success = true;

    if (!ast || !ctx) {
        return result;
    }

    optimization_clear_loop_affinities(ctx);

    LoopTypeAffinityState state = {0};
    state.ctx = ctx;
    state.loop_depth = 0;
    state.recorded = 0;

    TypedASTVisitor visitor = {0};
    visitor.pre = loop_type_affinity_pre_visit;
    visitor.post = loop_type_affinity_post_visit;

    bool ok = typed_ast_visit(ast, &visitor, &state);
    result.success = ok;
    result.optimizations_applied = state.recorded;

    return result;
}
