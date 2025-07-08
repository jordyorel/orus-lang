// Simplified type inference that works with existing vm.h Type structure
// File: src/type/type_inference.c
// This file implements a simplified type inference system for the Orus language. 
// A modern type inference system is complex and requires a deep understanding of the language semantics, but this implementation provides a basic framework for type inference using generic types, constraints, and substitutions.

#include "../../include/vm.h"
#include "../../include/type.h"
#include "../../include/ast.h"
#include "../../include/lexer.h"
#include "../../include/memory.h"
#include "../../include/common.h"
#include <stdlib.h>
#include <string.h>


// Forward declarations for internal structures
typedef struct HashMapEntry {
    char* key;
    void* value;
    struct HashMapEntry* next;
} HashMapEntry;

typedef struct HashMap {
    HashMapEntry** buckets;
    size_t capacity;
    size_t count;
} HashMap;

typedef struct Vec {
    void* data;
    size_t count;
    size_t capacity;
    size_t element_size;
} Vec;

// Hash map implementation with proper chaining
static size_t hash_string(const char* str) {
    size_t hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

static HashMap* hashmap_new(void) {
    HashMap* map = malloc(sizeof(HashMap));
    if (!map) return NULL;
    
    map->capacity = 16;
    map->count = 0;
    map->buckets = calloc(map->capacity, sizeof(HashMapEntry*));
    
    if (!map->buckets) {
        free(map);
        return NULL;
    }
    
    return map;
}

static void hashmap_free(HashMap* map) {
    if (!map) return;
    
    for (size_t i = 0; i < map->capacity; i++) {
        HashMapEntry* entry = map->buckets[i];
        while (entry) {
            HashMapEntry* next = entry->next;
            free(entry->key);
            free(entry);
            entry = next;
        }
    }
    
    free(map->buckets);
    free(map);
}

static void* hashmap_get(HashMap* map, const char* key) {
    if (!map || !key) return NULL;
    
    size_t index = hash_string(key) % map->capacity;
    HashMapEntry* entry = map->buckets[index];
    
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            return entry->value;
        }
        entry = entry->next;
    }
    
    return NULL;
}

static void hashmap_set(HashMap* map, const char* key, void* value) {
    if (!map || !key) return;
    
    size_t index = hash_string(key) % map->capacity;
    HashMapEntry* entry = map->buckets[index];
    
    // Check if key already exists
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            entry->value = value;
            return;
        }
        entry = entry->next;
    }
    
    // Create new entry
    entry = malloc(sizeof(HashMapEntry));
    if (!entry) return;
    
    entry->key = malloc(strlen(key) + 1);
    if (!entry->key) {
        free(entry);
        return;
    }
    
    strcpy(entry->key, key);
    entry->value = value;
    entry->next = map->buckets[index];
    map->buckets[index] = entry;
    map->count++;
}

// Vector implementation for constraints
static Vec* vec_new(size_t element_size) {
    Vec* vec = malloc(sizeof(Vec));
    if (!vec) return NULL;
    
    vec->capacity = 8;
    vec->count = 0;
    vec->element_size = element_size;
    vec->data = malloc(vec->capacity * element_size);
    
    if (!vec->data) {
        free(vec);
        return NULL;
    }
    
    return vec;
}

static void vec_free(Vec* vec) {
    if (!vec) return;
    free(vec->data);
    free(vec);
}

// Type Inferer implementation (simplified)
TypeInferer* type_inferer_new(void) {
    TypeInferer* inferer = malloc(sizeof(TypeInferer));
    if (!inferer) return NULL;
    
    inferer->next_type_var = 1000;
    inferer->substitutions = hashmap_new();
    inferer->constraints = vec_new(sizeof(Constraint));
    inferer->env = hashmap_new();
    
    if (!inferer->substitutions || !inferer->constraints || !inferer->env) {
        type_inferer_free(inferer);
        return NULL;
    }
    
    return inferer;
}

void type_inferer_free(TypeInferer* inferer) {
    if (!inferer) return;
    
    hashmap_free(inferer->substitutions);
    vec_free(inferer->constraints);
    hashmap_free(inferer->env);
    free(inferer);
}

