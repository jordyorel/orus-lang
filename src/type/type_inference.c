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
#include "internal/strutil.h"

#include "compiler/ast.h"
#include "compiler/typed_ast.h"
#include "public/common.h"
#include "runtime/memory.h"
#include "type/type.h"
#include "vm/vm.h"
#include "vm/module_manager.h"
#include "internal/error_reporting.h"
#include "errors/features/type_errors.h"
#include "errors/features/variable_errors.h"
#include "errors/features/control_flow_errors.h"
#include "debug/debug_config.h"

// ---- Helpers for struct and impl bookkeeping ----
static ObjString* create_compiler_string(const char* text) {
    if (!text) {
        return NULL;
    }

    size_t length = strlen(text);
    ObjString* string = malloc(sizeof(ObjString));
    if (!string) {
        return NULL;
    }

    string->obj.type = OBJ_STRING;
    string->obj.next = NULL;
    string->obj.isMarked = false;
    string->length = (int)length;
    string->chars = copyString(text, (int)length);
    if (!string->chars) {
        free(string);
        return NULL;
    }
    string->rope = NULL;
    string->hash = 0;
    return string;
}

static Variant* lookup_enum_variant(Type* enum_type, const char* variant_name, int* out_index) {
    if (!enum_type || !variant_name) {
        return NULL;
    }

    Type* base_type = enum_type;
    if (base_type->kind == TYPE_INSTANCE && base_type->info.instance.base) {
        base_type = base_type->info.instance.base;
    }

    if (base_type->kind != TYPE_ENUM) {
        return NULL;
    }

    TypeExtension* ext = get_type_extension(base_type);
    if (!ext || !ext->extended.enum_.variants) {
        return NULL;
    }

    for (int i = 0; i < ext->extended.enum_.variant_count; i++) {
        Variant* variant = &ext->extended.enum_.variants[i];
        if (variant->name && variant->name->chars && strcmp(variant->name->chars, variant_name) == 0) {
            if (out_index) {
                *out_index = i;
            }
            return variant;
        }
    }

    return NULL;
}

static const char* get_enum_type_name(Type* enum_type) {
    if (!enum_type) {
        return NULL;
    }

    Type* base_type = enum_type;
    if (base_type->kind == TYPE_INSTANCE && base_type->info.instance.base) {
        base_type = base_type->info.instance.base;
    }

    if (base_type->kind != TYPE_ENUM) {
        return NULL;
    }

    TypeExtension* ext = get_type_extension(base_type);
    if (!ext || !ext->extended.enum_.name) {
        return NULL;
    }

    return ext->extended.enum_.name->chars;
}

static void free_compiler_string(ObjString* string) {
    if (!string) {
        return;
    }
    if (string->chars) {
        free(string->chars);
    }
    free(string);
}

static void cleanup_field_info(FieldInfo* fields, int fieldCount) {
    if (!fields) {
        return;
    }

    for (int i = 0; i < fieldCount; i++) {
        free_compiler_string(fields[i].name);
    }
    free(fields);
}

static void cleanup_variant_info(Variant* variants, int variantCount) {
    if (!variants) {
        return;
    }

    for (int i = 0; i < variantCount; i++) {
        free_compiler_string(variants[i].name);
        if (variants[i].field_names) {
            for (int j = 0; j < variants[i].field_count; j++) {
                free_compiler_string(variants[i].field_names[j]);
            }
            free(variants[i].field_names);
        }
        if (variants[i].field_types) {
            free(variants[i].field_types);
        }
    }
    free(variants);
}

static void cleanup_new_methods(Method* methods, int start, int end) {
    if (!methods) {
        return;
    }

    for (int i = start; i < end; i++) {
        free_compiler_string(methods[i].name);
        methods[i].name = NULL;
        methods[i].type = NULL;
    }
}

static bool literal_values_equal(Value a, Value b) {
    if (a.type != b.type) {
        return false;
    }

    switch (a.type) {
        case VAL_BOOL:
            return AS_BOOL(a) == AS_BOOL(b);
        case VAL_I32:
            return AS_I32(a) == AS_I32(b);
        case VAL_I64:
            return AS_I64(a) == AS_I64(b);
        case VAL_U32:
            return AS_U32(a) == AS_U32(b);
        case VAL_U64:
            return AS_U64(a) == AS_U64(b);
        case VAL_F64:
            return AS_F64(a) == AS_F64(b);
        case VAL_STRING: {
            ObjString* left = AS_STRING(a);
            ObjString* right = AS_STRING(b);
            if (!left || !right || !left->chars || !right->chars) {
                return left == right;
            }
            return strcmp(left->chars, right->chars) == 0;
        }
        default:
            return false;
    }
}

static void format_literal_value(Value value, char* buffer, size_t size) {
    if (!buffer || size == 0) {
        return;
    }

    switch (value.type) {
        case VAL_BOOL:
            snprintf(buffer, size, "%s", AS_BOOL(value) ? "true" : "false");
            break;
        case VAL_I32:
            snprintf(buffer, size, "%d", AS_I32(value));
            break;
        case VAL_I64:
            snprintf(buffer, size, "%lld", (long long)AS_I64(value));
            break;
        case VAL_U32:
            snprintf(buffer, size, "%u", AS_U32(value));
            break;
        case VAL_U64:
            snprintf(buffer, size, "%llu", (unsigned long long)AS_U64(value));
            break;
        case VAL_F64:
            snprintf(buffer, size, "%g", AS_F64(value));
            break;
        case VAL_STRING: {
            ObjString* str = AS_STRING(value);
            const char* chars = (str && str->chars) ? str->chars : "";
            snprintf(buffer, size, "\"%s\"", chars);
            break;
        }
        default:
            snprintf(buffer, size, "<literal>");
            break;
    }
}

