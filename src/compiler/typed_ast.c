/*
 * File: src/compiler/typed_ast.c
 * Typed AST implementation for Orus compiler
 *
 * This file implements the typed AST data structure that bridges
 * the gap between HM type inference and bytecode generation.
 * It provides functions to create, manipulate, and visualize typed AST nodes.
 */

#include "compiler/typed_ast.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "runtime/memory.h"

// Forward declaration
static Type* infer_basic_type(ASTNode* ast);

// Create a new typed AST node from an original AST node
TypedASTNode* create_typed_ast_node(ASTNode* original) {
    if (!original) return NULL;

    TypedASTNode* typed = malloc(sizeof(TypedASTNode));
    if (!typed) return NULL;

    // Initialize basic fields
    typed->original = original;
    typed->resolvedType = original->dataType;  // Copy initial type if available
    typed->typeResolved = (original->dataType != NULL);
    typed->hasTypeError = false;
    typed->errorMessage = NULL;
    typed->isConstant = false;
    typed->canInline = false;
    typed->suggestedRegister = -1;
    typed->spillable = true;

    // Initialize union based on node type
    switch (original->type) {
        case NODE_PROGRAM:
            typed->typed.program.declarations = NULL;
            typed->typed.program.count = 0;
            break;
        case NODE_VAR_DECL:
            typed->typed.varDecl.initializer = NULL;
            typed->typed.varDecl.typeAnnotation = NULL;
            break;
        case NODE_BINARY:
            typed->typed.binary.left = NULL;
            typed->typed.binary.right = NULL;
            break;
        case NODE_ASSIGN:
            typed->typed.assign.value = NULL;
            break;
        case NODE_PRINT:
            typed->typed.print.values = NULL;
            typed->typed.print.count = 0;
            typed->typed.print.separator = NULL;
            break;
        case NODE_IF:
            typed->typed.ifStmt.condition = NULL;
            typed->typed.ifStmt.thenBranch = NULL;
            typed->typed.ifStmt.elseBranch = NULL;
            break;
        case NODE_WHILE:
            typed->typed.whileStmt.condition = NULL;
            typed->typed.whileStmt.body = NULL;
            break;
        case NODE_FOR_RANGE:
            typed->typed.forRange.start = NULL;
            typed->typed.forRange.end = NULL;
            typed->typed.forRange.step = NULL;
            typed->typed.forRange.body = NULL;
            break;
        case NODE_FOR_ITER:
            typed->typed.forIter.iterable = NULL;
            typed->typed.forIter.body = NULL;
            break;
        case NODE_BLOCK:
            typed->typed.block.statements = NULL;
            typed->typed.block.count = 0;
            break;
        case NODE_TERNARY:
            typed->typed.ternary.condition = NULL;
            typed->typed.ternary.trueExpr = NULL;
            typed->typed.ternary.falseExpr = NULL;
            break;
        case NODE_UNARY:
            typed->typed.unary.operand = NULL;
            break;
        case NODE_FUNCTION:
            typed->typed.function.returnType = NULL;
            typed->typed.function.body = NULL;
            break;
        case NODE_CALL:
            typed->typed.call.callee = NULL;
            typed->typed.call.args = NULL;
            typed->typed.call.argCount = 0;
            break;
        case NODE_RETURN:
            typed->typed.returnStmt.value = NULL;
            break;
        case NODE_CAST:
            typed->typed.cast.expression = NULL;
            typed->typed.cast.targetType = NULL;
            break;
        default:
            // For leaf nodes (IDENTIFIER, LITERAL, etc.), no additional
            // initialization needed
            break;
    }

    return typed;
}

