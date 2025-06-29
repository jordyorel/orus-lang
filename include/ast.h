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
        } typeAnnotation;
    };
};

#endif // ORUS_AST_H