// ---- Arena and utilities ----
static TypeArena* type_arena = NULL;
static void* type_arena_alloc(size_t size) {
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
    TypeVar* tv = type_arena_alloc(sizeof(TypeVar));
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

static void register_builtin_functions(TypeEnv* env);
static void register_builtin_enums(void);

// ---- Type constructors ----
Type* make_var_type(TypeEnv* env) {
    (void)env;  // Unused parameter
    TypeVar* tv = new_type_var_node();
    if (!tv) return NULL;
    Type* t = type_arena_alloc(sizeof(Type));
    if (!t) return NULL;
    memset(t, 0, sizeof(Type));
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

            if (v->instance) {
                return fresh_type(v->instance, mapping);
            }

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
            Type** ps = type_arena_alloc(sizeof(Type*) * t->info.function.arity);
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
        case TYPE_STRUCT: {
            TypeExtension* ext = get_type_extension(type);
            if (!ext || !ext->extended.structure.fields) return false;
            for (int i = 0; i < ext->extended.structure.fieldCount; i++) {
                if (occurs_in_type(var, ext->extended.structure.fields[i].type)) {
                    return true;
                }
            }
            return false;
        }
        case TYPE_ENUM: {
            TypeExtension* ext = get_type_extension(type);
            if (!ext || !ext->extended.enum_.variants) return false;
            for (int i = 0; i < ext->extended.enum_.variant_count; i++) {
                Variant* variant = &ext->extended.enum_.variants[i];
                for (int j = 0; j < variant->field_count; j++) {
                    if (occurs_in_type(var, variant->field_types[j])) {
                        return true;
                    }
                }
            }
            return false;
        }
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

    if (a->kind == TYPE_ANY || b->kind == TYPE_ANY) {
        return true;
    }

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
        case TYPE_STRUCT:
        case TYPE_ENUM:
            return equalsType(a, b);
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
    Type* expected_return_type;
} TypeEnv;

TypeEnv* type_env_new(TypeEnv* parent) {
    TypeEnv* env = type_arena_alloc(sizeof(TypeEnv));
    if (!env) return NULL;
    env->entries = NULL;
    env->parent = parent;
    env->expected_return_type = parent ? parent->expected_return_type : NULL;
    if (!parent) {
        register_builtin_functions(env);
        register_builtin_enums();
    }
    return env;
}

static Type* type_env_get_expected_return(TypeEnv* env) {
    for (TypeEnv* current = env; current; current = current->parent) {
        if (current->expected_return_type) {
            return current->expected_return_type;
        }
    }
    return NULL;
}

static void type_env_define(TypeEnv* env, const char* name,
                            TypeScheme* scheme) {
    if (!env || !name || !scheme) return;

    TypeEnvEntry* entry = type_arena_alloc(sizeof(TypeEnvEntry));
    if (!entry) return;

    entry->name = type_arena_alloc(strlen(name) + 1);
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

typedef struct {
    int count;
    int capacity;
    int* values;
} IntSet;

static IntSet* int_set_create(void) {
    IntSet* set = type_arena_alloc(sizeof(IntSet));
    if (!set) return NULL;
    set->count = 0;
    set->capacity = 8;
    set->values = type_arena_alloc(sizeof(int) * set->capacity);
    if (!set->values) {
        set->capacity = 0;
    }
    return set;
}

static bool int_set_contains(const IntSet* set, int value) {
    if (!set || !set->values) return false;
    for (int i = 0; i < set->count; i++) {
        if (set->values[i] == value) {
            return true;
        }
    }
    return false;
}

static void int_set_add(IntSet* set, int value) {
    if (!set) return;
    if (int_set_contains(set, value)) return;

    if (set->count >= set->capacity) {
        int new_capacity = set->capacity > 0 ? set->capacity * 2 : 4;
        int* new_values = type_arena_alloc(sizeof(int) * new_capacity);
        if (!new_values) return;
        if (set->values && set->count > 0) {
            memcpy(new_values, set->values, sizeof(int) * set->count);
        }
        set->values = new_values;
        set->capacity = new_capacity;
    }

    if (set->values) {
        set->values[set->count++] = value;
    }
}

static bool is_var_bound(char** bound_vars, int bound_count, int var_id) {
    if (!bound_vars || bound_count <= 0) return false;
    for (int i = 0; i < bound_count; i++) {
        if (!bound_vars[i]) continue;
        if (atoi(bound_vars[i]) == var_id) {
            return true;
        }
    }
    return false;
}

static void collect_free_type_vars_internal(Type* type, IntSet* set,
                                            char** bound_vars, int bound_count) {
    if (!type || !set) return;

    type = prune(type);
    if (!type) return;

    switch (type->kind) {
        case TYPE_VAR: {
            TypeVar* var = find_var((TypeVar*)type->info.var.var);
            if (!var) return;
            if (var->instance) {
                collect_free_type_vars_internal(var->instance, set, bound_vars,
                                                bound_count);
            } else if (!is_var_bound(bound_vars, bound_count, var->id)) {
                int_set_add(set, var->id);
            }
            break;
        }
        case TYPE_FUNCTION: {
            if (type->info.function.paramTypes) {
                for (int i = 0; i < type->info.function.arity; i++) {
                    collect_free_type_vars_internal(type->info.function.paramTypes[i],
                                                    set, bound_vars, bound_count);
                }
            }
            collect_free_type_vars_internal(type->info.function.returnType, set,
                                            bound_vars, bound_count);
            break;
        }
        case TYPE_ARRAY:
            collect_free_type_vars_internal(type->info.array.elementType, set,
                                            bound_vars, bound_count);
            break;
        case TYPE_INSTANCE: {
            collect_free_type_vars_internal(type->info.instance.base, set,
                                            bound_vars, bound_count);
            if (type->info.instance.args) {
                for (int i = 0; i < type->info.instance.argCount; i++) {
                    collect_free_type_vars_internal(type->info.instance.args[i], set,
                                                    bound_vars, bound_count);
                }
            }
            break;
        }
        case TYPE_GENERIC: {
            if (type->info.generic.params) {
                for (int i = 0; i < type->info.generic.paramCount; i++) {
                    collect_free_type_vars_internal(type->info.generic.params[i], set,
                                                    bound_vars, bound_count);
                }
            }
            break;
        }
        case TYPE_STRUCT: {
            TypeExtension* ext = get_type_extension(type);
            if (!ext || !ext->extended.structure.fields) break;
            for (int i = 0; i < ext->extended.structure.fieldCount; i++) {
                collect_free_type_vars_internal(ext->extended.structure.fields[i].type,
                                                set, bound_vars, bound_count);
            }
            break;
        }
        case TYPE_ENUM: {
            TypeExtension* ext = get_type_extension(type);
            if (!ext || !ext->extended.enum_.variants) break;
            for (int i = 0; i < ext->extended.enum_.variant_count; i++) {
                Variant* variant = &ext->extended.enum_.variants[i];
                for (int j = 0; j < variant->field_count; j++) {
                    collect_free_type_vars_internal(variant->field_types[j], set,
                                                    bound_vars, bound_count);
                }
            }
            break;
        }
        default:
            break;
    }
}

static void collect_free_type_vars_env(TypeEnv* env, IntSet* set) {
    if (!env || !set) return;

    for (TypeEnv* current = env; current; current = current->parent) {
        TypeEnvEntry* entry = current->entries;
        while (entry) {
            if (entry->scheme && entry->scheme->type) {
                collect_free_type_vars_internal(entry->scheme->type, set,
                                                entry->scheme->bound_vars,
                                                entry->scheme->bound_count);
            }
            entry = entry->next;
        }
    }
}

static TypeScheme* type_scheme_new(Type* type, char** bound_vars,
                                   int bound_count) {
    TypeScheme* scheme = type_arena_alloc(sizeof(TypeScheme));
    if (!scheme) return NULL;

    scheme->type = type;
    scheme->bound_count = bound_count;

    if (bound_count > 0) {
        scheme->bound_vars = type_arena_alloc(sizeof(char*) * bound_count);
        if (!scheme->bound_vars) return NULL;
        for (int i = 0; i < bound_count; i++) {
            scheme->bound_vars[i] = type_arena_alloc(strlen(bound_vars[i]) + 1);
            if (!scheme->bound_vars[i]) return NULL;
            strcpy(scheme->bound_vars[i], bound_vars[i]);
        }
    } else {
        scheme->bound_vars = NULL;
    }

    return scheme;
}


// ---- Generalization ----
static TypeScheme* generalize(TypeEnv* env, Type* type) {
    if (!type) return NULL;

    IntSet* free_in_type = int_set_create();
    if (free_in_type) {
        collect_free_type_vars_internal(type, free_in_type, NULL, 0);
    }

    IntSet* free_in_env = int_set_create();
    if (free_in_env) {
        collect_free_type_vars_env(env, free_in_env);
    }

    int quantifiable = 0;
    if (free_in_type && free_in_type->values) {
        for (int i = 0; i < free_in_type->count; i++) {
            int var_id = free_in_type->values[i];
            if (!int_set_contains(free_in_env, var_id)) {
                quantifiable++;
            }
        }
    }

    if (quantifiable == 0) {
        return type_scheme_new(type, NULL, 0);
    }

    char** bound_names = type_arena_alloc(sizeof(char*) * quantifiable);
    if (!bound_names) {
        return type_scheme_new(type, NULL, 0);
    }

    int actual_bound = 0;
    if (free_in_type && free_in_type->values) {
        for (int i = 0; i < free_in_type->count; i++) {
            int var_id = free_in_type->values[i];
            if (int_set_contains(free_in_env, var_id)) {
                continue;
            }

            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%d", var_id);
            char* name = type_arena_alloc(strlen(buffer) + 1);
            if (!name) {
                continue;
            }
            strcpy(name, buffer);
            bound_names[actual_bound++] = name;
        }
    }

    if (actual_bound == 0) {
        return type_scheme_new(type, NULL, 0);
    }

    return type_scheme_new(type, bound_names, actual_bound);
}

static bool type_env_define_import_binding(TypeEnv* env, const char* binding_name,
                                          const char* original_name, ModuleExportKind kind,
                                          Type* exported_type) {
    if (!env || !binding_name) {
        return false;
    }

    const char* lookup_name = original_name ? original_name : binding_name;

    Type* resolved_type = exported_type;
    if (!resolved_type) {
        switch (kind) {
            case MODULE_EXPORT_KIND_GLOBAL:
                resolved_type = getPrimitiveType(TYPE_ANY);
                break;
            case MODULE_EXPORT_KIND_FUNCTION:
                resolved_type = getPrimitiveType(TYPE_FUNCTION);
                break;
            case MODULE_EXPORT_KIND_STRUCT:
                resolved_type = findStructType(lookup_name);
                break;
            case MODULE_EXPORT_KIND_ENUM:
                resolved_type = findEnumType(lookup_name);
                break;
            default:
                return false;
        }
    }

    if (!resolved_type) {
        return false;
    }

    if (kind == MODULE_EXPORT_KIND_STRUCT) {
        registerStructTypeAlias(binding_name, resolved_type);
    } else if (kind == MODULE_EXPORT_KIND_ENUM) {
        registerEnumTypeAlias(binding_name, resolved_type);
    }

    TypeScheme* scheme = generalize(env, resolved_type);
    if (!scheme) {
        return false;
    }

    type_env_define(env, binding_name, scheme);
    return true;
}

static void define_builtin_function(TypeEnv* env, const char* name,
                                    Type* return_type, Type** params,
                                    int param_count) {
    if (!env || !name || !return_type) {
        return;
    }

    Type** param_copy = NULL;
    if (param_count > 0) {
        param_copy = type_arena_alloc(sizeof(Type*) * param_count);
        if (!param_copy) {
            return;
        }
        for (int i = 0; i < param_count; i++) {
            param_copy[i] = params[i];
        }
    }

    Type* func_type = createFunctionType(return_type, param_copy, param_count);
    if (!func_type) {
        return;
    }

    TypeScheme* scheme = generalize(env, func_type);
    if (!scheme) {
        return;
    }

    type_env_define(env, name, scheme);
}

static void register_builtin_functions(TypeEnv* env) {
    if (!env) {
        return;
    }

    Type* any_type = getPrimitiveType(TYPE_ANY);

    // len(array[T]) -> i32
    Type* len_element = make_var_type(NULL);
    if (!len_element) {
        len_element = any_type;
    }
    Type* len_array = createArrayType(len_element);
    if (len_array) {
        Type* len_params[1] = {len_array};
        define_builtin_function(env, "len", getPrimitiveType(TYPE_I32),
                                len_params, 1);
    }

    // push(array[T], T) -> array[T]
    Type* push_element = make_var_type(NULL);
    if (!push_element) {
        push_element = any_type;
    }
    Type* push_array = createArrayType(push_element);
    if (push_array) {
        Type* push_params[2] = {push_array, push_element};
        define_builtin_function(env, "push", push_array, push_params, 2);
    }

    // pop(array[T]) -> T
    Type* pop_element = make_var_type(NULL);
    if (!pop_element) {
        pop_element = any_type;
    }
    Type* pop_array = createArrayType(pop_element);
    if (pop_array) {
        Type* pop_params[1] = {pop_array};
        define_builtin_function(env, "pop", pop_element, pop_params, 1);
    }
}

static void register_builtin_enums(void) {
    const char* enum_name = "Result";
    if (findEnumType(enum_name)) {
        return;
    }

    ObjString* name = create_compiler_string(enum_name);
    if (!name) {
        return;
    }

    Variant* variants = calloc(2, sizeof(Variant));
    if (!variants) {
        free_compiler_string(name);
        return;
    }

    bool success = false;

    do {
        variants[0].name = create_compiler_string("Ok");
        if (!variants[0].name) {
            break;
        }
        variants[0].field_count = 1;
        variants[0].field_types = calloc(1, sizeof(Type*));
        if (!variants[0].field_types) {
            break;
        }
        variants[0].field_types[0] = getPrimitiveType(TYPE_ANY);
        variants[0].field_names = calloc(1, sizeof(ObjString*));
        if (!variants[0].field_names) {
            break;
        }
        variants[0].field_names[0] = create_compiler_string("value");
        if (!variants[0].field_names[0]) {
            break;
        }

        variants[1].name = create_compiler_string("Err");
        if (!variants[1].name) {
            break;
        }
        variants[1].field_count = 1;
        variants[1].field_types = calloc(1, sizeof(Type*));
        if (!variants[1].field_types) {
            break;
        }
        variants[1].field_types[0] = getPrimitiveType(TYPE_STRING);
        variants[1].field_names = calloc(1, sizeof(ObjString*));
        if (!variants[1].field_names) {
            break;
        }
        variants[1].field_names[0] = create_compiler_string("message");
        if (!variants[1].field_names[0]) {
            break;
        }

        if (!createEnumType(name, variants, 2)) {
            break;
        }

        success = true;
    } while (false);

    if (!success) {
        cleanup_variant_info(variants, 2);
        free_compiler_string(name);
    }
}

static Type* instantiate_type_scheme(TypeScheme* scheme) {
    if (!scheme) return NULL;
    if (scheme->bound_count <= 0) {
        return scheme->type;
    }

    HashMap* mapping = hashmap_new();
    if (!mapping) {
        return scheme->type;
    }

    for (int i = 0; i < scheme->bound_count; i++) {
        if (!scheme->bound_vars || !scheme->bound_vars[i]) continue;
        Type* fresh = make_var_type(NULL);
        if (!fresh) continue;
        hashmap_set(mapping, scheme->bound_vars[i], fresh);
    }

    Type* instantiated = fresh_type(scheme->type, mapping);
    hashmap_free(mapping);
    return instantiated ? instantiated : scheme->type;
}


// ---- Literal type inference ----
static Type* infer_literal(Value literal) {
    switch (literal.type) {
        case VAL_BOOL:
            return getPrimitiveType(TYPE_BOOL);
        case VAL_I32:
            return getPrimitiveType(TYPE_I32);
        case VAL_I64:
            return getPrimitiveType(TYPE_I64);
        case VAL_U32:
            return getPrimitiveType(TYPE_U32);
        case VAL_U64:
            return getPrimitiveType(TYPE_U64);
        case VAL_F64:
            return getPrimitiveType(TYPE_F64);
        case VAL_NUMBER:
            // Generic number - infer based on value
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
// Global error tracking for type inference
static bool type_inference_has_errors = false;

static void set_type_error(void) {
    type_inference_has_errors = true;
}

bool has_type_inference_errors(void) {
    return type_inference_has_errors;
}

void reset_type_inference_errors(void) {
    type_inference_has_errors = false;
}

// ---- Algorithm W ----
// ---- Cast validation ----
// ---- Scope tracking for variable violations ----


static bool is_numeric_type(Type* type) {
    if (!type) return false;

    type = prune(type);
    if (!type) return false;

    return type->kind == TYPE_I32 || type->kind == TYPE_I64 ||
           type->kind == TYPE_U32 || type->kind == TYPE_U64 ||
           type->kind == TYPE_F64;
}

static bool is_integer_type(Type* type) {
    if (!type) return false;

    type = prune(type);
    if (!type) return false;

    return type->kind == TYPE_I32 || type->kind == TYPE_I64 ||
           type->kind == TYPE_U32 || type->kind == TYPE_U64;
}

static bool is_cast_allowed(Type* from, Type* to) {
    if (!from || !to) return false;
    
    // Same types are always allowed
    if (type_equals_extended(from, to)) return true;
    
    // Allowed numeric conversions
    if (is_numeric_type(from) && is_numeric_type(to)) {
        return true;
    }
    
    // Bool conversions
    if (from->kind == TYPE_BOOL || to->kind == TYPE_BOOL) {
        // Only bool <-> numeric conversions are allowed, not string
        if (is_numeric_type(from) || is_numeric_type(to)) {
            return true;
        }
    }
    
    // String conversions - very restricted
    if (from->kind == TYPE_STRING || to->kind == TYPE_STRING) {
        // Only allow string -> string (identity) or between string and character types
        // No automatic conversions from string to numbers or bools
        return false;
    }
    
    return false;
}

Type* algorithm_w(TypeEnv* env, ASTNode* node) {
    if (!node) return NULL;

    // Simple recursion protection
    static bool in_error = false;
    if (in_error) {
        return getPrimitiveType(TYPE_ERROR);
    }
    
    switch (node->type) {
        case NODE_LITERAL:
            DEBUG_TYPE_INFERENCE_PRINT("Processing literal with value type %d", (int)node->literal.value.type);
            switch (node->literal.value.type) {
                case VAL_I32:
                    DEBUG_TYPE_INFERENCE_PRINT("VAL_I32 -> TYPE_I32");
                    return getPrimitiveType(TYPE_I32);
                case VAL_I64: return getPrimitiveType(TYPE_I64);
                case VAL_U32: return getPrimitiveType(TYPE_U32);
                case VAL_U64: return getPrimitiveType(TYPE_U64);
                case VAL_F64: return getPrimitiveType(TYPE_F64);
                case VAL_BOOL: 
                    DEBUG_TYPE_INFERENCE_PRINT("VAL_BOOL -> TYPE_BOOL");
                    return getPrimitiveType(TYPE_BOOL);
                case VAL_STRING: return getPrimitiveType(TYPE_STRING);
                default: 
                    DEBUG_TYPE_INFERENCE_PRINT("Unknown value type %d -> TYPE_UNKNOWN", (int)node->literal.value.type);
                    return getPrimitiveType(TYPE_UNKNOWN);
            }
        case NODE_ARRAY_LITERAL: {
            if (node->arrayLiteral.count == 0) {
                Type* element_var = make_var_type(NULL);
                if (!element_var) {
                    element_var = getPrimitiveType(TYPE_ANY);
                }
                return createArrayType(element_var);
            }

            Type* element_type = NULL;
            for (int i = 0; i < node->arrayLiteral.count; i++) {
                Type* current_type = algorithm_w(env, node->arrayLiteral.elements[i]);
                if (!current_type) {
                    return NULL;
                }

                if (!element_type) {
                    element_type = current_type;
                    continue;
                }

                if (!unify(element_type, current_type)) {
                    const char* expected = getTypeName(element_type->kind);
                    const char* found = getTypeName(current_type->kind);
                    report_type_mismatch(node->arrayLiteral.elements[i]->location,
                                         expected, found);
                    set_type_error();
                    return NULL;
                }
            }

            if (!element_type) {
                element_type = getPrimitiveType(TYPE_ANY);
            }

            return createArrayType(element_type);
        }
        case NODE_STRUCT_LITERAL: {
            if (!node->structLiteral.structName) {
                set_type_error();
                return NULL;
            }

            Type* struct_type = findStructType(node->structLiteral.structName);
            if (!struct_type) {
                report_undefined_type(node->location, node->structLiteral.structName);
                set_type_error();
                return NULL;
            }

            TypeExtension* ext = get_type_extension(struct_type);
            if (!ext) {
                report_type_mismatch(node->location, node->structLiteral.structName, "struct");
                set_type_error();
                return NULL;
            }

            for (int i = 0; i < node->structLiteral.fieldCount; i++) {
                StructLiteralField* field = &node->structLiteral.fields[i];
                bool matched = false;
                Type* field_type = NULL;

                if (ext->extended.structure.fields) {
                    for (int j = 0; j < ext->extended.structure.fieldCount; j++) {
                        FieldInfo* defined = &ext->extended.structure.fields[j];
                        if (defined->name && defined->name->chars &&
                            strcmp(defined->name->chars, field->name) == 0) {
                            matched = true;
                            field_type = defined->type;
                            break;
                        }
                    }
                }

                if (!matched) {
                    report_undefined_variable(node->location, field->name);
                    set_type_error();
                    return NULL;
                }

                Type* value_type = algorithm_w(env, field->value);
                if (!value_type) {
                    return NULL;
                }

                if (field_type && !unify(field_type, value_type)) {
                    report_type_mismatch(field->value->location, getTypeName(field_type->kind),
                                         getTypeName(value_type->kind));
                    set_type_error();
                    return NULL;
                }
            }

            return struct_type;
        }
        case NODE_INDEX_ACCESS: {
            Type* array_type = algorithm_w(env, node->indexAccess.array);
            Type* index_type = algorithm_w(env, node->indexAccess.index);
            if (!array_type || !index_type) {
                return NULL;
            }

            array_type = prune(array_type);
            index_type = prune(index_type);

            if (array_type && array_type->kind == TYPE_VAR) {
                Type* element_var = make_var_type(env);
                if (!element_var) {
                    set_type_error();
                    return NULL;
                }

                Type* inferred_array = createArrayType(element_var);
                if (!inferred_array) {
                    set_type_error();
                    return NULL;
                }

                if (!unify(array_type, inferred_array)) {
                    report_type_mismatch(node->indexAccess.array->location, "array",
                                         getTypeName(array_type->kind));
                    set_type_error();
                    return NULL;
                }
                array_type = prune(array_type);
            }

            if (index_type && index_type->kind == TYPE_VAR) {
                Type* assumed_index = getPrimitiveType(TYPE_I32);
                if (assumed_index) {
                    unify(index_type, assumed_index);
                    index_type = prune(index_type);
                }
            }

            if (array_type->kind != TYPE_ARRAY) {
                report_type_mismatch(node->indexAccess.array->location, "array",
                                     getTypeName(array_type->kind));
                set_type_error();
                return NULL;
            }

            if (!is_integer_type(index_type)) {
                report_type_mismatch(node->indexAccess.index->location, "integer index",
                                     getTypeName(index_type->kind));
                set_type_error();
                return NULL;
            }

            Type* element_type = array_type->info.array.elementType;
            if (!element_type) {
                element_type = getPrimitiveType(TYPE_ANY);
            }
            return element_type;
        }
        case NODE_MEMBER_ACCESS: {
            node->member.resolvesToEnum = false;
            node->member.resolvesToEnumVariant = false;
            node->member.enumVariantIndex = -1;
            node->member.enumVariantArity = 0;
            node->member.enumTypeName = NULL;
            Type* object_type = algorithm_w(env, node->member.object);
            if (!object_type) {
                return NULL;
            }

            Type* base_type = object_type;
            if (base_type->kind == TYPE_INSTANCE && base_type->info.instance.base) {
                base_type = base_type->info.instance.base;
            }

            if (base_type->kind == TYPE_STRUCT) {
                TypeExtension* ext = get_type_extension(base_type);
                if (!ext) {
                    report_type_mismatch(node->location, "struct", "unknown");
                    set_type_error();
                    return NULL;
                }

                // Look for matching field
                if (ext->extended.structure.fields) {
                    for (int i = 0; i < ext->extended.structure.fieldCount; i++) {
                        FieldInfo* field = &ext->extended.structure.fields[i];
                        if (field->name && field->name->chars &&
                            strcmp(field->name->chars, node->member.member) == 0) {
                            node->member.isMethod = false;
                            node->member.isInstanceMethod = false;
                            return field->type ? field->type : getPrimitiveType(TYPE_UNKNOWN);
                        }
                    }
                }

                // Look for matching method
                if (ext->extended.structure.methods) {
                    for (int i = 0; i < ext->extended.structure.methodCount; i++) {
                        Method* method = &ext->extended.structure.methods[i];
                        if (method->name && method->name->chars &&
                            strcmp(method->name->chars, node->member.member) == 0) {
                            node->member.isMethod = true;
                            node->member.isInstanceMethod = method->isInstance;
                            return method->type;
                        }
                    }
                }

                report_undefined_variable(node->location, node->member.member);
                set_type_error();
                return NULL;
            }

            if (base_type->kind == TYPE_ENUM) {
                TypeExtension* ext = get_type_extension(base_type);
                if (!ext || !ext->extended.enum_.variants) {
                    report_type_mismatch(node->location, "enum", "unknown");
                    set_type_error();
                    return NULL;
                }

                node->member.resolvesToEnum = true;
                if (ext->extended.enum_.name && ext->extended.enum_.name->chars) {
                    node->member.enumTypeName = ext->extended.enum_.name->chars;
                }

                for (int i = 0; i < ext->extended.enum_.variant_count; i++) {
                    Variant* variant = &ext->extended.enum_.variants[i];
                    if (variant->name && variant->name->chars &&
                        strcmp(variant->name->chars, node->member.member) == 0) {
                        node->member.isMethod = false;
                        node->member.isInstanceMethod = false;
                        node->member.resolvesToEnumVariant = true;
                        node->member.enumVariantIndex = i;
                        node->member.enumVariantArity = variant->field_count;

                        if (variant->field_count == 0) {
                            return base_type;
                        }

                        if (!variant->field_types && variant->field_count > 0) {
                            // Construct a default parameter type list of unknowns
                            Type** defaults = calloc((size_t)variant->field_count, sizeof(Type*));
                            if (!defaults) {
                                set_type_error();
                                return NULL;
                            }
                            for (int j = 0; j < variant->field_count; j++) {
                                defaults[j] = getPrimitiveType(TYPE_UNKNOWN);
                            }
                            Type* constructor_type = createFunctionType(base_type, defaults, variant->field_count);
                            free(defaults);
                            if (!constructor_type) {
                                set_type_error();
                                return NULL;
                            }
                            return constructor_type;
                        }

                        Type* constructor_type =
                            createFunctionType(base_type, variant->field_types, variant->field_count);
                        if (!constructor_type) {
                            set_type_error();
                            return NULL;
                        }
                        return constructor_type;
                    }
                }

                report_undefined_variable(node->location, node->member.member);
                set_type_error();
                return NULL;
            }

            report_type_mismatch(node->location, "struct or enum", getTypeName(base_type->kind));
            set_type_error();
            return NULL;
        }
        case NODE_IDENTIFIER: {
            DEBUG_TYPE_INFERENCE_PRINT("Looking up identifier '%s'", node->identifier.name);
            TypeScheme* scheme = type_env_lookup(env, node->identifier.name);
            if (scheme) {
                Type* instantiated = instantiate_type_scheme(scheme);
                DEBUG_TYPE_INFERENCE_PRINT("Found scheme for '%s', type kind: %d",
                       node->identifier.name, (int)(instantiated ? instantiated->kind : scheme->type->kind));
                return instantiated ? instantiated : scheme->type;
            }
            Type* struct_type = findStructType(node->identifier.name);
            if (struct_type) {
                DEBUG_TYPE_INFERENCE_PRINT("Identifier '%s' resolved as struct type", node->identifier.name);
                return struct_type;
            }
            Type* enum_type = findEnumType(node->identifier.name);
            if (enum_type) {
                DEBUG_TYPE_INFERENCE_PRINT("Identifier '%s' resolved as enum type", node->identifier.name);
                return enum_type;
            }
            DEBUG_TYPE_INFERENCE_PRINT("Identifier '%s' not found in type environment", node->identifier.name);
            report_undefined_variable(node->location, node->identifier.name);
            set_type_error();
            return NULL;
        }

        case NODE_ARRAY_ASSIGN: {
            Type* target_type = algorithm_w(env, node->arrayAssign.target);
            Type* value_type = algorithm_w(env, node->arrayAssign.value);
            if (!target_type || !value_type) {
                return NULL;
            }

            if (!unify(target_type, value_type)) {
                const char* expected = getTypeName(target_type->kind);
                const char* found = getTypeName(value_type->kind);
                report_type_mismatch(node->arrayAssign.value->location, expected, found);
                set_type_error();
                return NULL;
            }

            return target_type;
        }
        case NODE_MEMBER_ASSIGN: {
            if (!node->memberAssign.target) {
                set_type_error();
                return NULL;
            }

            Type* target_type = algorithm_w(env, node->memberAssign.target);
            if (!target_type) {
                return NULL;
            }

            if (node->memberAssign.target->type != NODE_MEMBER_ACCESS ||
                node->memberAssign.target->member.isMethod) {
                report_type_mismatch(node->location, "struct field", "method");
                set_type_error();
                return NULL;
            }

            Type* value_type = algorithm_w(env, node->memberAssign.value);
            if (!value_type) {
                return NULL;
            }

            if (!unify(target_type, value_type)) {
                report_type_mismatch(node->location, getTypeName(target_type->kind),
                                     getTypeName(value_type->kind));
                set_type_error();
                return NULL;
            }

            return getPrimitiveType(TYPE_VOID);
        }

        case NODE_ARRAY_SLICE: {
            Type* array_type = algorithm_w(env, node->arraySlice.array);
            if (!array_type) {
                return NULL;
            }

            array_type = prune(array_type);

            if (array_type->kind != TYPE_ARRAY) {
                report_type_mismatch(node->arraySlice.array->location, "array",
                                     getTypeName(array_type->kind));
                set_type_error();
                return NULL;
            }

            if (node->arraySlice.start) {
                Type* start_type = algorithm_w(env, node->arraySlice.start);
                if (!start_type) {
                    return NULL;
                }
                if (!is_integer_type(start_type)) {
                    report_type_mismatch(node->arraySlice.start->location, "integer index",
                                         getTypeName(start_type->kind));
                    set_type_error();
                    return NULL;
                }
            }

            if (node->arraySlice.end) {
                Type* end_type = algorithm_w(env, node->arraySlice.end);
                if (!end_type) {
                    return NULL;
                }
                if (!is_integer_type(end_type)) {
                    report_type_mismatch(node->arraySlice.end->location, "integer index",
                                         getTypeName(end_type->kind));
                    set_type_error();
                    return NULL;
                }
            }

            Type* element_type = array_type->info.array.elementType;
            if (!element_type) {
                element_type = getPrimitiveType(TYPE_ANY);
            }

            return createArrayType(element_type);
        }

        case NODE_TIME_STAMP: {
            // time_stamp() returns f64 (seconds as double)
            DEBUG_TYPE_INFERENCE_PRINT("Processing NODE_TIME_STAMP, returning TYPE_F64");
            return getPrimitiveType(TYPE_F64);
        }

        case NODE_BINARY: {
            Type* l = algorithm_w(env, node->binary.left);
            Type* r = algorithm_w(env, node->binary.right);
            if (!l || !r) return NULL;

            if (strcmp(node->binary.op, "+") == 0 ||
                strcmp(node->binary.op, "-") == 0 ||
                strcmp(node->binary.op, "*") == 0 ||
                strcmp(node->binary.op, "/") == 0 ||
                strcmp(node->binary.op, "%") == 0) {
                // Rust-like behavior: Allow literals to adapt to non-literal types
                bool left_is_literal = (node->binary.left && node->binary.left->type == NODE_LITERAL);
                bool right_is_literal = (node->binary.right && node->binary.right->type == NODE_LITERAL);
                
                if (left_is_literal && !right_is_literal) {
                    // Left is literal, adapt to right's type. When the right
                    // hand side is a fresh type variable we still need to
                    // unify it with the literal type so later uses of the
                    // variable see a concrete numeric type.
                    if (r && r->kind == TYPE_VAR) {
                        unify(r, l);
                    }
                    return r;
                } else if (right_is_literal && !left_is_literal) {
                    // Right is literal, adapt to left's type. Propagate the
                    // literal's concrete type into the inferred variable when
                    // possible so exported function signatures don't keep
                    // unconstrained type variables.
                    if (l && l->kind == TYPE_VAR) {
                        unify(l, r);
                    }
                    return l;
                } else if (unify(l, r)) {
                    // Both same type or both literals - use unified type
                    return l;
                } else {
                    // Neither is literal and types don't match - require explicit casting
                    // Show actual types for better error messages
                    const char* left_type = getTypeName(l->kind);
                    const char* right_type = getTypeName(r->kind);
                    
                    // Create a more helpful error message showing the actual operation
                    char error_context[256];
                    snprintf(error_context, sizeof(error_context), 
                            "Cannot %s %s and %s", 
                            (strcmp(node->binary.op, "+") == 0) ? "add" :
                            (strcmp(node->binary.op, "-") == 0) ? "subtract" :
                            (strcmp(node->binary.op, "*") == 0) ? "multiply" :
                            (strcmp(node->binary.op, "/") == 0) ? "divide" :
                            (strcmp(node->binary.op, "%") == 0) ? "modulo" : "operate on",
                            left_type, right_type);
                    
                    report_type_mismatch(node->location, left_type, right_type);
                    set_type_error();
                    return NULL;
                }
                return l;
            } else if (strcmp(node->binary.op, "==") == 0 ||
                       strcmp(node->binary.op, "!=") == 0 ||
                       strcmp(node->binary.op, "<") == 0 ||
                       strcmp(node->binary.op, "<=") == 0 ||
                       strcmp(node->binary.op, ">") == 0 ||
                       strcmp(node->binary.op, ">=") == 0) {
                // Comparison operations always return bool
                return getPrimitiveType(TYPE_BOOL);
            } else if (strcmp(node->binary.op, "and") == 0 ||
                       strcmp(node->binary.op, "or") == 0) {
                // Logical operations: both operands should be bool, result is bool
                DEBUG_TYPE_INFERENCE_PRINT("Processing logical operator '%s'", node->binary.op);
                DEBUG_TYPE_INFERENCE_PRINT("Left operand type: %s (kind=%d)", getTypeName(l->kind), (int)l->kind);
                DEBUG_TYPE_INFERENCE_PRINT("Right operand type: %s (kind=%d)", getTypeName(r->kind), (int)r->kind);
                
                if (l->kind != TYPE_BOOL) {
                    DEBUG_TYPE_INFERENCE_PRINT("Error: Left operand is not bool");
                    report_type_mismatch(node->location, "bool", getTypeName(l->kind));
                    set_type_error();
                    return NULL;
                }
                if (r->kind != TYPE_BOOL) {
                    DEBUG_TYPE_INFERENCE_PRINT("Error: Right operand is not bool");
                    report_type_mismatch(node->location, "bool", getTypeName(r->kind));
                    set_type_error();
                    return NULL;
                }
                DEBUG_TYPE_INFERENCE_PRINT("Both operands are bool, returning TYPE_BOOL");
                return getPrimitiveType(TYPE_BOOL);
            }
            report_unsupported_operation(node->location, node->binary.op, "binary");
            set_type_error();
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

            Type* var_type = NULL;
            if (init_type && anno_type) {
                // Rust-like behavior: Allow literals to adapt to declared type
                bool can_adapt = false;
                
                // Check if initializer is a literal that can adapt to the declared type
                if (node->varDecl.initializer && node->varDecl.initializer->type == NODE_LITERAL) {
                    can_adapt = true; // Literals can always adapt to declared types
                }
                
                if (can_adapt || unify(init_type, anno_type)) {
                    var_type = anno_type; // Use declared type, not inferred type
                } else {
                    report_type_mismatch(node->location, "declared type", "initializer type");
                    set_type_error();
                    return NULL;
                }
            } else if (init_type) {
                var_type = init_type;
            } else if (anno_type) {
                var_type = anno_type;
            } else {
                report_type_annotation_required(node->location, "variable declaration");
                set_type_error();
                return NULL;
            }

            // Add the variable to the type environment
            if (node->varDecl.name && var_type) {
                TypeScheme* scheme = NULL;
                if (node->varDecl.isMutable) {
                    scheme = type_scheme_new(var_type, NULL, 0);
                } else {
                    scheme = generalize(env, var_type);
                }
                if (scheme) {
                    type_env_define(env, node->varDecl.name, scheme);
                }
            }

            return getPrimitiveType(TYPE_VOID);
        }
        case NODE_ASSIGN: {
            Type* value_type = algorithm_w(env, node->assign.value);
            
            // Always create the variable, even if type inference fails
            if (node->assign.name) {
                Type* assigned_type;
                if (!value_type) {
                    // If value type inference failed, assign ERROR type to variable
                    assigned_type = getPrimitiveType(TYPE_ERROR);
                } else {
                    assigned_type = value_type;
                }
                
                TypeScheme* scheme = generalize(env, assigned_type);
                type_env_define(env, node->assign.name, scheme);
            }
            
            // Return NULL if value inference failed to propagate the error
            if (!value_type) return NULL;
            
            return getPrimitiveType(TYPE_VOID);
        }
        case NODE_PRINT: {
            for (int i = 0; i < node->print.count; i++) {
                Type* t = algorithm_w(env, node->print.values[i]);
                if (!t) return NULL;
            }
            return getPrimitiveType(TYPE_VOID);
        }
        case NODE_FUNCTION: {
            if (node->function.isMethod && !node->function.isInstanceMethod &&
                node->function.paramCount > 0 && node->function.params &&
                node->function.params[0].name &&
                strcmp(node->function.params[0].name, "self") == 0) {
                node->function.isInstanceMethod = true;
            }

            // For function declarations, get the actual return type from type annotation
            bool return_type_inferred = false;
            Type* return_type = NULL;

            if (node->function.returnType) {
                return_type = algorithm_w(env, node->function.returnType);
                if (!return_type) {
                    return_type = getPrimitiveType(TYPE_VOID);
                }
            } else {
                return_type = make_var_type(NULL);
                if (!return_type) {
                    return_type = getPrimitiveType(TYPE_VOID);
                } else {
                    return_type_inferred = true;
                }
            }

            Type* receiver_type = NULL;
            if (node->function.isMethod && node->function.methodStructName) {
                receiver_type = findStructType(node->function.methodStructName);
                if (!receiver_type) {
                    report_undefined_type(node->location, node->function.methodStructName);
                    set_type_error();
                    return NULL;
                }
            }

            // Process parameters and their types
            Type** param_types = NULL;
            int param_count = node->function.paramCount;

            if (param_count > 0) {
                param_types = type_arena_alloc(sizeof(Type*) * param_count);
                for (int i = 0; i < param_count; i++) {
                    if (node->function.isMethod && node->function.isInstanceMethod && i == 0) {
                        param_types[i] = receiver_type ? receiver_type : getPrimitiveType(TYPE_UNKNOWN);
                        if (node->function.params[i].typeAnnotation) {
                            Type* annotated = algorithm_w(env, node->function.params[i].typeAnnotation);
                            if (annotated && param_types[i] && !unify(param_types[i], annotated)) {
                                report_type_mismatch(node->location, node->function.methodStructName,
                                                     node->function.params[i].name);
                                set_type_error();
                                return NULL;
                            }
                        }
                    } else if (node->function.params[i].typeAnnotation) {
                        param_types[i] = algorithm_w(env, node->function.params[i].typeAnnotation);
                        if (!param_types[i]) param_types[i] = getPrimitiveType(TYPE_I32);
                    } else {
                        param_types[i] = make_var_type(NULL);
                        if (!param_types[i]) {
                            param_types[i] = getPrimitiveType(TYPE_ANY);
                        }
                    }
                }
            }

            Type* func_type = createFunctionType(return_type, param_types, param_count);

            // Add function to environment if it has a name
            if (node->function.name) {
                TypeScheme* scheme = generalize(env, func_type);
                type_env_define(env, node->function.name, scheme);
            }

            // Create a new scope for the function body and add parameters to it
            TypeEnv* function_env = type_env_new(env);
            if (function_env) {
                function_env->expected_return_type = return_type;
            }

            // Add parameters to the function's local environment
            for (int i = 0; i < param_count; i++) {
                if (node->function.params[i].name && param_types && param_types[i]) {
                    // Parameters are monomorphic within the body, so bind them directly
                    // without generalization. This lets any constraints inferred from the
                    // body flow back into the function signature's parameter types.
                    TypeScheme* param_scheme = type_scheme_new(param_types[i], NULL, 0);
                    type_env_define(function_env, node->function.params[i].name, param_scheme);
                }
            }

            // Type-check the function body in the new environment
            if (node->function.body) {
                Type* body_type = algorithm_w(function_env, node->function.body);
                // The body type isn't used for the function's type, but this ensures the body is type-checked
                (void)body_type;
            }

            // Prune the return and parameter types now that the body has been analyzed
            Type* resolved_return = prune(return_type);
            if (!resolved_return) {
                resolved_return = getPrimitiveType(TYPE_VOID);
            } else if (return_type_inferred && resolved_return->kind == TYPE_VAR) {
                resolved_return = getPrimitiveType(TYPE_VOID);
            }
            func_type->info.function.returnType = resolved_return;

            if (func_type->info.function.paramTypes && param_types) {
                for (int i = 0; i < param_count; i++) {
                    if (!param_types[i]) continue;
                    Type* pruned = prune(param_types[i]);
                    if (pruned) {
                        func_type->info.function.paramTypes[i] = pruned;
                        param_types[i] = pruned;
                    }
                }
            }

            return func_type;
        }
        case NODE_CALL: {
            bool is_member_call = (node->call.callee->type == NODE_MEMBER_ACCESS);

            Type* callee_type = NULL;
            if (node->call.callee->type == NODE_IDENTIFIER) {
                TypeScheme* scheme = type_env_lookup(env, node->call.callee->identifier.name);
                if (scheme && scheme->type) {
                    callee_type = instantiate_type_scheme(scheme);
                }
            }

            if (!callee_type) {
                callee_type = algorithm_w(env, node->call.callee);
            }

            bool is_method_call = false;
            bool is_instance_method = false;
            if (is_member_call) {
                is_method_call = node->call.callee->member.isMethod;
                is_instance_method = node->call.callee->member.isInstanceMethod;
            }

            Type** arg_types = NULL;
            if (node->call.argCount > 0) {
                arg_types = malloc(sizeof(Type*) * node->call.argCount);
                if (!arg_types) {
                    set_type_error();
                    return NULL;
                }
                for (int i = 0; i < node->call.argCount; i++) {
                    arg_types[i] = algorithm_w(env, node->call.args[i]);
                    if (!arg_types[i]) {
                        free(arg_types);
                        return NULL;
                    }
                }
            }

            if (callee_type && callee_type->kind == TYPE_FUNCTION) {
                int total_params = callee_type->info.function.arity;
                int offset = 0;
                if (is_member_call && is_method_call && is_instance_method && total_params > 0) {
                    offset = 1;
                }
                int expected_args = total_params - offset;
                if (expected_args < 0) expected_args = 0;

                if (node->call.argCount != expected_args) {
                    if (arg_types) free(arg_types);
                    if (!node->call.arity_error_reported) {
                        const char* callee_name = NULL;
                        if (node->call.callee->type == NODE_IDENTIFIER) {
                            callee_name = node->call.callee->identifier.name;
                        } else if (node->call.callee->type == NODE_MEMBER_ACCESS) {
                            callee_name = node->call.callee->member.member;
                        }
                        report_argument_count_mismatch(node->location,
                                                       callee_name,
                                                       expected_args,
                                                       node->call.argCount);
                        node->call.arity_error_reported = true;
                    }
                    set_type_error();
                    return NULL;
                }

                for (int i = 0; i < node->call.argCount; i++) {
                    Type* param_type = NULL;
                    if (callee_type->info.function.paramTypes && (i + offset) < callee_type->info.function.arity) {
                        param_type = callee_type->info.function.paramTypes[i + offset];
                    }
                    Type* arg_type = arg_types ? arg_types[i] : NULL;
                    if (arg_type && param_type && !unify(arg_type, param_type)) {
                        if (arg_types) free(arg_types);
                        report_type_mismatch(node->call.args[i]->location,
                                             getTypeName(param_type->kind),
                                             getTypeName(arg_type->kind));
                        set_type_error();
                        return NULL;
                    }
                }

                Type* return_type = callee_type->info.function.returnType;
                if (arg_types) free(arg_types);
                return return_type;
            }

            if (arg_types) free(arg_types);
            return getPrimitiveType(TYPE_I32);
        }
        case NODE_RETURN: {
            Type* expected_return = type_env_get_expected_return(env);

            if (node->returnStmt.value) {
                Type* value_type = algorithm_w(env, node->returnStmt.value);
                if (!value_type) {
                    return NULL;
                }

                if (expected_return) {
                    bool expected_is_var = (expected_return->kind == TYPE_VAR);
                    if (!unify(value_type, expected_return)) {
                        bool literal_return = node->returnStmt.value &&
                                              node->returnStmt.value->type == NODE_LITERAL;
                        bool conditional_return = node->returnStmt.value &&
                                                   node->returnStmt.value->type == NODE_IF;

                        if (!expected_is_var && (literal_return || conditional_return)) {
                            // Allow literals and expression-style if/else forms to adopt the
                            // annotated return type, matching the lenient behavior used for
                            // variable declarations and ternary expressions.
                            return expected_return;
                        }

                        report_type_mismatch(node->returnStmt.value->location,
                                             getTypeName(expected_return->kind),
                                             getTypeName(value_type->kind));
                        set_type_error();
                        return NULL;
                    }
                    return expected_return;
                }

                return value_type; // No expected type, propagate value type
            }

            Type* void_type = getPrimitiveType(TYPE_VOID);
            if (expected_return) {
                if (!unify(void_type, expected_return)) {
                    report_type_mismatch(node->location, getTypeName(expected_return->kind),
                                         getTypeName(void_type->kind));
                    set_type_error();
                    return NULL;
                }
                return expected_return;
            }
            return void_type;
        }
        case NODE_TYPE: {
            if (!node->typeAnnotation.name) {
                return getPrimitiveType(TYPE_UNKNOWN);
            }

            const char* type_name = node->typeAnnotation.name;

            if (strcmp(type_name, "i32") == 0) {
                return getPrimitiveType(TYPE_I32);
            } else if (strcmp(type_name, "i64") == 0) {
                return getPrimitiveType(TYPE_I64);
            } else if (strcmp(type_name, "u32") == 0) {
                return getPrimitiveType(TYPE_U32);
            } else if (strcmp(type_name, "u64") == 0) {
                return getPrimitiveType(TYPE_U64);
            } else if (strcmp(type_name, "f64") == 0) {
                return getPrimitiveType(TYPE_F64);
            } else if (strcmp(type_name, "bool") == 0) {
                return getPrimitiveType(TYPE_BOOL);
            } else if (strcmp(type_name, "string") == 0) {
                return getPrimitiveType(TYPE_STRING);
            } else if (strcmp(type_name, "void") == 0) {
                return getPrimitiveType(TYPE_VOID);
            }

            Type* struct_type = findStructType(type_name);
            if (struct_type) {
                return struct_type;
            }

            Type* enum_type = findEnumType(type_name);
            if (enum_type) {
                return enum_type;
            }

            report_undefined_type(node->location, type_name);
            set_type_error();
            return NULL;
        }
        case NODE_CAST: {
            // Cast expressions - validate that the cast is allowed
            Type* source_type = NULL;
            Type* target_type = NULL;
            
            if (node->cast.expression) {
                source_type = algorithm_w(env, node->cast.expression);
            }
            
            if (node->cast.targetType) {
                target_type = algorithm_w(env, node->cast.targetType);
            }
            
            if (!source_type || !target_type) {
                return getPrimitiveType(TYPE_UNKNOWN);
            }
            
            // Check if cast is allowed
            if (!is_cast_allowed(source_type, target_type)) {
                const char* source_name = getTypeName(source_type->kind);
                const char* target_name = getTypeName(target_type->kind);
                
                // Report cast error
                report_type_mismatch(node->location, target_name, source_name);
                set_type_error();
                return NULL;
            }
            
            return target_type;
        }
        case NODE_TERNARY: {
            // Ternary expressions: condition ? true_expr : false_expr
            Type* condition_type = algorithm_w(env, node->ternary.condition);
            Type* true_type = algorithm_w(env, node->ternary.trueExpr);
            Type* false_type = algorithm_w(env, node->ternary.falseExpr);
            
            if (!condition_type || !true_type || !false_type) {
                return getPrimitiveType(TYPE_UNKNOWN);
            }
            
            // Condition should be boolean (we can relax this later)
            // For now, just unify the two branches
            if (unify(true_type, false_type)) {
                return true_type;
            }
            
            // If types don't unify, return the true branch type as fallback
            return true_type;
        }
        case NODE_PROGRAM: {
            // Program nodes should type-check their declarations
            for (int i = 0; i < node->program.count; i++) {
                algorithm_w(env, node->program.declarations[i]);
            }
            return getPrimitiveType(TYPE_VOID);
        }
        case NODE_BLOCK: {
            // Block nodes should share the same environment as their parent
            // Only loops should create isolated scopes for their variables
            for (int i = 0; i < node->block.count; i++) {
                algorithm_w(env, node->block.statements[i]);
            }
            return getPrimitiveType(TYPE_VOID);
        }
        case NODE_UNARY: {
            // Unary operations - type-check operand and validate operation
            if (node->unary.operand) {
                Type* operand_type = algorithm_w(env, node->unary.operand);
                if (!operand_type) return NULL;
                
                // Check if the unary operator is valid for the operand type
                if (node->unary.op && strcmp(node->unary.op, "-") == 0) {
                    // Unary minus: only allowed on numeric types
                    if (!is_numeric_type(operand_type)) {
                        const char* type_name = getTypeName(operand_type->kind);
                        report_type_mismatch(node->location, "numeric type", type_name);
                        set_type_error();
                        return NULL;
                    }
                }
                // Add more unary operator validations here if needed
                
                return operand_type;
            }
            return getPrimitiveType(TYPE_UNKNOWN);
        }
        case NODE_IF: {
            // If statements - type-check condition and branches
            if (node->ifStmt.condition) {
                algorithm_w(env, node->ifStmt.condition);
            }
            if (node->ifStmt.thenBranch) {
                algorithm_w(env, node->ifStmt.thenBranch);
            }
            if (node->ifStmt.elseBranch) {
                algorithm_w(env, node->ifStmt.elseBranch);
            }
            return getPrimitiveType(TYPE_VOID);
        }
        case NODE_WHILE: {
            // While statements - type-check condition and body
            if (node->whileStmt.condition) {
                algorithm_w(env, node->whileStmt.condition);
            }
            if (node->whileStmt.body) {
                algorithm_w(env, node->whileStmt.body);
            }
            return getPrimitiveType(TYPE_VOID);
        }
        case NODE_TRY: {
            if (node->tryStmt.tryBlock) {
                algorithm_w(env, node->tryStmt.tryBlock);
            }

            if (node->tryStmt.catchBlock) {
                TypeEnv* catch_env = type_env_new(env);
                if (!catch_env) {
                    set_type_error();
                    return NULL;
                }

                if (node->tryStmt.catchVar) {
                    TypeScheme* error_scheme = type_arena_alloc(sizeof(TypeScheme));
                    if (!error_scheme) {
                        set_type_error();
                        return NULL;
                    }
                    error_scheme->type = getPrimitiveType(TYPE_ERROR);
                    error_scheme->bound_vars = NULL;
                    error_scheme->bound_count = 0;
                    type_env_define(catch_env, node->tryStmt.catchVar, error_scheme);
                }

                algorithm_w(catch_env, node->tryStmt.catchBlock);
            }

            node->dataType = getPrimitiveType(TYPE_VOID);
            return node->dataType;
        }
        case NODE_THROW: {
            if (!node->throwStmt.value) {
                return getPrimitiveType(TYPE_VOID);
            }

            Type* thrown_type = algorithm_w(env, node->throwStmt.value);
            if (!thrown_type) {
                return NULL;
            }

            Type* error_type = getPrimitiveType(TYPE_ERROR);
            if (thrown_type->kind == TYPE_STRING) {
                node->throwStmt.value->dataType = error_type;
                return getPrimitiveType(TYPE_VOID);
            }
            if (!unify(thrown_type, error_type)) {
                report_type_mismatch(node->throwStmt.value->location, "error",
                                     getTypeName(thrown_type->kind));
                set_type_error();
                return NULL;
            }

            return getPrimitiveType(TYPE_VOID);
        }
        case NODE_BREAK: {
            // Break statements have void type
            return getPrimitiveType(TYPE_VOID);
        }
        case NODE_CONTINUE: {
            // Continue statements have void type
            return getPrimitiveType(TYPE_VOID);
        }
        case NODE_STRUCT_DECL: {
            Type* existing = findStructType(node->structDecl.name);
            bool creating_new = (existing == NULL);
            FieldInfo* fields = NULL;

            if (creating_new && node->structDecl.fieldCount > 0) {
                fields = calloc((size_t)node->structDecl.fieldCount, sizeof(FieldInfo));
                if (!fields) {
                    set_type_error();
                    return NULL;
                }
            }

            for (int i = 0; i < node->structDecl.fieldCount; i++) {
                ASTNode* typeAnno = node->structDecl.fields[i].typeAnnotation;
                ASTNode* defaultValue = node->structDecl.fields[i].defaultValue;
                Type* field_type = NULL;

                if (typeAnno) {
                    field_type = algorithm_w(env, typeAnno);
                    if (!field_type) {
                        cleanup_field_info(fields, node->structDecl.fieldCount);
                        return NULL;
                    }
                } else {
                    field_type = getPrimitiveType(TYPE_UNKNOWN);
                }

                if (defaultValue) {
                    Type* default_type = algorithm_w(env, defaultValue);
                    if (!default_type) {
                        cleanup_field_info(fields, node->structDecl.fieldCount);
                        return NULL;
                    }
                    if (field_type && default_type && !unify(field_type, default_type)) {
                        report_type_mismatch(defaultValue->location, getTypeName(field_type->kind),
                                             getTypeName(default_type->kind));
                        set_type_error();
                        cleanup_field_info(fields, node->structDecl.fieldCount);
                        return NULL;
                    }
                }

                if (creating_new && fields) {
                    fields[i].type = field_type;
                    fields[i].name = create_compiler_string(node->structDecl.fields[i].name);
                    if (!fields[i].name) {
                        set_type_error();
                        cleanup_field_info(fields, node->structDecl.fieldCount);
                        return NULL;
                    }
                }
            }

            Type* struct_type = existing;
            if (creating_new) {
                ObjString* name = create_compiler_string(node->structDecl.name);
                if (!name) {
                    set_type_error();
                    cleanup_field_info(fields, node->structDecl.fieldCount);
                    return NULL;
                }

                struct_type = createStructType(name, fields, node->structDecl.fieldCount, NULL, 0);
                if (!struct_type) {
                    set_type_error();
                    free_compiler_string(name);
                    cleanup_field_info(fields, node->structDecl.fieldCount);
                    return NULL;
                }
            }

            if (struct_type) {
                node->dataType = struct_type;
            }

            return getPrimitiveType(TYPE_VOID);
        }
        case NODE_ENUM_DECL: {
            Type* existing = NULL;
            if (node->enumDecl.name) {
                existing = findEnumType(node->enumDecl.name);
            }
            bool creating_new = (existing == NULL);
            Variant* variants = NULL;

            if (creating_new && node->enumDecl.variantCount > 0) {
                variants = calloc((size_t)node->enumDecl.variantCount, sizeof(Variant));
                if (!variants) {
                    set_type_error();
                    return NULL;
                }
            }

            for (int i = 0; i < node->enumDecl.variantCount; i++) {
                int fieldCount = node->enumDecl.variants[i].fieldCount;
                Type** fieldTypes = NULL;
                ObjString** fieldNames = NULL;

                if (creating_new && variants && fieldCount > 0) {
                    fieldTypes = calloc((size_t)fieldCount, sizeof(Type*));
                    fieldNames = calloc((size_t)fieldCount, sizeof(ObjString*));
                    if (!fieldTypes || !fieldNames) {
                        if (fieldTypes) free(fieldTypes);
                        if (fieldNames) free(fieldNames);
                        set_type_error();
                        cleanup_variant_info(variants, node->enumDecl.variantCount);
                        return NULL;
                    }
                }

                for (int j = 0; j < fieldCount; j++) {
                    ASTNode* typeAnno = node->enumDecl.variants[i].fields[j].typeAnnotation;
                    Type* fieldType = NULL;
                    if (typeAnno) {
                        fieldType = algorithm_w(env, typeAnno);
                        if (!fieldType) {
                            if (fieldTypes) free(fieldTypes);
                            if (fieldNames) {
                                for (int k = 0; k < j; k++) {
                                    free_compiler_string(fieldNames[k]);
                                }
                                free(fieldNames);
                            }
                            cleanup_variant_info(variants, node->enumDecl.variantCount);
                            return NULL;
                        }
                    } else {
                        fieldType = getPrimitiveType(TYPE_UNKNOWN);
                    }

                    if (fieldTypes) {
                        fieldTypes[j] = fieldType;
                    }

                    if (fieldNames) {
                        const char* fieldNameSource = node->enumDecl.variants[i].fields[j].name;
                        if (fieldNameSource) {
                            fieldNames[j] = create_compiler_string(fieldNameSource);
                            if (!fieldNames[j]) {
                                for (int k = 0; k < j; k++) {
                                    free_compiler_string(fieldNames[k]);
                                }
                                free(fieldNames);
                                if (fieldTypes) free(fieldTypes);
                                set_type_error();
                                cleanup_variant_info(variants, node->enumDecl.variantCount);
                                return NULL;
                            }
                        } else {
                            fieldNames[j] = NULL;
                        }
                    }
                }

                if (creating_new && variants) {
                    variants[i].field_types = fieldTypes;
                    variants[i].field_names = fieldNames;
                    variants[i].field_count = fieldCount;
                    variants[i].name = create_compiler_string(node->enumDecl.variants[i].name);
                    if (!variants[i].name) {
                        set_type_error();
                        if (fieldNames) {
                            for (int j = 0; j < fieldCount; j++) {
                                free_compiler_string(fieldNames[j]);
                            }
                            free(fieldNames);
                        }
                        if (fieldTypes) free(fieldTypes);
                        cleanup_variant_info(variants, node->enumDecl.variantCount);
                        return NULL;
                    }
                } else {
                    if (fieldTypes) {
                        free(fieldTypes);
                    }
                    if (fieldNames) {
                        for (int j = 0; j < fieldCount; j++) {
                            free_compiler_string(fieldNames[j]);
                        }
                        free(fieldNames);
                    }
                }
            }

            Type* enum_type = existing;
            if (creating_new) {
                ObjString* name = create_compiler_string(node->enumDecl.name);
                if (!name) {
                    set_type_error();
                    cleanup_variant_info(variants, node->enumDecl.variantCount);
                    return NULL;
                }

                enum_type = createEnumType(name, variants, node->enumDecl.variantCount);
                if (!enum_type) {
                    set_type_error();
                    free_compiler_string(name);
                    cleanup_variant_info(variants, node->enumDecl.variantCount);
                    return NULL;
                }
            }

            if (enum_type) {
                node->dataType = enum_type;
            }

            return getPrimitiveType(TYPE_VOID);
        }
        case NODE_ENUM_MATCH_TEST: {
            Type* enum_type = algorithm_w(env, node->enumMatchTest.value);
            if (!enum_type) {
                return NULL;
            }

            Type* base_type = enum_type;
            if (base_type->kind == TYPE_INSTANCE && base_type->info.instance.base) {
                base_type = base_type->info.instance.base;
            }

            if (base_type->kind != TYPE_ENUM) {
                report_type_mismatch(node->enumMatchTest.value->location, "enum",
                                     getTypeName(base_type->kind));
                set_type_error();
                return NULL;
            }

            const char* variant_name = node->enumMatchTest.variantName;
            int variant_index = -1;
            Variant* variant = lookup_enum_variant(base_type, variant_name, &variant_index);
            if (!variant) {
                report_undefined_variable(node->location, variant_name ? variant_name : "<variant>");
                set_type_error();
                return NULL;
            }

            if (node->enumMatchTest.expectedPayloadCount >= 0 &&
                variant->field_count != node->enumMatchTest.expectedPayloadCount) {
                char expected[64];
                char found[64];
                snprintf(expected, sizeof(expected), "%d field%s",
                         variant->field_count, variant->field_count == 1 ? "" : "s");
                snprintf(found, sizeof(found), "%d binding%s",
                         node->enumMatchTest.expectedPayloadCount,
                         node->enumMatchTest.expectedPayloadCount == 1 ? "" : "s");
                report_type_mismatch(node->location, expected, found);
                set_type_error();
                return NULL;
            }

            node->enumMatchTest.variantIndex = variant_index;
            if (!node->enumMatchTest.enumTypeName) {
                node->enumMatchTest.enumTypeName = (char*)get_enum_type_name(base_type);
            }

            Type* bool_type = getPrimitiveType(TYPE_BOOL);
            node->dataType = bool_type;
            return bool_type;
        }
        case NODE_ENUM_PAYLOAD: {
            Type* enum_type = algorithm_w(env, node->enumPayload.value);
            if (!enum_type) {
                return NULL;
            }

            Type* base_type = enum_type;
            if (base_type->kind == TYPE_INSTANCE && base_type->info.instance.base) {
                base_type = base_type->info.instance.base;
            }

            if (base_type->kind != TYPE_ENUM) {
                report_type_mismatch(node->enumPayload.value->location, "enum",
                                     getTypeName(base_type->kind));
                set_type_error();
                return NULL;
            }

            int variant_index = -1;
            Variant* variant = lookup_enum_variant(base_type, node->enumPayload.variantName, &variant_index);
            if (!variant) {
                report_undefined_variable(node->location,
                                          node->enumPayload.variantName ? node->enumPayload.variantName : "<variant>");
                set_type_error();
                return NULL;
            }

            int field_index = node->enumPayload.fieldIndex;
            if (field_index < 0 || field_index >= variant->field_count) {
                report_type_mismatch(node->location, "variant payload", "pattern binding");
                set_type_error();
                return NULL;
            }

            node->enumPayload.variantIndex = variant_index;
            if (!node->enumPayload.enumTypeName) {
                node->enumPayload.enumTypeName = (char*)get_enum_type_name(base_type);
            }

            Type* field_type = NULL;
            if (variant->field_types && field_index < variant->field_count) {
                field_type = variant->field_types[field_index];
            }
            if (!field_type) {
                field_type = getPrimitiveType(TYPE_UNKNOWN);
            }

            node->dataType = field_type;
            return field_type;
        }
        case NODE_ENUM_MATCH_CHECK: {
            Type* enum_type = algorithm_w(env, node->enumMatchCheck.value);
            if (!enum_type) {
                return NULL;
            }

            Type* base_type = enum_type;
            if (base_type->kind == TYPE_INSTANCE && base_type->info.instance.base) {
                base_type = base_type->info.instance.base;
            }

            if (base_type->kind != TYPE_ENUM) {
                report_type_mismatch(node->location, "enum", getTypeName(base_type->kind));
                set_type_error();
                return getPrimitiveType(TYPE_VOID);
            }

            TypeExtension* ext = get_type_extension(base_type);
            if (!ext) {
                set_type_error();
                return getPrimitiveType(TYPE_VOID);
            }

            const char* canonical_name = get_enum_type_name(base_type);
            if (node->enumMatchCheck.enumTypeName && canonical_name &&
                strcmp(node->enumMatchCheck.enumTypeName, canonical_name) != 0) {
                report_type_mismatch(node->location, canonical_name, node->enumMatchCheck.enumTypeName);
                set_type_error();
                return getPrimitiveType(TYPE_VOID);
            }
            if (!node->enumMatchCheck.enumTypeName && canonical_name) {
                node->enumMatchCheck.enumTypeName = (char*)canonical_name;
            }

            int variant_total = ext->extended.enum_.variant_count;
            bool* seen = NULL;
            if (variant_total > 0) {
                seen = calloc((size_t)variant_total, sizeof(bool));
                if (!seen) {
                    set_type_error();
                    return getPrimitiveType(TYPE_VOID);
                }
            }

            bool encountered_error = false;

            for (int i = 0; i < node->enumMatchCheck.variantCount; i++) {
                const char* name = NULL;
                if (node->enumMatchCheck.variantNames && node->enumMatchCheck.variantNames[i]) {
                    name = node->enumMatchCheck.variantNames[i];
                }
                if (!name) {
                    continue;
                }

                int variant_index = -1;
                Variant* variant = lookup_enum_variant(base_type, name, &variant_index);
                if (!variant) {
                    report_undefined_variable(node->location, name);
                    encountered_error = true;
                    continue;
                }
                if (variant_index >= 0 && seen) {
                    if (seen[variant_index]) {
                        report_duplicate_match_arm(node->location, canonical_name, name);
                        encountered_error = true;
                        continue;
                    }
                    seen[variant_index] = true;
                }
            }

            if (!node->enumMatchCheck.hasWildcard && seen && variant_total > 0) {
                int missing_count = 0;
                size_t buffer_len = 0;
                for (int i = 0; i < variant_total; i++) {
                    if (!seen[i]) {
                        missing_count++;
                        Variant* variant = &ext->extended.enum_.variants[i];
                        if (variant && variant->name && variant->name->chars) {
                            buffer_len += strlen(variant->name->chars) + 2;
                        }
                    }
                }

                if (missing_count > 0) {
                    char* buffer = NULL;
                    if (buffer_len > 0) {
                        buffer = malloc(buffer_len + 1);
                    }
                    if (buffer) {
                        buffer[0] = '\0';
                        bool first = true;
                        for (int i = 0; i < variant_total; i++) {
                            if (!seen[i]) {
                                Variant* variant = &ext->extended.enum_.variants[i];
                                if (variant && variant->name && variant->name->chars) {
                                    if (!first) {
                                        strcat(buffer, ", ");
                                    }
                                    strcat(buffer, variant->name->chars);
                                    first = false;
                                }
                            }
                        }
                    }

                    report_non_exhaustive_match(node->location, canonical_name, buffer);
                    if (buffer) {
                        free(buffer);
                    }
                    encountered_error = true;
                }
            }

            if (seen) {
                free(seen);
            }

            if (encountered_error) {
                set_type_error();
            }

            node->dataType = getPrimitiveType(TYPE_VOID);
            return node->dataType;
        }
        case NODE_MATCH_EXPRESSION: {
            if (!node->matchExpr.arms || node->matchExpr.armCount == 0) {
                set_type_error();
                node->dataType = getPrimitiveType(TYPE_UNKNOWN);
                return node->dataType;
            }

            Type* scrutinee_type = algorithm_w(env, node->matchExpr.subject);
            if (!scrutinee_type) {
                return NULL;
            }

            Type* base_type = scrutinee_type;
            if (base_type->kind == TYPE_INSTANCE && base_type->info.instance.base) {
                base_type = base_type->info.instance.base;
            }

            bool subject_is_enum = (base_type->kind == TYPE_ENUM);
            TypeExtension* enum_ext = subject_is_enum ? get_type_extension(base_type) : NULL;
            int variant_total = (enum_ext) ? enum_ext->extended.enum_.variant_count : 0;
            bool* seen_variants = NULL;
            if (subject_is_enum && variant_total > 0) {
                seen_variants = calloc((size_t)variant_total, sizeof(bool));
            }

            TypeEnv* match_env = type_env_new(env);
            if (!match_env) {
                if (seen_variants) free(seen_variants);
                set_type_error();
                return NULL;
            }

            if (node->matchExpr.tempName) {
                TypeScheme* scrutinee_scheme = generalize(match_env, scrutinee_type);
                type_env_define(match_env, node->matchExpr.tempName, scrutinee_scheme);
            }

            typedef struct {
                Value value;
            } LiteralCase;

            LiteralCase* literal_cases = NULL;
            int literal_count = 0;
            int literal_capacity = 0;

            Type* result_type = NULL;
            bool encountered_error = false;
            bool has_enum_arm = false;

            for (int i = 0; i < node->matchExpr.armCount; i++) {
                MatchArm* arm = &node->matchExpr.arms[i];

                TypeEnv* branch_env = type_env_new(match_env);
                if (!branch_env) {
                    encountered_error = true;
                    break;
                }

                if (arm->isEnumCase) {
                    has_enum_arm = true;
                    if (!subject_is_enum) {
                        report_type_mismatch(arm->location, "enum", getTypeName(base_type->kind));
                        encountered_error = true;
                    } else {
                        const char* canonical_name = get_enum_type_name(base_type);
                        if (arm->enumTypeName && canonical_name &&
                            strcmp(arm->enumTypeName, canonical_name) != 0) {
                            report_type_mismatch(arm->location, canonical_name, arm->enumTypeName);
                            encountered_error = true;
                        }

                        int variant_index = -1;
                        Variant* variant = lookup_enum_variant(base_type, arm->variantName, &variant_index);
                        if (!variant) {
                            report_undefined_variable(arm->location,
                                                      arm->variantName ? arm->variantName : "<variant>");
                            encountered_error = true;
                        } else {
                            if (arm->payloadCount >= 0 && variant->field_count != arm->payloadCount) {
                                char expected[64];
                                char found[64];
                                snprintf(expected, sizeof(expected), "%d field%s",
                                         variant->field_count, variant->field_count == 1 ? "" : "s");
                                snprintf(found, sizeof(found), "%d binding%s",
                                         arm->payloadCount, arm->payloadCount == 1 ? "" : "s");
                                report_type_mismatch(arm->location, expected, found);
                                encountered_error = true;
                            }
                            arm->variantIndex = variant_index;
                            if (seen_variants && variant_index >= 0) {
                                if (seen_variants[variant_index]) {
                                    const char* canonical = get_enum_type_name(base_type);
                                    report_duplicate_match_arm(arm->location, canonical, arm->variantName);
                                    encountered_error = true;
                                } else {
                                    seen_variants[variant_index] = true;
                                }
                            }

                            if (variant && variant->field_types && arm->payloadNames) {
                                for (int j = 0; j < arm->payloadCount && j < variant->field_count; j++) {
                                    if (!arm->payloadNames[j]) {
                                        continue;
                                    }
                                    Type* field_type = variant->field_types[j];
                                    if (!field_type) {
                                        field_type = getPrimitiveType(TYPE_UNKNOWN);
                                    }
                                    TypeScheme* field_scheme = generalize(branch_env, field_type);
                                    type_env_define(branch_env, arm->payloadNames[j], field_scheme);
                                }
                            }
                        }
                    }
                } else if (arm->valuePattern) {
                    Type* pattern_type = algorithm_w(match_env, arm->valuePattern);
                    if (pattern_type && !unify(scrutinee_type, pattern_type)) {
                        report_type_mismatch(arm->valuePattern->location,
                                             getTypeName(scrutinee_type->kind),
                                             getTypeName(pattern_type->kind));
                        encountered_error = true;
                    }

                    if (arm->valuePattern->type == NODE_LITERAL) {
                        Value literal_value = arm->valuePattern->literal.value;
                        bool duplicate_literal = false;
                        for (int j = 0; j < literal_count; j++) {
                            if (literal_values_equal(literal_cases[j].value, literal_value)) {
                                duplicate_literal = true;
                                break;
                            }
                        }
                        if (duplicate_literal) {
                            char repr[128];
                            format_literal_value(literal_value, repr, sizeof(repr));
                            report_duplicate_literal_match_arm(arm->location, repr);
                            encountered_error = true;
                        } else {
                            if (literal_count + 1 > literal_capacity) {
                                int new_cap = literal_capacity == 0 ? 4 : literal_capacity * 2;
                                LiteralCase* new_arr = realloc(literal_cases, sizeof(LiteralCase) * new_cap);
                                if (!new_arr) {
                                    encountered_error = true;
                                } else {
                                    literal_cases = new_arr;
                                    literal_capacity = new_cap;
                                }
                            }
                            if (literal_count + 1 <= literal_capacity && literal_cases) {
                                literal_cases[literal_count++].value = literal_value;
                            }
                        }
                    }
                }

                if (arm->condition) {
                    Type* cond_type = algorithm_w(match_env, arm->condition);
                    if (cond_type && !unify(cond_type, getPrimitiveType(TYPE_BOOL))) {
                        report_type_mismatch(arm->condition->location, "bool",
                                             getTypeName(cond_type->kind));
                        encountered_error = true;
                    }
                }

                if (arm->payloadAccesses) {
                    for (int j = 0; j < arm->payloadCount; j++) {
                        if (arm->payloadAccesses[j]) {
                            algorithm_w(match_env, arm->payloadAccesses[j]);
                        }
                    }
                }

                Type* branch_type = NULL;
                if (arm->body) {
                    branch_type = algorithm_w(branch_env, arm->body);
                }
                if (!branch_type) {
                    encountered_error = true;
                } else if (!result_type) {
                    result_type = branch_type;
                } else if (!unify(result_type, branch_type)) {
                    report_type_mismatch(arm->location,
                                         getTypeName(result_type->kind),
                                         getTypeName(branch_type->kind));
                    encountered_error = true;
                }
            }

            if (!encountered_error && subject_is_enum && has_enum_arm && !node->matchExpr.hasWildcard &&
                seen_variants && enum_ext) {
                int missing_count = 0;
                size_t buffer_len = 0;
                for (int i = 0; i < variant_total; i++) {
                    if (!seen_variants[i]) {
                        missing_count++;
                        Variant* variant = &enum_ext->extended.enum_.variants[i];
                        if (variant && variant->name && variant->name->chars) {
                            buffer_len += strlen(variant->name->chars) + 2;
                        }
                    }
                }
                if (missing_count > 0) {
                    char* buffer = NULL;
                    if (buffer_len > 0) {
                        buffer = malloc(buffer_len + 1);
                    }
                    if (buffer) {
                        buffer[0] = '\0';
                        bool first = true;
                        for (int i = 0; i < variant_total; i++) {
                            if (!seen_variants[i]) {
                                Variant* variant = &enum_ext->extended.enum_.variants[i];
                                if (variant && variant->name && variant->name->chars) {
                                    if (!first) {
                                        strcat(buffer, ", ");
                                    }
                                    strcat(buffer, variant->name->chars);
                                    first = false;
                                }
                            }
                        }
                    }

                    const char* canonical = get_enum_type_name(base_type);
                    report_non_exhaustive_match(node->location, canonical, buffer);
                    if (buffer) {
                        free(buffer);
                    }
                    encountered_error = true;
                }
            }

            if (seen_variants) {
                free(seen_variants);
            }
            if (literal_cases) {
                free(literal_cases);
            }

            if (encountered_error) {
                set_type_error();
            }

            node->dataType = result_type ? result_type : getPrimitiveType(TYPE_UNKNOWN);
            return node->dataType;
        }
        case NODE_IMPL_BLOCK: {
            Type* struct_type = findStructType(node->implBlock.structName);
            if (!struct_type) {
                report_undefined_type(node->location, node->implBlock.structName);
                set_type_error();
                return NULL;
            }

            TypeExtension* ext = get_type_extension(struct_type);
            if (!ext) {
                ext = calloc(1, sizeof(TypeExtension));
                if (!ext) {
                    set_type_error();
                    return NULL;
                }
                ext->extended.structure.name = create_compiler_string(node->implBlock.structName);
                if (!ext->extended.structure.name) {
                    free(ext);
                    set_type_error();
                    return NULL;
                }
                set_type_extension(struct_type, ext);
            }

            int existing_methods = ext->extended.structure.methodCount;
            if (node->implBlock.methodCount > 0) {
                Method* resized = realloc(ext->extended.structure.methods,
                                          sizeof(Method) * (existing_methods + node->implBlock.methodCount));
                if (!resized) {
                    set_type_error();
                    return NULL;
                }
                ext->extended.structure.methods = resized;

                for (int i = 0; i < node->implBlock.methodCount; i++) {
                    Type* method_type = algorithm_w(env, node->implBlock.methods[i]);
                    if (!method_type) {
                        cleanup_new_methods(resized, existing_methods, existing_methods + i);
                        ext->extended.structure.methodCount = existing_methods;
                        return NULL;
                    }

                    if (node->implBlock.methods[i]->function.isInstanceMethod) {
                        if (!method_type || method_type->kind != TYPE_FUNCTION ||
                            method_type->info.function.arity < 1) {
                            report_type_mismatch(node->implBlock.methods[i]->location,
                                                 "instance method", "invalid receiver");
                            cleanup_new_methods(resized, existing_methods, existing_methods + i);
                            ext->extended.structure.methodCount = existing_methods;
                            set_type_error();
                            return NULL;
                        }
                    }

                    ObjString* method_name = create_compiler_string(node->implBlock.methods[i]->function.name);
                    if (!method_name) {
                        cleanup_new_methods(resized, existing_methods, existing_methods + i);
                        ext->extended.structure.methodCount = existing_methods;
                        set_type_error();
                        return NULL;
                    }

                    resized[existing_methods + i].name = method_name;
                    resized[existing_methods + i].type = method_type;
                    bool is_instance = node->implBlock.methods[i]->function.isInstanceMethod;
                    resized[existing_methods + i].isInstance = is_instance;
                    resized[existing_methods + i].isStatic = !is_instance;
                }

                ext->extended.structure.methodCount = existing_methods + node->implBlock.methodCount;
            }

            node->dataType = struct_type;
            return getPrimitiveType(TYPE_VOID);
        }
        case NODE_FOR_RANGE: {
            // For range loops: for var in start..end or start..=end
            // Type-check start, end, and optional step expressions
            if (node->forRange.start) {
                Type* start_type = algorithm_w(env, node->forRange.start);
                if (!start_type) return NULL;
            }
            if (node->forRange.end) {
                Type* end_type = algorithm_w(env, node->forRange.end);
                if (!end_type) return NULL;
            }
            if (node->forRange.step) {
                Type* step_type = algorithm_w(env, node->forRange.step);
                if (!step_type) return NULL;
            }
            
            // Create a new scope for the loop and its variable
            TypeEnv* loop_env = type_env_new(env);
            
            // Add loop variable to the loop scope only
            if (node->forRange.varName) {
                TypeScheme* var_scheme = type_arena_alloc(sizeof(TypeScheme));
                var_scheme->type = getPrimitiveType(TYPE_I32);
                var_scheme->bound_vars = NULL;
                var_scheme->bound_count = 0;
                type_env_define(loop_env, node->forRange.varName, var_scheme);
            }
            
            // Type-check body in the loop environment 
            if (node->forRange.body) {
                Type* body_type = algorithm_w(loop_env, node->forRange.body);
                if (!body_type) return NULL;
            }
            
            return getPrimitiveType(TYPE_VOID);
        }
        case NODE_FOR_ITER: {
            // For iteration loops: for var in iterable
            // Type-check iterable expression
            if (node->forIter.iterable) {
                Type* iterable_type = algorithm_w(env, node->forIter.iterable);
                if (!iterable_type) return NULL;
                
                // Create a new scope for the loop and its variable
                TypeEnv* loop_env = type_env_new(env);
                
                // Add loop variable to the loop scope only
                if (node->forIter.varName) {
                    TypeScheme* var_scheme = type_arena_alloc(sizeof(TypeScheme));
                    var_scheme->type = getPrimitiveType(TYPE_I32);
                    var_scheme->bound_vars = NULL;
                    var_scheme->bound_count = 0;
                    type_env_define(loop_env, node->forIter.varName, var_scheme);
                }
                
                // Type-check body in the loop environment
                if (node->forIter.body) {
                    Type* body_type = algorithm_w(loop_env, node->forIter.body);
                    if (!body_type) return NULL;
                }
            }

            return getPrimitiveType(TYPE_VOID);
        }
        case NODE_IMPORT: {
            ModuleManager* manager = vm.register_file.module_manager;
            const char* module_name = node->import.moduleName;

            if (!manager) {
                report_compile_error(E3004_IMPORT_FAILED, node->location,
                                     "module manager is not initialized");
                set_type_error();
                return getPrimitiveType(TYPE_UNKNOWN);
            }

            if (!module_name) {
                return getPrimitiveType(TYPE_VOID);
            }

            RegisterModule* module_entry = find_module(manager, module_name);
            if (!module_entry) {
                report_compile_error(E3003_MODULE_NOT_FOUND, node->location,
                                     "module '%s' is not loaded", module_name);
                set_type_error();
                return getPrimitiveType(TYPE_UNKNOWN);
            }

            bool imported_value = false;
            if (node->import.importAll || node->import.symbolCount == 0) {
                for (uint16_t i = 0; i < module_entry->exports.export_count; i++) {
                    const char* symbol_name = module_entry->exports.exported_names[i];
                    ModuleExportKind kind = module_entry->exports.exported_kinds[i];
                    if (!symbol_name) {
                        continue;
                    }

                    Type* exported_type = NULL;
                    if (module_entry->exports.exported_types &&
                        i < module_entry->exports.export_count) {
                        exported_type = module_entry->exports.exported_types[i];
                    }

                    if (type_env_define_import_binding(env, symbol_name, symbol_name, kind, exported_type)) {
                        imported_value = true;
                    } else {
                        report_compile_error(E3004_IMPORT_FAILED, node->location,
                                             "failed to use '%s' from module '%s'",
                                             symbol_name, module_name);
                        set_type_error();
                    }
                }

                if (!imported_value) {
                    report_compile_error(E3004_IMPORT_FAILED, node->location,
                                         "module '%s' has no usable globals, functions, or types",
                                         module_name);
                    set_type_error();
                }
            } else {
                for (int i = 0; i < node->import.symbolCount; i++) {
                    ImportSymbol* symbol = &node->import.symbols[i];
                    if (!symbol->name) {
                        continue;
                    }

                    ModuleExportKind kind = MODULE_EXPORT_KIND_GLOBAL;
                    uint16_t register_index = MODULE_EXPORT_NO_REGISTER;
                    Type* exported_type = NULL;
                    if (!module_manager_resolve_export(manager, module_name, symbol->name, &kind,
                                                       &register_index, &exported_type)) {
                        report_compile_error(E3004_IMPORT_FAILED, node->location,
                                             "module '%s' does not export '%s'",
                                             module_name, symbol->name);
                        set_type_error();
                        continue;
                    }

                    const char* binding_name = symbol->alias ? symbol->alias : symbol->name;

                    if (!type_env_define_import_binding(env, binding_name, symbol->name, kind, exported_type)) {
                        report_compile_error(E3004_IMPORT_FAILED, node->location,
                                             "failed to use '%s' from module '%s'",
                                             symbol->name, module_name);
                        set_type_error();
                        continue;
                    }

                    imported_value = true;
                }
            }

            return getPrimitiveType(TYPE_VOID);
        }
        default:
            report_unsupported_operation(node->location, "type inference", "node type");
            set_type_error();
            return getPrimitiveType(TYPE_UNKNOWN);
    }
}

// ---- Typed AST Generation ----
void populate_ast_types(ASTNode* node, TypeEnv* env) {
    if (!node) return;

    // Run type inference on this node - this will recursively type-check children
    Type* inferred_type = algorithm_w(env, node);
    if (inferred_type) {
        node->dataType = inferred_type;
    }
    
    // Recursively populate types for all child nodes
    // We need to call populate_ast_types on children because algorithm_w only sets
    // the dataType on the current node, not on child nodes
    switch (node->type) {
        case NODE_PROGRAM:
            for (int i = 0; i < node->program.count; i++) {
                populate_ast_types(node->program.declarations[i], env);
            }
            break;
        case NODE_BINARY:
            populate_ast_types(node->binary.left, env);
            populate_ast_types(node->binary.right, env);
            break;
        case NODE_UNARY:
            if (node->unary.operand) {
                populate_ast_types(node->unary.operand, env);
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
        case NODE_ASSIGN:
            if (node->assign.value) {
                populate_ast_types(node->assign.value, env);
            }
            break;
        case NODE_FUNCTION: {
            if (node->function.isMethod && !node->function.isInstanceMethod &&
                node->function.paramCount > 0 && node->function.params &&
                node->function.params[0].name &&
                strcmp(node->function.params[0].name, "self") == 0) {
                node->function.isInstanceMethod = true;
            }

            if (node->function.returnType) {
                populate_ast_types(node->function.returnType, env);
            }
            if (node->function.body) {
                // Create function environment with parameters - same logic as algorithm_w
                TypeEnv* function_env = type_env_new(env);
                Type* receiver_type = NULL;
                if (node->function.isMethod && node->function.methodStructName) {
                    receiver_type = findStructType(node->function.methodStructName);
                }

                // Add parameters to the function's local environment using the same logic as algorithm_w
                Type* function_type = node->dataType;
                Type** inferred_params = NULL;
                int inferred_param_count = 0;
                if (function_type && function_type->kind == TYPE_FUNCTION) {
                    inferred_params = function_type->info.function.paramTypes;
                    inferred_param_count = function_type->info.function.arity;
                }

                for (int i = 0; i < node->function.paramCount; i++) {
                    if (!node->function.params[i].name) {
                        continue;
                    }

                    Type* param_type = NULL;
                    if (inferred_params && i < inferred_param_count) {
                        param_type = inferred_params[i];
                    }

                    if (!param_type) {
                        if (node->function.isMethod && node->function.isInstanceMethod && i == 0) {
                            param_type = receiver_type ? receiver_type : getPrimitiveType(TYPE_UNKNOWN);
                        } else if (node->function.params[i].typeAnnotation) {
                            param_type = algorithm_w(env, node->function.params[i].typeAnnotation);
                        }
                    }

                    if (!param_type) {
                        param_type = make_var_type(NULL);
                        if (!param_type) {
                            param_type = getPrimitiveType(TYPE_ANY);
                        }
                    }

                    TypeScheme* param_scheme = type_scheme_new(param_type, NULL, 0);
                    if (param_scheme) {
                        type_env_define(function_env, node->function.params[i].name, param_scheme);
                    }
                }

                populate_ast_types(node->function.body, function_env);
            }
            break;
        }
        case NODE_CALL:
            if (node->call.callee) {
                populate_ast_types(node->call.callee, env);
            }
            for (int i = 0; i < node->call.argCount; i++) {
                populate_ast_types(node->call.args[i], env);
            }
            break;
        case NODE_RETURN:
            if (node->returnStmt.value) {
                populate_ast_types(node->returnStmt.value, env);
            }
            break;
        case NODE_BLOCK:
            // Block nodes should share the same environment as their parent
            // Only loops should create isolated scopes for their variables
            for (int i = 0; i < node->block.count; i++) {
                populate_ast_types(node->block.statements[i], env);
            }
            break;
        case NODE_PRINT:
            for (int i = 0; i < node->print.count; i++) {
                populate_ast_types(node->print.values[i], env);
            }
            break;
        case NODE_ARRAY_LITERAL:
            for (int i = 0; i < node->arrayLiteral.count; i++) {
                populate_ast_types(node->arrayLiteral.elements[i], env);
            }
            break;
        case NODE_ARRAY_SLICE:
            if (node->arraySlice.array) {
                populate_ast_types(node->arraySlice.array, env);
            }
            if (node->arraySlice.start) {
                populate_ast_types(node->arraySlice.start, env);
            }
            if (node->arraySlice.end) {
                populate_ast_types(node->arraySlice.end, env);
            }
            break;
        case NODE_CAST:
            if (node->cast.expression) {
                populate_ast_types(node->cast.expression, env);
            }
            if (node->cast.targetType) {
                populate_ast_types(node->cast.targetType, env);
            }
            break;
        case NODE_IF:
            if (node->ifStmt.condition) {
                populate_ast_types(node->ifStmt.condition, env);
            }
            if (node->ifStmt.thenBranch) {
                populate_ast_types(node->ifStmt.thenBranch, env);
            }
            if (node->ifStmt.elseBranch) {
                populate_ast_types(node->ifStmt.elseBranch, env);
            }
            break;
        case NODE_WHILE:
            if (node->whileStmt.condition) {
                populate_ast_types(node->whileStmt.condition, env);
            }
            if (node->whileStmt.body) {
                populate_ast_types(node->whileStmt.body, env);
            }
            break;
        case NODE_TRY:
            if (node->tryStmt.tryBlock) {
                populate_ast_types(node->tryStmt.tryBlock, env);
            }
            if (node->tryStmt.catchBlock) {
                TypeEnv* catch_env = type_env_new(env);
                if (!catch_env) {
                    catch_env = env;
                } else if (node->tryStmt.catchVar) {
                    TypeScheme* var_scheme = type_arena_alloc(sizeof(TypeScheme));
                    if (var_scheme) {
                        var_scheme->type = getPrimitiveType(TYPE_ERROR);
                        var_scheme->bound_vars = NULL;
                        var_scheme->bound_count = 0;
                        type_env_define(catch_env, node->tryStmt.catchVar, var_scheme);
                    }
                }
                populate_ast_types(node->tryStmt.catchBlock, catch_env);
            }
            break;
        case NODE_TERNARY:
            if (node->ternary.condition) {
                populate_ast_types(node->ternary.condition, env);
            }
            if (node->ternary.trueExpr) {
                populate_ast_types(node->ternary.trueExpr, env);
            }
            if (node->ternary.falseExpr) {
                populate_ast_types(node->ternary.falseExpr, env);
            }
            break;
        case NODE_FOR_RANGE:
            if (node->forRange.start) {
                populate_ast_types(node->forRange.start, env);
            }
            if (node->forRange.end) {
                populate_ast_types(node->forRange.end, env);
            }
            if (node->forRange.step) {
                populate_ast_types(node->forRange.step, env);
            }
            if (node->forRange.body) {
                // Create a new scope for the loop and its variable - same as algorithm_w
                TypeEnv* loop_env = type_env_new(env);
                
                // Add loop variable to the loop scope only - same as algorithm_w
                if (node->forRange.varName) {
                    TypeScheme* var_scheme = type_arena_alloc(sizeof(TypeScheme));
                    var_scheme->type = getPrimitiveType(TYPE_I32);
                    var_scheme->bound_vars = NULL;
                    var_scheme->bound_count = 0;
                    type_env_define(loop_env, node->forRange.varName, var_scheme);
                }
                
                populate_ast_types(node->forRange.body, loop_env);
            }
            break;
        case NODE_FOR_ITER:
            if (node->forIter.iterable) {
                populate_ast_types(node->forIter.iterable, env);
            }
            if (node->forIter.body) {
                // Create a new scope for the loop and its variable - same as algorithm_w
                TypeEnv* loop_env = type_env_new(env);
                
                // Add loop variable to the loop scope only - same as algorithm_w
                if (node->forIter.varName) {
                    TypeScheme* var_scheme = type_arena_alloc(sizeof(TypeScheme));
                    var_scheme->type = getPrimitiveType(TYPE_I32);
                    var_scheme->bound_vars = NULL;
                    var_scheme->bound_count = 0;
                    type_env_define(loop_env, node->forIter.varName, var_scheme);
                }
                
                populate_ast_types(node->forIter.body, loop_env);
            }
            break;
        case NODE_STRUCT_DECL:
            if (node->structDecl.fieldCount > 0 && node->structDecl.fields) {
                for (int i = 0; i < node->structDecl.fieldCount; i++) {
                    if (node->structDecl.fields[i].typeAnnotation) {
                        populate_ast_types(node->structDecl.fields[i].typeAnnotation, env);
                    }
                    if (node->structDecl.fields[i].defaultValue) {
                        populate_ast_types(node->structDecl.fields[i].defaultValue, env);
                    }
                }
            }
            break;
        case NODE_ENUM_DECL:
            if (node->enumDecl.variantCount > 0 && node->enumDecl.variants) {
                for (int i = 0; i < node->enumDecl.variantCount; i++) {
                    if (node->enumDecl.variants[i].fieldCount > 0 &&
                        node->enumDecl.variants[i].fields) {
                        for (int j = 0; j < node->enumDecl.variants[i].fieldCount; j++) {
                            if (node->enumDecl.variants[i].fields[j].typeAnnotation) {
                                populate_ast_types(node->enumDecl.variants[i].fields[j].typeAnnotation, env);
                            }
                        }
                    }
                }
            }
            break;
        case NODE_STRUCT_LITERAL:
            if (node->structLiteral.fieldCount > 0 && node->structLiteral.fields) {
                for (int i = 0; i < node->structLiteral.fieldCount; i++) {
                    if (node->structLiteral.fields[i].value) {
                        populate_ast_types(node->structLiteral.fields[i].value, env);
                    }
                }
            }
            break;
        case NODE_MEMBER_ACCESS:
            if (node->member.object) {
                populate_ast_types(node->member.object, env);
            }
            break;
        case NODE_MEMBER_ASSIGN:
            if (node->memberAssign.target) {
                populate_ast_types(node->memberAssign.target, env);
            }
            if (node->memberAssign.value) {
                populate_ast_types(node->memberAssign.value, env);
            }
            break;
        case NODE_MATCH_EXPRESSION: {
            if (node->matchExpr.subject) {
                populate_ast_types(node->matchExpr.subject, env);
            }

            TypeEnv* match_env = type_env_new(env);
            if (!match_env) {
                break;
            }

            if (node->matchExpr.tempName) {
                Type* scrutinee_type = NULL;
                if (node->matchExpr.subject) {
                    scrutinee_type = node->matchExpr.subject->dataType;
                    if (!scrutinee_type) {
                        scrutinee_type = algorithm_w(env, node->matchExpr.subject);
                    }
                }

                if (scrutinee_type) {
                    TypeScheme* scrutinee_scheme = generalize(match_env, scrutinee_type);
                    if (scrutinee_scheme) {
                        type_env_define(match_env, node->matchExpr.tempName, scrutinee_scheme);
                    }
                }
            }

            if (node->matchExpr.arms) {
                for (int i = 0; i < node->matchExpr.armCount; i++) {
                    MatchArm* arm = &node->matchExpr.arms[i];

                    if (arm->valuePattern) {
                        populate_ast_types(arm->valuePattern, match_env);
                    }
                    if (arm->condition) {
                        populate_ast_types(arm->condition, match_env);
                    }
                    if (arm->payloadAccesses) {
                        for (int j = 0; j < arm->payloadCount; j++) {
                            if (arm->payloadAccesses[j]) {
                                populate_ast_types(arm->payloadAccesses[j], match_env);
                            }
                        }
                    }

                    TypeEnv* branch_env = type_env_new(match_env);
                    if (!branch_env) {
                        branch_env = match_env;
                    }

                    if (arm->payloadNames && arm->payloadAccesses) {
                        for (int j = 0; j < arm->payloadCount; j++) {
                            if (!arm->payloadNames[j] || !arm->payloadAccesses[j]) {
                                continue;
                            }

                            Type* payload_type = arm->payloadAccesses[j]->dataType;
                            if (!payload_type) {
                                payload_type = algorithm_w(match_env, arm->payloadAccesses[j]);
                            }
                            if (!payload_type) {
                                continue;
                            }

                            TypeScheme* payload_scheme = generalize(branch_env, payload_type);
                            if (payload_scheme) {
                                type_env_define(branch_env, arm->payloadNames[j], payload_scheme);
                            }
                        }
                    }

                    if (arm->body) {
                        populate_ast_types(arm->body, branch_env);
                    }
                }
            }
            break;
        }
        case NODE_IMPL_BLOCK:
            if (node->implBlock.methodCount > 0 && node->implBlock.methods) {
                for (int i = 0; i < node->implBlock.methodCount; i++) {
                    populate_ast_types(node->implBlock.methods[i], env);
                }
            }
            break;
        default:
            // For leaf nodes (literals, identifiers, etc.) no children to process
            break;
    }
}

// Forward declaration for recursive helper
static TypedASTNode* generate_typed_ast_recursive(ASTNode* ast, TypeEnv* type_env);

TypedASTNode* generate_typed_ast(ASTNode* root, TypeEnv* env) {
    if (!root || !env) return NULL;

    // Reset error tracking
    reset_type_inference_errors();
    
    // Run type inference once
    populate_ast_types(root, env);
    
    // If there were type errors, return NULL to halt compilation
    if (has_type_inference_errors()) {
        return NULL;
    }
    
    // Generate typed AST without re-running type inference
    return generate_typed_ast_recursive(root, env);
}

// Recursive helper function to create typed AST with children
static TypedASTNode* generate_typed_ast_recursive(ASTNode* ast, TypeEnv* type_env) {
    if (!ast) return NULL;

    TypedASTNode* typed = create_typed_ast_node(ast);
    if (!typed) return NULL;

    // Use the type from type inference if available
    if (ast->dataType) {
        typed->resolvedType = ast->dataType;
        typed->typeResolved = true;
    } else {
        typed->hasTypeError = true;
        typed->errorMessage = orus_strdup("Type inference failed");
    }

    // Recursively generate children based on node type
    switch (ast->type) {
        case NODE_PROGRAM:
            if (ast->program.count > 0) {
                typed->typed.program.declarations = malloc(sizeof(TypedASTNode*) * ast->program.count);
                if (!typed->typed.program.declarations) {
                    free_typed_ast_node(typed);
                    return NULL;
                }
                typed->typed.program.count = ast->program.count;
                for (int i = 0; i < ast->program.count; i++) {
                    typed->typed.program.declarations[i] = generate_typed_ast_recursive(ast->program.declarations[i], type_env);
                    if (!typed->typed.program.declarations[i]) {
                        // Cleanup on failure
                        for (int j = 0; j < i; j++) {
                            free_typed_ast_node(typed->typed.program.declarations[j]);
                        }
                        free(typed->typed.program.declarations);
                        free_typed_ast_node(typed);
                        return NULL;
                    }
                }
            }
            typed->typed.program.moduleName = ast->program.moduleName;
            typed->typed.program.hasModuleDeclaration = ast->program.hasModuleDeclaration;
            break;

        case NODE_VAR_DECL:
            if (ast->varDecl.initializer) {
                typed->typed.varDecl.initializer = generate_typed_ast_recursive(ast->varDecl.initializer, type_env);
            }
            if (ast->varDecl.typeAnnotation) {
                typed->typed.varDecl.typeAnnotation = generate_typed_ast_recursive(ast->varDecl.typeAnnotation, type_env);
            }
            break;

        case NODE_BINARY:
            // DEBUG_TYPE_INFERENCE_PRINT("NODE_BINARY: ast->binary.left=%p, ast->binary.right=%p\n", 
            //        (void*)ast->binary.left, (void*)ast->binary.right);
            typed->typed.binary.left = generate_typed_ast_recursive(ast->binary.left, type_env);
            typed->typed.binary.right = generate_typed_ast_recursive(ast->binary.right, type_env);
            // DEBUG_TYPE_INFERENCE_PRINT("NODE_BINARY: typed->typed.binary.left=%p, typed->typed.binary.right=%p\n", 
            //        (void*)typed->typed.binary.left, (void*)typed->typed.binary.right);
            break;

        case NODE_ASSIGN:
            // Copy the variable name
            if (ast->assign.name) {
                typed->typed.assign.name = orus_strdup(ast->assign.name);
            }
            if (ast->assign.value) {
                typed->typed.assign.value = generate_typed_ast_recursive(ast->assign.value, type_env);
            }
            break;

        case NODE_FUNCTION:
            if (ast->function.returnType) {
                typed->typed.function.returnType = generate_typed_ast_recursive(ast->function.returnType, type_env);
            }
            typed->typed.function.body = generate_typed_ast_recursive(ast->function.body, type_env);
            break;

        case NODE_CALL:
            typed->typed.call.callee = generate_typed_ast_recursive(ast->call.callee, type_env);
            if (ast->call.argCount > 0) {
                typed->typed.call.args = malloc(sizeof(TypedASTNode*) * ast->call.argCount);
                if (!typed->typed.call.args) {
                    free_typed_ast_node(typed);
                    return NULL;
                }
                typed->typed.call.argCount = ast->call.argCount;
                for (int i = 0; i < ast->call.argCount; i++) {
                    typed->typed.call.args[i] = generate_typed_ast_recursive(ast->call.args[i], type_env);
                    if (!typed->typed.call.args[i]) {
                        // Cleanup on failure
                        for (int j = 0; j < i; j++) {
                            free_typed_ast_node(typed->typed.call.args[j]);
                        }
                        free(typed->typed.call.args);
                        free_typed_ast_node(typed);
                        return NULL;
                    }
                }
            }
            break;

        case NODE_RETURN:
            if (ast->returnStmt.value) {
                typed->typed.returnStmt.value = generate_typed_ast_recursive(ast->returnStmt.value, type_env);
            }
            break;

        case NODE_BLOCK:
            if (ast->block.count > 0) {
                typed->typed.block.statements = malloc(sizeof(TypedASTNode*) * ast->block.count);
                if (!typed->typed.block.statements) {
                    free_typed_ast_node(typed);
                    return NULL;
                }
                typed->typed.block.count = ast->block.count;
                for (int i = 0; i < ast->block.count; i++) {
                    typed->typed.block.statements[i] = generate_typed_ast_recursive(ast->block.statements[i], type_env);
                    if (!typed->typed.block.statements[i]) {
                        // Cleanup on failure
                        for (int j = 0; j < i; j++) {
                            free_typed_ast_node(typed->typed.block.statements[j]);
                        }
                        free(typed->typed.block.statements);
                        free_typed_ast_node(typed);
                        return NULL;
                    }
                }
            }
            break;

        case NODE_PRINT:
            // Create TypedAST nodes for print arguments
            if (ast->print.count > 0) {
                typed->typed.print.values = malloc(sizeof(TypedASTNode*) * ast->print.count);
                if (!typed->typed.print.values) {
                    free_typed_ast_node(typed);
                    return NULL;
                }
                typed->typed.print.count = ast->print.count;
                for (int i = 0; i < ast->print.count; i++) {
                    typed->typed.print.values[i] = generate_typed_ast_recursive(ast->print.values[i], type_env);
                    if (!typed->typed.print.values[i]) {
                        // Cleanup on failure
                        for (int j = 0; j < i; j++) {
                            free_typed_ast_node(typed->typed.print.values[j]);
                        }
                        free(typed->typed.print.values);
                        free_typed_ast_node(typed);
                        return NULL;
                    }
                }
            }
            break;

        case NODE_ARRAY_LITERAL:
            if (ast->arrayLiteral.count > 0) {
                typed->typed.arrayLiteral.elements = malloc(sizeof(TypedASTNode*) * ast->arrayLiteral.count);
                if (!typed->typed.arrayLiteral.elements) {
                    free_typed_ast_node(typed);
                    return NULL;
                }
                typed->typed.arrayLiteral.count = ast->arrayLiteral.count;
                for (int i = 0; i < ast->arrayLiteral.count; i++) {
                    typed->typed.arrayLiteral.elements[i] = generate_typed_ast_recursive(ast->arrayLiteral.elements[i], type_env);
                    if (!typed->typed.arrayLiteral.elements[i]) {
                        for (int j = 0; j < i; j++) {
                            free_typed_ast_node(typed->typed.arrayLiteral.elements[j]);
                        }
                        free(typed->typed.arrayLiteral.elements);
                        free_typed_ast_node(typed);
                        return NULL;
                    }
                }
            }
            break;
        case NODE_INDEX_ACCESS:
            typed->typed.indexAccess.array = generate_typed_ast_recursive(ast->indexAccess.array, type_env);
            if (!typed->typed.indexAccess.array) {
                free_typed_ast_node(typed);
                return NULL;
            }
            typed->typed.indexAccess.index = generate_typed_ast_recursive(ast->indexAccess.index, type_env);
            if (!typed->typed.indexAccess.index) {
                free_typed_ast_node(typed->typed.indexAccess.array);
                free_typed_ast_node(typed);
                return NULL;
            }
            break;

        case NODE_CAST:
            // Handle cast expressions: expr as type
            if (ast->cast.expression) {
                typed->typed.cast.expression = generate_typed_ast_recursive(ast->cast.expression, type_env);
            }
            if (ast->cast.targetType) {
                typed->typed.cast.targetType = generate_typed_ast_recursive(ast->cast.targetType, type_env);
            }
            break;
        case NODE_ARRAY_ASSIGN:
            if (ast->arrayAssign.target) {
                typed->typed.arrayAssign.target = generate_typed_ast_recursive(ast->arrayAssign.target, type_env);
                if (!typed->typed.arrayAssign.target) {
                    free_typed_ast_node(typed);
                    return NULL;
                }
            }
            if (ast->arrayAssign.value) {
                typed->typed.arrayAssign.value = generate_typed_ast_recursive(ast->arrayAssign.value, type_env);
                if (!typed->typed.arrayAssign.value) {
                    free_typed_ast_node(typed->typed.arrayAssign.target);
                    free_typed_ast_node(typed);
                    return NULL;
                }
            }
            break;

        case NODE_IF:
            // Handle if statements: condition, thenBranch, elseBranch
            if (ast->ifStmt.condition) {
                typed->typed.ifStmt.condition = generate_typed_ast_recursive(ast->ifStmt.condition, type_env);
            }
            if (ast->ifStmt.thenBranch) {
                typed->typed.ifStmt.thenBranch = generate_typed_ast_recursive(ast->ifStmt.thenBranch, type_env);
            }
            if (ast->ifStmt.elseBranch) {
                typed->typed.ifStmt.elseBranch = generate_typed_ast_recursive(ast->ifStmt.elseBranch, type_env);
            }
            break;

        case NODE_WHILE:
            // Handle while statements: condition and body
            if (ast->whileStmt.condition) {
                typed->typed.whileStmt.condition = generate_typed_ast_recursive(ast->whileStmt.condition, type_env);
            }
            if (ast->whileStmt.body) {
                typed->typed.whileStmt.body = generate_typed_ast_recursive(ast->whileStmt.body, type_env);
            }
            break;

        case NODE_TRY:
            if (ast->tryStmt.tryBlock) {
                typed->typed.tryStmt.tryBlock = generate_typed_ast_recursive(ast->tryStmt.tryBlock, type_env);
            }
            if (ast->tryStmt.catchBlock) {
                typed->typed.tryStmt.catchBlock = generate_typed_ast_recursive(ast->tryStmt.catchBlock, type_env);
            }
            if (ast->tryStmt.catchVar) {
                typed->typed.tryStmt.catchVarName = orus_strdup(ast->tryStmt.catchVar);
            }
            break;

        case NODE_BREAK:
            // Break statements have no children to process
            break;

        case NODE_CONTINUE:
            // Continue statements have no children to process  
            break;

        case NODE_FOR_RANGE:
            // Handle for range loops: varName, start, end, step, body
            DEBUG_TYPE_INFERENCE_PRINT("Processing NODE_FOR_RANGE, varName='%s'", 
                   ast->forRange.varName ? ast->forRange.varName : "(null)");
            if (ast->forRange.varName) {
                typed->typed.forRange.varName = orus_strdup(ast->forRange.varName);
                DEBUG_TYPE_INFERENCE_PRINT("Copied varName='%s' to typed AST", typed->typed.forRange.varName);
            }
            if (ast->forRange.start) {
                typed->typed.forRange.start = generate_typed_ast_recursive(ast->forRange.start, type_env);
            }
            if (ast->forRange.end) {
                typed->typed.forRange.end = generate_typed_ast_recursive(ast->forRange.end, type_env);
            }
            if (ast->forRange.step) {
                typed->typed.forRange.step = generate_typed_ast_recursive(ast->forRange.step, type_env);
            }
            if (ast->forRange.body) {
                typed->typed.forRange.body = generate_typed_ast_recursive(ast->forRange.body, type_env);
            }
            typed->typed.forRange.inclusive = ast->forRange.inclusive;
            if (ast->forRange.label) {
                typed->typed.forRange.label = orus_strdup(ast->forRange.label);
            }
            break;

        case NODE_FOR_ITER:
            // Handle for iteration loops: varName, iterable, body
            if (ast->forIter.varName) {
                typed->typed.forIter.varName = orus_strdup(ast->forIter.varName);
            }
            if (ast->forIter.iterable) {
                typed->typed.forIter.iterable = generate_typed_ast_recursive(ast->forIter.iterable, type_env);
            }
            if (ast->forIter.body) {
                typed->typed.forIter.body = generate_typed_ast_recursive(ast->forIter.body, type_env);
            }
            if (ast->forIter.label) {
                typed->typed.forIter.label = orus_strdup(ast->forIter.label);
            }
            break;
        case NODE_STRUCT_DECL:
            typed->typed.structDecl.name = ast->structDecl.name;
            typed->typed.structDecl.isPublic = ast->structDecl.isPublic;
            typed->typed.structDecl.fieldCount = ast->structDecl.fieldCount;
            if (ast->structDecl.fieldCount > 0 && ast->structDecl.fields) {
                typed->typed.structDecl.fields = malloc(sizeof(TypedStructField) * ast->structDecl.fieldCount);
                if (!typed->typed.structDecl.fields) {
                    free_typed_ast_node(typed);
                    return NULL;
                }
                for (int i = 0; i < ast->structDecl.fieldCount; i++) {
                    typed->typed.structDecl.fields[i].name = ast->structDecl.fields[i].name;
                    typed->typed.structDecl.fields[i].typeAnnotation = NULL;
                    typed->typed.structDecl.fields[i].defaultValue = NULL;
                    if (ast->structDecl.fields[i].typeAnnotation) {
                        typed->typed.structDecl.fields[i].typeAnnotation =
                            generate_typed_ast_recursive(ast->structDecl.fields[i].typeAnnotation, type_env);
                        if (!typed->typed.structDecl.fields[i].typeAnnotation) {
                            for (int j = 0; j < i; j++) {
                                free_typed_ast_node(typed->typed.structDecl.fields[j].typeAnnotation);
                                free_typed_ast_node(typed->typed.structDecl.fields[j].defaultValue);
                            }
                            free(typed->typed.structDecl.fields);
                            free_typed_ast_node(typed);
                            return NULL;
                        }
                    }
                    if (ast->structDecl.fields[i].defaultValue) {
                        typed->typed.structDecl.fields[i].defaultValue =
                            generate_typed_ast_recursive(ast->structDecl.fields[i].defaultValue, type_env);
                        if (!typed->typed.structDecl.fields[i].defaultValue) {
                            for (int j = 0; j <= i; j++) {
                                free_typed_ast_node(typed->typed.structDecl.fields[j].typeAnnotation);
                                if (j < i) {
                                    free_typed_ast_node(typed->typed.structDecl.fields[j].defaultValue);
                                }
                            }
                            free(typed->typed.structDecl.fields);
                            free_typed_ast_node(typed);
                            return NULL;
                        }
                    }
                }
            }
            break;
        case NODE_ENUM_DECL:
            typed->typed.enumDecl.name = ast->enumDecl.name;
            typed->typed.enumDecl.isPublic = ast->enumDecl.isPublic;
            typed->typed.enumDecl.variantCount = ast->enumDecl.variantCount;
            if (ast->enumDecl.variantCount > 0 && ast->enumDecl.variants) {
                typed->typed.enumDecl.variants = malloc(sizeof(TypedEnumVariant) * ast->enumDecl.variantCount);
                if (!typed->typed.enumDecl.variants) {
                    free_typed_ast_node(typed);
                    return NULL;
                }
                for (int i = 0; i < ast->enumDecl.variantCount; i++) {
                    typed->typed.enumDecl.variants[i].name = ast->enumDecl.variants[i].name;
                    typed->typed.enumDecl.variants[i].fieldCount = ast->enumDecl.variants[i].fieldCount;
                    typed->typed.enumDecl.variants[i].fields = NULL;
                    if (ast->enumDecl.variants[i].fieldCount > 0 && ast->enumDecl.variants[i].fields) {
                        typed->typed.enumDecl.variants[i].fields =
                            malloc(sizeof(TypedEnumVariantField) * ast->enumDecl.variants[i].fieldCount);
                        if (!typed->typed.enumDecl.variants[i].fields) {
                            for (int j = 0; j < i; j++) {
                                if (typed->typed.enumDecl.variants[j].fields) {
                                    for (int k = 0; k < typed->typed.enumDecl.variants[j].fieldCount; k++) {
                                        free_typed_ast_node(typed->typed.enumDecl.variants[j].fields[k].typeAnnotation);
                                    }
                                    free(typed->typed.enumDecl.variants[j].fields);
                                }
                            }
                            free(typed->typed.enumDecl.variants);
                            free_typed_ast_node(typed);
                            return NULL;
                        }
                        for (int j = 0; j < ast->enumDecl.variants[i].fieldCount; j++) {
                            typed->typed.enumDecl.variants[i].fields[j].name =
                                ast->enumDecl.variants[i].fields[j].name;
                            typed->typed.enumDecl.variants[i].fields[j].typeAnnotation = NULL;
                            if (ast->enumDecl.variants[i].fields[j].typeAnnotation) {
                                typed->typed.enumDecl.variants[i].fields[j].typeAnnotation =
                                    generate_typed_ast_recursive(ast->enumDecl.variants[i].fields[j].typeAnnotation, type_env);
                                if (!typed->typed.enumDecl.variants[i].fields[j].typeAnnotation) {
                                    for (int k = 0; k <= j; k++) {
                                        if (typed->typed.enumDecl.variants[i].fields[k].typeAnnotation) {
                                            free_typed_ast_node(typed->typed.enumDecl.variants[i].fields[k].typeAnnotation);
                                        }
                                    }
                                    for (int prev = 0; prev < i; prev++) {
                                        if (typed->typed.enumDecl.variants[prev].fields) {
                                            for (int k = 0; k < typed->typed.enumDecl.variants[prev].fieldCount; k++) {
                                                free_typed_ast_node(
                                                    typed->typed.enumDecl.variants[prev].fields[k].typeAnnotation);
                                            }
                                            free(typed->typed.enumDecl.variants[prev].fields);
                                        }
                                    }
                                    free(typed->typed.enumDecl.variants[i].fields);
                                    free(typed->typed.enumDecl.variants);
                                    free_typed_ast_node(typed);
                                    return NULL;
                                }
                            }
                        }
                    }
                }
            }
            break;
        case NODE_STRUCT_LITERAL:
            typed->typed.structLiteral.structName = ast->structLiteral.structName;
            typed->typed.structLiteral.fieldCount = ast->structLiteral.fieldCount;
            typed->typed.structLiteral.fields = ast->structLiteral.fields;
            if (ast->structLiteral.fieldCount > 0 && ast->structLiteral.fields) {
                typed->typed.structLiteral.values = malloc(sizeof(TypedASTNode*) * ast->structLiteral.fieldCount);
                if (!typed->typed.structLiteral.values) {
                    free_typed_ast_node(typed);
                    return NULL;
                }
                for (int i = 0; i < ast->structLiteral.fieldCount; i++) {
                    typed->typed.structLiteral.values[i] = NULL;
                    if (ast->structLiteral.fields[i].value) {
                        typed->typed.structLiteral.values[i] =
                            generate_typed_ast_recursive(ast->structLiteral.fields[i].value, type_env);
                        if (!typed->typed.structLiteral.values[i]) {
                            for (int j = 0; j < i; j++) {
                                free_typed_ast_node(typed->typed.structLiteral.values[j]);
                            }
                            free(typed->typed.structLiteral.values);
                            typed->typed.structLiteral.values = NULL;
                            free_typed_ast_node(typed);
                            return NULL;
                        }
                    }
                }
            }
            break;
        case NODE_MEMBER_ACCESS:
            typed->typed.member.member = ast->member.member;
            typed->typed.member.isMethod = ast->member.isMethod;
            typed->typed.member.isInstanceMethod = ast->member.isInstanceMethod;
            typed->typed.member.resolvesToEnum = ast->member.resolvesToEnum;
            typed->typed.member.resolvesToEnumVariant = ast->member.resolvesToEnumVariant;
            typed->typed.member.enumVariantIndex = ast->member.enumVariantIndex;
            typed->typed.member.enumVariantArity = ast->member.enumVariantArity;
            typed->typed.member.enumTypeName = ast->member.enumTypeName;
            if (ast->member.object) {
                typed->typed.member.object = generate_typed_ast_recursive(ast->member.object, type_env);
                if (!typed->typed.member.object) {
                    free_typed_ast_node(typed);
                    return NULL;
                }
            }
            break;
        case NODE_MEMBER_ASSIGN:
            if (ast->memberAssign.target) {
                typed->typed.memberAssign.target = generate_typed_ast_recursive(ast->memberAssign.target, type_env);
                if (!typed->typed.memberAssign.target) {
                    free_typed_ast_node(typed);
                    return NULL;
                }
            }
            if (ast->memberAssign.value) {
                typed->typed.memberAssign.value = generate_typed_ast_recursive(ast->memberAssign.value, type_env);
                if (!typed->typed.memberAssign.value) {
                    free_typed_ast_node(typed->typed.memberAssign.target);
                    typed->typed.memberAssign.target = NULL;
                    free_typed_ast_node(typed);
                    return NULL;
                }
            }
            break;
        case NODE_ENUM_MATCH_TEST:
            if (ast->enumMatchTest.value) {
                typed->typed.enumMatchTest.value = generate_typed_ast_recursive(ast->enumMatchTest.value, type_env);
                if (!typed->typed.enumMatchTest.value) {
                    free_typed_ast_node(typed);
                    return NULL;
                }
            }
            typed->typed.enumMatchTest.enumTypeName = ast->enumMatchTest.enumTypeName;
            typed->typed.enumMatchTest.variantName = ast->enumMatchTest.variantName;
            typed->typed.enumMatchTest.variantIndex = ast->enumMatchTest.variantIndex;
            typed->typed.enumMatchTest.expectedPayloadCount = ast->enumMatchTest.expectedPayloadCount;
            break;
        case NODE_ENUM_PAYLOAD:
            if (ast->enumPayload.value) {
                typed->typed.enumPayload.value = generate_typed_ast_recursive(ast->enumPayload.value, type_env);
                if (!typed->typed.enumPayload.value) {
                    free_typed_ast_node(typed);
                    return NULL;
                }
            }
            typed->typed.enumPayload.enumTypeName = ast->enumPayload.enumTypeName;
            typed->typed.enumPayload.variantName = ast->enumPayload.variantName;
            typed->typed.enumPayload.variantIndex = ast->enumPayload.variantIndex;
            typed->typed.enumPayload.fieldIndex = ast->enumPayload.fieldIndex;
            break;
        case NODE_ENUM_MATCH_CHECK:
            if (ast->enumMatchCheck.value) {
                typed->typed.enumMatchCheck.value = generate_typed_ast_recursive(ast->enumMatchCheck.value, type_env);
                if (!typed->typed.enumMatchCheck.value) {
                    free_typed_ast_node(typed);
                    return NULL;
                }
            }
            typed->typed.enumMatchCheck.enumTypeName = ast->enumMatchCheck.enumTypeName;
            typed->typed.enumMatchCheck.hasWildcard = ast->enumMatchCheck.hasWildcard;
            typed->typed.enumMatchCheck.variantCount = ast->enumMatchCheck.variantCount;
            if (ast->enumMatchCheck.variantCount > 0 && ast->enumMatchCheck.variantNames) {
                const char** names = malloc(sizeof(char*) * ast->enumMatchCheck.variantCount);
                if (!names) {
                    free_typed_ast_node(typed);
                    return NULL;
                }
                for (int i = 0; i < ast->enumMatchCheck.variantCount; i++) {
                    names[i] = ast->enumMatchCheck.variantNames[i];
                }
                typed->typed.enumMatchCheck.variantNames = names;
            }
            break;
        case NODE_MATCH_EXPRESSION:
            typed->typed.matchExpr.tempName = ast->matchExpr.tempName;
            typed->typed.matchExpr.hasWildcard = ast->matchExpr.hasWildcard;
            typed->typed.matchExpr.armCount = ast->matchExpr.armCount;
            if (ast->matchExpr.subject) {
                typed->typed.matchExpr.subject = generate_typed_ast_recursive(ast->matchExpr.subject, type_env);
                if (!typed->typed.matchExpr.subject) {
                    free_typed_ast_node(typed);
                    return NULL;
                }
            }
            if (ast->matchExpr.armCount > 0 && ast->matchExpr.arms) {
                TypedMatchArm* arms = malloc(sizeof(TypedMatchArm) * ast->matchExpr.armCount);
                if (!arms) {
                    free_typed_ast_node(typed);
                    return NULL;
                }
                for (int i = 0; i < ast->matchExpr.armCount; i++) {
                    MatchArm* armAst = &ast->matchExpr.arms[i];
                    TypedMatchArm* arm = &arms[i];
                    arm->isWildcard = armAst->isWildcard;
                    arm->isEnumCase = armAst->isEnumCase;
                    arm->enumTypeName = armAst->enumTypeName;
                    arm->variantName = armAst->variantName;
                    arm->variantIndex = armAst->variantIndex;
                    arm->expectedPayloadCount = armAst->payloadCount;
                    arm->payloadNames = (const char**)armAst->payloadNames;
                    arm->payloadCount = armAst->payloadCount;
                    arm->location = armAst->location;
                    arm->valuePattern = NULL;
                    arm->body = NULL;
                    arm->condition = NULL;
                    arm->payloadAccesses = NULL;
                    if (armAst->valuePattern) {
                        arm->valuePattern = generate_typed_ast_recursive(armAst->valuePattern, type_env);
                        if (!arm->valuePattern) {
                            for (int j = 0; j < i; j++) {
                                free_typed_ast_node(arms[j].valuePattern);
                                free_typed_ast_node(arms[j].body);
                                free_typed_ast_node(arms[j].condition);
                                if (arms[j].payloadAccesses) {
                                    for (int k = 0; k < arms[j].payloadCount; k++) {
                                        free_typed_ast_node(arms[j].payloadAccesses[k]);
                                    }
                                    free(arms[j].payloadAccesses);
                                }
                            }
                            free(arms);
                            free_typed_ast_node(typed);
                            return NULL;
                        }
                    }
                    if (armAst->body) {
                        arm->body = generate_typed_ast_recursive(armAst->body, type_env);
                        if (!arm->body) {
                            free_typed_ast_node(arm->valuePattern);
                            for (int j = 0; j < i; j++) {
                                free_typed_ast_node(arms[j].valuePattern);
                                free_typed_ast_node(arms[j].body);
                                free_typed_ast_node(arms[j].condition);
                                if (arms[j].payloadAccesses) {
                                    for (int k = 0; k < arms[j].payloadCount; k++) {
                                        free_typed_ast_node(arms[j].payloadAccesses[k]);
                                    }
                                    free(arms[j].payloadAccesses);
                                }
                            }
                            free(arms);
                            free_typed_ast_node(typed);
                            return NULL;
                        }
                    }
                    if (armAst->condition) {
                        arm->condition = generate_typed_ast_recursive(armAst->condition, type_env);
                        if (!arm->condition) {
                            free_typed_ast_node(arm->valuePattern);
                            free_typed_ast_node(arm->body);
                            for (int j = 0; j < i; j++) {
                                free_typed_ast_node(arms[j].valuePattern);
                                free_typed_ast_node(arms[j].body);
                                free_typed_ast_node(arms[j].condition);
                                if (arms[j].payloadAccesses) {
                                    for (int k = 0; k < arms[j].payloadCount; k++) {
                                        free_typed_ast_node(arms[j].payloadAccesses[k]);
                                    }
                                    free(arms[j].payloadAccesses);
                                }
                            }
                            free(arms);
                            free_typed_ast_node(typed);
                            return NULL;
                        }
                    }
                    if (armAst->payloadCount > 0 && armAst->payloadAccesses) {
                        arm->payloadAccesses = malloc(sizeof(TypedASTNode*) * armAst->payloadCount);
                        if (!arm->payloadAccesses) {
                            free_typed_ast_node(arm->valuePattern);
                            free_typed_ast_node(arm->body);
                            free_typed_ast_node(arm->condition);
                            for (int j = 0; j < i; j++) {
                                free_typed_ast_node(arms[j].valuePattern);
                                free_typed_ast_node(arms[j].body);
                                free_typed_ast_node(arms[j].condition);
                                if (arms[j].payloadAccesses) {
                                    for (int k = 0; k < arms[j].payloadCount; k++) {
                                        free_typed_ast_node(arms[j].payloadAccesses[k]);
                                    }
                                    free(arms[j].payloadAccesses);
                                }
                            }
                            free(arms);
                            free_typed_ast_node(typed);
                            return NULL;
                        }
                        for (int j = 0; j < armAst->payloadCount; j++) {
                            if (armAst->payloadAccesses[j]) {
                                arm->payloadAccesses[j] =
                                    generate_typed_ast_recursive(armAst->payloadAccesses[j], type_env);
                                if (!arm->payloadAccesses[j]) {
                                    for (int k = 0; k < j; k++) {
                                        free_typed_ast_node(arm->payloadAccesses[k]);
                                    }
                                    free(arm->payloadAccesses);
                                    free_typed_ast_node(arm->valuePattern);
                                    free_typed_ast_node(arm->body);
                                    free_typed_ast_node(arm->condition);
                                    for (int m = 0; m < i; m++) {
                                        free_typed_ast_node(arms[m].valuePattern);
                                        free_typed_ast_node(arms[m].body);
                                        free_typed_ast_node(arms[m].condition);
                                        if (arms[m].payloadAccesses) {
                                            for (int n = 0; n < arms[m].payloadCount; n++) {
                                                free_typed_ast_node(arms[m].payloadAccesses[n]);
                                            }
                                            free(arms[m].payloadAccesses);
                                        }
                                    }
                                    free(arms);
                                    free_typed_ast_node(typed);
                                    return NULL;
                                }
                            } else {
                                arm->payloadAccesses[j] = NULL;
                            }
                        }
                    }
                }
                typed->typed.matchExpr.arms = arms;
            }
            break;
        case NODE_IMPL_BLOCK:
            typed->typed.implBlock.structName = ast->implBlock.structName;
            typed->typed.implBlock.isPublic = ast->implBlock.isPublic;
            typed->typed.implBlock.methodCount = ast->implBlock.methodCount;
            if (ast->implBlock.methodCount > 0 && ast->implBlock.methods) {
                typed->typed.implBlock.methods = malloc(sizeof(TypedASTNode*) * ast->implBlock.methodCount);
                if (!typed->typed.implBlock.methods) {
                    free_typed_ast_node(typed);
                    return NULL;
                }
                for (int i = 0; i < ast->implBlock.methodCount; i++) {
                    typed->typed.implBlock.methods[i] =
                        generate_typed_ast_recursive(ast->implBlock.methods[i], type_env);
                    if (!typed->typed.implBlock.methods[i]) {
                        for (int j = 0; j < i; j++) {
                            free_typed_ast_node(typed->typed.implBlock.methods[j]);
                        }
                        free(typed->typed.implBlock.methods);
                        free_typed_ast_node(typed);
                        return NULL;
                    }
                }
            }
            break;

        default:
            // For other node types that don't have children, we're done
            break;
    }

    return typed;
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
        case NODE_TIME_STAMP:
            // time_stamp() returns f64 (seconds as double)  
            return getPrimitiveType(TYPE_F64);
        case NODE_IDENTIFIER:
            return getPrimitiveType(TYPE_ANY);
        case NODE_BINARY:
            return getPrimitiveType(TYPE_I32);
        default:
            return getPrimitiveType(TYPE_UNKNOWN);
    }
}