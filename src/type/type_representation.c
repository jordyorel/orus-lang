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
#include <string.h>
#include <stdlib.h>

// Use TypeArena from vm.h
static TypeArena* type_arena = NULL;

// Primitive type cache for performance
static HashMap* primitive_cache = NULL;
static bool type_system_initialized = false;

// ---- Arena allocation ----
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

// ---- Type system initialization ----
void init_type_representation(void) {
    if (type_system_initialized) return;
    
    primitive_cache = hashmap_new();
    if (!primitive_cache) return;
    
    // Initialize primitive types
    for (TypeKind k = TYPE_UNKNOWN; k <= TYPE_ANY; k++) {
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

// ---- Type constructors ----
Type* getPrimitive(TypeKind k) {
    if (!type_system_initialized) {
        init_type_representation();
    }
    return hashmap_get_int(primitive_cache, k);
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
bool equalsType(Type* a, Type* b) {
    if (a == b) return true;
    if (!a || !b || a->kind != b->kind) return false;
    
    switch (a->kind) {
    case TYPE_ARRAY:
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
        return type_assignable_to_extended(from->info.array.elementType, to->info.array.elementType);
    }
    
    return false;
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

// ---- Type creation functions ----
Type* createArrayType(Type* elementType) {
    if (!elementType) return NULL;
    
    Type* array_type = arena_alloc(sizeof(Type));
    if (!array_type) return NULL;
    
    array_type->kind = TYPE_ARRAY;
    array_type->info.array.elementType = elementType;
    
    return array_type;
}

Type* createFunctionType(Type* returnType, Type** paramTypes, int paramCount) {
    if (!returnType || (paramCount > 0 && !paramTypes)) return NULL;
    
    Type* func_type = arena_alloc(sizeof(Type));
    if (!func_type) return NULL;
    
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

Type* infer_literal_type_extended(Value* value) {
    if (!value) return getPrimitive(TYPE_UNKNOWN);
    
    switch (value->type) {
    case VAL_BOOL:
        return getPrimitive(TYPE_BOOL);
    case VAL_NIL:
        return getPrimitive(TYPE_NIL);
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
    case VAL_FUNCTION:
    case VAL_CLOSURE:
        return getPrimitive(TYPE_FUNCTION);
    default:
        return getPrimitive(TYPE_UNKNOWN);
    }
}

// ---- Type extension management ----
TypeExtension* get_type_extension(Type* type) {
    // TODO: Implement type extension retrieval
    (void)type;
    return NULL;
}

void set_type_extension(Type* type, TypeExtension* ext) {
    // TODO: Implement type extension storage
    (void)type;
    (void)ext;
}

Type* create_generic_type(const char* name, Type* constraint) {
    // TODO: Implement generic type creation with constraints
    (void)constraint;
    return createGeneric(name, 0);
}

// ---- Bridge functions ----
TypeKind value_type_to_type_kind(ValueType value_type) {
    switch (value_type) {
    case VAL_BOOL: return TYPE_BOOL;
    case VAL_NIL: return TYPE_NIL;
    case VAL_I32: return TYPE_I32;
    case VAL_I64: return TYPE_I64;
    case VAL_U32: return TYPE_U32;
    case VAL_U64: return TYPE_U64;
    case VAL_F64: return TYPE_F64;
    case VAL_STRING: return TYPE_STRING;
    case VAL_ARRAY: return TYPE_ARRAY;
    case VAL_ERROR: return TYPE_ERROR;
    case VAL_RANGE_ITERATOR: return TYPE_UNKNOWN;
    case VAL_FUNCTION:
    case VAL_CLOSURE: return TYPE_FUNCTION;
    default: return TYPE_UNKNOWN;
    }
}

ValueType type_kind_to_value_type(TypeKind type_kind) {
    switch (type_kind) {
    case TYPE_BOOL: return VAL_BOOL;
    case TYPE_NIL: return VAL_NIL;
    case TYPE_I32: return VAL_I32;
    case TYPE_I64: return VAL_I64;
    case TYPE_U32: return VAL_U32;
    case TYPE_U64: return VAL_U64;
    case TYPE_F64: return VAL_F64;
    case TYPE_STRING: return VAL_STRING;
    case TYPE_ARRAY: return VAL_ARRAY;
    case TYPE_ERROR: return VAL_ERROR;
    case TYPE_FUNCTION: return VAL_FUNCTION;
    case TYPE_ANY: return VAL_NIL; // No direct ValueType equivalent
    case TYPE_UNKNOWN: return VAL_NIL;
    case TYPE_VOID: return VAL_NIL;
    default: return VAL_NIL;
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