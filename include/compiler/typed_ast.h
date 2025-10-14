// Orus Language Project
// ---------------------------------------------------------------------------
// File: include/compiler/typed_ast.h
// Author: Jordy Orel KONDA
// Copyright (c) 2025 Jordy Orel KONDA
// License: MIT License (see LICENSE file in the project root)
// Description: Defines typed AST nodes produced after semantic analysis and type inference.


#ifndef ORUS_TYPED_AST_H
#define ORUS_TYPED_AST_H

#include <stddef.h>

#include "compiler/ast.h"
#include "vm/vm.h"

// Forward declarations
typedef struct TypedASTNode TypedASTNode;

typedef struct {
    const char* name;
    TypedASTNode* typeAnnotation;
    TypedASTNode* defaultValue;
} TypedStructField;

typedef struct {
    const char* name;
    TypedASTNode* typeAnnotation;
} TypedEnumVariantField;

typedef struct {
    const char* name;
    TypedEnumVariantField* fields;
    int fieldCount;
} TypedEnumVariant;

typedef struct {
    bool isWildcard;
    bool isEnumCase;
    const char* enumTypeName;
    const char* variantName;
    int variantIndex;
    int expectedPayloadCount;
    const char** payloadNames;
    int payloadCount;
    TypedASTNode* valuePattern;
    TypedASTNode* body;
    TypedASTNode* condition;
    TypedASTNode** payloadAccesses;
    SrcLocation location;
} TypedMatchArm;

typedef struct TypedImportSymbol {
    const char* name;
    const char* alias;
} TypedImportSymbol;

// Generic visitor interface for walking typed AST nodes
typedef bool (*TypedASTVisitFn)(TypedASTNode* node, void* user_data);

typedef struct TypedASTVisitor {
    TypedASTVisitFn pre;
    TypedASTVisitFn post;
} TypedASTVisitor;

// Typed AST node that contains the original AST plus resolved type information
struct TypedASTNode {
    ASTNode* original;       // Original AST node from parser
    Type* resolvedType;      // Type resolved by HM type inference
    
    // Type inference metadata
    bool typeResolved;       // Whether type has been successfully resolved
    bool hasTypeError;       // Whether there was a type error
    char* errorMessage;      // Error message if type resolution failed
    
    // Compiler optimization hints
    bool isConstant;         // Whether this node represents a compile-time constant
    bool canInline;          // Whether this expression can be inlined

    // Register allocation hints for the compiler
    int suggestedRegister;   // Suggested register for this value (-1 if none)
    bool spillable;          // Whether this value can be spilled to memory
    bool prefer_typed_register;     // Loop analysis hint: prefer typed register usage
    bool requires_loop_residency;   // Loop analysis hint: needs residency across loop
    int loop_binding_id;            // Loop affinity binding identifier (-1 if none)

