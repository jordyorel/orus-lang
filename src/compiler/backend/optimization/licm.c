#include "compiler/optimization/licm.h"
#include "compiler/optimization/constantfold.h"
#include "debug/debug_config.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static LICMStats g_licm_stats;

static bool type_is_bool(const Type* type) {
    return type && type->kind == TYPE_BOOL;
}

static bool has_stable_bool_witness(const TypedASTNode* node) {
    if (!node) {
        return false;
    }
    if (!node->typeResolved || node->hasTypeError) {
        return false;
    }
    return type_is_bool(node->resolvedType);
}

static void licm_mark_loop_metadata(TypedASTNode* node,
                                    uint32_t guard_mask,
                                    int hoisted_guards) {
    if (!node) {
        return;
    }

    if (guard_mask != 0u && hoisted_guards > 0) {
        node->typed_guard_witness = true;
        node->typed_metadata_stable = true;
        node->typed_escape_mask = guard_mask;
    } else {
        if (guard_mask == 0u) {
            node->typed_guard_witness = false;
        }
        node->typed_metadata_stable = false;
        node->typed_escape_mask = 0u;
    }
}

typedef struct {
    const char** items;
    int count;
    int capacity;
} NameSet;

typedef struct {
    const char** names;
    int* counts;
    int count;
    int capacity;
} NameCounter;

static void nameset_init(NameSet* set) {
    if (!set) {
        return;
    }

    set->items = NULL;
    set->count = 0;
    set->capacity = 0;
}

static void nameset_free(NameSet* set) {
    if (!set) {
        return;
    }

    if (set->items) {
        free(set->items);
    }
    set->items = NULL;
    set->count = 0;
    set->capacity = 0;
}

static void namecounter_init(NameCounter* counter) {
    if (!counter) {
        return;
    }

    counter->names = NULL;
    counter->counts = NULL;
    counter->count = 0;
    counter->capacity = 0;
}

static void namecounter_free(NameCounter* counter) {
    if (!counter) {
        return;
    }

    if (counter->names) {
        free(counter->names);
    }
    if (counter->counts) {
        free(counter->counts);
    }
    counter->names = NULL;
    counter->counts = NULL;
    counter->count = 0;
    counter->capacity = 0;
}

static bool namecounter_increment(NameCounter* counter, const char* name) {
    if (!counter || !name) {
        return true;
    }

    for (int i = 0; i < counter->count; i++) {
        if (counter->names[i] && strcmp(counter->names[i], name) == 0) {
            counter->counts[i] += 1;
            return true;
        }
    }

    if (counter->count == counter->capacity) {
        int new_capacity = counter->capacity == 0 ? 8 : counter->capacity * 2;
        const char** new_names = realloc(counter->names, sizeof(const char*) * new_capacity);
        int* new_counts = realloc(counter->counts, sizeof(int) * new_capacity);
        if (!new_names || !new_counts) {
            free(new_names);
            free(new_counts);
            return false;
        }
        counter->names = new_names;
        counter->counts = new_counts;
        counter->capacity = new_capacity;
    }

    counter->names[counter->count] = name;
    counter->counts[counter->count] = 1;
    counter->count += 1;
    return true;
}

static int namecounter_get(const NameCounter* counter, const char* name) {
    if (!counter || !name) {
        return 0;
    }

    for (int i = 0; i < counter->count; i++) {
        if (counter->names[i] && strcmp(counter->names[i], name) == 0) {
            return counter->counts[i];
        }
    }
    return 0;
}

static bool nameset_contains(const NameSet* set, const char* name) {
    if (!set || !name) {
        return false;
    }

    for (int i = 0; i < set->count; i++) {
        if (set->items[i] && strcmp(set->items[i], name) == 0) {
            return true;
        }
    }
    return false;
}

static bool nameset_add(NameSet* set, const char* name) {
    if (!set || !name) {
        return true;
    }

    if (nameset_contains(set, name)) {
        return true;
    }

    if (set->count == set->capacity) {
        int new_capacity = set->capacity == 0 ? 8 : set->capacity * 2;
        const char** new_items = realloc(set->items, sizeof(const char*) * new_capacity);
        if (!new_items) {
            return false;
        }
        set->items = new_items;
        set->capacity = new_capacity;
    }

    set->items[set->count++] = name;
    return true;
}

static void collect_loop_metadata(TypedASTNode* node,
                                 NameSet* locals,
                                 NameSet* mutated,
                                 NameCounter* mutation_counts);

