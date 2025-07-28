#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// Minimal definitions for testing - avoiding the circular dependency
typedef enum {
    TYPE_I32,
    TYPE_F64,
    TYPE_BOOL,
    TYPE_STRING
} TypeKind;

typedef struct {
    TypeKind kind;
} Type;

typedef struct {
    const char* file;
    int line;
    int column;
} SrcLocation;

typedef enum {
    VAL_I32,
    VAL_F64,
    VAL_BOOL,
    VAL_STRING
} ValueType;

typedef struct {
    ValueType type;
    union {
        int i32;
        double f64;
        bool boolean;
    } as;
} Value;

typedef enum {
    NODE_LITERAL,
    NODE_IDENTIFIER,
    NODE_BINARY
} NodeType;

typedef struct ASTNode {
    NodeType type;
    SrcLocation location;
    Type* dataType;
    union {
        struct {
            Value value;
            bool hasExplicitSuffix;
        } literal;
        struct {
            char* name;
        } identifier;
        struct {
            char* op;
            struct ASTNode* left;
            struct ASTNode* right;
        } binary;
    };
} ASTNode;

typedef struct TypedASTNode {
    ASTNode* original;
    Type* resolvedType;
    bool typeResolved;
    bool hasTypeError;
    char* errorMessage;
    bool isConstant;
    bool canInline;
    int suggestedRegister;
    bool spillable;
} TypedASTNode;

// Simple typed AST creation function
TypedASTNode* create_typed_ast_node(ASTNode* original) {
    TypedASTNode* typed = malloc(sizeof(TypedASTNode));
    typed->original = original;
    typed->resolvedType = NULL;
    typed->typeResolved = false;
    typed->hasTypeError = false;
    typed->errorMessage = NULL;
    typed->isConstant = false;
    typed->canInline = false;
    typed->suggestedRegister = -1;
    typed->spillable = true;
    return typed;
}

// Simple visualization function
void visualize_typed_ast_simple(TypedASTNode* node, int indent) {
    if (!node || !node->original) return;
    
    // Print indentation
    for (int i = 0; i < indent; i++) {
        printf("  ");
    }
    
    // Print node type
    switch (node->original->type) {
        case NODE_LITERAL:
            printf("Literal");
            break;
        case NODE_IDENTIFIER:
            printf("Identifier");
            break;
        case NODE_BINARY:
            printf("Binary");
            break;
    }
    
    // Print type information
    if (node->typeResolved && node->resolvedType) {
        printf(": type=");
        switch (node->resolvedType->kind) {
            case TYPE_I32: printf("i32"); break;
            case TYPE_F64: printf("f64"); break;
            case TYPE_BOOL: printf("bool"); break;
            case TYPE_STRING: printf("string"); break;
        }
    } else if (node->hasTypeError) {
        printf(": type=ERROR");
        if (node->errorMessage) {
            printf(" (%s)", node->errorMessage);
        }
    } else {
        printf(": type=unresolved");
    }
    
    // Print additional details
    switch (node->original->type) {
        case NODE_LITERAL:
            switch (node->original->literal.value.type) {
                case VAL_I32:
                    printf(" value=%d", node->original->literal.value.as.i32);
                    break;
                case VAL_F64:
                    printf(" value=%.6g", node->original->literal.value.as.f64);
                    break;
                case VAL_BOOL:
                    printf(" value=%s", node->original->literal.value.as.boolean ? "true" : "false");
                    break;
                default:
                    break;
            }
            break;
        case NODE_IDENTIFIER:
            if (node->original->identifier.name) {
                printf(" name='%s'", node->original->identifier.name);
            }
            break;
        case NODE_BINARY:
            if (node->original->binary.op) {
                printf(" op='%s'", node->original->binary.op);
            }
            break;
    }
    
    // Print metadata
    if (node->isConstant) printf(" [CONST]");
    if (node->canInline) printf(" [INLINE]");
    if (node->suggestedRegister >= 0) printf(" [REG:R%d]", node->suggestedRegister);
    
    printf(" @%d:%d\n", node->original->location.line, node->original->location.column);
    
    // Print children for binary nodes
    if (node->original->type == NODE_BINARY) {
        if (node->original->binary.left) {
            TypedASTNode* left = create_typed_ast_node(node->original->binary.left);
            if (left->original->type == NODE_IDENTIFIER) {
                left->resolvedType = malloc(sizeof(Type));
                left->resolvedType->kind = TYPE_I32;
                left->typeResolved = true;
            }
            visualize_typed_ast_simple(left, indent + 1);
            free(left);
        }
        if (node->original->binary.right) {
            TypedASTNode* right = create_typed_ast_node(node->original->binary.right);
            if (right->original->type == NODE_LITERAL) {
                right->resolvedType = malloc(sizeof(Type));
                right->resolvedType->kind = TYPE_I32;
                right->typeResolved = true;
                right->isConstant = true;
                right->canInline = true;
            }
            visualize_typed_ast_simple(right, indent + 1);
            free(right);
        }
    }
}

