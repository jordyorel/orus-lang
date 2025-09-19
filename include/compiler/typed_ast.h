#ifndef ORUS_TYPED_AST_H
#define ORUS_TYPED_AST_H

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
    
    // Child nodes (typed versions)
    union {
        struct {
            TypedASTNode** declarations;
            int count;
        } program;
        struct {
            TypedASTNode* initializer;
            TypedASTNode* typeAnnotation;
        } varDecl;
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
            TypedASTNode* separator;
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
            TypedASTNode* array;
            TypedASTNode* index;
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
        } enumDecl;
    } typed;
};

// Function declarations for typed AST management
TypedASTNode* create_typed_ast_node(ASTNode* original);
void free_typed_ast_node(TypedASTNode* node);
TypedASTNode* copy_typed_ast_node(TypedASTNode* node);

// Type resolution functions
bool resolve_node_type(TypedASTNode* node, TypeEnv* env);
bool validate_typed_ast(TypedASTNode* root);

// Utility functions
const char* typed_node_type_string(TypedASTNode* node);
void print_typed_ast(TypedASTNode* node, int indent);

#endif // ORUS_TYPED_AST_H