static void collect_loop_metadata_from_array(TypedASTNode** nodes,
                                            int count,
                                            NameSet* locals,
                                            NameSet* mutated,
                                            NameCounter* mutation_counts) {
    if (!nodes || count <= 0) {
        return;
    }

    for (int i = 0; i < count; i++) {
        collect_loop_metadata(nodes[i], locals, mutated, mutation_counts);
    }
}

static void collect_match_arm_metadata(TypedMatchArm* arm,
                                       NameSet* locals,
                                       NameSet* mutated,
                                       NameCounter* mutation_counts) {
    if (!arm) {
        return;
    }

    collect_loop_metadata(arm->valuePattern, locals, mutated, mutation_counts);
    collect_loop_metadata(arm->body, locals, mutated, mutation_counts);
    collect_loop_metadata(arm->condition, locals, mutated, mutation_counts);
    if (arm->payloadAccesses && arm->payloadCount > 0) {
        for (int i = 0; i < arm->payloadCount; i++) {
            collect_loop_metadata(arm->payloadAccesses[i], locals, mutated, mutation_counts);
        }
    }
}

static void collect_loop_metadata(TypedASTNode* node,
                                 NameSet* locals,
                                 NameSet* mutated,
                                 NameCounter* mutation_counts) {
    if (!node || !node->original) {
        return;
    }

    switch (node->original->type) {
        case NODE_PROGRAM:
            collect_loop_metadata_from_array(node->typed.program.declarations,
                                             node->typed.program.count,
                                             locals,
                                             mutated,
                                             mutation_counts);
            break;
        case NODE_BLOCK:
            collect_loop_metadata_from_array(node->typed.block.statements,
                                             node->typed.block.count,
                                             locals,
                                             mutated,
                                             mutation_counts);
            break;
        case NODE_VAR_DECL:
            if (!node->typed.varDecl.isGlobal) {
                nameset_add(locals, node->original->varDecl.name);
            }
            collect_loop_metadata(node->typed.varDecl.initializer,
                                 locals,
                                 mutated,
                                 mutation_counts);
            collect_loop_metadata(node->typed.varDecl.typeAnnotation,
                                 locals,
                                 mutated,
                                 mutation_counts);
            break;
        case NODE_ASSIGN:
            if (node->original->assign.name) {
                nameset_add(mutated, node->original->assign.name);
                namecounter_increment(mutation_counts, node->original->assign.name);
            }
            collect_loop_metadata(node->typed.assign.value,
                                 locals,
                                 mutated,
                                 mutation_counts);
            break;
        case NODE_IF:
            collect_loop_metadata(node->typed.ifStmt.condition,
                                 locals,
                                 mutated,
                                 mutation_counts);
            collect_loop_metadata(node->typed.ifStmt.thenBranch,
                                 locals,
                                 mutated,
                                 mutation_counts);
            collect_loop_metadata(node->typed.ifStmt.elseBranch,
                                 locals,
                                 mutated,
                                 mutation_counts);
            break;
        case NODE_WHILE:
            collect_loop_metadata(node->typed.whileStmt.condition,
                                 locals,
                                 mutated,
                                 mutation_counts);
            collect_loop_metadata(node->typed.whileStmt.body,
                                 locals,
                                 mutated,
                                 mutation_counts);
            break;
        case NODE_FOR_RANGE:
            if (node->original->forRange.varName) {
                nameset_add(locals, node->original->forRange.varName);
            }
            collect_loop_metadata(node->typed.forRange.start,
                                 locals,
                                 mutated,
                                 mutation_counts);
            collect_loop_metadata(node->typed.forRange.end,
                                 locals,
                                 mutated,
                                 mutation_counts);
            collect_loop_metadata(node->typed.forRange.step,
                                 locals,
                                 mutated,
                                 mutation_counts);
            collect_loop_metadata(node->typed.forRange.body,
                                 locals,
                                 mutated,
                                 mutation_counts);
            break;
        case NODE_FOR_ITER:
            if (node->original->forIter.varName) {
                nameset_add(locals, node->original->forIter.varName);
            }
            collect_loop_metadata(node->typed.forIter.iterable,
                                 locals,
                                 mutated,
                                 mutation_counts);
            collect_loop_metadata(node->typed.forIter.body,
                                 locals,
                                 mutated,
                                 mutation_counts);
            break;
        case NODE_MATCH_EXPRESSION:
            collect_loop_metadata(node->typed.matchExpr.subject,
                                 locals,
                                 mutated,
                                 mutation_counts);
            if (node->typed.matchExpr.arms && node->typed.matchExpr.armCount > 0) {
                for (int i = 0; i < node->typed.matchExpr.armCount; i++) {
                    collect_match_arm_metadata(&node->typed.matchExpr.arms[i],
                                               locals,
                                               mutated,
                                               mutation_counts);
                }
            }
            break;
        case NODE_FUNCTION:
            // Function bodies execute out of loop scope; skip to avoid false positives.
            break;
        case NODE_BINARY:
            collect_loop_metadata(node->typed.binary.left,
                                 locals,
                                 mutated,
                                 mutation_counts);
            collect_loop_metadata(node->typed.binary.right,
                                 locals,
                                 mutated,
                                 mutation_counts);
            break;
        case NODE_UNARY:
            collect_loop_metadata(node->typed.unary.operand,
                                 locals,
                                 mutated,
                                 mutation_counts);
            break;
        case NODE_TERNARY:
            collect_loop_metadata(node->typed.ternary.condition,
                                 locals,
                                 mutated,
                                 mutation_counts);
            collect_loop_metadata(node->typed.ternary.trueExpr,
                                 locals,
                                 mutated,
                                 mutation_counts);
            collect_loop_metadata(node->typed.ternary.falseExpr,
                                 locals,
                                 mutated,
                                 mutation_counts);
            break;
        case NODE_CAST:
            collect_loop_metadata(node->typed.cast.expression,
                                 locals,
                                 mutated,
                                 mutation_counts);
            collect_loop_metadata(node->typed.cast.targetType,
                                 locals,
                                 mutated,
                                 mutation_counts);
            break;
        case NODE_CALL:
            collect_loop_metadata(node->typed.call.callee,
                                 locals,
                                 mutated,
                                 mutation_counts);
            collect_loop_metadata_from_array(node->typed.call.args,
                                             node->typed.call.argCount,
                                             locals,
                                             mutated,
                                             mutation_counts);
            break;
        case NODE_ARRAY_LITERAL:
            collect_loop_metadata_from_array(node->typed.arrayLiteral.elements,
                                             node->typed.arrayLiteral.count,
                                             locals,
                                             mutated,
                                             mutation_counts);
            break;
        case NODE_INDEX_ACCESS:
            collect_loop_metadata(node->typed.indexAccess.array,
                                 locals,
                                 mutated,
                                 mutation_counts);
            collect_loop_metadata(node->typed.indexAccess.index,
                                 locals,
                                 mutated,
                                 mutation_counts);
            break;
        case NODE_MEMBER_ACCESS:
            collect_loop_metadata(node->typed.member.object,
                                 locals,
                                 mutated,
                                 mutation_counts);
            break;
        case NODE_MEMBER_ASSIGN:
            collect_loop_metadata(node->typed.memberAssign.target,
                                 locals,
                                 mutated,
                                 mutation_counts);
            collect_loop_metadata(node->typed.memberAssign.value,
                                 locals,
                                 mutated,
                                 mutation_counts);
            break;
        case NODE_ARRAY_ASSIGN:
            collect_loop_metadata(node->typed.arrayAssign.target,
                                 locals,
                                 mutated,
                                 mutation_counts);
            collect_loop_metadata(node->typed.arrayAssign.value,
                                 locals,
                                 mutated,
                                 mutation_counts);
            break;
        case NODE_PRINT:
            collect_loop_metadata_from_array(node->typed.print.values,
                                             node->typed.print.count,
                                             locals,
                                             mutated,
                                             mutation_counts);
            collect_loop_metadata(node->typed.print.separator,
                                 locals,
                                 mutated,
                                 mutation_counts);
            break;
        case NODE_RETURN:
            collect_loop_metadata(node->typed.returnStmt.value,
                                 locals,
                                 mutated,
                                 mutation_counts);
            break;
        case NODE_THROW:
            collect_loop_metadata(node->typed.throwStmt.value,
                                 locals,
                                 mutated,
                                 mutation_counts);
            break;
        case NODE_TRY:
            collect_loop_metadata(node->typed.tryStmt.tryBlock,
                                 locals,
                                 mutated,
                                 mutation_counts);
            collect_loop_metadata(node->typed.tryStmt.catchBlock,
                                 locals,
                                 mutated,
                                 mutation_counts);
            break;
        default:
            break;
    }
}

