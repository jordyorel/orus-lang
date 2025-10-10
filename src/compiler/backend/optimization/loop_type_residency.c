//  Orus Language Project
//  ---------------------------------------------------------------------------
//  File: src/compiler/backend/optimization/loop_type_residency.c
//  Description: Identifies loop-invariant operands that can remain in typed
//               registers across loop iterations.

#include "compiler/optimization/loop_type_residency.h"

#include "type/type.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char* name;
    const Type* type;
} IdentifierInfo;

typedef struct {
    IdentifierInfo* items;
    size_t count;
    size_t capacity;
} IdentifierInfoSet;

typedef struct {
    OptimizationContext* ctx;
    int recorded;
} LoopResidencyState;

typedef struct {
    const IdentifierInfoSet* identifiers;
    bool mutated;
} MutationSearch;

typedef struct {
    IdentifierInfoSet* identifiers;
    bool has_missing_type;
} IdentifierCollectState;

static const Type* resolve_type(const Type* type);

static void identifier_info_set_init(IdentifierInfoSet* set) {
    if (!set) {
        return;
    }

    set->items = NULL;
    set->count = 0;
    set->capacity = 0;
}

static void identifier_info_set_free(IdentifierInfoSet* set) {
    if (!set) {
        return;
    }

    free(set->items);
    set->items = NULL;
    set->count = 0;
    set->capacity = 0;
}

static bool identifier_info_set_ensure_capacity(IdentifierInfoSet* set, size_t required) {
    if (!set) {
        return false;
    }

    if (set->capacity >= required) {
        return true;
    }

    size_t new_capacity = set->capacity == 0 ? 4 : set->capacity;
    while (new_capacity < required) {
        new_capacity *= 2;
    }

    IdentifierInfo* resized = realloc(set->items, new_capacity * sizeof(IdentifierInfo));
    if (!resized) {
        return false;
    }

    set->items = resized;
    set->capacity = new_capacity;
    return true;
}

static const IdentifierInfo* identifier_info_set_lookup(const IdentifierInfoSet* set, const char* name) {
    if (!set || !name) {
        return NULL;
    }

    for (size_t i = 0; i < set->count; ++i) {
        if (strcmp(set->items[i].name, name) == 0) {
            return &set->items[i];
        }
    }

    return NULL;
}

static bool identifier_info_set_add(IdentifierInfoSet* set, const char* name, const Type* type) {
    if (!set || !name) {
        return false;
    }

    for (size_t i = 0; i < set->count; ++i) {
        if (strcmp(set->items[i].name, name) == 0) {
            if (!set->items[i].type && type) {
                set->items[i].type = type;
            }
            return true;
        }
    }

    if (!identifier_info_set_ensure_capacity(set, set->count + 1)) {
        return false;
    }

    set->items[set->count].name = name;
    set->items[set->count].type = type;
    set->count += 1;
    return true;
}

static bool identifier_info_set_has_types(const IdentifierInfoSet* set) {
    if (!set) {
        return false;
    }

    for (size_t i = 0; i < set->count; ++i) {
        if (!set->items[i].type) {
            return false;
        }
    }

    return true;
}

static bool identifier_collect_pre_visit(TypedASTNode* node, void* user_data) {
    IdentifierCollectState* state = (IdentifierCollectState*)user_data;
    if (!state || !state->identifiers || !node || !node->original) {
        return true;
    }

    if (node->original->type == NODE_IDENTIFIER) {
        const char* name = node->original->identifier.name;
        const Type* type = resolve_type(node->resolvedType);
        if (!type) {
            state->has_missing_type = true;
        }
        if (!identifier_info_set_add(state->identifiers, name, type)) {
            state->has_missing_type = true;
        }
    }

    return true;
}

static bool identifier_is_in_set(const IdentifierInfoSet* set, const char* name) {
    return identifier_info_set_lookup(set, name) != NULL;
}

typedef struct {
    const IdentifierInfoSet* identifiers;
    bool found;
} IdentifierLookupState;