// Generate fresh type variable
Type* fresh_type_var(TypeInferer* inferer) {
    if (!inferer) return NULL;
    
    // Create a generic type with unique ID
    char var_name[32];
    snprintf(var_name, sizeof(var_name), "t%d", inferer->next_type_var++);
    
    Type* var_type = create_generic_type(var_name, NULL);
    return var_type ? var_type : getPrimitiveType(TYPE_ANY);
}

// Add constraint to the constraint set
void add_constraint(TypeInferer* inferer, Type* left, Type* right) {
    if (!inferer || !left || !right) return;
    
    // Grow vector if needed
    if (inferer->constraints->count >= inferer->constraints->capacity) {
        size_t new_capacity = inferer->constraints->capacity * 2;
        void* new_data = realloc(inferer->constraints->data, 
                                new_capacity * inferer->constraints->element_size);
        if (!new_data) return;
        
        inferer->constraints->data = new_data;
        inferer->constraints->capacity = new_capacity;
    }
    
    // Add constraint to vector
    Constraint* constraints = (Constraint*)inferer->constraints->data;
    constraints[inferer->constraints->count].left = left;
    constraints[inferer->constraints->count].right = right;
    inferer->constraints->count++;
}

// Add substitution
void add_substitution(TypeInferer* inferer, int var_id, Type* type) {
    if (!inferer || !type) return;
    
    char var_name[32];
    snprintf(var_name, sizeof(var_name), "t%d", var_id);
    
    hashmap_set(inferer->substitutions, var_name, type);
}

// Apply substitutions to a type
Type* apply_substitutions(TypeInferer* inferer, Type* type) {
    if (!inferer || !type) return type;
    
    // Check if this type is a generic variable that has a substitution
    TypeExtension* ext = get_type_extension(type);
    if (ext && type->kind == TYPE_ANY) { // Using TYPE_ANY as generic placeholder
        char var_name[32];
        snprintf(var_name, sizeof(var_name), "t%d", ext->extended.generic.id);
        
        Type* substitution = (Type*)hashmap_get(inferer->substitutions, var_name);
        if (substitution) {
            return apply_substitutions(inferer, substitution);
        }
    }
    
    return type;
}

// Occurs check - prevents infinite types
bool occurs_check(Type* var, Type* type) {
    if (!var || !type) return false;
    
    if (type_equals_extended(var, type)) return true;
    
    // Check compound types
    switch (type->kind) {
        case TYPE_ARRAY:
            return occurs_check(var, type->info.array.elementType);
        case TYPE_FUNCTION: {
            if (occurs_check(var, type->info.function.returnType)) return true;
            for (int i = 0; i < type->info.function.arity; i++) {
                if (occurs_check(var, type->info.function.paramTypes[i])) return true;
            }
            return false;
        }
        default:
            return false;
    }
}

// Unification algorithm - core of type inference
bool unify(TypeInferer* inferer, Type* t1, Type* t2) {
    if (!inferer || !t1 || !t2) return false;
    
    // Apply existing substitutions
    t1 = apply_substitutions(inferer, t1);
    t2 = apply_substitutions(inferer, t2);
    
    // If they're already equal, we're done
    if (type_equals_extended(t1, t2)) return true;
    
    // Handle generic variables (using TYPE_ANY as placeholder)
    TypeExtension* ext1 = get_type_extension(t1);
    TypeExtension* ext2 = get_type_extension(t2);
    
    if (ext1 && t1->kind == TYPE_ANY) {
        if (occurs_check(t1, t2)) return false;
        add_substitution(inferer, ext1->extended.generic.id, t2);
        return true;
    }
    
    if (ext2 && t2->kind == TYPE_ANY) {
        if (occurs_check(t2, t1)) return false;
        add_substitution(inferer, ext2->extended.generic.id, t1);
        return true;
    }
    
    // Unify compound types
    if (t1->kind == t2->kind) {
        switch (t1->kind) {
            case TYPE_ARRAY:
                return unify(inferer, t1->info.array.elementType, t2->info.array.elementType);
            case TYPE_FUNCTION:
                if (t1->info.function.arity != t2->info.function.arity) return false;
                if (!unify(inferer, t1->info.function.returnType, t2->info.function.returnType)) return false;
                for (int i = 0; i < t1->info.function.arity; i++) {
                    if (!unify(inferer, t1->info.function.paramTypes[i], t2->info.function.paramTypes[i])) {
                        return false;
                    }
                }
                return true;
            default:
                return false;
        }
    }
    
    return false;
}

