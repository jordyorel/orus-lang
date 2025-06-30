#include "../../include/vm.h"
#include "../../include/type.h"
#include "../../include/memory.h"
#include "../../include/common.h"
#include <string.h>
#include <stdlib.h>

// Cache-aligned type table for primitive types (following AGENTS.md performance guidelines)
#define TYPE_CACHE_SIZE 16
static Type* primitive_type_cache[TYPE_CACHE_SIZE] __attribute__((aligned(64)));
static bool type_system_initialized = false;

// Arena allocator for type objects (zero-cost abstraction as per AGENTS.md)
typedef struct TypeArena {
    uint8_t* memory;
    size_t size;
    size_t used;
    struct TypeArena* next;
} TypeArena;

static TypeArena* type_arena = NULL;
static const size_t ARENA_SIZE = 64 * 1024; // 64KB chunks

// High-performance arena allocation (following AGENTS.md memory principles)
static void* arena_alloc(size_t size) {
    // Align to 8-byte boundary for optimal performance
    size = (size + 7) & ~7;
    
    if (!type_arena || type_arena->used + size > type_arena->size) {
        TypeArena* new_arena = malloc(sizeof(TypeArena));
        if (!new_arena) return NULL;
        
        new_arena->size = size > ARENA_SIZE ? size : ARENA_SIZE;
        new_arena->memory = malloc(new_arena->size);
        new_arena->used = 0;
        new_arena->next = type_arena;
        type_arena = new_arena;
        
        if (!new_arena->memory) {
            free(new_arena);
            return NULL;
        }
    }
    
    void* result = type_arena->memory + type_arena->used;
    type_arena->used += size;
    return result;
}

// Initialize primitive type cache (SIMD-friendly initialization)
static void init_primitive_types(void) {
    if (type_system_initialized) return;
    
    // Initialize all primitive types to NULL first
    memset(primitive_type_cache, 0, sizeof(primitive_type_cache));
    
    // Create primitive types with optimal memory layout
    // Include all types from TYPE_UNKNOWN to TYPE_ANY
    for (int i = TYPE_UNKNOWN; i <= TYPE_ANY && i < TYPE_CACHE_SIZE; i++) {
        Type* type = arena_alloc(sizeof(Type));
        if (!type) continue;
        
        memset(type, 0, sizeof(Type));
        type->kind = (TypeKind)i;
        
        // Initialize union fields to default values
        if (i == TYPE_ARRAY) {
            type->info.array.elementType = NULL;
        } else if (i == TYPE_FUNCTION) {
            type->info.function.arity = 0;
            type->info.function.paramTypes = NULL;
            type->info.function.returnType = NULL;
        }
        
        primitive_type_cache[i] = type;
    }
    
    type_system_initialized = true;
}

// Fast primitive type retrieval (cache-optimized) - use existing createPrimitiveType pattern
Type* get_primitive_type_cached(TypeKind kind) {
    if (!type_system_initialized) {
        init_primitive_types();
    }
    
    if (kind >= 0 && kind < TYPE_CACHE_SIZE) {
        return primitive_type_cache[kind];
    }
    
    return NULL;
}

// High-performance type equality (SIMD-optimized where possible)
bool type_equals_extended(Type* a, Type* b) {
    if (a == b) return true;
    if (!a || !b) return false;
    if (a->kind != b->kind) return false;
    
    switch (a->kind) {
        case TYPE_UNKNOWN:
        case TYPE_I32:
        case TYPE_I64:
        case TYPE_U32:
        case TYPE_U64:
        case TYPE_F64:
        case TYPE_BOOL:
        case TYPE_STRING:
        case TYPE_VOID:
        case TYPE_NIL:
        case TYPE_ANY:
        case TYPE_ERROR:
            return true; // Primitive types are equal if kinds match
            
        case TYPE_ARRAY:
            return type_equals_extended(a->info.array.elementType, b->info.array.elementType);
                   
        case TYPE_FUNCTION:
            if (a->info.function.arity != b->info.function.arity) {
                return false;
            }
            
            if (!type_equals_extended(a->info.function.returnType, b->info.function.returnType)) {
                return false;
            }
            
            // Compare parameter types
            for (int i = 0; i < a->info.function.arity; i++) {
                if (!type_equals_extended(a->info.function.paramTypes[i], 
                                        b->info.function.paramTypes[i])) {
                    return false;
                }
            }
            return true;
            
        default:
            return false;
    }
}

