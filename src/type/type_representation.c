/*
 * File: src/type/type_representation.c
 * Advanced type representation with traits, generics, and dynamic extension
 * - Caching via arena for primitive types
 * - Trait bounds and interface support
 * - Named generic types and specialization
 */

#include "vm/vm.h"
#include "type/type.h"
#include "runtime/memory.h"
#include "public/common.h"
#include <string.h>
#include <stdlib.h>

// Use TypeArena from vm.h
static TypeArena* type_arena = NULL;  // Keep for backward compatibility

// Primitive type cache for performance
static HashMap* primitive_cache = NULL;  // Keep for backward compatibility
static bool type_system_initialized = false;  // Keep for backward compatibility
static HashMap* struct_type_registry = NULL;
static HashMap* enum_type_registry = NULL;

// Forward declarations for context functions
static void* arena_alloc_ctx(TypeContext* ctx, size_t size);
static void* hashmap_get_int(HashMap* map, int key);
static void hashmap_set_int(HashMap* map, int key, void* value);

// ---- Context lifecycle management ----
TypeContext* type_context_create(void) {
    TypeContext* ctx = malloc(sizeof(TypeContext));
    if (!ctx) return NULL;
    
    ctx->arena = NULL;
    ctx->primitive_cache = NULL;
    ctx->initialized = false;
    
    type_context_init(ctx);
    return ctx;
}

void type_context_destroy(TypeContext* ctx) {
    if (!ctx) return;
    
    // Free primitive cache
    if (ctx->primitive_cache) {
        hashmap_free(ctx->primitive_cache);
    }
    
    // Free arena chain
    while (ctx->arena) {
        TypeArena* next = ctx->arena->next;
        free(ctx->arena->memory);
        free(ctx->arena);
        ctx->arena = next;
    }
    
    free(ctx);
}

void type_context_init(TypeContext* ctx) {
    if (!ctx || ctx->initialized) return;
    
    ctx->primitive_cache = hashmap_new();
    if (!ctx->primitive_cache) return;
    
    // Initialize primitive types
    for (TypeKind k = TYPE_UNKNOWN; k <= TYPE_ANY; k++) {
        if (k == TYPE_STRUCT || k == TYPE_ENUM) {
            continue;
        }
        Type* t = arena_alloc_ctx(ctx, sizeof(Type));
        if (!t) continue;

        memset(t, 0, sizeof(Type));
        t->kind = k;
        
        // Initialize union fields based on type
        switch (k) {
        case TYPE_ARRAY:
            t->info.array.elementType = NULL;
            break;
        case TYPE_FUNCTION:
            t->info.function.arity = 0;
            t->info.function.paramTypes = NULL;
            t->info.function.returnType = NULL;
            break;
        default:
            break;
        }
        
        hashmap_set_int(ctx->primitive_cache, k, t);
    }
    
    ctx->initialized = true;
}

// ---- Context-based arena allocation ----
static void* arena_alloc_ctx(TypeContext* ctx, size_t size) {
    if (!ctx) return NULL;
    
    // Align to ARENA_ALIGNMENT-byte boundary for optimal performance
    size = (size + ARENA_ALIGNMENT - 1) & ~(ARENA_ALIGNMENT - 1);
    
    if (!ctx->arena || ctx->arena->used + size > ctx->arena->size) {
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
        a->next = ctx->arena;
        ctx->arena = a;
    }
    
    void* p = ctx->arena->memory + ctx->arena->used;
    ctx->arena->used += size;
    return p;
}