static bool is_invariant_expression(const TypedASTNode* node,
                                    const NameSet* locals,
                                    const NameSet* mutated,
                                    const NameSet* hoisted) {
    if (!node || !node->original) {
        return false;
    }

    if (node->original->type == NODE_LITERAL) {
        return true;
    }

    switch (node->original->type) {
        case NODE_IDENTIFIER: {
            const char* name = node->original->identifier.name;
            if (!name) {
                return false;
            }
            if (nameset_contains(mutated, name)) {
                if (!hoisted || !nameset_contains(hoisted, name)) {
                    return false;
                }
            }
            if (nameset_contains(locals, name) &&
                (!hoisted || !nameset_contains(hoisted, name))) {
                return false;
            }
            return true;
        }
        case NODE_BINARY:
            if (!node->typed.binary.left || !node->typed.binary.right) {
                return false;
            }
            return is_invariant_expression(node->typed.binary.left,
                                           locals,
                                           mutated,
                                           hoisted) &&
                   is_invariant_expression(node->typed.binary.right,
                                           locals,
                                           mutated,
                                           hoisted);
        case NODE_UNARY:
            if (!node->typed.unary.operand) {
                return false;
            }
            return is_invariant_expression(node->typed.unary.operand,
                                           locals,
                                           mutated,
                                           hoisted);
        case NODE_CAST:
            if (!node->typed.cast.expression) {
                return false;
            }
            return is_invariant_expression(node->typed.cast.expression,
                                           locals,
                                           mutated,
                                           hoisted);
        default:
            break;
    }

    return false;
}