// Type assignability with performance optimization  
bool type_assignable_to_extended(Type* from, Type* to) {
    if (type_equals_extended(from, to)) return true;
    if (!from || !to) return false;
    
    // ANY type accepts anything
    if (to->kind == TYPE_ANY) return true;
    
    // Numeric type promotions (following AGENTS.md zero-cost principles)
    if (from->kind == TYPE_I32 && to->kind == TYPE_I64) return true;
    if (from->kind == TYPE_U32 && to->kind == TYPE_U64) return true;
    if (from->kind == TYPE_I32 && to->kind == TYPE_F64) return true;
    if (from->kind == TYPE_I64 && to->kind == TYPE_F64) return true;
    
    return false;
}

// Type union operation (simplified for compatibility)
Type* type_union_extended(Type* a, Type* b) {
    if (!a) return b;
    if (!b) return a;
    if (type_equals_extended(a, b)) return a;
    
    // For now, return ANY type for unions
    return get_primitive_type_cached(TYPE_ANY);
}

// Type intersection operation (simplified for compatibility)
Type* type_intersection_extended(Type* a, Type* b) {
    if (!a || !b) return NULL;
    if (type_equals_extended(a, b)) return a;
    
    // Simple intersection rules
    if (type_assignable_to_extended(a, b)) return a;
    if (type_assignable_to_extended(b, a)) return b;
    
    return NULL; // No intersection
}

// Numeric type for constraints
Type* get_numeric_type(void) {
    return get_primitive_type_cached(TYPE_I32);
}

// Comparable type for constraints
Type* get_comparable_type(void) {
    return get_primitive_type_cached(TYPE_I32);
}

// Infer type from literal value
Type* infer_literal_type_extended(Value* value) {
    if (!value) return get_primitive_type_cached(TYPE_UNKNOWN);
    
    switch (value->type) {
        case VAL_BOOL:
            return get_primitive_type_cached(TYPE_BOOL);
        case VAL_NIL:
            return get_primitive_type_cached(TYPE_NIL);
        case VAL_I32:
            return get_primitive_type_cached(TYPE_I32);
        case VAL_I64:
            return get_primitive_type_cached(TYPE_I64);
        case VAL_U32:
            return get_primitive_type_cached(TYPE_U32);
        case VAL_U64:
            return get_primitive_type_cached(TYPE_U64);
        case VAL_F64:
            return get_primitive_type_cached(TYPE_F64);
        case VAL_STRING:
            return get_primitive_type_cached(TYPE_STRING);
        case VAL_ARRAY:
            return get_primitive_type_cached(TYPE_ARRAY);
        case VAL_ERROR:
            return get_primitive_type_cached(TYPE_ERROR);
        case VAL_RANGE_ITERATOR:
            return get_primitive_type_cached(TYPE_UNKNOWN);
    }
    
    return get_primitive_type_cached(TYPE_UNKNOWN);
}

// Type extension management (placeholder implementations)
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
    // TODO: Implement generic type creation
    (void)name;
    (void)constraint;
    return get_primitive_type_cached(TYPE_UNKNOWN);
}

// Bridge functions between ValueType and TypeKind
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
    }
    return TYPE_UNKNOWN;
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
        case TYPE_FUNCTION: return VAL_NIL; // No direct ValueType equivalent
        case TYPE_ANY: return VAL_NIL;      // No direct ValueType equivalent
        case TYPE_UNKNOWN: return VAL_NIL;
        case TYPE_VOID: return VAL_NIL;
    }
    return VAL_NIL;
}

// Initialize the extended type system (called from existing initTypeSystem in vm.c)
void init_extended_type_system(void) {
    init_primitive_types();
}

// Free the entire type system
void freeTypeSystem(void) {
    while (type_arena) {
        TypeArena* next = type_arena->next;
        free(type_arena->memory);
        free(type_arena);
        type_arena = next;
    }
    
    type_system_initialized = false;
}