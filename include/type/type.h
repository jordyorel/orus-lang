#ifndef ORUS_TYPE_H
#define ORUS_TYPE_H

#include <stdbool.h>

// Forward declarations - the actual definitions are in vm.h
typedef struct Obj Obj;
typedef struct ObjString ObjString;
typedef struct Type Type;
typedef struct TypeEnv TypeEnv;

// Use existing TypeKind from vm.h - just add missing types
// (The TypeKind enum is already defined in vm.h)

typedef struct FieldInfo {
    ObjString* name;
    struct Type* type;
} FieldInfo;

// Extended type system structures (complement the existing Type in vm.h)
typedef struct Variant {
    ObjString* name;
    struct Type** field_types;
    int field_count;
} Variant;

typedef struct Method {
    ObjString* name;
    struct Type* type;
} Method;

// Extended type information (can be attached to existing Type struct)
typedef struct TypeExtension {
    bool is_mutable;
    bool is_nullable;
    
    // Extended union data for new type kinds
    union {
        struct {
            ObjString* name;
            FieldInfo* fields;
            int fieldCount;
            Method* methods;
            int methodCount;
            ObjString** genericParams;
            int genericCount;
        } structure;
        struct {
            ObjString* name;
            Variant* variants;
            int variant_count;
        } enum_;
        struct {
            ObjString* name;
            struct Type* constraint;
            int id;
        } generic;
    } extended;
} TypeExtension;

Type* createPrimitiveType(TypeKind kind);
Type* createArrayType(Type* elementType);
Type* createSizedArrayType(Type* elementType, int length);
Type* createFunctionType(Type* returnType, Type** paramTypes, int paramCount);
Type* createStructType(ObjString* name, FieldInfo* fields, int fieldCount,
                       ObjString** generics, int genericCount);
Type* createGenericType(ObjString* name);
Type* findStructType(const char* name);
void freeType(Type* type);
bool typesEqual(Type* a, Type* b);
const char* getTypeName(TypeKind kind);
void initTypeSystem(void);
void freeTypeSystem(void);  // Add this
Type* getPrimitiveType(TypeKind kind);
void markTypeRoots();
Type* substituteGenerics(Type* type, ObjString** names, Type** subs, int count);
Type* instantiateStructType(Type* base, Type** args, int argCount);

extern Type* primitiveTypes[];

typedef enum {
    CONSTRAINT_NONE,
    CONSTRAINT_NUMERIC,
    CONSTRAINT_COMPARABLE
} GenericConstraint;

// Extended Type System Operations (work with existing Type from vm.h)
bool type_equals_extended(Type* a, Type* b);
bool type_assignable_to_extended(Type* from, Type* to);
Type* type_union_extended(Type* a, Type* b);
Type* type_intersection_extended(Type* a, Type* b);

// Extended type constructors (complement existing ones)
Type* create_generic_type(const char* name, Type* constraint);
TypeExtension* get_type_extension(Type* type);
void set_type_extension(Type* type, TypeExtension* ext);

// Context-based type system state (replaces global state)
typedef struct TypeContext {
    TypeArena* arena;
    HashMap* primitive_cache;
    bool initialized;
} TypeContext;

// Context lifecycle management
TypeContext* type_context_create(void);
void type_context_destroy(TypeContext* ctx);
void type_context_init(TypeContext* ctx);

// Context-based API
Type* getPrimitive_ctx(TypeContext* ctx, TypeKind k);
Type* createGeneric_ctx(TypeContext* ctx, const char* name, int paramCount);
Type* createArrayType_ctx(TypeContext* ctx, Type* elementType);
Type* createFunctionType_ctx(TypeContext* ctx, Type* returnType, Type** paramTypes, int paramCount);
Type* createPrimitiveType_ctx(TypeContext* ctx, TypeKind kind);
Type* infer_literal_type_extended_ctx(TypeContext* ctx, Value* value);
void init_type_representation_ctx(TypeContext* ctx);

// Forward declarations for Type Inference Engine
typedef struct HashMap HashMap;
typedef struct Vec Vec;
typedef struct ASTNode ASTNode;

typedef struct {
    struct Type* left;
    struct Type* right;
} Constraint;

typedef struct {
    int next_type_var;
    HashMap* substitutions;
    Vec* constraints;
    HashMap* env;
} TypeInferer;

// Advanced Type Inference Functions (Algorithm W)
void init_type_inference(void);
void cleanup_type_inference(void);
Type* algorithm_w(TypeEnv* env, ASTNode* node);

// Typed AST Generation
typedef struct TypedASTNode TypedASTNode;  // Forward declaration
TypedASTNode* generate_typed_ast(ASTNode* root, TypeEnv* env);
void populate_ast_types(ASTNode* node, TypeEnv* env);

// Error tracking functions
bool has_type_inference_errors(void);
void reset_type_inference_errors(void);
TypeEnv* type_env_new(TypeEnv* parent);
Type* make_var_type(TypeEnv* env);
Type* fresh_type(Type* t, HashMap* mapping);
Type* prune(Type* t);
bool occurs_in_type(TypeVar* var, Type* type);
bool unify(Type* a, Type* b);

// HashMap functions
HashMap* hashmap_new(void);
void hashmap_free(HashMap* map);
void* hashmap_get(HashMap* map, const char* key);
void hashmap_set(HashMap* map, const char* key, void* value);

// Hindley-Milner Type Inference Functions
TypeInferer* type_inferer_new(void);
void type_inferer_free(TypeInferer* inferer);
Type* infer_type(TypeInferer* inferer, ASTNode* expr);
bool solve_constraints(TypeInferer* inferer);
Type* fresh_type_var(TypeInferer* inferer);
void add_constraint(TypeInferer* inferer, Type* left, Type* right);
void add_substitution(TypeInferer* inferer, int var_id, Type* type);
Type* apply_substitutions(TypeInferer* inferer, Type* type);
bool occurs_check(Type* var, Type* type);
Type* instantiate(Type* type, TypeInferer* inferer);

// Helper types for inference (work with existing system)
Type* get_numeric_type(void);
Type* get_comparable_type(void);
Type* infer_literal_type_extended(Value* value);

// Core type system functions (moved from vm.c)
void init_extended_type_system(void);
Type* get_primitive_type_cached(TypeKind kind);

// Bridge functions between ValueType and TypeKind
TypeKind value_type_to_type_kind(ValueType value_type);
ValueType type_kind_to_value_type(TypeKind type_kind);

#endif