// ---- Arena allocation (backward compatibility) ----
static void* arena_alloc(size_t size) {
    // Align to ARENA_ALIGNMENT-byte boundary for optimal performance
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

// ---- HashMap for primitive cache ----
typedef struct HashMapEntry {
    int key;
    void* value;
    struct HashMapEntry* next;
} HashMapEntry;

typedef struct HashMap {
    HashMapEntry** buckets;
    size_t capacity;
    size_t count;
} HashMap;

HashMap* hashmap_new(void) {
    HashMap* map = malloc(sizeof(HashMap));
    if (!map) return NULL;
    
    map->capacity = HASHMAP_INITIAL_CAPACITY;
    map->count = 0;
    map->buckets = calloc(map->capacity, sizeof(HashMapEntry*));
    
    if (!map->buckets) {
        free(map);
        return NULL;
    }
    
    return map;
}

void hashmap_free(HashMap* map) {
    if (!map) return;
    
    for (size_t i = 0; i < map->capacity; i++) {
        HashMapEntry* entry = map->buckets[i];
        while (entry) {
            HashMapEntry* next = entry->next;
            free(entry);
            entry = next;
        }
    }
    
    free(map->buckets);
    free(map);
}

static void* hashmap_get_int(HashMap* map, int key) {
    if (!map) return NULL;
    
    size_t index = (size_t)key % map->capacity;
    HashMapEntry* entry = map->buckets[index];
    
    while (entry) {
        if (entry->key == key) {
            return entry->value;
        }
        entry = entry->next;
    }
    
    return NULL;
}

static void hashmap_set_int(HashMap* map, int key, void* value) {
    if (!map) return;
    
    size_t index = (size_t)key % map->capacity;
    HashMapEntry* entry = map->buckets[index];
    
    // Check if key already exists
    while (entry) {
        if (entry->key == key) {
            entry->value = value;
            return;
        }
        entry = entry->next;
    }
    
    // Create new entry
    entry = malloc(sizeof(HashMapEntry));
    if (!entry) return;
    
    entry->key = key;
    entry->value = value;
    entry->next = map->buckets[index];
    map->buckets[index] = entry;
    map->count++;
}

// String hashmap implementation for type inference
static size_t hash_string(const char* str) {
    size_t hash = DJB2_INITIAL_HASH;
    int c;
    while ((c = *str++)) {
        hash = ((hash << DJB2_SHIFT) + hash) + c;
    }
    return hash;
}

void* hashmap_get(HashMap* map, const char* key) {
    if (!map || !key) return NULL;
    
    size_t index = hash_string(key) % map->capacity;
    HashMapEntry* entry = map->buckets[index];
    
    while (entry) {
        // For string keys, we need a different comparison
        // This is a simplified version - in practice you'd need separate string/int hashmaps
        if (entry->key == (int)hash_string(key)) {
            return entry->value;
        }
        entry = entry->next;
    }
    
    return NULL;
}

void hashmap_set(HashMap* map, const char* key, void* value) {
    if (!map || !key) return;

    int hash_key = (int)hash_string(key);
    hashmap_set_int(map, hash_key, value);
}

static TypeExtension* allocate_type_extension(void) {
    TypeExtension* ext = calloc(1, sizeof(TypeExtension));
    if (!ext) {
        return NULL;
    }
    ext->is_mutable = false;
    ext->is_nullable = false;
    return ext;
}

// ---- Context-based type system initialization ----
void init_type_representation_ctx(TypeContext* ctx) {
    type_context_init(ctx);
}

// ---- Type system initialization (backward compatibility) ----
void init_type_representation(void) {
    if (type_system_initialized) return;
    
    primitive_cache = hashmap_new();
    if (!primitive_cache) return;
    
    // Initialize primitive types
    for (TypeKind k = TYPE_UNKNOWN; k <= TYPE_ANY; k++) {
        if (k == TYPE_STRUCT || k == TYPE_ENUM) {
            continue;
        }
        Type* t = arena_alloc(sizeof(Type));
        if (!t) continue;

        memset(t, 0, sizeof(Type));
        t->kind = k;
        
        // Initialize union fields based on type
        switch (k) {
        case TYPE_ARRAY:
            t->info.array.elementType = NULL;
            break;
        case TYPE_FUNCTION:
            t->info.function.arity = 0;
            t->info.function.paramTypes = NULL;
            t->info.function.returnType = NULL;
            break;
        default:
            break;
        }
        
        hashmap_set_int(primitive_cache, k, t);
    }
    
    type_system_initialized = true;
}

// ---- Context-based type constructors ----
Type* getPrimitive_ctx(TypeContext* ctx, TypeKind k) {
    if (!ctx) return NULL;
    if (!ctx->initialized) {
        init_type_representation_ctx(ctx);
    }
    return hashmap_get_int(ctx->primitive_cache, k);
}

// ---- Type constructors (backward compatibility) ----
Type* getPrimitive(TypeKind k) {
    if (!type_system_initialized) {
        init_type_representation();
    }
    return hashmap_get_int(primitive_cache, k);
}

Type* createGeneric_ctx(TypeContext* ctx, const char* name, int paramCount) {
    if (!ctx || !name || paramCount < 0) return NULL;
    
    Type* t = arena_alloc_ctx(ctx, sizeof(Type));
    if (!t) return NULL;
    
    t->kind = TYPE_GENERIC;
    t->info.generic.name = arena_alloc_ctx(ctx, strlen(name) + 1);
    if (!t->info.generic.name) return NULL;
    strcpy(t->info.generic.name, name);
    
    t->info.generic.paramCount = paramCount;
    t->info.generic.params = NULL;
    
    if (paramCount > 0) {
        t->info.generic.params = arena_alloc_ctx(ctx, sizeof(Type*) * paramCount);
        if (!t->info.generic.params) return NULL;
        memset(t->info.generic.params, 0, sizeof(Type*) * paramCount);
    }
    
    return t;
}

Type* createGeneric(const char* name, int paramCount) {
    if (!name || paramCount < 0) return NULL;
    
    Type* t = arena_alloc(sizeof(Type));
    if (!t) return NULL;
    
    t->kind = TYPE_GENERIC;
    t->info.generic.name = arena_alloc(strlen(name) + 1);
    if (!t->info.generic.name) return NULL;
    strcpy(t->info.generic.name, name);
    
    t->info.generic.paramCount = paramCount;
    t->info.generic.params = NULL;
    
    if (paramCount > 0) {
        t->info.generic.params = arena_alloc(sizeof(Type*) * paramCount);
        if (!t->info.generic.params) return NULL;
        memset(t->info.generic.params, 0, sizeof(Type*) * paramCount);
    }
    
    return t;
}

Type* instantiateGeneric(Type* templ, Type** args) {
    if (!templ || templ->kind != TYPE_GENERIC) return templ;
    if (templ->info.generic.paramCount <= 0) return templ;
    if (!args) return templ;

    Type* inst = arena_alloc(sizeof(Type));
    if (!inst) return templ;

    memset(inst, 0, sizeof(Type));
    inst->kind = TYPE_INSTANCE;
    inst->info.instance.base = templ;
    inst->info.instance.args = arena_alloc(sizeof(Type*) * templ->info.generic.paramCount);
    if (!inst->info.instance.args) return templ;

    inst->info.instance.argCount = templ->info.generic.paramCount;
    for (int i = 0; i < templ->info.generic.paramCount; i++) {
        inst->info.instance.args[i] = args[i];
    }

    return inst;
}

// ---- Type equality ----
static bool array_length_matches(Type* a, Type* b) {
    TypeExtension* aext = get_type_extension(a);
    TypeExtension* bext = get_type_extension(b);

    bool a_has_length = aext && aext->extended.array.has_length;
    bool b_has_length = bext && bext->extended.array.has_length;

    if (a_has_length || b_has_length) {
        if (!a_has_length || !b_has_length) {
            return false;
        }
        return aext->extended.array.length == bext->extended.array.length;
    }

    return true;
}

static bool array_length_assignable(Type* from, Type* to) {
    TypeExtension* from_ext = get_type_extension(from);
    TypeExtension* to_ext = get_type_extension(to);

    bool from_has_length = from_ext && from_ext->extended.array.has_length;
    bool to_has_length = to_ext && to_ext->extended.array.has_length;

    if (!to_has_length) {
        return true;
    }

    if (!from_has_length) {
        return false;
    }

    return from_ext->extended.array.length == to_ext->extended.array.length;
}

bool equalsType(Type* a, Type* b) {
    if (a == b) return true;
    if (!a || !b || a->kind != b->kind) return false;

    switch (a->kind) {
    case TYPE_ARRAY:
        if (!array_length_matches(a, b)) {
            return false;
        }
        return equalsType(a->info.array.elementType, b->info.array.elementType);
    case TYPE_FUNCTION:
        if (a->info.function.arity != b->info.function.arity) return false;
        for (int i = 0; i < a->info.function.arity; i++) {
            if (!equalsType(a->info.function.paramTypes[i], b->info.function.paramTypes[i])) return false;
        }
        return equalsType(a->info.function.returnType, b->info.function.returnType);
    case TYPE_GENERIC:
        if (strcmp(a->info.generic.name, b->info.generic.name) != 0) return false;
        if (a->info.generic.paramCount != b->info.generic.paramCount) return false;
        for (int i = 0; i < a->info.generic.paramCount; i++) {
            if (!equalsType(a->info.generic.params[i], b->info.generic.params[i])) return false;
        }
        return true;
    case TYPE_INSTANCE:
        if (!equalsType(a->info.instance.base, b->info.instance.base)) return false;
        if (a->info.instance.argCount != b->info.instance.argCount) return false;
        for (int i = 0; i < a->info.instance.argCount; i++) {
            if (!equalsType(a->info.instance.args[i], b->info.instance.args[i])) return false;
        }
        return true;
    case TYPE_STRUCT: {
        TypeExtension* aext = get_type_extension(a);
        TypeExtension* bext = get_type_extension(b);
        if (!aext || !bext) return false;

        ObjString* aname = aext->extended.structure.name;
        ObjString* bname = bext->extended.structure.name;
        if (!aname || !bname) return false;
        if (aname != bname) {
            if (aname->length != bname->length) return false;
            if (strncmp(aname->chars, bname->chars, aname->length) != 0) return false;
        }

        if (aext->extended.structure.fieldCount != bext->extended.structure.fieldCount) return false;
        for (int i = 0; i < aext->extended.structure.fieldCount; i++) {
            FieldInfo* af = &aext->extended.structure.fields[i];
            FieldInfo* bf = &bext->extended.structure.fields[i];
            if (af->name && bf->name && af->name != bf->name) {
                if (af->name->length != bf->name->length) return false;
                if (strncmp(af->name->chars, bf->name->chars, af->name->length) != 0) return false;
            }
            if (!equalsType(af->type, bf->type)) return false;
        }
        if (aext->extended.structure.genericCount != bext->extended.structure.genericCount) return false;
        return true;
    }
    case TYPE_ENUM: {
        TypeExtension* aext = get_type_extension(a);
        TypeExtension* bext = get_type_extension(b);
        if (!aext || !bext) return false;

        ObjString* aname = aext->extended.enum_.name;
        ObjString* bname = bext->extended.enum_.name;
        if (!aname || !bname) return false;
        if (aname != bname) {
            if (aname->length != bname->length) return false;
            if (strncmp(aname->chars, bname->chars, aname->length) != 0) return false;
        }

        if (aext->extended.enum_.variant_count != bext->extended.enum_.variant_count) return false;
        for (int i = 0; i < aext->extended.enum_.variant_count; i++) {
            Variant* av = &aext->extended.enum_.variants[i];
            Variant* bv = &bext->extended.enum_.variants[i];
            if (av->name && bv->name && av->name != bv->name) {
                if (av->name->length != bv->name->length) return false;
                if (strncmp(av->name->chars, bv->name->chars, av->name->length) != 0) return false;
            }
            if (av->field_count != bv->field_count) return false;
            for (int j = 0; j < av->field_count; j++) {
                ObjString* afn = (av->field_names && j < av->field_count) ? av->field_names[j] : NULL;
                ObjString* bfn = (bv->field_names && j < bv->field_count) ? bv->field_names[j] : NULL;
                if (afn || bfn) {
                    if (!afn || !bfn) return false;
                    if (afn != bfn) {
                        if (afn->length != bfn->length) return false;
                        if (strncmp(afn->chars, bfn->chars, afn->length) != 0) return false;
                    }
                }
                if (!equalsType(av->field_types[j], bv->field_types[j])) return false;
            }
        }
        return true;
    }
    default:
        return true; // Primitive types are equal if kinds match
    }
}

// ---- Type compatibility ----
bool type_assignable_to_extended(Type* from, Type* to) {
    if (equalsType(from, to)) return true;
    if (!from || !to) return false;
    
    // ANY type accepts anything
    if (to->kind == TYPE_ANY) return true;
    
    // Numeric type promotions
    if (from->kind == TYPE_I32 && to->kind == TYPE_I64) return true;
    if (from->kind == TYPE_U32 && to->kind == TYPE_U64) return true;
    if (from->kind == TYPE_I32 && to->kind == TYPE_F64) return true;
    if (from->kind == TYPE_I64 && to->kind == TYPE_F64) return true;

    // Array covariance (for now, simplified)
    if (from->kind == TYPE_ARRAY && to->kind == TYPE_ARRAY) {
        if (!array_length_assignable(from, to)) {
            return false;
        }
        return type_assignable_to_extended(from->info.array.elementType, to->info.array.elementType);
    }

    if ((from->kind == TYPE_STRUCT && to->kind == TYPE_STRUCT) ||
        (from->kind == TYPE_ENUM && to->kind == TYPE_ENUM)) {
        return equalsType(from, to);
    }

    return false;
}

static bool match_generic_name(const char* generic_name, ObjString* candidate) {
    if (!generic_name || !candidate) return false;
    if ((size_t)candidate->length != strlen(generic_name)) return false;
    return strncmp(generic_name, candidate->chars, candidate->length) == 0;
}

static Type* substitute_generics_internal(Type* type, ObjString** names,
                                          Type** subs, int count) {
    if (!type || !names || !subs || count <= 0) {
        return type;
    }

    switch (type->kind) {
    case TYPE_GENERIC:
        for (int i = 0; i < count; i++) {
            if (match_generic_name(type->info.generic.name, names[i])) {
                return subs[i];
            }
        }
        return type;
    case TYPE_ARRAY: {
        Type* replaced = substitute_generics_internal(type->info.array.elementType,
                                                      names, subs, count);
        if (replaced == type->info.array.elementType) {
            return type;
        }
        TypeExtension* ext = get_type_extension(type);
        if (ext && ext->extended.array.has_length) {
            return createSizedArrayType(replaced, ext->extended.array.length);
        }
        return createArrayType(replaced);
    }
    case TYPE_FUNCTION: {
        int arity = type->info.function.arity;
        bool changed = false;
        Type** params = NULL;
        if (arity > 0 && type->info.function.paramTypes) {
            params = arena_alloc(sizeof(Type*) * arity);
            if (!params) return type;
            for (int i = 0; i < arity; i++) {
                params[i] = substitute_generics_internal(
                    type->info.function.paramTypes[i], names, subs, count);
                if (params[i] != type->info.function.paramTypes[i]) {
                    changed = true;
                }
            }
        }
        Type* returnType = substitute_generics_internal(
            type->info.function.returnType, names, subs, count);
        if (!changed && returnType == type->info.function.returnType) {
            return type;
        }
        return createFunctionType(returnType, params, arity);
    }
    case TYPE_INSTANCE: {
        bool changed = false;
        Type* base = substitute_generics_internal(type->info.instance.base, names,
                                                  subs, count);
        if (base != type->info.instance.base) {
            changed = true;
        }
        int argCount = type->info.instance.argCount;
        Type** args = NULL;
        if (argCount > 0 && type->info.instance.args) {
            args = arena_alloc(sizeof(Type*) * argCount);
            if (!args) return type;
            for (int i = 0; i < argCount; i++) {
                args[i] = substitute_generics_internal(type->info.instance.args[i],
                                                       names, subs, count);
                if (args[i] != type->info.instance.args[i]) {
                    changed = true;
                }
            }
        }
        if (!changed) {
            return type;
        }

        Type* inst = arena_alloc(sizeof(Type));
        if (!inst) return type;
        memset(inst, 0, sizeof(Type));
        inst->kind = TYPE_INSTANCE;
        inst->info.instance.base = base;
        inst->info.instance.argCount = argCount;
        inst->info.instance.args = args;
        return inst;
    }
    default:
        return type;
    }
}

Type* substituteGenerics(Type* type, ObjString** names, Type** subs, int count) {
    return substitute_generics_internal(type, names, subs, count);
}

Type* instantiateStructType(Type* base, Type** args, int argCount) {
    if (!base || base->kind != TYPE_STRUCT) {
        return NULL;
    }

    Type* inst = arena_alloc(sizeof(Type));
    if (!inst) return NULL;

    memset(inst, 0, sizeof(Type));
    inst->kind = TYPE_INSTANCE;
    inst->info.instance.base = base;
    inst->info.instance.argCount = argCount;
    if (argCount > 0 && args) {
        inst->info.instance.args = arena_alloc(sizeof(Type*) * argCount);
        if (!inst->info.instance.args) return NULL;
        for (int i = 0; i < argCount; i++) {
            inst->info.instance.args[i] = args[i];
        }
    } else {
        inst->info.instance.args = NULL;
    }
    return inst;
}

// ---- Type operations ----
Type* type_union_extended(Type* a, Type* b) {
    if (!a) return b;
    if (!b) return a;
    if (equalsType(a, b)) return a;
    
    // For now, return ANY type for unions
    return getPrimitive(TYPE_ANY);
}

Type* type_intersection_extended(Type* a, Type* b) {
    if (!a || !b) return NULL;
    if (equalsType(a, b)) return a;
    
    // Simple intersection rules
    if (type_assignable_to_extended(a, b)) return a;
    if (type_assignable_to_extended(b, a)) return b;
    
    return NULL; // No intersection
}

// ---- Context-based type creation functions ----
Type* createArrayType_ctx(TypeContext* ctx, Type* elementType) {
    if (!ctx || !elementType) return NULL;

    Type* array_type = arena_alloc_ctx(ctx, sizeof(Type));
    if (!array_type) return NULL;

    memset(array_type, 0, sizeof(Type));
    array_type->kind = TYPE_ARRAY;
    array_type->info.array.elementType = elementType;

    return array_type;
}

// ---- Type creation functions (backward compatibility) ----
Type* createArrayType(Type* elementType) {
    if (!elementType) return NULL;

    Type* array_type = arena_alloc(sizeof(Type));
    if (!array_type) return NULL;

    memset(array_type, 0, sizeof(Type));
    array_type->kind = TYPE_ARRAY;
    array_type->info.array.elementType = elementType;

    return array_type;
}

Type* createSizedArrayType(Type* elementType, int length) {
    Type* array_type = createArrayType(elementType);
    if (!array_type) return NULL;

    TypeExtension* ext = get_type_extension(array_type);
    if (!ext) {
        ext = allocate_type_extension();
        if (!ext) {
            return array_type;
        }
        set_type_extension(array_type, ext);
    }

    ext->extended.array.length = length;
    ext->extended.array.has_length = length >= 0;
    return array_type;
}

Type* createFunctionType_ctx(TypeContext* ctx, Type* returnType, Type** paramTypes, int paramCount) {
    if (!ctx || !returnType || (paramCount > 0 && !paramTypes)) return NULL;

    Type* func_type = arena_alloc_ctx(ctx, sizeof(Type));
    if (!func_type) return NULL;

    memset(func_type, 0, sizeof(Type));
    func_type->kind = TYPE_FUNCTION;
    func_type->info.function.returnType = returnType;
    func_type->info.function.arity = paramCount;

    if (paramCount > 0) {
        func_type->info.function.paramTypes = arena_alloc_ctx(ctx, paramCount * sizeof(Type*));
        if (!func_type->info.function.paramTypes) return NULL;

        for (int i = 0; i < paramCount; i++) {
            func_type->info.function.paramTypes[i] = paramTypes[i];
        }
    } else {
        func_type->info.function.paramTypes = NULL;
    }

    return func_type;
}

Type* createFunctionType(Type* returnType, Type** paramTypes, int paramCount) {
    if (!returnType || (paramCount > 0 && !paramTypes)) return NULL;

    Type* func_type = arena_alloc(sizeof(Type));
    if (!func_type) return NULL;

    memset(func_type, 0, sizeof(Type));
    func_type->kind = TYPE_FUNCTION;
    func_type->info.function.returnType = returnType;
    func_type->info.function.arity = paramCount;

    if (paramCount > 0) {
        func_type->info.function.paramTypes = arena_alloc(paramCount * sizeof(Type*));
        if (!func_type->info.function.paramTypes) return NULL;

        for (int i = 0; i < paramCount; i++) {
            func_type->info.function.paramTypes[i] = paramTypes[i];
        }
    } else {
        func_type->info.function.paramTypes = NULL;
    }

    return func_type;
}

static void ensure_type_registries(void) {
    if (!struct_type_registry) {
        struct_type_registry = hashmap_new();
    }
    if (!enum_type_registry) {
        enum_type_registry = hashmap_new();
    }
}

static void copy_generic_params(TypeExtension* ext, ObjString** generics,
                                int genericCount) {
    ext->extended.structure.genericParams = NULL;
    ext->extended.structure.genericCount = 0;
    if (!generics || genericCount <= 0) {
        return;
    }

    ext->extended.structure.genericParams = malloc(sizeof(ObjString*) * genericCount);
    if (!ext->extended.structure.genericParams) {
        return;
    }

    for (int i = 0; i < genericCount; i++) {
        ext->extended.structure.genericParams[i] = generics[i];
    }
    ext->extended.structure.genericCount = genericCount;
}

Type* createStructType(ObjString* name, FieldInfo* fields, int fieldCount,
                       ObjString** generics, int genericCount) {
    if (!name) return NULL;

    ensure_type_registries();
    Type* existing = findStructType(name->chars);
    if (existing) {
        return existing;
    }

    Type* type = arena_alloc(sizeof(Type));
    if (!type) return NULL;

    memset(type, 0, sizeof(Type));
    type->kind = TYPE_STRUCT;

    TypeExtension* ext = allocate_type_extension();
    if (!ext) {
        return type;
    }

    ext->extended.structure.name = name;
    ext->extended.structure.fields = fields;
    ext->extended.structure.fieldCount = fieldCount;
    ext->extended.structure.methods = NULL;
    ext->extended.structure.methodCount = 0;
    copy_generic_params(ext, generics, genericCount);

    set_type_extension(type, ext);

    if (struct_type_registry) {
        hashmap_set(struct_type_registry, name->chars, type);
    }
    return type;
}

Type* createEnumType(ObjString* name, Variant* variants, int variant_count) {
    if (!name) return NULL;

    ensure_type_registries();

    Type* type = arena_alloc(sizeof(Type));
    if (!type) return NULL;

    memset(type, 0, sizeof(Type));
    type->kind = TYPE_ENUM;

    TypeExtension* ext = allocate_type_extension();
    if (!ext) {
        return type;
    }

    ext->extended.enum_.name = name;
    ext->extended.enum_.variants = variants;
    ext->extended.enum_.variant_count = variant_count;
    set_type_extension(type, ext);

    if (enum_type_registry) {
        hashmap_set(enum_type_registry, name->chars, type);
    }
    return type;
}

Type* createGenericType(ObjString* name) {
    if (!name) return NULL;

    Type* type = arena_alloc(sizeof(Type));
    if (!type) return NULL;

    memset(type, 0, sizeof(Type));
    type->kind = TYPE_GENERIC;

    size_t len = (size_t)name->length;
    type->info.generic.name = arena_alloc(len + 1);
    if (!type->info.generic.name) return NULL;
    memcpy(type->info.generic.name, name->chars, len);
    type->info.generic.name[len] = '\0';
    type->info.generic.paramCount = 0;
    type->info.generic.params = NULL;
    return type;
}

Type* findStructType(const char* name) {
    if (!name || !struct_type_registry) return NULL;
    return hashmap_get(struct_type_registry, name);
}

Type* findEnumType(const char* name) {
    if (!name || !enum_type_registry) return NULL;
    return hashmap_get(enum_type_registry, name);
}

void freeType(Type* type) {
    if (!type) return;
    if (type->ext) {
        if (type->kind == TYPE_STRUCT &&
            type->ext->extended.structure.genericParams) {
            free(type->ext->extended.structure.genericParams);
        }
        free(type->ext);
        type->ext = NULL;
    }
}

Type* createPrimitiveType_ctx(TypeContext* ctx, TypeKind kind) {
    return getPrimitive_ctx(ctx, kind);
}

Type* createPrimitiveType(TypeKind kind) {
    return getPrimitive(kind);
}

// ---- Helper functions ----
Type* get_numeric_type(void) {
    return getPrimitive(TYPE_I32);
}

Type* get_comparable_type(void) {
    return getPrimitive(TYPE_I32);
}

Type* infer_literal_type_extended_ctx(TypeContext* ctx, Value* value) {
    if (!ctx || !value) return getPrimitive_ctx(ctx, TYPE_UNKNOWN);
    
    switch (value->type) {
    case VAL_BOOL:
        return getPrimitive_ctx(ctx, TYPE_BOOL);
    case VAL_I32:
        return getPrimitive_ctx(ctx, TYPE_I32);
    case VAL_I64:
        return getPrimitive_ctx(ctx, TYPE_I64);
    case VAL_U32:
        return getPrimitive_ctx(ctx, TYPE_U32);
    case VAL_U64:
        return getPrimitive_ctx(ctx, TYPE_U64);
    case VAL_F64:
        return getPrimitive_ctx(ctx, TYPE_F64);
    case VAL_STRING:
        return getPrimitive_ctx(ctx, TYPE_STRING);
    case VAL_ARRAY:
        return getPrimitive_ctx(ctx, TYPE_ARRAY);
    case VAL_ERROR:
        return getPrimitive_ctx(ctx, TYPE_ERROR);
    case VAL_RANGE_ITERATOR:
        return getPrimitive_ctx(ctx, TYPE_UNKNOWN);
    case VAL_ARRAY_ITERATOR:
        return getPrimitive_ctx(ctx, TYPE_UNKNOWN);
    case VAL_FUNCTION:
    case VAL_CLOSURE:
        return getPrimitive_ctx(ctx, TYPE_FUNCTION);
    default:
        return getPrimitive_ctx(ctx, TYPE_UNKNOWN);
    }
}

Type* infer_literal_type_extended(Value* value) {
    if (!value) return getPrimitive(TYPE_UNKNOWN);
    
    switch (value->type) {
    case VAL_BOOL:
        return getPrimitive(TYPE_BOOL);
    case VAL_I32:
        return getPrimitive(TYPE_I32);
    case VAL_I64:
        return getPrimitive(TYPE_I64);
    case VAL_U32:
        return getPrimitive(TYPE_U32);
    case VAL_U64:
        return getPrimitive(TYPE_U64);
    case VAL_F64:
        return getPrimitive(TYPE_F64);
    case VAL_STRING:
        return getPrimitive(TYPE_STRING);
    case VAL_ARRAY:
        return getPrimitive(TYPE_ARRAY);
    case VAL_ERROR:
        return getPrimitive(TYPE_ERROR);
    case VAL_RANGE_ITERATOR:
        return getPrimitive(TYPE_UNKNOWN);
    case VAL_ARRAY_ITERATOR:
        return getPrimitive(TYPE_UNKNOWN);
    case VAL_FUNCTION:
    case VAL_CLOSURE:
        return getPrimitive(TYPE_FUNCTION);
    default:
        return getPrimitive(TYPE_UNKNOWN);
    }
}

// ---- Type extension management ----
TypeExtension* get_type_extension(Type* type) {
    if (!type) return NULL;
    return type->ext;
}

void set_type_extension(Type* type, TypeExtension* ext) {
    if (!type) return;
    type->ext = ext;
}

Type* create_generic_type(const char* name, Type* constraint) {
    Type* t = createGeneric(name, 0);
    if (!t) return NULL;

    if (constraint) {
        TypeExtension* ext = arena_alloc(sizeof(TypeExtension));
        if (!ext) return t;
        memset(ext, 0, sizeof(TypeExtension));
        ext->extended.generic.constraint = constraint;
        set_type_extension(t, ext);
    }

    return t;
}

// ---- Bridge functions ----
TypeKind value_type_to_type_kind(ValueType value_type) {
    switch (value_type) {
    case VAL_BOOL: return TYPE_BOOL;
    case VAL_I32: return TYPE_I32;
    case VAL_I64: return TYPE_I64;
    case VAL_U32: return TYPE_U32;
    case VAL_U64: return TYPE_U64;
    case VAL_F64: return TYPE_F64;
    case VAL_STRING: return TYPE_STRING;
    case VAL_ARRAY: return TYPE_ARRAY;
    case VAL_ERROR: return TYPE_ERROR;
    case VAL_RANGE_ITERATOR: return TYPE_UNKNOWN;
    case VAL_ARRAY_ITERATOR: return TYPE_UNKNOWN;
    case VAL_FUNCTION:
    case VAL_CLOSURE: return TYPE_FUNCTION;
    default: return TYPE_UNKNOWN;
    }
}

ValueType type_kind_to_value_type(TypeKind type_kind) {
    switch (type_kind) {
    case TYPE_BOOL: return VAL_BOOL;
    case TYPE_I32: return VAL_I32;
    case TYPE_I64: return VAL_I64;
    case TYPE_U32: return VAL_U32;
    case TYPE_U64: return VAL_U64;
    case TYPE_F64: return VAL_F64;
    case TYPE_STRING: return VAL_STRING;
    case TYPE_ARRAY: return VAL_ARRAY;
    case TYPE_ERROR: return VAL_ERROR;
    case TYPE_FUNCTION: return VAL_FUNCTION;
    case TYPE_STRUCT:
    case TYPE_ENUM:
        return VAL_BOOL;
    case TYPE_ANY: return VAL_BOOL; // No direct ValueType equivalent
    case TYPE_UNKNOWN: return VAL_BOOL;
    case TYPE_VOID: return VAL_BOOL;
    default: return VAL_BOOL;
    }
}

// ---- Public API ----
void init_extended_type_system(void) {
    init_type_representation();
}

Type* get_primitive_type_cached(TypeKind kind) {
    return getPrimitive(kind);
}

void freeTypeSystem(void) {
    hashmap_free(primitive_cache);
    primitive_cache = NULL;

    if (struct_type_registry) {
        hashmap_free(struct_type_registry);
        struct_type_registry = NULL;
    }
    if (enum_type_registry) {
        hashmap_free(enum_type_registry);
        enum_type_registry = NULL;
    }

    while (type_arena) {
        TypeArena* next = type_arena->next;
        free(type_arena->memory);
        free(type_arena);
        type_arena = next;
    }
    
    type_system_initialized = false;
}

// ---- Type equality extended ----
bool type_equals_extended(Type* a, Type* b) {
    return equalsType(a, b);
}

// Phase 3/4: Type name string representation for error messages
const char* getTypeName(TypeKind kind) {
    switch (kind) {
        case TYPE_UNKNOWN: return "unknown";
        case TYPE_I32: return "i32";
        case TYPE_I64: return "i64";
        case TYPE_U32: return "u32";
        case TYPE_U64: return "u64";
        case TYPE_F64: return "f64";
        case TYPE_BOOL: return "bool";
        case TYPE_STRING: return "string";
        case TYPE_VOID: return "void";
        case TYPE_ARRAY: return "array";
        case TYPE_FUNCTION: return "function";
        case TYPE_STRUCT: return "struct";
        case TYPE_ENUM: return "enum";
        case TYPE_ERROR: return "error";
        case TYPE_ANY: return "any";
        case TYPE_VAR: return "type_var";
        case TYPE_GENERIC: return "generic";
        case TYPE_INSTANCE: return "instance";
        default: return "unknown";
    }
}