static bool is_supported_loop_node(const TypedASTNode* node) {
    if (!node || !node->original) {
        return false;
    }

    switch (node->original->type) {
        case NODE_WHILE:
        case NODE_FOR_RANGE:
        case NODE_FOR_ITER:
            return true;
        default:
            return false;
    }
}

static bool expression_is_boolean(const TypedASTNode* node) {
    if (!node) {
        return false;
    }
    return type_is_bool(node->resolvedType);
}

static const ASTNode* extract_guard_base_expression(const TypedASTNode* initializer) {
    if (!initializer || !initializer->original) {
        return NULL;
    }

    switch (initializer->original->type) {
        case NODE_IDENTIFIER:
            return initializer->original;
        case NODE_BINARY: {
            const char* op = initializer->original->binary.op;
            if (!op) {
                return NULL;
            }
            if (strcmp(op, "and") != 0) {
                return NULL;
            }

            if (initializer->typed.binary.right) {
                return extract_guard_base_expression(initializer->typed.binary.right);
            }
            return NULL;
        }
        default:
            break;
    }

    return NULL;
}

static int fuse_hoisted_guard_initializers(TypedASTNode** hoisted_nodes,
                                           int hoistable_count) {
    if (!hoisted_nodes || hoistable_count <= 0) {
        return 0;
    }

    const char* last_guard_name = NULL;
    const ASTNode* last_guard_base = NULL;
    const char* last_guard_base_name = NULL;
    TypedASTNode* last_guard_initializer = NULL;
    int redundant_rewrites = 0;

    for (int i = 0; i < hoistable_count; ++i) {
        TypedASTNode* node = hoisted_nodes[i];
        if (!node || !node->original) {
            continue;
        }

        if (node->original->type != NODE_VAR_DECL || !node->typed_guard_witness) {
            last_guard_name = NULL;
            last_guard_base = NULL;
            last_guard_base_name = NULL;
            last_guard_initializer = NULL;
            continue;
        }

        const char* guard_name = node->original->varDecl.name;
        TypedASTNode* initializer = node->typed.varDecl.initializer;
        const ASTNode* guard_base = extract_guard_base_expression(initializer);
        const char* guard_base_name = NULL;
        if (guard_base && guard_base->type == NODE_IDENTIFIER) {
            guard_base_name = guard_base->identifier.name;
        }
        TypedASTNode* previous_guard_initializer = last_guard_initializer;

        if (last_guard_name && initializer && initializer->original &&
            initializer->original->type == NODE_BINARY) {
            const char* op = initializer->original->binary.op;
            if (op && strcmp(op, "and") == 0) {
                TypedASTNode* left = initializer->typed.binary.left;
                TypedASTNode* right = initializer->typed.binary.right;
                bool base_matches = false;
                if (right && last_guard_base && right->original == last_guard_base) {
                    base_matches = true;
                } else if (right && right->original &&
                           right->original->type == NODE_IDENTIFIER &&
                           last_guard_base_name &&
                           right->original->identifier.name &&
                           strcmp(right->original->identifier.name, last_guard_base_name) == 0) {
                    base_matches = true;
                }
                if (left && left->original && left->original->type == NODE_IDENTIFIER &&
                    strcmp(left->original->identifier.name, last_guard_name) == 0 &&
                    base_matches) {
                    initializer->typed.binary.left = NULL;
                    initializer->typed.binary.right = NULL;
                    node->typed.varDecl.initializer = left;
                    if (right && right != previous_guard_initializer) {
                        free_typed_ast_node(right);
                    }
                    free_typed_ast_node(initializer);
                    node->typed_metadata_stable = true;
                    node->typed_guard_witness = true;
                    guard_base = last_guard_base;
                    guard_base_name = last_guard_base_name;
                    last_guard_initializer = left;
                    redundant_rewrites++;
                    last_guard_name = guard_name;
                    last_guard_base = guard_base;
                    last_guard_base_name = guard_base_name;
                    continue;
                }
            }
        }

        last_guard_name = guard_name;
        last_guard_base = guard_base;
        last_guard_base_name = guard_base_name;
        last_guard_initializer = initializer;
    }

    return redundant_rewrites;
}