// Free a typed AST node and all its children
void free_typed_ast_node(TypedASTNode* node) {
    if (!node) return;

    // Free error message if allocated
    if (node->errorMessage) {
        free(node->errorMessage);
    }

    // Free children based on node type
    switch (node->original->type) {
        case NODE_PROGRAM:
            if (node->typed.program.declarations) {
                for (int i = 0; i < node->typed.program.count; i++) {
                    free_typed_ast_node(node->typed.program.declarations[i]);
                }
                free(node->typed.program.declarations);
            }
            break;
        case NODE_VAR_DECL:
            free_typed_ast_node(node->typed.varDecl.initializer);
            free_typed_ast_node(node->typed.varDecl.typeAnnotation);
            break;
        case NODE_BINARY:
            free_typed_ast_node(node->typed.binary.left);
            free_typed_ast_node(node->typed.binary.right);
            break;
        case NODE_ASSIGN:
            free_typed_ast_node(node->typed.assign.value);
            break;
        case NODE_PRINT:
            if (node->typed.print.values) {
                for (int i = 0; i < node->typed.print.count; i++) {
                    free_typed_ast_node(node->typed.print.values[i]);
                }
                free(node->typed.print.values);
            }
            free_typed_ast_node(node->typed.print.separator);
            break;
        case NODE_IF:
            free_typed_ast_node(node->typed.ifStmt.condition);
            free_typed_ast_node(node->typed.ifStmt.thenBranch);
            free_typed_ast_node(node->typed.ifStmt.elseBranch);
            break;
        case NODE_WHILE:
            free_typed_ast_node(node->typed.whileStmt.condition);
            free_typed_ast_node(node->typed.whileStmt.body);
            break;
        case NODE_FOR_RANGE:
            free_typed_ast_node(node->typed.forRange.start);
            free_typed_ast_node(node->typed.forRange.end);
            free_typed_ast_node(node->typed.forRange.step);
            free_typed_ast_node(node->typed.forRange.body);
            break;
        case NODE_FOR_ITER:
            free_typed_ast_node(node->typed.forIter.iterable);
            free_typed_ast_node(node->typed.forIter.body);
            break;
        case NODE_BLOCK:
            if (node->typed.block.statements) {
                for (int i = 0; i < node->typed.block.count; i++) {
                    free_typed_ast_node(node->typed.block.statements[i]);
                }
                free(node->typed.block.statements);
            }
            break;
        case NODE_TERNARY:
            free_typed_ast_node(node->typed.ternary.condition);
            free_typed_ast_node(node->typed.ternary.trueExpr);
            free_typed_ast_node(node->typed.ternary.falseExpr);
            break;
        case NODE_UNARY:
            free_typed_ast_node(node->typed.unary.operand);
            break;
        case NODE_FUNCTION:
            free_typed_ast_node(node->typed.function.returnType);
            free_typed_ast_node(node->typed.function.body);
            break;
        case NODE_CALL:
            free_typed_ast_node(node->typed.call.callee);
            if (node->typed.call.args) {
                for (int i = 0; i < node->typed.call.argCount; i++) {
                    free_typed_ast_node(node->typed.call.args[i]);
                }
                free(node->typed.call.args);
            }
            break;
        case NODE_RETURN:
            free_typed_ast_node(node->typed.returnStmt.value);
            break;
        case NODE_CAST:
            free_typed_ast_node(node->typed.cast.expression);
            free_typed_ast_node(node->typed.cast.targetType);
            break;
        default:
            // Leaf nodes have no children to free
            break;
    }

    free(node);
}

// Copy a typed AST node (shallow copy of metadata, deep copy of structure)
TypedASTNode* copy_typed_ast_node(TypedASTNode* node) {
    if (!node) return NULL;

    TypedASTNode* copy = create_typed_ast_node(node->original);
    if (!copy) return NULL;

    // Copy metadata
    copy->resolvedType = node->resolvedType;
    copy->typeResolved = node->typeResolved;
    copy->hasTypeError = node->hasTypeError;
    copy->isConstant = node->isConstant;
    copy->canInline = node->canInline;
    copy->suggestedRegister = node->suggestedRegister;
    copy->spillable = node->spillable;

    if (node->errorMessage) {
        copy->errorMessage = strdup(node->errorMessage);
    }

    // Note: Children are not copied here - this is a shallow copy
    // Full deep copy would require recursive copying of all children

    return copy;
}