// Constraint solving - iteratively unify all constraints
bool solve_constraints(TypeInferer* inferer) {
    if (!inferer || !inferer->constraints) return true;
    
    Constraint* constraints = (Constraint*)inferer->constraints->data;
    
    for (size_t i = 0; i < inferer->constraints->count; i++) {
        if (!unify(inferer, constraints[i].left, constraints[i].right)) {
            return false;
        }
    }
    
    return true;
}

// Type instantiation - create fresh instances of generic types
Type* instantiate(Type* type, TypeInferer* inferer) {
    if (!type || !inferer) return type;
    
    // Apply any existing substitutions first
    type = apply_substitutions(inferer, type);
    
    // For generic types, create fresh variables
    TypeExtension* ext = get_type_extension(type);
    if (ext && type->kind == TYPE_ANY) {
        return fresh_type_var(inferer);
    }
    
    // For compound types, recursively instantiate components
    switch (type->kind) {
        case TYPE_ARRAY: {
            Type* elem_type = instantiate(type->info.array.elementType, inferer);
            return createArrayType(elem_type);
        }
        case TYPE_FUNCTION: {
            Type* return_type = instantiate(type->info.function.returnType, inferer);
            Type** param_types = malloc(type->info.function.arity * sizeof(Type*));
            if (!param_types) return type;
            
            for (int i = 0; i < type->info.function.arity; i++) {
                param_types[i] = instantiate(type->info.function.paramTypes[i], inferer);
            }
            
            Type* func_type = createFunctionType(return_type, param_types, type->info.function.arity);
            free(param_types);
            return func_type;
        }
        default:
            return type;
    }
}

// Simplified type inference function
Type* infer_type(TypeInferer* inferer, ASTNode* expr) {
    if (!inferer || !expr) return NULL;
    
    switch (expr->type) {
        case NODE_LITERAL:
            return infer_literal_type_extended(&expr->literal.value);
            
        case NODE_IDENTIFIER: {
            Type* type = hashmap_get(inferer->env, expr->identifier.name);
            if (!type) {
                // Create fresh type variable for unknown identifier
                type = fresh_type_var(inferer);
                hashmap_set(inferer->env, expr->identifier.name, type);
            }
            return instantiate(type, inferer);
        }
        
        case NODE_BINARY: {
            Type* left = infer_type(inferer, expr->binary.left);
            Type* right = infer_type(inferer, expr->binary.right);
            
            // Check operation type by string comparison
            if (strcmp(expr->binary.op, "+") == 0 ||
                strcmp(expr->binary.op, "-") == 0 ||
                strcmp(expr->binary.op, "*") == 0 ||
                strcmp(expr->binary.op, "/") == 0) {
                // Numeric operations
                add_constraint(inferer, left, right);
                add_constraint(inferer, left, get_numeric_type());
                return left;
            } else if (strcmp(expr->binary.op, "<") == 0 ||
                       strcmp(expr->binary.op, ">") == 0 ||
                       strcmp(expr->binary.op, "<=") == 0 ||
                       strcmp(expr->binary.op, ">=") == 0) {
                // Comparison operations
                add_constraint(inferer, left, right);
                add_constraint(inferer, left, get_comparable_type());
                return getPrimitiveType(TYPE_BOOL);
            } else if (strcmp(expr->binary.op, "==") == 0 ||
                       strcmp(expr->binary.op, "!=") == 0) {
                // Equality operations
                add_constraint(inferer, left, right);
                return getPrimitiveType(TYPE_BOOL);
            } else {
                return getPrimitiveType(TYPE_UNKNOWN);
            }
        }
        
        default:
            return getPrimitiveType(TYPE_UNKNOWN);
    }
}

// Helper functions are implemented in type_representation.c