static bool identifier_lookup_pre_visit(TypedASTNode* node, void* user_data) {
    IdentifierLookupState* state = (IdentifierLookupState*)user_data;
    if (!state || state->found || !node || !node->original) {
        return state ? !state->found : true;
    }

    if (node->original->type == NODE_IDENTIFIER) {
        const char* name = node->original->identifier.name;
        if (identifier_is_in_set(state->identifiers, name)) {
            state->found = true;
            return false;
        }
    }

    return true;
}

static bool expression_references_identifier_in_set(TypedASTNode* expr, const IdentifierInfoSet* set) {
    if (!expr || !set || set->count == 0) {
        return false;
    }

    IdentifierLookupState lookup = { set, false };
    TypedASTVisitor visitor = {0};
    visitor.pre = identifier_lookup_pre_visit;
    typed_ast_visit(expr, &visitor, &lookup);
    return lookup.found;
}

static bool mutation_search_pre_visit(TypedASTNode* node, void* user_data) {
    MutationSearch* search = (MutationSearch*)user_data;
    if (!search || search->mutated || !node || !node->original) {
        return search && !search->mutated;
    }

    const char* target_name = NULL;
    const Type* assigned_type = NULL;

    switch (node->original->type) {
        case NODE_ASSIGN:
            target_name = node->typed.assign.name;
            if (node->typed.assign.value) {
                assigned_type = resolve_type(node->typed.assign.value->resolvedType);
            }
            break;
        case NODE_VAR_DECL:
            target_name = node->original->varDecl.name;
            if (node->typed.varDecl.initializer) {
                assigned_type = resolve_type(node->typed.varDecl.initializer->resolvedType);
            }
            break;
        case NODE_FOR_RANGE:
            target_name = node->typed.forRange.varName;
            assigned_type = resolve_type(node->typed.forRange.start ? node->typed.forRange.start->resolvedType : NULL);
            break;
        case NODE_FOR_ITER:
            target_name = node->typed.forIter.varName;
            if (node->typed.forIter.iterable) {
                assigned_type = resolve_type(node->typed.forIter.iterable->resolvedType);
            }
            break;
        case NODE_MEMBER_ASSIGN:
            if (expression_references_identifier_in_set(node->typed.memberAssign.target, search->identifiers)) {
                search->mutated = true;
                return false;
            }
            break;
        case NODE_ARRAY_ASSIGN:
            if (expression_references_identifier_in_set(node->typed.arrayAssign.target, search->identifiers)) {
                search->mutated = true;
                return false;
            }
            break;
        default:
            break;
    }

    if (target_name && identifier_is_in_set(search->identifiers, target_name)) {
        const IdentifierInfo* info = identifier_info_set_lookup(search->identifiers, target_name);
        const Type* expected_type = info ? resolve_type(info->type) : NULL;
        if (!expected_type || !assigned_type) {
            search->mutated = true;
            return false;
        }

        if (!type_equals_extended((Type*)expected_type, (Type*)assigned_type)) {
            search->mutated = true;
            return false;
        }
    }

    return true;
}

static bool expression_has_type_invariant_identifiers(TypedASTNode* expr, TypedASTNode* loop_body) {
    if (!expr) {
        return false;
    }

    IdentifierInfoSet identifiers;
    identifier_info_set_init(&identifiers);

    IdentifierCollectState collect_state = { &identifiers, false };
    TypedASTVisitor collect_visitor = {0};
    collect_visitor.pre = identifier_collect_pre_visit;
    typed_ast_visit(expr, &collect_visitor, &collect_state);

    bool invariant = true;

    if (collect_state.has_missing_type) {
        invariant = false;
    }

    if (invariant && identifiers.count > 0) {
        if (!identifier_info_set_has_types(&identifiers)) {
            invariant = false;
        } else if (loop_body) {
            MutationSearch search = { &identifiers, false };
            TypedASTVisitor visitor = {0};
            visitor.pre = mutation_search_pre_visit;
            typed_ast_visit(loop_body, &visitor, &search);
            invariant = !search.mutated;
        }
    }

    identifier_info_set_free(&identifiers);
    return invariant;
}

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