static bool is_hoistable_statement(const TypedASTNode* node,
                                   const NameSet* locals,
                                   const NameSet* mutated,
                                   const NameCounter* mutation_counts,
                                   NameSet* hoisted_names,
                                   bool* out_is_guard) {
    if (!node || !node->original) {
        return false;
    }

    if (out_is_guard) {
        *out_is_guard = false;
    }

    switch (node->original->type) {
        case NODE_VAR_DECL: {
            if (node->typed.varDecl.isGlobal) {
                return false;
            }

            const char* decl_name = node->original->varDecl.name;
            if (!decl_name) {
                return false;
            }

            if (nameset_contains(mutated, decl_name)) {
                return false;
            }

            TypedASTNode* initializer = node->typed.varDecl.initializer;
            if (!initializer) {
                return false;
            }

            if (expression_is_boolean(initializer)) {
                if (!has_stable_bool_witness(initializer)) {
                    return false;
                }
                if (out_is_guard) {
                    *out_is_guard = true;
                }
            }

            if (!is_invariant_expression(initializer, locals, mutated, hoisted_names)) {
                return false;
            }

            if (!nameset_add(hoisted_names, decl_name)) {
                return false;
            }
            return true;
        }
        case NODE_ASSIGN: {
            const char* target = node->original->assign.name;
            if (!target) {
                return false;
            }

            if (nameset_contains(locals, target)) {
                return false;
            }

            if (namecounter_get(mutation_counts, target) != 1) {
                return false;
            }

            TypedASTNode* value = node->typed.assign.value;
            if (!value) {
                return false;
            }

            if (expression_is_boolean(value)) {
                if (!has_stable_bool_witness(value)) {
                    return false;
                }
                if (out_is_guard) {
                    *out_is_guard = true;
                }
            }

            if (!is_invariant_expression(value, locals, mutated, hoisted_names)) {
                return false;
            }

            if (!nameset_add(hoisted_names, target)) {
                return false;
            }
            return true;
        }
        default:
            return false;
    }
}

static TypedASTNode* get_loop_body(TypedASTNode* loop_node) {
    if (!loop_node || !loop_node->original) {
        return NULL;
    }

    switch (loop_node->original->type) {
        case NODE_WHILE:
            return loop_node->typed.whileStmt.body;
        case NODE_FOR_RANGE:
            return loop_node->typed.forRange.body;
        case NODE_FOR_ITER:
            return loop_node->typed.forIter.body;
        default:
            return NULL;
    }
}

