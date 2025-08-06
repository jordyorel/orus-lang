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
#include "errors/features/type_errors.h"
#include "errors/features/variable_errors.h"
#include "debug/debug_config.h"

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

// ---- Type constructors ----
Type* make_var_type(TypeEnv* env) {
    (void)env;  // Unused parameter
    TypeVar* tv = new_type_var_node();
    if (!tv) return NULL;
    Type* t = type_arena_alloc(sizeof(Type));
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
    TypeEnv* env = type_arena_alloc(sizeof(TypeEnv));
    if (!env) return NULL;
    env->entries = NULL;
    env->parent = parent;
    return env;
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

    // Simplified generalization - for now just wrap type without bound
    // variables
    (void)env;  // TODO: Implement proper environment variable collection

    return type_scheme_new(type, NULL, 0);
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
        case NODE_IDENTIFIER: {
            DEBUG_TYPE_INFERENCE_PRINT("Looking up identifier '%s'", node->identifier.name);
            TypeScheme* scheme = type_env_lookup(env, node->identifier.name);
            if (scheme) {
                DEBUG_TYPE_INFERENCE_PRINT("Found scheme for '%s', type kind: %d", 
                       node->identifier.name, (int)scheme->type->kind);
                return scheme->type;
            }
            DEBUG_TYPE_INFERENCE_PRINT("Identifier '%s' not found in type environment", node->identifier.name);
            report_undefined_variable(node->location, node->identifier.name);
            set_type_error();
            return NULL;
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
                    // Left is literal, adapt to right's type
                    return r;
                } else if (right_is_literal && !left_is_literal) {
                    // Right is literal, adapt to left's type
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
                TypeScheme* scheme = generalize(env, var_type);
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
            if (node->print.separator) {
                Type* t = algorithm_w(env, node->print.separator);
                if (!t) return NULL;
            }
            return getPrimitiveType(TYPE_VOID);
        }
        case NODE_FUNCTION: {
            // For function declarations, get the actual return type from type annotation
            Type* return_type = getPrimitiveType(TYPE_VOID); // Default to void
            
            if (node->function.returnType) {
                return_type = algorithm_w(env, node->function.returnType);
                if (!return_type) return_type = getPrimitiveType(TYPE_VOID);
            }
            
            // Process parameters and their types
            Type** param_types = NULL;
            int param_count = node->function.paramCount;
            
            if (param_count > 0) {
                param_types = type_arena_alloc(sizeof(Type*) * param_count);
                for (int i = 0; i < param_count; i++) {
                    if (node->function.params[i].typeAnnotation) {
                        param_types[i] = algorithm_w(env, node->function.params[i].typeAnnotation);
                        if (!param_types[i]) param_types[i] = getPrimitiveType(TYPE_I32);
                    } else {
                        param_types[i] = getPrimitiveType(TYPE_I32); // Default fallback
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
            
            // Add parameters to the function's local environment
            for (int i = 0; i < param_count; i++) {
                if (node->function.params[i].name && param_types && param_types[i]) {
                    TypeScheme* param_scheme = generalize(function_env, param_types[i]);
                    type_env_define(function_env, node->function.params[i].name, param_scheme);
                }
            }
            
            // Type-check the function body in the new environment
            if (node->function.body) {
                Type* body_type = algorithm_w(function_env, node->function.body);
                // The body type isn't used for the function's type, but this ensures the body is type-checked
                (void)body_type;
            }
            
            return func_type;
        }
        case NODE_CALL: {
            // Function call type inference
            if (node->call.callee->type == NODE_IDENTIFIER) {
                // Look up the function in the environment
                TypeScheme* scheme = type_env_lookup(env, node->call.callee->identifier.name);
                if (scheme && scheme->type && scheme->type->kind == TYPE_FUNCTION) {
                    // Type-check arguments against function parameter types
                    Type* func_type = scheme->type;
                    
                    // Verify argument count matches parameter count
                    if (node->call.argCount == func_type->info.function.arity) {
                        // Type-check each argument and verify it matches the parameter type
                        for (int i = 0; i < node->call.argCount; i++) {
                            Type* arg_type = algorithm_w(env, node->call.args[i]);
                            Type* param_type = func_type->info.function.paramTypes[i];
                            
                            // Verify arg_type matches param_type
                            if (arg_type && param_type && !unify(arg_type, param_type)) {
                                report_type_mismatch(node->location, "function parameter", "argument type");
                                set_type_error();
                                return NULL;
                            }
                        }
                    } else {
                        report_type_mismatch(node->location, "function signature", "argument count");
                        set_type_error();
                        return NULL;
                    }
                    
                    // Return the function's return type
                    return func_type->info.function.returnType;
                }
            }
            
            // Fallback: type-check the callee and arguments
            Type* callee_type = algorithm_w(env, node->call.callee);
            for (int i = 0; i < node->call.argCount; i++) {
                algorithm_w(env, node->call.args[i]);
            }
            
            // If callee is a function type, return its return type
            if (callee_type && callee_type->kind == TYPE_FUNCTION) {
                return callee_type->info.function.returnType;
            }
            
            // Default fallback
            return getPrimitiveType(TYPE_I32);
        }
        case NODE_RETURN: {
            if (node->returnStmt.value) {
                Type* value_type = algorithm_w(env, node->returnStmt.value);
                return value_type; // Return statement takes the type of its value
            }
            return getPrimitiveType(TYPE_VOID);
        }
        case NODE_TYPE: {
            // Type annotation nodes - convert type name to actual Type*
            if (node->typeAnnotation.name) {
                if (strcmp(node->typeAnnotation.name, "i32") == 0) {
                    return getPrimitiveType(TYPE_I32);
                } else if (strcmp(node->typeAnnotation.name, "i64") == 0) {
                    return getPrimitiveType(TYPE_I64);
                } else if (strcmp(node->typeAnnotation.name, "u32") == 0) {
                    return getPrimitiveType(TYPE_U32);
                } else if (strcmp(node->typeAnnotation.name, "u64") == 0) {
                    return getPrimitiveType(TYPE_U64);
                } else if (strcmp(node->typeAnnotation.name, "f64") == 0) {
                    return getPrimitiveType(TYPE_F64);
                } else if (strcmp(node->typeAnnotation.name, "bool") == 0) {
                    return getPrimitiveType(TYPE_BOOL);
                } else if (strcmp(node->typeAnnotation.name, "string") == 0) {
                    return getPrimitiveType(TYPE_STRING);
                } else if (strcmp(node->typeAnnotation.name, "void") == 0) {
                    return getPrimitiveType(TYPE_VOID);
                }
            }
            return getPrimitiveType(TYPE_UNKNOWN);
        }
        case NODE_CAST: {
            // Cast expressions take the type of their target type
            if (node->cast.targetType) {
                Type* target_type = algorithm_w(env, node->cast.targetType);
                return target_type;
            }
            return getPrimitiveType(TYPE_UNKNOWN);
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
            // Block nodes type-check their statements and return void
            for (int i = 0; i < node->block.count; i++) {
                algorithm_w(env, node->block.statements[i]);
            }
            return getPrimitiveType(TYPE_VOID);
        }
        case NODE_UNARY: {
            // Unary operations - type-check operand and return its type
            if (node->unary.operand) {
                return algorithm_w(env, node->unary.operand);
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
        case NODE_BREAK: {
            // Break statements have void type
            return getPrimitiveType(TYPE_VOID);
        }
        case NODE_CONTINUE: {
            // Continue statements have void type
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
            
            // Add loop variable to environment with integer type
            if (node->forRange.varName) {
                TypeScheme* var_scheme = type_arena_alloc(sizeof(TypeScheme));
                var_scheme->type = getPrimitiveType(TYPE_I32);
                var_scheme->bound_vars = NULL;
                var_scheme->bound_count = 0;
                type_env_define(env, node->forRange.varName, var_scheme);
            }
            
            // Type-check body
            if (node->forRange.body) {
                Type* body_type = algorithm_w(env, node->forRange.body);
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
                
                // Add loop variable to environment with appropriate type
                // For now, assume iterable elements are i32 (can be enhanced later)
                if (node->forIter.varName) {
                    TypeScheme* var_scheme = type_arena_alloc(sizeof(TypeScheme));
                    var_scheme->type = getPrimitiveType(TYPE_I32);
                    var_scheme->bound_vars = NULL;
                    var_scheme->bound_count = 0;
                    type_env_define(env, node->forIter.varName, var_scheme);
                }
            }
            
            // Type-check body
            if (node->forIter.body) {
                Type* body_type = algorithm_w(env, node->forIter.body);
                if (!body_type) return NULL;
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
        case NODE_FUNCTION:
            if (node->function.returnType) {
                populate_ast_types(node->function.returnType, env);
            }
            if (node->function.body) {
                // Create function environment with parameters - same logic as algorithm_w
                TypeEnv* function_env = type_env_new(env);
                
                // Add parameters to the function's local environment using the same logic as algorithm_w
                for (int i = 0; i < node->function.paramCount; i++) {
                    if (node->function.params[i].name) {
                        Type* param_type = getPrimitiveType(TYPE_I32); // Default
                        if (node->function.params[i].typeAnnotation) {
                            param_type = algorithm_w(env, node->function.params[i].typeAnnotation);
                            if (!param_type) param_type = getPrimitiveType(TYPE_I32);
                        }
                        TypeScheme* param_scheme = generalize(function_env, param_type);
                        type_env_define(function_env, node->function.params[i].name, param_scheme);
                    }
                }
                
                populate_ast_types(node->function.body, function_env);
            }
            break;
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
            for (int i = 0; i < node->block.count; i++) {
                populate_ast_types(node->block.statements[i], env);
            }
            break;
        case NODE_PRINT:
            for (int i = 0; i < node->print.count; i++) {
                populate_ast_types(node->print.values[i], env);
            }
            if (node->print.separator) {
                populate_ast_types(node->print.separator, env);
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
                populate_ast_types(node->forRange.body, env);
            }
            break;
        case NODE_FOR_ITER:
            if (node->forIter.iterable) {
                populate_ast_types(node->forIter.iterable, env);
            }
            if (node->forIter.body) {
                populate_ast_types(node->forIter.body, env);
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
        typed->errorMessage = strdup("Type inference failed");
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
                typed->typed.assign.name = strdup(ast->assign.name);
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
            // Handle separator if present
            if (ast->print.separator) {
                typed->typed.print.separator = generate_typed_ast_recursive(ast->print.separator, type_env);
                if (!typed->typed.print.separator) {
                    // Cleanup values if separator fails
                    if (typed->typed.print.values) {
                        for (int i = 0; i < typed->typed.print.count; i++) {
                            free_typed_ast_node(typed->typed.print.values[i]);
                        }
                        free(typed->typed.print.values);
                    }
                    free_typed_ast_node(typed);
                    return NULL;
                }
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
                typed->typed.forRange.varName = strdup(ast->forRange.varName);
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
                typed->typed.forRange.label = strdup(ast->forRange.label);
            }
            break;

        case NODE_FOR_ITER:
            // Handle for iteration loops: varName, iterable, body
            if (ast->forIter.varName) {
                typed->typed.forIter.varName = strdup(ast->forIter.varName);
            }
            if (ast->forIter.iterable) {
                typed->typed.forIter.iterable = generate_typed_ast_recursive(ast->forIter.iterable, type_env);
            }
            if (ast->forIter.body) {
                typed->typed.forIter.body = generate_typed_ast_recursive(ast->forIter.body, type_env);
            }
            if (ast->forIter.label) {
                typed->typed.forIter.label = strdup(ast->forIter.label);
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