static bool node_is_constant(const TypedASTNode* node) {
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

static void record_for_range_plan(LoopResidencyState* state, TypedASTNode* loop) {
    if (!state || !state->ctx || !loop) {
        return;
    }

    TypedASTNode* end_node = loop->typed.forRange.end;
    TypedASTNode* step_node = loop->typed.forRange.step;
    TypedASTNode* body_node = loop->typed.forRange.body;

    bool have_end_hint = false;
    bool have_step_hint = false;

    LoopTypeResidencyPlan plan = {0};
    plan.loop_node = loop;
    plan.range_end_node = end_node;
    plan.range_step_node = step_node;

    if (end_node) {
        bool end_typed = type_supports_typed_registers(end_node->resolvedType);
        bool end_invariant = end_typed && expression_has_type_invariant_identifiers(end_node, body_node);
        if (end_invariant) {
            plan.range_end_prefers_typed = true;
            plan.range_end_requires_residency = !node_is_constant(end_node);
            have_end_hint = true;
        }
    }

    if (step_node) {
        bool step_typed = type_supports_typed_registers(step_node->resolvedType);
        bool step_invariant = step_typed && expression_has_type_invariant_identifiers(step_node, body_node);
        if (step_invariant) {
            plan.range_step_prefers_typed = true;
            plan.range_step_requires_residency = !node_is_constant(step_node);
            have_step_hint = true;
        }
    }

    if (have_end_hint || have_step_hint) {
        if (optimization_add_loop_residency_plan(state->ctx, &plan) >= 0) {
            state->recorded += 1;
        }
    }
}

static void record_while_plan(LoopResidencyState* state, TypedASTNode* loop) {
    if (!state || !state->ctx || !loop) {
        return;
    }

    TypedASTNode* condition = loop->typed.whileStmt.condition;
    if (!condition || !condition->original || condition->original->type != NODE_BINARY) {
        return;
    }

    const char* op = condition->original->binary.op;
    if (!op) {
        return;
    }

    bool is_comparison = (strcmp(op, "<") == 0) || (strcmp(op, "<=") == 0) ||
                         (strcmp(op, ">") == 0) || (strcmp(op, ">=") == 0);
    if (!is_comparison) {
        return;
    }

    TypedASTNode* left = condition->typed.binary.left;
    TypedASTNode* right = condition->typed.binary.right;
    TypedASTNode* body = loop->typed.whileStmt.body;

    bool left_typed = type_supports_typed_registers(left ? left->resolvedType : NULL);
    bool right_typed = type_supports_typed_registers(right ? right->resolvedType : NULL);

    if (!left_typed && !right_typed) {
        return;
    }

    LoopTypeResidencyPlan plan = {0};
    plan.loop_node = loop;
    plan.guard_left_node = left;
    plan.guard_right_node = right;

    if (left_typed && expression_has_type_invariant_identifiers(left, body)) {
        plan.guard_left_prefers_typed = true;
        plan.guard_left_requires_residency = !node_is_constant(left);
    }

    if (right_typed && expression_has_type_invariant_identifiers(right, body)) {
        plan.guard_right_prefers_typed = true;
        plan.guard_right_requires_residency = !node_is_constant(right);
    }

    if (!plan.guard_left_prefers_typed && !plan.guard_right_prefers_typed) {
        return;
    }

    if (optimization_add_loop_residency_plan(state->ctx, &plan) >= 0) {
        state->recorded += 1;
    }
}

static bool loop_residency_pre_visit(TypedASTNode* node, void* user_data) {
    LoopResidencyState* state = (LoopResidencyState*)user_data;
    if (!state || !node || !node->original) {
        return true;
    }

    switch (node->original->type) {
        case NODE_FOR_RANGE:
            record_for_range_plan(state, node);
            break;
        case NODE_WHILE:
            record_while_plan(state, node);
            break;
        default:
            break;
    }

    return true;
}

OptimizationPassResult run_loop_type_residency_pass(TypedASTNode* ast, OptimizationContext* ctx) {
    OptimizationPassResult result = {0};
    result.success = true;

    if (!ast || !ctx) {
        return result;
    }

    optimization_clear_loop_residency_plans(ctx);

    LoopResidencyState state = {0};
    state.ctx = ctx;
    state.recorded = 0;

    TypedASTVisitor visitor = {0};
    visitor.pre = loop_residency_pre_visit;

    if (!typed_ast_visit(ast, &visitor, &state)) {
        result.success = false;
        return result;
    }

    result.optimizations_applied = state.recorded;
    return result;
}
