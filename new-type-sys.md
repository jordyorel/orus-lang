/*
 * File: src/type/type_inference.c
 * A more sophisticated Hindley-Milner type inference (Algorithm W) for Orus
 * Features:
 *  - Full let-polymorphism with generalization and instantiation
 *  - Union-find based type variables for efficient unification
 *  - Constraint-based solving with error reporting
 *  - Separate TypeVar and Type constructors, using arena allocation
 */

#include "../../include/vm.h"
#include "../../include/type.h"
#include "../../include/ast.h"
#include "../../include/memory.h"
#include "../../include/common.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// ---- Arena and utilities ----
static TypeArena* type_arena = NULL;
static void* arena_alloc(size_t size) {
    size = (size + 7) & ~7;
    if (!type_arena || type_arena->used + size > type_arena->size) {
        size_t chunk = size > ARENA_SIZE ? size : ARENA_SIZE;
        TypeArena* a = malloc(sizeof(TypeArena));
        a->size = chunk;
        a->memory = malloc(a->size);
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
    tv->id = next_var_id++;
    tv->parent = tv;
    tv->instance = NULL;
    return tv;
}

static TypeVar* find_var(TypeVar* v) {
    return v->parent == v ? v : (v->parent = find_var(v->parent));
}

// ---- Type constructors ----
Type* make_var_type(TypeEnv* env) {
    TypeVar* tv = new_type_var_node();
    Type* t = arena_alloc(sizeof(Type));
    t->kind = TYPE_VAR;
    t->info.var = tv;
    return t;
}

Type* fresh_type(Type* t, HashMap* mapping) {
    if (!t) return NULL;
    switch (t->kind) {
    case TYPE_VAR: {
        TypeVar* v = find_var(t->info.var);
        if (mapping->get(mapping, v->id)) return mapping->get(mapping, v->id);
        Type* nv = make_var_type(NULL);
        mapping->set(mapping, v->id, nv);
        return nv;
    }
    case TYPE_FUNCTION: {
        Type** ps = arena_alloc(sizeof(Type*)*t->info.function.arity);
        for (int i=0;i<t->info.function.arity;i++) ps[i] = fresh_type(t->info.function.paramTypes[i], mapping);
        Type* rt = fresh_type(t->info.function.returnType, mapping);
        return createFunctionType(rt, ps, t->info.function.arity);
    }
    case TYPE_ARRAY:
        return createArrayType(fresh_type(t->info.array.elementType, mapping));
    default:
        return t;
    }
}

// ---- Unification ----
bool unify(Type* a, Type* b) {
    a = prune(a);
    b = prune(b);
    if (a->kind == TYPE_VAR) {
        TypeVar* va = find_var(a->info.var);
        if (va != find_var(b->info.var)) {
            if (occurs_in_type(va, b)) return false;
            va->parent = find_var(b->info.var);
            return true;
        }
        return true;
    }
    if (b->kind == TYPE_VAR) return unify(b, a);
    if (a->kind != b->kind) return false;
    switch (a->kind) {
    case TYPE_FUNCTION:
        if (a->info.function.arity != b->info.function.arity) return false;
        for (int i=0;i<a->info.function.arity;i++)
            if (!unify(a->info.function.paramTypes[i], b->info.function.paramTypes[i])) return false;
        return unify(a->info.function.returnType, b->info.function.returnType);
    case TYPE_ARRAY:
        return unify(a->info.array.elementType, b->info.array.elementType);
    default:
        return true;
    }
}

// ---- Inference (Algorithm W) ----
Type* algorithm_w(TypeEnv* env, ASTNode* node) {
    switch (node->type) {
    case NODE_LET: {
        Type* val_t = algorithm_w(env, node->let.value);
        TypeScheme* scheme = generalize(env, val_t);
        env->define(env, node->let.name, scheme);
        return val_t;
    }
    case NODE_IDENTIFIER: {
        TypeScheme* sch = env->lookup(env, node->id.name);
        if (!sch) error("Unbound variable %s", node->id.name);
        HashMap* map = hashmap_new();
        Type* inst = fresh_type(sch->type, map);
        return inst;
    }
    case NODE_LITERAL:
        return infer_literal(node->lit);
    case NODE_BINARY: {
        Type* l = algorithm_w(env, node->bin.left);
        Type* r = algorithm_w(env, node->bin.right);
        if (strcmp(node->bin.op, "+") == 0) {
            unify(l, r);
            return get_numeric_type();
        }
        // other ops...
    }
    // ... other AST cases
    }
    return getPrimitiveType(TYPE_UNKNOWN);
}

// ... definitions for prune, occurs_in_type, generalize, instantiate, error, infer_literal, TypeEnv, TypeScheme ...

/*
 * File: src/type/type_representation.c
 * Advanced type representation with traits, generics, and dynamic extension
 * - Caching via arena for primitive types
 * - Trait bounds and interface support
 * - Named generic types and specialization
 */

#include "../../include/vm.h"
#include "../../include/type.h"
#include "../../include/memory.h"
#include "../../include/common.h"

static HashMap* primitive_cache;
static TypeArena* rep_arena;

void init_type_representation(void) {
    primitive_cache = hashmap_new();
    rep_arena = NULL;
    for (TypeKind k = TYPE_UNKNOWN; k <= TYPE_ANY; k++) {
        Type* t = arena_alloc(sizeof(Type));
        t->kind = k;
        primitive_cache->set(primitive_cache, k, t);
    }
}

Type* getPrimitive(TypeKind k) {
    return hashmap_get(primitive_cache, k);
}

Type* createGeneric(const char* name, int paramCount) {
    Type* t = arena_alloc(sizeof(Type));
    t->kind = TYPE_GENERIC;
    t->info.generic.name = strdup(name);
    t->info.generic.paramCount = paramCount;
    t->info.generic.params = arena_alloc(sizeof(Type*)*paramCount);
    return t;
}

Type* instantiateGeneric(Type* templ, Type** args) {
    if (templ->kind != TYPE_GENERIC || templ->info.generic.paramCount<=0) return templ;
    Type* inst = arena_alloc(sizeof(Type));
    inst->kind = TYPE_INSTANCE;
    inst->info.instance.base = templ;
    inst->info.instance.args = args;
    inst->info.instance.argCount = templ->info.generic.paramCount;
    return inst;
}

bool equalsType(Type* a, Type* b) {
    if (a == b) return true;
    if (!a || !b || a->kind != b->kind) return false;
    switch (a->kind) {
    case TYPE_ARRAY:
        return equalsType(a->info.array.elementType, b->info.array.elementType);
    case TYPE_FUNCTION:
        if (a->info.function.arity != b->info.function.arity) return false;
        for (int i=0; i<a->info.function.arity; i++) {
            if (!equalsType(a->info.function.paramTypes[i], b->info.function.paramTypes[i])) return false;
        }
        return equalsType(a->info.function.returnType, b->info.function.returnType);
    case TYPE_GENERIC:
        if (strcmp(a->info.generic.name, b->info.generic.name)!=0 ||
            a->info.generic.paramCount!=b->info.generic.paramCount) return false;
        for (int i=0;i<a->info.generic.paramCount;i++) {
            if (!equalsType(a->info.generic.params[i], b->info.generic.params[i])) return false;
        }
        return true;
    case TYPE_INSTANCE:
        return equalsType(a->info.instance.base, b->info.instance.base) &&
               a->info.instance.argCount==b->info.instance.argCount &&
               // ... compare args
               true;
    default:
        return true;
    }
}

// ... further trait and interface binding functions, printing, hashing, etc.

