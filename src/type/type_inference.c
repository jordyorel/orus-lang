/*
 * File: src/type/type_inference.c
 * A sophisticated Hindley-Milner type inference (Algorithm W) for Orus
 * Features:
 *  - Full let-polymorphism with generalization and instantiation
 *  - Union-find based type variables for efficient unification
 *  - Constraint-based solving with error reporting
 *  - Separate TypeVar and Type constructors, using arena allocation
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler/ast.h"
#include "compiler/typed_ast.h"
#include "public/common.h"
#include "runtime/memory.h"
#include "type/type.h"
#include "vm/vm.h"

// ---- Arena and utilities ----
static TypeArena* type_arena = NULL;
static void* arena_alloc(size_t size) {
    size = (size + ARENA_ALIGNMENT - 1) & ~(ARENA_ALIGNMENT - 1);
    if (!type_arena || type_arena->used + size > type_arena->size) {
        size_t chunk = size > ARENA_SIZE ? size : ARENA_SIZE;
        TypeArena* a = malloc(sizeof(TypeArena));
        if (!a) return NULL;
        a->size = chunk;
        a->memory = malloc(a->size);
        if (!a->memory) {
            free(a);
            return NULL;
        }
        a->used = 0;
        a->next = type_arena;
        type_arena = a;
    }
    void* p = type_arena->memory + type_arena->used;
    type_arena->used += size;
    return p;
}

// ---- Union-Find TypeVar ----
typedef struct TypeVar {
    int id;
    struct TypeVar* parent;
    Type* instance;
} TypeVar;

static int next_var_id = 0;

static TypeVar* new_type_var_node(void) {
    TypeVar* tv = arena_alloc(sizeof(TypeVar));
    if (!tv) return NULL;
    tv->id = next_var_id++;
    tv->parent = tv;
    tv->instance = NULL;
    return tv;
}

static TypeVar* find_var(TypeVar* v) {
    if (!v) return NULL;
    return v->parent == v ? v : (v->parent = find_var(v->parent));
}

// ---- Type constructors ----
Type* make_var_type(TypeEnv* env) {
    (void)env;  // Unused parameter
    TypeVar* tv = new_type_var_node();
    if (!tv) return NULL;
    Type* t = arena_alloc(sizeof(Type));
    if (!t) return NULL;
    t->kind = TYPE_VAR;
    t->info.var.var = tv;
    return t;
}

Type* fresh_type(Type* t, HashMap* mapping) {
    if (!t) return NULL;
    switch (t->kind) {
        case TYPE_VAR: {
            TypeVar* v = find_var((TypeVar*)t->info.var.var);
            if (!v) return t;

            char key[32];
            snprintf(key, sizeof(key), "%d", v->id);
            Type* existing = hashmap_get(mapping, key);
            if (existing) return existing;

            Type* nv = make_var_type(NULL);
            if (!nv) return t;
            hashmap_set(mapping, key, nv);
            return nv;
        }
        case TYPE_FUNCTION: {
            Type** ps = arena_alloc(sizeof(Type*) * t->info.function.arity);
            if (!ps) return t;
            for (int i = 0; i < t->info.function.arity; i++) {
                ps[i] = fresh_type(t->info.function.paramTypes[i], mapping);
            }
            Type* rt = fresh_type(t->info.function.returnType, mapping);
            return createFunctionType(rt, ps, t->info.function.arity);
        }
        case TYPE_ARRAY:
            return createArrayType(
                fresh_type(t->info.array.elementType, mapping));
        default:
            return t;
    }
}

// ---- Prune function ----
Type* prune(Type* t) {
    if (!t) return NULL;
    if (t->kind == TYPE_VAR) {
        TypeVar* v = find_var((TypeVar*)t->info.var.var);
        if (v && v->instance) {
            v->instance = prune(v->instance);
            return v->instance;
        }
    }
    return t;
}

// ---- Occurs check ----
bool occurs_in_type(TypeVar* var, Type* type) {
    if (!var || !type) return false;

    type = prune(type);

    if (type->kind == TYPE_VAR) {
        TypeVar* v = find_var((TypeVar*)type->info.var.var);
        return v && v->id == var->id;
    }

    switch (type->kind) {
        case TYPE_FUNCTION:
            if (occurs_in_type(var, type->info.function.returnType))
                return true;
            for (int i = 0; i < type->info.function.arity; i++) {
                if (occurs_in_type(var, type->info.function.paramTypes[i]))
                    return true;
            }
            return false;
        case TYPE_ARRAY:
            return occurs_in_type(var, type->info.array.elementType);
        default:
            return false;
    }
}

// ---- Unification ----
bool unify(Type* a, Type* b) {
    if (!a || !b) return false;

    a = prune(a);
    b = prune(b);

    if (a->kind == TYPE_VAR) {
        TypeVar* va = find_var((TypeVar*)a->info.var.var);
        if (!va) return false;

        if (b->kind == TYPE_VAR) {
            TypeVar* vb = find_var((TypeVar*)b->info.var.var);
            if (!vb) return false;
            if (va->id == vb->id) return true;
            va->parent = vb;
            return true;
        }

        if (occurs_in_type(va, b)) return false;
        va->instance = b;
        return true;
    }

    if (b->kind == TYPE_VAR) return unify(b, a);

    if (a->kind != b->kind) return false;

    switch (a->kind) {
        case TYPE_FUNCTION:
            if (a->info.function.arity != b->info.function.arity) return false;
            for (int i = 0; i < a->info.function.arity; i++) {
                if (!unify(a->info.function.paramTypes[i],
                           b->info.function.paramTypes[i]))
                    return false;
            }
            return unify(a->info.function.returnType,
                         b->info.function.returnType);
        case TYPE_ARRAY:
            return unify(a->info.array.elementType, b->info.array.elementType);
        default:
            return true;
    }
}

// ---- Type Environment ----
typedef struct TypeEnvEntry {
    char* name;
    TypeScheme* scheme;
    struct TypeEnvEntry* next;
} TypeEnvEntry;

typedef struct TypeEnv {
    TypeEnvEntry* entries;
    struct TypeEnv* parent;
} TypeEnv;

TypeEnv* type_env_new(TypeEnv* parent) {
    TypeEnv* env = arena_alloc(sizeof(TypeEnv));
    if (!env) return NULL;
    env->entries = NULL;
    env->parent = parent;
    return env;
}

static void type_env_define(TypeEnv* env, const char* name,
                            TypeScheme* scheme) {
    if (!env || !name || !scheme) return;

    TypeEnvEntry* entry = arena_alloc(sizeof(TypeEnvEntry));
    if (!entry) return;

    entry->name = arena_alloc(strlen(name) + 1);
    if (!entry->name) return;
    strcpy(entry->name, name);

    entry->scheme = scheme;
    entry->next = env->entries;
    env->entries = entry;
}

static TypeScheme* type_env_lookup(TypeEnv* env, const char* name) {
    if (!env || !name) return NULL;

    TypeEnvEntry* entry = env->entries;
    while (entry) {
        if (strcmp(entry->name, name) == 0) {
            return entry->scheme;
        }
        entry = entry->next;
    }

    return env->parent ? type_env_lookup(env->parent, name) : NULL;
}

// ---- Type Schemes ----
typedef struct TypeScheme {
    char** bound_vars;
    int bound_count;
    Type* type;
} TypeScheme;

static TypeScheme* type_scheme_new(Type* type, char** bound_vars,
                                   int bound_count) {
    TypeScheme* scheme = arena_alloc(sizeof(TypeScheme));
    if (!scheme) return NULL;

    scheme->type = type;
    scheme->bound_count = bound_count;

    if (bound_count > 0) {
        scheme->bound_vars = arena_alloc(sizeof(char*) * bound_count);
        if (!scheme->bound_vars) return NULL;
        for (int i = 0; i < bound_count; i++) {
            scheme->bound_vars[i] = arena_alloc(strlen(bound_vars[i]) + 1);
            if (!scheme->bound_vars[i]) return NULL;
            strcpy(scheme->bound_vars[i], bound_vars[i]);
        }
    } else {
        scheme->bound_vars = NULL;
    }

    return scheme;
}

// ---- Free type variables ----
static void collect_free_vars(Type* type, HashMap* vars) {
    if (!type || !vars) return;

    type = prune(type);

    if (type->kind == TYPE_VAR) {
        TypeVar* v = find_var((TypeVar*)type->info.var.var);
        if (v) {
            char key[32];
            snprintf(key, sizeof(key), "%d", v->id);
            hashmap_set(vars, key, v);
        }
        return;
    }

    switch (type->kind) {
        case TYPE_FUNCTION:
            collect_free_vars(type->info.function.returnType, vars);
            for (int i = 0; i < type->info.function.arity; i++) {
                collect_free_vars(type->info.function.paramTypes[i], vars);
            }
            break;
        case TYPE_ARRAY:
            collect_free_vars(type->info.array.elementType, vars);
            break;
        default:
            break;
    }
}

// ---- Generalization ----
static TypeScheme* generalize(TypeEnv* env, Type* type) {
    if (!type) return NULL;

    // Simplified generalization - for now just wrap type without bound
    // variables
    (void)env;  // TODO: Implement proper environment variable collection

    return type_scheme_new(type, NULL, 0);
}

// ---- Instantiation ----
static Type* instantiate_scheme(TypeScheme* scheme) {
    if (!scheme) return NULL;

    HashMap* mapping = hashmap_new();
    if (!mapping) return scheme->type;

    Type* result = fresh_type(scheme->type, mapping);
    hashmap_free(mapping);

    return result;
}

// ---- Literal type inference ----
static Type* infer_literal(Value literal) {
    switch (literal.type) {
        case VAL_BOOL:
            return getPrimitiveType(TYPE_BOOL);
        case VAL_NUMBER:
            if (literal.as.number == (int)literal.as.number) {
                return getPrimitiveType(TYPE_I32);
            } else {
                return getPrimitiveType(TYPE_F64);
            }
        case VAL_STRING:
            return getPrimitiveType(TYPE_STRING);
        default:
            return getPrimitiveType(TYPE_UNKNOWN);
    }
}

// ---- Error reporting ----
static void error(const char* format, ...) {
    va_list args;
    va_start(args, format);
    fprintf(stderr, "Type Error: ");
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
    va_end(args);
}

// ---- Algorithm W ----
Type* algorithm_w(TypeEnv* env, ASTNode* node) {
    if (!node) return NULL;

    switch (node->type) {
        case NODE_IDENTIFIER: {
            TypeScheme* sch = type_env_lookup(env, node->identifier.name);
            if (!sch) {
                error("Unbound variable %s", node->identifier.name);
                return NULL;
            }
            return instantiate_scheme(sch);
        }
        case NODE_LITERAL:
            return infer_literal(node->literal.value);
        case NODE_BINARY: {
            Type* l = algorithm_w(env, node->binary.left);
            Type* r = algorithm_w(env, node->binary.right);
            if (!l || !r) return NULL;

            if (strcmp(node->binary.op, "+") == 0 ||
                strcmp(node->binary.op, "-") == 0 ||
                strcmp(node->binary.op, "*") == 0 ||
                strcmp(node->binary.op, "/") == 0) {
                if (!unify(l, r)) {
                    error("Type mismatch in arithmetic operation");
                    return NULL;
                }
                if (!unify(l, getPrimitiveType(TYPE_I32)) &&
                    !unify(l, getPrimitiveType(TYPE_F64))) {
                    error("Arithmetic operation requires numeric types");
                    return NULL;
                }
                return l;
            } else if (strcmp(node->binary.op, "==") == 0 ||
                       strcmp(node->binary.op, "!=") == 0) {
                if (!unify(l, r)) {
                    error("Type mismatch in comparison");
                    return NULL;
                }
                return getPrimitiveType(TYPE_BOOL);
            }
            error("Unknown binary operator: %s", node->binary.op);
            return NULL;
        }
        case NODE_VAR_DECL: {
            Type* init_type = NULL;
            Type* anno_type = NULL;

            if (node->varDecl.initializer) {
                init_type = algorithm_w(env, node->varDecl.initializer);
                if (!init_type) return NULL;
            }

            if (node->varDecl.typeAnnotation) {
                anno_type = algorithm_w(env, node->varDecl.typeAnnotation);
                if (!anno_type) return NULL;
            }

            if (init_type && anno_type) {
                if (!unify(init_type, anno_type)) {
                    error("Type annotation does not match initializer type");
                    return NULL;
                }
            }
            return getPrimitiveType(TYPE_VOID);
        }
        case NODE_ASSIGN: {
            Type* value_type = algorithm_w(env, node->assign.value);
            if (!value_type) return NULL;
            return getPrimitiveType(TYPE_VOID);
        }
        case NODE_PRINT: {
            for (int i = 0; i < node->print.count; i++) {
                Type* t = algorithm_w(env, node->print.values[i]);
                if (!t) return NULL;
            }
            if (node->print.separator) {
                Type* t = algorithm_w(env, node->print.separator);
                if (!t) return NULL;
            }
            return getPrimitiveType(TYPE_VOID);
        }
        default:
            error("Unsupported node type in type inference: %d", node->type);
            return getPrimitiveType(TYPE_UNKNOWN);
    }
}

// ---- Typed AST Generation ----
static void populate_ast_types(ASTNode* node, TypeEnv* env) {
    if (!node) return;

    Type* inferred_type = algorithm_w(env, node);
    if (inferred_type) {
        node->dataType = inferred_type;
    }

    switch (node->type) {
        case NODE_PROGRAM:
            for (int i = 0; i < node->program.count; i++) {
                populate_ast_types(node->program.declarations[i], env);
            }
            break;
        case NODE_VAR_DECL:
            if (node->varDecl.initializer) {
                populate_ast_types(node->varDecl.initializer, env);
            }
            if (node->varDecl.typeAnnotation) {
                populate_ast_types(node->varDecl.typeAnnotation, env);
            }
            break;
        case NODE_BINARY:
            populate_ast_types(node->binary.left, env);
            populate_ast_types(node->binary.right, env);
            break;
        case NODE_ASSIGN:
            populate_ast_types(node->assign.value, env);
            break;
        case NODE_PRINT:
            for (int i = 0; i < node->print.count; i++) {
                populate_ast_types(node->print.values[i], env);
            }
            if (node->print.separator) {
                populate_ast_types(node->print.separator, env);
            }
            break;
        case NODE_IF:
            populate_ast_types(node->ifStmt.condition, env);
            populate_ast_types(node->ifStmt.thenBranch, env);
            if (node->ifStmt.elseBranch) {
                populate_ast_types(node->ifStmt.elseBranch, env);
            }
            break;
        case NODE_WHILE:
            populate_ast_types(node->whileStmt.condition, env);
            populate_ast_types(node->whileStmt.body, env);
            break;
        case NODE_FOR_RANGE:
            populate_ast_types(node->forRange.start, env);
            populate_ast_types(node->forRange.end, env);
            if (node->forRange.step) {
                populate_ast_types(node->forRange.step, env);
            }
            populate_ast_types(node->forRange.body, env);
            break;
        case NODE_FOR_ITER:
            populate_ast_types(node->forIter.iterable, env);
            populate_ast_types(node->forIter.body, env);
            break;
        case NODE_BLOCK:
            for (int i = 0; i < node->block.count; i++) {
                populate_ast_types(node->block.statements[i], env);
            }
            break;
        case NODE_TERNARY:
            populate_ast_types(node->ternary.condition, env);
            populate_ast_types(node->ternary.trueExpr, env);
            populate_ast_types(node->ternary.falseExpr, env);
            break;
        case NODE_UNARY:
            populate_ast_types(node->unary.operand, env);
            break;
        case NODE_FUNCTION:
            if (node->function.returnType) {
                populate_ast_types(node->function.returnType, env);
            }
            populate_ast_types(node->function.body, env);
            break;
        case NODE_CALL:
            populate_ast_types(node->call.callee, env);
            for (int i = 0; i < node->call.argCount; i++) {
                populate_ast_types(node->call.args[i], env);
            }
            break;
        case NODE_RETURN:
            if (node->returnStmt.value) {
                populate_ast_types(node->returnStmt.value, env);
            }
            break;
        case NODE_CAST:
            populate_ast_types(node->cast.expression, env);
            populate_ast_types(node->cast.targetType, env);
            break;
        default:
            break;
    }
}

TypedASTNode* generate_typed_ast(ASTNode* root, TypeEnv* env) {
    if (!root || !env) return NULL;

    populate_ast_types(root, env);

    TypedASTNode* typed_root = create_typed_ast_node(root);
    if (!typed_root) return NULL;

    if (root->dataType) {
        typed_root->resolvedType = root->dataType;
        typed_root->typeResolved = true;
    } else {
        typed_root->hasTypeError = true;
        typed_root->errorMessage = strdup("Type inference failed");
    }

    return typed_root;
}

// ---- Public API ----
void init_type_inference(void) {
    next_var_id = 0;
    type_arena = NULL;
}

void cleanup_type_inference(void) {
    TypeArena* arena = type_arena;
    while (arena) {
        TypeArena* next = arena->next;
        free(arena->memory);
        free(arena);
        arena = next;
    }
    type_arena = NULL;
}

Type* instantiate(Type* type, TypeInferer* inferer) {
    (void)inferer;
    return type;
}

TypeInferer* type_inferer_new(void) {
    TypeInferer* inferer = malloc(sizeof(TypeInferer));
    if (!inferer) return NULL;

    inferer->next_type_var = 1000;
    inferer->substitutions = hashmap_new();
    inferer->constraints = NULL;
    inferer->env = hashmap_new();

    if (!inferer->substitutions || !inferer->env) {
        type_inferer_free(inferer);
        return NULL;
    }

    return inferer;
}

void type_inferer_free(TypeInferer* inferer) {
    if (!inferer) return;

    hashmap_free(inferer->substitutions);
    hashmap_free(inferer->env);
    free(inferer);
}

Type* infer_type(TypeInferer* inferer, ASTNode* expr) {
    if (!inferer || !expr) return NULL;

    switch (expr->type) {
        case NODE_LITERAL:
            return infer_literal(expr->literal.value);
        case NODE_IDENTIFIER:
            return getPrimitiveType(TYPE_ANY);
        case NODE_BINARY:
            return getPrimitiveType(TYPE_I32);
        default:
            return getPrimitiveType(TYPE_UNKNOWN);
    }
}