// Validate that a typed AST tree has all types resolved
bool validate_typed_ast(TypedASTNode* root) {
    if (!root) return false;

    // Check if this node has type errors
    if (root->hasTypeError) {
        return false;
    }

    // Check if type is resolved
    if (!root->typeResolved || !root->resolvedType) {
        return false;
    }

    // Recursively validate children based on node type
    switch (root->original->type) {
        case NODE_PROGRAM:
            for (int i = 0; i < root->typed.program.count; i++) {
                if (!validate_typed_ast(root->typed.program.declarations[i])) {
                    return false;
                }
            }
            break;
        case NODE_BINARY:
            return validate_typed_ast(root->typed.binary.left) &&
                   validate_typed_ast(root->typed.binary.right);
        case NODE_UNARY:
            return validate_typed_ast(root->typed.unary.operand);
        case NODE_VAR_DECL:
            if (root->typed.varDecl.initializer &&
                !validate_typed_ast(root->typed.varDecl.initializer)) {
                return false;
            }
            if (root->typed.varDecl.typeAnnotation &&
                !validate_typed_ast(root->typed.varDecl.typeAnnotation)) {
                return false;
            }
            break;
        case NODE_ASSIGN:
            return validate_typed_ast(root->typed.assign.value);
        case NODE_PRINT:
            for (int i = 0; i < root->typed.print.count; i++) {
                if (!validate_typed_ast(root->typed.print.values[i])) {
                    return false;
                }
            }
            if (root->typed.print.separator &&
                !validate_typed_ast(root->typed.print.separator)) {
                return false;
            }
            break;
        default:
            // For leaf nodes, just check if type is resolved
            break;
    }

    return true;
}

// Get string representation of typed node's type
const char* typed_node_type_string(TypedASTNode* node) {
    if (!node) return "unresolved";
    if (!node->resolvedType) return "unresolved";

    switch (node->resolvedType->kind) {
        case TYPE_I32:
            return "i32";
        case TYPE_I64:
            return "i64";
        case TYPE_U32:
            return "u32";
        case TYPE_U64:
            return "u64";
        case TYPE_F64:
            return "f64";
        case TYPE_BOOL:
            return "bool";
        case TYPE_STRING:
            return "string";
        case TYPE_VOID:
            return "void";
        case TYPE_ARRAY:
            return "array";
        case TYPE_FUNCTION:
            return "function";
        case TYPE_ERROR:
            return "error";
        case TYPE_ANY:
            return "any";
        case TYPE_VAR:
            return "var";
        case TYPE_GENERIC:
            return "generic";
        case TYPE_INSTANCE:
            return "instance";
        default:
            return "unknown";
    }
}

