#ifndef ORUS_AST_H
#define ORUS_AST_H

#include "common.h"

// Forward declarations for types defined in vm.h
struct Value;
struct Type;
struct SrcLocation;

// Forward declaration
typedef struct ASTNode ASTNode;

// Different kinds of AST nodes supported in the minimal language
typedef enum {
    NODE_PROGRAM,
    NODE_VAR_DECL,
    NODE_IDENTIFIER,
    NODE_LITERAL,
    NODE_BINARY,
    NODE_ASSIGN,
    NODE_PRINT,
    NODE_IF,
    NODE_WHILE,
    NODE_FOR_RANGE,
    NODE_FOR_ITER,
    NODE_BLOCK,
    NODE_TERNARY,
    NODE_TYPE
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
        } whileStmt;
        struct {
            char* varName;
            ASTNode* start;
            ASTNode* end;
            ASTNode* step;
            bool inclusive;
            ASTNode* body;
        } forRange;
        struct {
            char* varName;
            ASTNode* iterable;
            ASTNode* body;
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
            char* name;
        } typeAnnotation;
    };
};

#endif // ORUS_AST_H
