#ifndef ORUS_AST_H
#define ORUS_AST_H

#include "common.h"

// Forward declarations for types defined in vm.h
struct Value;
struct Type;
struct SrcLocation;

// Forward declaration
typedef struct ASTNode ASTNode;

// Function parameter representation
typedef struct {
    char* name;
    ASTNode* typeAnnotation;  // Optional type annotation
} FunctionParam;

// Different kinds of AST nodes supported in the minimal language
typedef enum {
    NODE_PROGRAM,
    NODE_VAR_DECL,
    NODE_IDENTIFIER,
    NODE_LITERAL,
    NODE_BINARY,
    NODE_ASSIGN,
    NODE_PRINT,
    NODE_TIME_STAMP,
    NODE_IF,
    NODE_WHILE,
    NODE_FOR_RANGE,
    NODE_FOR_ITER,
    NODE_BLOCK,
    NODE_TERNARY,
    NODE_UNARY,
    NODE_TYPE,
    NODE_BREAK,
    NODE_CONTINUE,
    NODE_FUNCTION,
    NODE_CALL,
    NODE_RETURN,
    NODE_LET,
    NODE_CAST        // Add cast node for 'as' operator
} NodeType;

struct ASTNode {
    NodeType type;
    SrcLocation location;
    Type* dataType;
    union {
        struct {
            ASTNode** declarations;
            int count;
        } program;
        struct {
            char* name;
            bool isPublic;
            ASTNode* initializer;
            ASTNode* typeAnnotation;
            bool isConst;
            bool isMutable;
        } varDecl;
        struct {
            char* name;
        } identifier;
        struct {
            Value value;
        } literal;
        struct {
            char* op;
            ASTNode* left;
            ASTNode* right;
        } binary;
        struct {
            char* name;
            ASTNode* value;
        } assign;
        struct {
            ASTNode** values;
            int count;
            bool newline;
        } print;
        struct {
            ASTNode* condition;
            ASTNode* thenBranch;
            ASTNode* elseBranch;
        } ifStmt;
        struct {
            ASTNode* condition;
            ASTNode* body;
            char* label;
        } whileStmt;
        struct {
            char* varName;
            ASTNode* start;
            ASTNode* end;
            ASTNode* step;
            bool inclusive;
            ASTNode* body;
            char* label;
        } forRange;
        struct {
            char* varName;
            ASTNode* iterable;
            ASTNode* body;
            char* label;
        } forIter;
        struct {
            ASTNode** statements;
            int count;
        } block;
        struct {
            ASTNode* condition;
            ASTNode* trueExpr;
            ASTNode* falseExpr;
        } ternary;
        struct {
            char* op;
            ASTNode* operand;
        } unary;
        struct {
            char* name;
        } typeAnnotation;
        struct {
            char* label;
        } breakStmt;
        struct {
            char* label;
        } continueStmt;
        struct {
            char* name;                    // Function name
            FunctionParam* params;         // Parameters
            int paramCount;                // Number of parameters
            ASTNode* returnType;           // Optional return type annotation
            ASTNode* body;                 // Function body (block)
        } function;
        struct {
            ASTNode* callee;               // Function expression
            ASTNode** args;                // Arguments
            int argCount;                  // Number of arguments
        } call;
        struct {
            ASTNode* value;                // Return value (NULL for void return)
        } returnStmt;
        struct {
            char* name;                    // Variable name
            ASTNode* value;                // Value expression
        } let;
        struct {
            ASTNode* expression;           // Expression to cast
            ASTNode* targetType;           // Target type
        } cast;
    };
};

#endif // ORUS_AST_H