// Print typed AST for debugging
void print_typed_ast(TypedASTNode* node, int indent) {
    if (!node) return;

    // Print indentation
    for (int i = 0; i < indent; i++) {
        printf("  ");
    }

    // Print node info
    const char* nodeTypeStr = "Unknown";
    switch (node->original->type) {
        case NODE_PROGRAM:
            nodeTypeStr = "Program";
            break;
        case NODE_VAR_DECL:
            nodeTypeStr = "VarDecl";
            break;
        case NODE_IDENTIFIER:
            nodeTypeStr = "Identifier";
            break;
        case NODE_LITERAL:
            nodeTypeStr = "Literal";
            break;
        case NODE_BINARY:
            nodeTypeStr = "Binary";
            break;
        case NODE_ASSIGN:
            nodeTypeStr = "Assign";
            break;
        case NODE_PRINT:
            nodeTypeStr = "Print";
            break;
        case NODE_IF:
            nodeTypeStr = "If";
            break;
        case NODE_WHILE:
            nodeTypeStr = "While";
            break;
        case NODE_FOR_RANGE:
            nodeTypeStr = "ForRange";
            break;
        case NODE_FOR_ITER:
            nodeTypeStr = "ForIter";
            break;
        case NODE_BLOCK:
            nodeTypeStr = "Block";
            break;
        case NODE_TERNARY:
            nodeTypeStr = "Ternary";
            break;
        case NODE_UNARY:
            nodeTypeStr = "Unary";
            break;
        case NODE_FUNCTION:
            nodeTypeStr = "Function";
            break;
        case NODE_CALL:
            nodeTypeStr = "Call";
            break;
        case NODE_RETURN:
            nodeTypeStr = "Return";
            break;
        case NODE_CAST:
            nodeTypeStr = "Cast";
            break;
        case NODE_BREAK:
            nodeTypeStr = "Break";
            break;
        case NODE_CONTINUE:
            nodeTypeStr = "Continue";
            break;
        case NODE_TYPE:
            nodeTypeStr = "Type";
            break;
        case NODE_TIME_STAMP:
            nodeTypeStr = "TimeStamp";
            break;
        default:
            break;
    }

    printf("%s: type=%s", nodeTypeStr, typed_node_type_string(node));

    if (node->hasTypeError) {
        printf(" [ERROR: %s]",
               node->errorMessage ? node->errorMessage : "unknown error");
    }

    if (node->isConstant) {
        printf(" [CONST]");
    }

    if (node->canInline) {
        printf(" [INLINE]");
    }

    if (node->suggestedRegister >= 0) {
        printf(" [REG:R%d]", node->suggestedRegister);
    }

    if (!node->spillable) {
        printf(" [NO_SPILL]");
    }

    printf("\n");

    // Print children
    switch (node->original->type) {
        case NODE_PROGRAM:
            if (node->typed.program.declarations) {
                for (int i = 0; i < node->typed.program.count; i++) {
                    print_typed_ast(node->typed.program.declarations[i],
                                    indent + 1);
                }
            }
            break;
        case NODE_BINARY:
            print_typed_ast(node->typed.binary.left, indent + 1);
            print_typed_ast(node->typed.binary.right, indent + 1);
            break;
        case NODE_UNARY:
            print_typed_ast(node->typed.unary.operand, indent + 1);
            break;
        case NODE_VAR_DECL:
            if (node->typed.varDecl.typeAnnotation) {
                print_typed_ast(node->typed.varDecl.typeAnnotation, indent + 1);
            }
            if (node->typed.varDecl.initializer) {
                print_typed_ast(node->typed.varDecl.initializer, indent + 1);
            }
            break;
        case NODE_ASSIGN:
            print_typed_ast(node->typed.assign.value, indent + 1);
            break;
        case NODE_PRINT:
            if (node->typed.print.values) {
                for (int i = 0; i < node->typed.print.count; i++) {
                    print_typed_ast(node->typed.print.values[i], indent + 1);
                }
            }
            if (node->typed.print.separator) {
                print_typed_ast(node->typed.print.separator, indent + 1);
            }
            break;
        case NODE_IF:
            print_typed_ast(node->typed.ifStmt.condition, indent + 1);
            print_typed_ast(node->typed.ifStmt.thenBranch, indent + 1);
            if (node->typed.ifStmt.elseBranch) {
                print_typed_ast(node->typed.ifStmt.elseBranch, indent + 1);
            }
            break;
        case NODE_WHILE:
            print_typed_ast(node->typed.whileStmt.condition, indent + 1);
            print_typed_ast(node->typed.whileStmt.body, indent + 1);
            break;
        case NODE_FOR_RANGE:
            print_typed_ast(node->typed.forRange.start, indent + 1);
            print_typed_ast(node->typed.forRange.end, indent + 1);
            if (node->typed.forRange.step) {
                print_typed_ast(node->typed.forRange.step, indent + 1);
            }
            print_typed_ast(node->typed.forRange.body, indent + 1);
            break;
        case NODE_FOR_ITER:
            print_typed_ast(node->typed.forIter.iterable, indent + 1);
            print_typed_ast(node->typed.forIter.body, indent + 1);
            break;
        case NODE_BLOCK:
            if (node->typed.block.statements) {
                for (int i = 0; i < node->typed.block.count; i++) {
                    print_typed_ast(node->typed.block.statements[i],
                                    indent + 1);
                }
            }
            break;
        case NODE_TERNARY:
            print_typed_ast(node->typed.ternary.condition, indent + 1);
            print_typed_ast(node->typed.ternary.trueExpr, indent + 1);
            print_typed_ast(node->typed.ternary.falseExpr, indent + 1);
            break;
        case NODE_FUNCTION:
            if (node->typed.function.returnType) {
                print_typed_ast(node->typed.function.returnType, indent + 1);
            }
            print_typed_ast(node->typed.function.body, indent + 1);
            break;
        case NODE_CALL:
            print_typed_ast(node->typed.call.callee, indent + 1);
            if (node->typed.call.args) {
                for (int i = 0; i < node->typed.call.argCount; i++) {
                    print_typed_ast(node->typed.call.args[i], indent + 1);
                }
            }
            break;
        case NODE_RETURN:
            if (node->typed.returnStmt.value) {
                print_typed_ast(node->typed.returnStmt.value, indent + 1);
            }
            break;
        case NODE_CAST:
            print_typed_ast(node->typed.cast.expression, indent + 1);
            print_typed_ast(node->typed.cast.targetType, indent + 1);
            break;
        default:
            break;
    }
}