static int hoist_invariants_from_loop(TypedASTNode*** parent_array_ptr,
                                      int* parent_count_ptr,
                                      int loop_index,
                                      int* hoisted_guard_count,
                                      uint32_t* hoisted_guard_mask) {
    if (!parent_array_ptr || !parent_count_ptr || loop_index < 0) {
        return 0;
    }

    if (hoisted_guard_count) {
        *hoisted_guard_count = 0;
    }
    if (hoisted_guard_mask) {
        *hoisted_guard_mask = 0u;
    }

    TypedASTNode** parent_statements = *parent_array_ptr;
    int parent_count = *parent_count_ptr;
    if (!parent_statements || parent_count <= loop_index) {
        return 0;
    }

    TypedASTNode* loop_node = parent_statements[loop_index];
    TypedASTNode* loop_body = get_loop_body(loop_node);
    if (!loop_body || !loop_body->original || loop_body->original->type != NODE_BLOCK) {
        return 0;
    }

    TypedASTNode** body_statements = loop_body->typed.block.statements;
    int body_count = loop_body->typed.block.count;
    if (!body_statements || body_count <= 0) {
        return 0;
    }

    NameSet locals;
    NameSet mutated;
    NameSet hoisted_names;
    NameCounter mutation_counts;
    nameset_init(&locals);
    nameset_init(&mutated);
    nameset_init(&hoisted_names);
    namecounter_init(&mutation_counts);

    if (loop_node->original->type == NODE_FOR_RANGE &&
        loop_node->original->forRange.varName) {
        nameset_add(&locals, loop_node->original->forRange.varName);
    } else if (loop_node->original->type == NODE_FOR_ITER &&
               loop_node->original->forIter.varName) {
        nameset_add(&locals, loop_node->original->forIter.varName);
    }

    collect_loop_metadata(loop_body, &locals, &mutated, &mutation_counts);

    bool* hoist_flags = calloc(body_count, sizeof(bool));
    bool* guard_flags = calloc(body_count, sizeof(bool));
    if (!hoist_flags || !guard_flags) {
        nameset_free(&hoisted_names);
        namecounter_free(&mutation_counts);
        nameset_free(&mutated);
        nameset_free(&locals);
        free(guard_flags);
        return 0;
    }

    int hoistable_count = 0;
    int guard_hoist_count = 0;
    for (int i = 0; i < body_count; i++) {
        bool is_guard = false;
        if (is_hoistable_statement(body_statements[i],
                                   &locals,
                                   &mutated,
                                   &mutation_counts,
                                   &hoisted_names,
                                   &is_guard)) {
            hoist_flags[i] = true;
            hoistable_count++;
            if (is_guard) {
                guard_flags[i] = true;
                guard_hoist_count++;
            }
        }
    }

    if (hoistable_count == 0) {
        free(guard_flags);
        free(hoist_flags);
        nameset_free(&hoisted_names);
        namecounter_free(&mutation_counts);
        nameset_free(&mutated);
        nameset_free(&locals);
        if (hoisted_guard_count) {
            *hoisted_guard_count = 0;
        }
        if (hoisted_guard_mask) {
            *hoisted_guard_mask = 0u;
        }
        return 0;
    }

    TypedASTNode** hoisted_nodes = malloc(sizeof(TypedASTNode*) * hoistable_count);
    if (!hoisted_nodes) {
        free(guard_flags);
        free(hoist_flags);
        nameset_free(&hoisted_names);
        namecounter_free(&mutation_counts);
        nameset_free(&mutated);
        nameset_free(&locals);
        return 0;
    }

    int new_body_count = body_count - hoistable_count;
    TypedASTNode** new_body = NULL;
    if (new_body_count > 0) {
        new_body = malloc(sizeof(TypedASTNode*) * new_body_count);
        if (!new_body) {
            free(hoisted_nodes);
            free(guard_flags);
            free(hoist_flags);
            nameset_free(&hoisted_names);
            namecounter_free(&mutation_counts);
            nameset_free(&mutated);
            nameset_free(&locals);
            return 0;
        }
    }

    int hoisted_index = 0;
    int body_write_index = 0;
    uint32_t guard_mask = 0u;
    uint32_t next_guard_bit = 1u;
    for (int i = 0; i < body_count; i++) {
        TypedASTNode* stmt = body_statements[i];
        if (hoist_flags[i]) {
            hoisted_nodes[hoisted_index++] = stmt;
            if (guard_flags[i]) {
                uint32_t guard_bit = next_guard_bit;
                if (next_guard_bit != 0u) {
                    guard_mask |= guard_bit;
                    if (next_guard_bit == 0x80000000u) {
                        next_guard_bit = 0u;
                    } else {
                        next_guard_bit <<= 1u;
                    }
                }

                stmt->typed_guard_witness = true;
                stmt->typed_metadata_stable = true;
                stmt->typed_escape_mask = guard_bit;
            }
        } else if (new_body) {
            new_body[body_write_index++] = stmt;
        }
    }

    int redundant_rewrites = fuse_hoisted_guard_initializers(hoisted_nodes, hoistable_count);

    TypedASTNode** new_parent = malloc(sizeof(TypedASTNode*) * (parent_count + hoistable_count));
    if (!new_parent) {
        if (new_body) {
            free(new_body);
        }
        free(hoisted_nodes);
        free(guard_flags);
        free(hoist_flags);
        nameset_free(&hoisted_names);
        namecounter_free(&mutation_counts);
        nameset_free(&mutated);
        nameset_free(&locals);
        return 0;
    }

    int out_index = 0;
    for (int i = 0; i < loop_index; i++) {
        new_parent[out_index++] = parent_statements[i];
    }
    for (int i = 0; i < hoistable_count; i++) {
        new_parent[out_index++] = hoisted_nodes[i];
    }
    new_parent[out_index++] = parent_statements[loop_index];
    for (int i = loop_index + 1; i < parent_count; i++) {
        new_parent[out_index++] = parent_statements[i];
    }

    for (int i = 0; i < hoistable_count; i++) {
        apply_constant_folding_recursive(hoisted_nodes[i]);
    }

    free(parent_statements);
    *parent_array_ptr = new_parent;
    *parent_count_ptr = parent_count + hoistable_count;

    free(body_statements);
    loop_body->typed.block.statements = new_body;
    loop_body->typed.block.count = new_body_count;

    free(hoisted_nodes);
    free(guard_flags);
    free(hoist_flags);
    nameset_free(&hoisted_names);
    namecounter_free(&mutation_counts);
    nameset_free(&mutated);
    nameset_free(&locals);
    if (hoisted_guard_count) {
        *hoisted_guard_count = guard_hoist_count;
    }
    if (hoisted_guard_mask) {
        *hoisted_guard_mask = guard_mask;
    }
    if (redundant_rewrites > 0) {
        g_licm_stats.redundant_guard_fusions += redundant_rewrites;
    }
    return hoistable_count;
}

