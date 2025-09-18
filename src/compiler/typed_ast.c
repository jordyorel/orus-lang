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
#include "internal/strutil.h"

#include "runtime/memory.h"
#include "type/type.h"

// Forward declaration

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
        case NODE_ARRAY_LITERAL:
            typed->typed.arrayLiteral.elements = NULL;
            typed->typed.arrayLiteral.count = 0;
            break;
        case NODE_INDEX_ACCESS:
            typed->typed.indexAccess.array = NULL;
            typed->typed.indexAccess.index = NULL;
            break;
        case NODE_RETURN:
            typed->typed.returnStmt.value = NULL;
            break;
        case NODE_CAST:
            typed->typed.cast.expression = NULL;
            typed->typed.cast.targetType = NULL;
            break;
        case NODE_ARRAY_ASSIGN:
            typed->typed.arrayAssign.target = NULL;
            typed->typed.arrayAssign.value = NULL;
            break;
        case NODE_ARRAY_SLICE:
            typed->typed.arraySlice.array = NULL;
            typed->typed.arraySlice.start = NULL;
            typed->typed.arraySlice.end = NULL;
            break;
        case NODE_STRUCT_DECL:
            typed->typed.structDecl.name = original->structDecl.name;
            typed->typed.structDecl.isPublic = original->structDecl.isPublic;
            typed->typed.structDecl.fields = NULL;
            typed->typed.structDecl.fieldCount = 0;
            break;
        case NODE_IMPL_BLOCK:
            typed->typed.implBlock.structName = original->implBlock.structName;
            typed->typed.implBlock.isPublic = original->implBlock.isPublic;
            typed->typed.implBlock.methods = NULL;
            typed->typed.implBlock.methodCount = 0;
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
            if (node->typed.assign.name) {
                free(node->typed.assign.name);
            }
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
        case NODE_ARRAY_LITERAL:
            if (node->typed.arrayLiteral.elements) {
                for (int i = 0; i < node->typed.arrayLiteral.count; i++) {
                    free_typed_ast_node(node->typed.arrayLiteral.elements[i]);
                }
                free(node->typed.arrayLiteral.elements);
            }
            break;
        case NODE_INDEX_ACCESS:
            free_typed_ast_node(node->typed.indexAccess.array);
            free_typed_ast_node(node->typed.indexAccess.index);
            break;
        case NODE_RETURN:
            free_typed_ast_node(node->typed.returnStmt.value);
            break;
        case NODE_CAST:
            free_typed_ast_node(node->typed.cast.expression);
            free_typed_ast_node(node->typed.cast.targetType);
            break;
        case NODE_ARRAY_ASSIGN:
            free_typed_ast_node(node->typed.arrayAssign.target);
            free_typed_ast_node(node->typed.arrayAssign.value);
            break;
        case NODE_ARRAY_SLICE:
            free_typed_ast_node(node->typed.arraySlice.array);
            free_typed_ast_node(node->typed.arraySlice.start);
            free_typed_ast_node(node->typed.arraySlice.end);
            break;
        case NODE_STRUCT_DECL:
            if (node->typed.structDecl.fields) {
                for (int i = 0; i < node->typed.structDecl.fieldCount; i++) {
                    free_typed_ast_node(node->typed.structDecl.fields[i].typeAnnotation);
                    free_typed_ast_node(node->typed.structDecl.fields[i].defaultValue);
                }
                free(node->typed.structDecl.fields);
            }
            break;
        case NODE_IMPL_BLOCK:
            if (node->typed.implBlock.methods) {
                for (int i = 0; i < node->typed.implBlock.methodCount; i++) {
                    free_typed_ast_node(node->typed.implBlock.methods[i]);
                }
                free(node->typed.implBlock.methods);
            }
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
        copy->errorMessage = orus_strdup(node->errorMessage);
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
        case NODE_ARRAY_ASSIGN:
            return validate_typed_ast(root->typed.arrayAssign.target) &&
                   validate_typed_ast(root->typed.arrayAssign.value);
        case NODE_ARRAY_SLICE:
            return validate_typed_ast(root->typed.arraySlice.array) &&
                   validate_typed_ast(root->typed.arraySlice.start) &&
                   validate_typed_ast(root->typed.arraySlice.end);
        case NODE_STRUCT_DECL:
            if (root->typed.structDecl.fields) {
                for (int i = 0; i < root->typed.structDecl.fieldCount; i++) {
                    if (root->typed.structDecl.fields[i].typeAnnotation &&
                        !validate_typed_ast(root->typed.structDecl.fields[i].typeAnnotation)) {
                        return false;
                    }
                    if (root->typed.structDecl.fields[i].defaultValue &&
                        !validate_typed_ast(root->typed.structDecl.fields[i].defaultValue)) {
                        return false;
                    }
                }
            }
            break;
        case NODE_IMPL_BLOCK:
            if (root->typed.implBlock.methods) {
                for (int i = 0; i < root->typed.implBlock.methodCount; i++) {
                    if (!validate_typed_ast(root->typed.implBlock.methods[i])) {
                        return false;
                    }
                }
            }
            break;
        case NODE_INDEX_ACCESS:
            return validate_typed_ast(root->typed.indexAccess.array) &&
                   validate_typed_ast(root->typed.indexAccess.index);
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
        case NODE_ARRAY_LITERAL:
            nodeTypeStr = "ArrayLiteral";
            break;
        case NODE_INDEX_ACCESS:
            nodeTypeStr = "IndexAccess";
            break;
        case NODE_BINARY:
            nodeTypeStr = "Binary";
            break;
        case NODE_ASSIGN:
            nodeTypeStr = "Assign";
            break;
        case NODE_ARRAY_ASSIGN:
            nodeTypeStr = "ArrayAssign";
            break;
        case NODE_ARRAY_SLICE:
            nodeTypeStr = "ArraySlice";
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
        case NODE_STRUCT_DECL:
            nodeTypeStr = "StructDecl";
            break;
        case NODE_IMPL_BLOCK:
            nodeTypeStr = "ImplBlock";
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
        case NODE_ARRAY_LITERAL:
            if (node->typed.arrayLiteral.elements) {
                for (int i = 0; i < node->typed.arrayLiteral.count; i++) {
                    print_typed_ast(node->typed.arrayLiteral.elements[i], indent + 1);
                }
            }
            break;
        case NODE_INDEX_ACCESS:
            print_typed_ast(node->typed.indexAccess.array, indent + 1);
            print_typed_ast(node->typed.indexAccess.index, indent + 1);
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
        case NODE_ARRAY_ASSIGN:
            print_typed_ast(node->typed.arrayAssign.target, indent + 1);
            print_typed_ast(node->typed.arrayAssign.value, indent + 1);
            break;
        case NODE_ARRAY_SLICE:
            print_typed_ast(node->typed.arraySlice.array, indent + 1);
            print_typed_ast(node->typed.arraySlice.start, indent + 1);
            print_typed_ast(node->typed.arraySlice.end, indent + 1);
            break;
        case NODE_STRUCT_DECL:
            if (node->typed.structDecl.fields) {
                for (int i = 0; i < node->typed.structDecl.fieldCount; i++) {
                    if (node->typed.structDecl.fields[i].typeAnnotation) {
                        print_typed_ast(node->typed.structDecl.fields[i].typeAnnotation,
                                        indent + 1);
                    }
                    if (node->typed.structDecl.fields[i].defaultValue) {
                        print_typed_ast(node->typed.structDecl.fields[i].defaultValue,
                                        indent + 1);
                    }
                }
            }
            break;
        case NODE_IMPL_BLOCK:
            if (node->typed.implBlock.methods) {
                for (int i = 0; i < node->typed.implBlock.methodCount; i++) {
                    print_typed_ast(node->typed.implBlock.methods[i], indent + 1);
                }
            }
            break;
        default:
            break;
    }
}

// Note: The generate_typed_ast function has been moved to type_inference.c
// to ensure proper integration with Algorithm W type inference.