TypedASTNode* generate_typed_ast(ASTNode* ast, void* type_env) {
    if (!ast) return NULL;

    TypedASTNode* typed = create_typed_ast_node(ast);
    if (!typed) return NULL;

    // Preserve the type from type inference if available, otherwise infer basic types
    if (ast->dataType) {
        typed->resolvedType = ast->dataType;
        typed->typeResolved = true;
    } else {
        // Basic type inference for nodes without existing type information
        typed->resolvedType = infer_basic_type(ast);
        typed->typeResolved = (typed->resolvedType != NULL);
    }

    // Recursively generate children
    switch (ast->type) {
        case NODE_PROGRAM:
            if (ast->program.count > 0) {
                typed->typed.program.declarations =
                    malloc(sizeof(TypedASTNode*) * ast->program.count);
                if (!typed->typed.program.declarations) {
                    free_typed_ast_node(typed);
                    return NULL;
                }
                typed->typed.program.count = ast->program.count;
                for (int i = 0; i < ast->program.count; i++) {
                    typed->typed.program.declarations[i] = generate_typed_ast(
                        ast->program.declarations[i], type_env);
                    if (!typed->typed.program.declarations[i]) {
                        // Cleanup on failure
                        for (int j = 0; j < i; j++) {
                            free_typed_ast_node(
                                typed->typed.program.declarations[j]);
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
                typed->typed.varDecl.initializer =
                    generate_typed_ast(ast->varDecl.initializer, type_env);
            }
            if (ast->varDecl.typeAnnotation) {
                typed->typed.varDecl.typeAnnotation =
                    generate_typed_ast(ast->varDecl.typeAnnotation, type_env);
            }
            break;

        case NODE_BINARY:
            typed->typed.binary.left =
                generate_typed_ast(ast->binary.left, type_env);
            typed->typed.binary.right =
                generate_typed_ast(ast->binary.right, type_env);
            break;

        case NODE_ASSIGN:
            if (ast->assign.value) {
                typed->typed.assign.value =
                    generate_typed_ast(ast->assign.value, type_env);
            }
            break;

        case NODE_PRINT:
            if (ast->print.count > 0) {
                typed->typed.print.values =
                    malloc(sizeof(TypedASTNode*) * ast->print.count);
                if (!typed->typed.print.values) {
                    free_typed_ast_node(typed);
                    return NULL;
                }
                typed->typed.print.count = ast->print.count;
                for (int i = 0; i < ast->print.count; i++) {
                    typed->typed.print.values[i] =
                        generate_typed_ast(ast->print.values[i], type_env);
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
            if (ast->print.separator) {
                typed->typed.print.separator =
                    generate_typed_ast(ast->print.separator, type_env);
            }
            break;

        case NODE_IF:
            typed->typed.ifStmt.condition =
                generate_typed_ast(ast->ifStmt.condition, type_env);
            typed->typed.ifStmt.thenBranch =
                generate_typed_ast(ast->ifStmt.thenBranch, type_env);
            if (ast->ifStmt.elseBranch) {
                typed->typed.ifStmt.elseBranch =
                    generate_typed_ast(ast->ifStmt.elseBranch, type_env);
            }
            break;

        case NODE_WHILE:
            typed->typed.whileStmt.condition =
                generate_typed_ast(ast->whileStmt.condition, type_env);
            typed->typed.whileStmt.body =
                generate_typed_ast(ast->whileStmt.body, type_env);
            break;

        case NODE_FOR_RANGE:
            typed->typed.forRange.start =
                generate_typed_ast(ast->forRange.start, type_env);
            typed->typed.forRange.end =
                generate_typed_ast(ast->forRange.end, type_env);
            if (ast->forRange.step) {
                typed->typed.forRange.step =
                    generate_typed_ast(ast->forRange.step, type_env);
            }
            typed->typed.forRange.body =
                generate_typed_ast(ast->forRange.body, type_env);
            break;

        case NODE_FOR_ITER:
            typed->typed.forIter.iterable =
                generate_typed_ast(ast->forIter.iterable, type_env);
            typed->typed.forIter.body =
                generate_typed_ast(ast->forIter.body, type_env);
            break;

        case NODE_BLOCK:
            if (ast->block.count > 0) {
                typed->typed.block.statements =
                    malloc(sizeof(TypedASTNode*) * ast->block.count);
                if (!typed->typed.block.statements) {
                    free_typed_ast_node(typed);
                    return NULL;
                }
                typed->typed.block.count = ast->block.count;
                for (int i = 0; i < ast->block.count; i++) {
                    typed->typed.block.statements[i] =
                        generate_typed_ast(ast->block.statements[i], type_env);
                    if (!typed->typed.block.statements[i]) {
                        // Cleanup on failure
                        for (int j = 0; j < i; j++) {
                            free_typed_ast_node(
                                typed->typed.block.statements[j]);
                        }
                        free(typed->typed.block.statements);
                        free_typed_ast_node(typed);
                        return NULL;
                    }
                }
            }
            break;

        case NODE_TERNARY:
            typed->typed.ternary.condition =
                generate_typed_ast(ast->ternary.condition, type_env);
            typed->typed.ternary.trueExpr =
                generate_typed_ast(ast->ternary.trueExpr, type_env);
            typed->typed.ternary.falseExpr =
                generate_typed_ast(ast->ternary.falseExpr, type_env);
            break;

        case NODE_UNARY:
            typed->typed.unary.operand =
                generate_typed_ast(ast->unary.operand, type_env);
            break;

        case NODE_FUNCTION:
            if (ast->function.returnType) {
                typed->typed.function.returnType =
                    generate_typed_ast(ast->function.returnType, type_env);
            }
            typed->typed.function.body =
                generate_typed_ast(ast->function.body, type_env);
            break;

        case NODE_CALL:
            typed->typed.call.callee =
                generate_typed_ast(ast->call.callee, type_env);
            if (ast->call.argCount > 0) {
                typed->typed.call.args =
                    malloc(sizeof(TypedASTNode*) * ast->call.argCount);
                if (!typed->typed.call.args) {
                    free_typed_ast_node(typed);
                    return NULL;
                }
                typed->typed.call.argCount = ast->call.argCount;
                for (int i = 0; i < ast->call.argCount; i++) {
                    typed->typed.call.args[i] =
                        generate_typed_ast(ast->call.args[i], type_env);
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
                typed->typed.returnStmt.value =
                    generate_typed_ast(ast->returnStmt.value, type_env);
            }
            break;

        case NODE_CAST:
            typed->typed.cast.expression =
                generate_typed_ast(ast->cast.expression, type_env);
            typed->typed.cast.targetType =
                generate_typed_ast(ast->cast.targetType, type_env);
            break;

        default:
            break;
    }

    return typed;
}

// Basic type inference for AST nodes
static Type* infer_basic_type(ASTNode* ast) {
    if (!ast) return NULL;
    
    Type* type = malloc(sizeof(Type));
    if (!type) return NULL;
    
    switch (ast->type) {
        case NODE_LITERAL:
            // Infer type from literal value
            switch (ast->literal.value.type) {
                case VAL_I32:
                    type->kind = TYPE_I32;
                    break;
                case VAL_I64:
                    type->kind = TYPE_I64;
                    break;
                case VAL_U32:
                    type->kind = TYPE_U32;
                    break;
                case VAL_U64:
                    type->kind = TYPE_U64;
                    break;  
                case VAL_F64:
                    type->kind = TYPE_F64;
                    break;
                case VAL_BOOL:
                    type->kind = TYPE_BOOL;
                    break;
                case VAL_STRING:
                    type->kind = TYPE_STRING;
                    break;
                default:
                    free(type);
                    return NULL;
            }
            break;
            
        case NODE_BINARY:
            // Binary operations: arithmetic returns operand type, comparisons return bool
            if (ast->binary.op) {
                if (strcmp(ast->binary.op, ">") == 0 ||
                    strcmp(ast->binary.op, "<") == 0 ||
                    strcmp(ast->binary.op, ">=") == 0 ||
                    strcmp(ast->binary.op, "<=") == 0 ||
                    strcmp(ast->binary.op, "==") == 0 ||
                    strcmp(ast->binary.op, "!=") == 0) {
                    type->kind = TYPE_BOOL;
                } else {
                    // Arithmetic operations: try to infer from operands
                    if (ast->binary.left) {
                        Type* left_type = infer_basic_type(ast->binary.left);
                        if (left_type) {
                            type->kind = left_type->kind;
                            free(left_type);
                        } else {
                            type->kind = TYPE_I32; // Default fallback
                        }
                    } else {
                        type->kind = TYPE_I32; // Default fallback
                    }
                }
            } else {
                type->kind = TYPE_I32; // Default fallback
            }
            break;
            
        case NODE_IDENTIFIER:
            // Default to i32 for untyped identifiers (would be resolved by proper type inference)
            type->kind = TYPE_I32;
            break;
            
        case NODE_PROGRAM:
        case NODE_VAR_DECL:
        case NODE_ASSIGN:
        case NODE_PRINT:
        case NODE_IF:
        case NODE_WHILE:
        case NODE_FOR_RANGE:
        case NODE_FOR_ITER:
        case NODE_BLOCK:
            // Statements typically return void
            type->kind = TYPE_VOID;
            break;
            
        case NODE_RETURN:
            // Return statement type depends on what it returns
            if (ast->returnStmt.value) {
                // If there's a return value, recursively infer its type
                Type* value_type = infer_basic_type(ast->returnStmt.value);
                if (value_type) {
                    type->kind = value_type->kind;
                    free(value_type); // Free the temporary type
                } else {
                    type->kind = TYPE_VOID;
                }
            } else {
                type->kind = TYPE_VOID;
            }
            break;
            
        case NODE_FUNCTION:
            type->kind = TYPE_FUNCTION;
            break;
            
        case NODE_CALL:
            // Function calls default to i32 (would be resolved by proper type inference)
            type->kind = TYPE_I32;
            break;
            
        case NODE_TYPE:
            // Type annotation nodes - parse the type name
            if (ast->typeAnnotation.name) {
                if (strcmp(ast->typeAnnotation.name, "i32") == 0) {
                    type->kind = TYPE_I32;
                } else if (strcmp(ast->typeAnnotation.name, "i64") == 0) {
                    type->kind = TYPE_I64;
                } else if (strcmp(ast->typeAnnotation.name, "u32") == 0) {
                    type->kind = TYPE_U32;
                } else if (strcmp(ast->typeAnnotation.name, "u64") == 0) {
                    type->kind = TYPE_U64;
                } else if (strcmp(ast->typeAnnotation.name, "f64") == 0) {
                    type->kind = TYPE_F64;
                } else if (strcmp(ast->typeAnnotation.name, "bool") == 0) {
                    type->kind = TYPE_BOOL;
                } else if (strcmp(ast->typeAnnotation.name, "string") == 0) {
                    type->kind = TYPE_STRING;
                } else if (strcmp(ast->typeAnnotation.name, "void") == 0) {
                    type->kind = TYPE_VOID;
                } else {
                    type->kind = TYPE_UNKNOWN;
                }
            } else {
                type->kind = TYPE_UNKNOWN;
            }
            break;
            
        case NODE_CAST:
            // Cast expressions take the type of their target type
            if (ast->cast.targetType) {
                Type* target_type = infer_basic_type(ast->cast.targetType);
                if (target_type) {
                    type->kind = target_type->kind;
                    free(target_type);
                } else {
                    type->kind = TYPE_UNKNOWN;
                }
            } else {
                type->kind = TYPE_UNKNOWN;
            }
            break;
            
        case NODE_TERNARY:
            // Ternary expressions: condition ? true_expr : false_expr
            // Type is the unified type of both branches
            if (ast->ternary.trueExpr && ast->ternary.falseExpr) {
                Type* true_type = infer_basic_type(ast->ternary.trueExpr);
                Type* false_type = infer_basic_type(ast->ternary.falseExpr);
                
                if (true_type && false_type && true_type->kind == false_type->kind) {
                    type->kind = true_type->kind;
                } else if (true_type) {
                    type->kind = true_type->kind;
                } else {
                    type->kind = TYPE_UNKNOWN;
                }
                
                if (true_type) free(true_type);
                if (false_type) free(false_type);
            } else {
                type->kind = TYPE_UNKNOWN;
            }
            break;
            
        default:
            // Unknown or unhandled node types
            free(type);
            return NULL;
    }
    
    return type;
}