static bool process_statement_array(TypedASTNode*** array_ptr, int* count_ptr);
static bool traverse_node(TypedASTNode* node);

static bool process_statement_array(TypedASTNode*** array_ptr, int* count_ptr) {
    if (!array_ptr || !count_ptr) {
        return false;
    }

    TypedASTNode** statements = *array_ptr;
    int count = *count_ptr;
    if (!statements || count <= 0) {
        return false;
    }

    bool changed = false;
    int index = 0;
    while (index < *count_ptr) {
        statements = *array_ptr;
        if (!statements) {
            break;
        }
        int current_count = *count_ptr;
        if (index >= current_count) {
            break;
        }

        TypedASTNode* stmt = statements[index];
        if (!stmt || !stmt->original) {
            index++;
            continue;
        }

        if (is_supported_loop_node(stmt)) {
            TypedASTNode* original_loop = stmt;
            int hoisted_guard_count = 0;
            uint32_t hoisted_guard_mask = 0u;
            int hoisted = hoist_invariants_from_loop(array_ptr,
                                                    count_ptr,
                                                    index,
                                                    &hoisted_guard_count,
                                                    &hoisted_guard_mask);
            if (hoisted > 0) {
                changed = true;
                g_licm_stats.changed = true;
                g_licm_stats.invariants_hoisted += hoisted;
                g_licm_stats.loops_optimized++;
                g_licm_stats.guard_fusions += hoisted_guard_count;
                licm_mark_loop_metadata(original_loop, hoisted_guard_mask, hoisted_guard_count);

                statements = *array_ptr;
                index += hoisted;
                if (index >= *count_ptr) {
                    break;
                }
                stmt = statements[index];
            }

            if (hoisted == 0) {
                licm_mark_loop_metadata(stmt, 0u, 0);
            }

            if (stmt && stmt->original && is_supported_loop_node(stmt)) {
                switch (stmt->original->type) {
                    case NODE_WHILE:
                        if (stmt->typed.whileStmt.condition) {
                            traverse_node(stmt->typed.whileStmt.condition);
                        }
                        if (stmt->typed.whileStmt.body &&
                            traverse_node(stmt->typed.whileStmt.body)) {
                            changed = true;
                        }
                        break;
                    case NODE_FOR_RANGE:
                        if (stmt->typed.forRange.start &&
                            traverse_node(stmt->typed.forRange.start)) {
                            changed = true;
                        }
                        if (stmt->typed.forRange.end &&
                            traverse_node(stmt->typed.forRange.end)) {
                            changed = true;
                        }
                        if (stmt->typed.forRange.step &&
                            traverse_node(stmt->typed.forRange.step)) {
                            changed = true;
                        }
                        if (stmt->typed.forRange.body &&
                            traverse_node(stmt->typed.forRange.body)) {
                            changed = true;
                        }
                        break;
                    case NODE_FOR_ITER:
                        if (stmt->typed.forIter.iterable &&
                            traverse_node(stmt->typed.forIter.iterable)) {
                            changed = true;
                        }
                        if (stmt->typed.forIter.body &&
                            traverse_node(stmt->typed.forIter.body)) {
                            changed = true;
                        }
                        break;
                    default:
                        break;
                }
            }

            index++;
            continue;
        }

        if (traverse_node(stmt)) {
            changed = true;
        }
        index++;
    }

    return changed;
}