    // Child nodes (typed versions)
    union {
        struct {
            TypedASTNode** declarations;
            int count;
            const char* moduleName;
        } program;
        struct {
            TypedASTNode* initializer;
            TypedASTNode* typeAnnotation;
            bool isGlobal;
            bool isPublic;
        } varDecl;
        struct {
            const char* moduleName;
            const char* moduleAlias;
            TypedImportSymbol* symbols;
            int symbolCount;
            bool importAll;
            bool importModule;
        } import;
        struct {
            TypedASTNode* left;
            TypedASTNode* right;
        } binary;
        struct {
            char* name;              // Variable name being assigned to
            TypedASTNode* value;     // Expression being assigned
        } assign;
        struct {
            TypedASTNode** values;
            int count;
        } print;
        struct {
            TypedASTNode* condition;
            TypedASTNode* thenBranch;
            TypedASTNode* elseBranch;
        } ifStmt;
        struct {
            TypedASTNode* condition;
            TypedASTNode* body;
        } whileStmt;
        struct {
            char* varName;
            TypedASTNode* start;
            TypedASTNode* end;
            TypedASTNode* step;
            bool inclusive;
            TypedASTNode* body;
            char* label;
        } forRange;
        struct {
            char* varName;
            TypedASTNode* iterable;
            TypedASTNode* body;
            char* label;
        } forIter;
        struct {
            TypedASTNode* tryBlock;
            TypedASTNode* catchBlock;
            char* catchVarName;
        } tryStmt;
        struct {
            TypedASTNode** statements;
            int count;
        } block;
        struct {
            TypedASTNode* condition;
            TypedASTNode* trueExpr;
            TypedASTNode* falseExpr;
        } ternary;
        struct {
            TypedASTNode* operand;
        } unary;
        struct {
            TypedASTNode* returnType;
            TypedASTNode* body;
            bool isPublic;
            bool isMethod;
            bool isInstanceMethod;
            const char* methodStructName;
        } function;
        struct {
            TypedASTNode* callee;
            TypedASTNode** args;
            int argCount;
        } call;
        struct {
            TypedASTNode** elements;
            int count;
        } arrayLiteral;
        struct {
            TypedASTNode* value;
            TypedASTNode* lengthExpr;
            int resolvedLength;
        } arrayFill;
        struct {
            TypedASTNode* array;
            TypedASTNode* index;
            bool isStringIndex;
        } indexAccess;
        struct {
            TypedASTNode* value;
        } returnStmt;
        struct {
            TypedASTNode* expression;
            TypedASTNode* targetType;
        } cast;
        struct {
            TypedASTNode* target;  // NODE_INDEX_ACCESS in typed form
            TypedASTNode* value;
        } arrayAssign;
        struct {
            TypedASTNode* array;
            TypedASTNode* start;
            TypedASTNode* end;
        } arraySlice;
        struct {
            const char* name;
            bool isPublic;
            TypedStructField* fields;
            int fieldCount;
        } structDecl;
        struct {
            const char* structName;
            bool isPublic;
            TypedASTNode** methods;
            int methodCount;
        } implBlock;
        struct {
            const char* structName;
            const char* moduleAlias;
            const char* resolvedModuleName;
            StructLiteralField* fields;
            int fieldCount;
            TypedASTNode** values;
        } structLiteral;
        struct {
            TypedASTNode* object;
            const char* member;
            bool isMethod;
            bool isInstanceMethod;
            bool resolvesToEnum;
            bool resolvesToEnumVariant;
            int enumVariantIndex;
            int enumVariantArity;
            const char* enumTypeName;
            bool resolvesToModule;
            const char* moduleName;
            const char* moduleAliasBinding;
            ModuleExportKind moduleExportKind;
            uint16_t moduleRegisterIndex;
        } member;
        struct {
            TypedASTNode* target;
            TypedASTNode* value;
        } memberAssign;
        struct {
            const char* name;
            bool isPublic;
            TypedEnumVariant* variants;
            int variantCount;
            const char** genericParams;
            int genericParamCount;
        } enumDecl;
        struct {
            TypedASTNode* value;
            const char* enumTypeName;
            const char* variantName;
            int variantIndex;
            int expectedPayloadCount;
        } enumMatchTest;
        struct {
            TypedASTNode* value;
            const char* enumTypeName;
            const char* variantName;
            int variantIndex;
            int fieldIndex;
        } enumPayload;
        struct {
            TypedASTNode* value;
            const char* enumTypeName;
            const char** variantNames;
            int variantCount;
            bool hasWildcard;
        } enumMatchCheck;
        struct {
            TypedASTNode* subject;
            const char* tempName;
            TypedMatchArm* arms;
            int armCount;
            bool hasWildcard;
        } matchExpr;
    } typed;
};

// Function declarations for typed AST management
TypedASTNode* create_typed_ast_node(ASTNode* original);
void free_typed_ast_node(TypedASTNode* node);
TypedASTNode* copy_typed_ast_node(TypedASTNode* node);
size_t typed_ast_registry_checkpoint(void);
void typed_ast_release_from_checkpoint(size_t checkpoint);
void typed_ast_release_orphans(void);

// Type resolution functions
bool resolve_node_type(TypedASTNode* node, TypeEnv* env);
bool validate_typed_ast(TypedASTNode* root);

// Utility functions
const char* typed_node_type_string(TypedASTNode* node);
void print_typed_ast(TypedASTNode* node, int indent);
bool typed_ast_visit(TypedASTNode* root, const TypedASTVisitor* visitor,
                     void* user_data);

#endif // ORUS_TYPED_AST_H