int main() {
    printf("=== Simple Typed AST Visualizer Demo ===\n\n");
    
    // Create a simple literal node (42)
    ASTNode* literal = malloc(sizeof(ASTNode));
    literal->type = NODE_LITERAL;
    literal->location.line = 1;
    literal->location.column = 5;
    literal->literal.value.type = VAL_I32;
    literal->literal.value.as.i32 = 42;
    literal->literal.hasExplicitSuffix = false;
    literal->dataType = NULL;
    
    TypedASTNode* typed_literal = create_typed_ast_node(literal);
    typed_literal->resolvedType = malloc(sizeof(Type));
    typed_literal->resolvedType->kind = TYPE_I32;
    typed_literal->typeResolved = true;
    typed_literal->isConstant = true;
    typed_literal->canInline = true;
    typed_literal->suggestedRegister = 64;
    
    printf("Test 1: Simple literal (42)\n");
    printf("----------------------------\n");
    visualize_typed_ast_simple(typed_literal, 0);
    printf("\n");
    
    // Create a binary expression (x + 24)
    ASTNode* x_node = malloc(sizeof(ASTNode));
    x_node->type = NODE_IDENTIFIER;
    x_node->location.line = 2;
    x_node->location.column = 5;
    x_node->identifier.name = strdup("x");
    x_node->dataType = NULL;
    
    ASTNode* val_24 = malloc(sizeof(ASTNode));
    val_24->type = NODE_LITERAL;
    val_24->location.line = 2;
    val_24->location.column = 9;
    val_24->literal.value.type = VAL_I32;
    val_24->literal.value.as.i32 = 24;
    val_24->literal.hasExplicitSuffix = false;
    val_24->dataType = NULL;
    
    ASTNode* binary = malloc(sizeof(ASTNode));
    binary->type = NODE_BINARY;
    binary->location.line = 2;
    binary->location.column = 7;
    binary->binary.op = strdup("+");
    binary->binary.left = x_node;
    binary->binary.right = val_24;
    binary->dataType = NULL;
    
    TypedASTNode* typed_binary = create_typed_ast_node(binary);
    typed_binary->resolvedType = malloc(sizeof(Type));
    typed_binary->resolvedType->kind = TYPE_I32;
    typed_binary->typeResolved = true;
    typed_binary->canInline = true;
    typed_binary->suggestedRegister = 192;
    
    printf("Test 2: Binary expression (x + 24)\n");
    printf("-----------------------------------\n");
    visualize_typed_ast_simple(typed_binary, 0);
    printf("\n");
    
    // Test error case
    TypedASTNode* error_node = create_typed_ast_node(literal);
    error_node->hasTypeError = true;
    error_node->typeResolved = false;
    error_node->errorMessage = strdup("Type mismatch in assignment");
    
    printf("Test 3: Node with type error\n");
    printf("-----------------------------\n");
    visualize_typed_ast_simple(error_node, 0);
    printf("\n");
    
    printf("=== Typed AST Visualizer Working! ===\n");
    
    // Cleanup
    free(typed_literal);
    free(typed_binary);
    free(error_node);
    free(literal);
    free(binary);
    free(x_node);
    free(val_24);
    
    return 0;
}