static bool traverse_node(TypedASTNode* node) {
    if (!node || !node->original) {
        return false;
    }

    bool changed = false;

    switch (node->original->type) {
        case NODE_PROGRAM:
            if (process_statement_array(&node->typed.program.declarations,
                                        &node->typed.program.count)) {
                changed = true;
            }
            break;
        case NODE_BLOCK:
            if (process_statement_array(&node->typed.block.statements,
                                        &node->typed.block.count)) {
                changed = true;
            }
            break;
        case NODE_FUNCTION:
            if (node->typed.function.body) {
                if (traverse_node(node->typed.function.body)) {
                    changed = true;
                }
            }
            break;
        case NODE_IF:
            if (node->typed.ifStmt.thenBranch &&
                traverse_node(node->typed.ifStmt.thenBranch)) {
                changed = true;
            }
            if (node->typed.ifStmt.elseBranch &&
                traverse_node(node->typed.ifStmt.elseBranch)) {
                changed = true;
            }
            if (node->typed.ifStmt.condition &&
                traverse_node(node->typed.ifStmt.condition)) {
                changed = true;
            }
            break;
        case NODE_WHILE:
            if (node->typed.whileStmt.condition &&
                traverse_node(node->typed.whileStmt.condition)) {
                changed = true;
            }
            if (node->typed.whileStmt.body &&
                traverse_node(node->typed.whileStmt.body)) {
                changed = true;
            }
            break;
        case NODE_FOR_RANGE:
            if (node->typed.forRange.body &&
                traverse_node(node->typed.forRange.body)) {
                changed = true;
            }
            if (node->typed.forRange.start &&
                traverse_node(node->typed.forRange.start)) {
                changed = true;
            }
            if (node->typed.forRange.end &&
                traverse_node(node->typed.forRange.end)) {
                changed = true;
            }
            if (node->typed.forRange.step &&
                traverse_node(node->typed.forRange.step)) {
                changed = true;
            }
            break;
        case NODE_FOR_ITER:
            if (node->typed.forIter.body &&
                traverse_node(node->typed.forIter.body)) {
                changed = true;
            }
            if (node->typed.forIter.iterable &&
                traverse_node(node->typed.forIter.iterable)) {
                changed = true;
            }
            break;
        case NODE_MATCH_EXPRESSION:
            if (node->typed.matchExpr.subject &&
                traverse_node(node->typed.matchExpr.subject)) {
                changed = true;
            }
            if (node->typed.matchExpr.arms && node->typed.matchExpr.armCount > 0) {
                for (int i = 0; i < node->typed.matchExpr.armCount; i++) {
                    TypedMatchArm* arm = &node->typed.matchExpr.arms[i];
                    if (arm->body && traverse_node(arm->body)) {
                        changed = true;
                    }
                    if (arm->condition && traverse_node(arm->condition)) {
                        changed = true;
                    }
                    if (arm->valuePattern && traverse_node(arm->valuePattern)) {
                        changed = true;
                    }
                    if (arm->payloadAccesses && arm->payloadCount > 0) {
                        for (int j = 0; j < arm->payloadCount; j++) {
                            if (arm->payloadAccesses[j] &&
                                traverse_node(arm->payloadAccesses[j])) {
                                changed = true;
                            }
                        }
                    }
                }
            }
            break;
        default:
            break;
    }

    return changed;
}

void init_licm_stats(LICMStats* stats) {
    if (!stats) {
        return;
    }

    stats->invariants_hoisted = 0;
    stats->loops_optimized = 0;
    stats->guard_fusions = 0;
    stats->redundant_guard_fusions = 0;
    stats->changed = false;
}

void print_licm_statistics(const LICMStats* stats) {
    if (!stats) {
        return;
    }

    if (stats->changed) {
        DEBUG_OPTIMIZER_PRINT(
            "[LICM] Hoisted %d invariant declarations across %d loop(s) with %d guard fusion(s) (%d redundant rewrites)\n",
            stats->invariants_hoisted,
            stats->loops_optimized,
            stats->guard_fusions,
            stats->redundant_guard_fusions);
    } else {
        DEBUG_OPTIMIZER_PRINT("[LICM] No loop-invariant declarations found\n");
    }
}

bool apply_loop_invariant_code_motion(TypedASTNode* ast, OptimizationContext* opt_ctx) {
    if (!ast) {
        return false;
    }

    init_licm_stats(&g_licm_stats);
    DEBUG_OPTIMIZER_PRINT("[LICM] Starting loop invariant code motion pass...\n");

    bool changed = traverse_node(ast);

    if (opt_ctx && g_licm_stats.changed) {
        opt_ctx->optimizations_applied += g_licm_stats.invariants_hoisted;
        opt_ctx->loop_invariants_hoisted += g_licm_stats.invariants_hoisted;
        opt_ctx->loops_optimized += g_licm_stats.loops_optimized;
        opt_ctx->licm_guard_fusions += g_licm_stats.guard_fusions;
        opt_ctx->licm_redundant_guard_fusions += g_licm_stats.redundant_guard_fusions;
    }

    print_licm_statistics(&g_licm_stats);
    return changed;
}
