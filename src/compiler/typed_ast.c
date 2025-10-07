//  Orus Language Project
//  ---------------------------------------------------------------------------
//  File: src/compiler/typed_ast.c
//  Author: Jordy Orel KONDA
//  Copyright (c) 2024 Jordy Orel KONDA
//  License: MIT License (see LICENSE file in the project root)
//  Description: Builds and manipulates typed AST nodes during semantic analysis.


//  This file implements the typed AST data structure that bridges
//  the gap between HM type inference and bytecode generation.
//  It provides functions to create, manipulate, and visualize typed AST nodes.


#include "compiler/typed_ast.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "internal/strutil.h"

#include "runtime/memory.h"
#include "type/type.h"

typedef struct {
    TypedASTNode** nodes;
    size_t count;
    size_t capacity;
} TypedASTRegistry;

static TypedASTRegistry g_typed_registry = {NULL, 0, 0};

static bool typed_ast_registry_ensure_capacity(size_t required) {
    if (g_typed_registry.capacity >= required) {
        return true;
    }

    size_t new_capacity = g_typed_registry.capacity == 0 ? 64 : g_typed_registry.capacity;
    while (new_capacity < required) {
        new_capacity *= 2;
    }

    TypedASTNode** resized = realloc(g_typed_registry.nodes, new_capacity * sizeof(TypedASTNode*));
    if (!resized) {
        return false;
    }

    g_typed_registry.nodes = resized;
    g_typed_registry.capacity = new_capacity;
    return true;
}

static void typed_ast_registry_register(TypedASTNode* node) {
    if (!node) {
        return;
    }

    if (!typed_ast_registry_ensure_capacity(g_typed_registry.count + 1)) {
        return;  // Allocation failure: allow leak rather than crash
    }

    g_typed_registry.nodes[g_typed_registry.count++] = node;
}

static void typed_ast_registry_unregister(TypedASTNode* node) {
    if (!node || g_typed_registry.count == 0 || !g_typed_registry.nodes) {
        return;
    }

    for (size_t i = 0; i < g_typed_registry.count; ++i) {
        if (g_typed_registry.nodes[i] == node) {
            g_typed_registry.count--;
            g_typed_registry.nodes[i] = g_typed_registry.nodes[g_typed_registry.count];
            g_typed_registry.nodes[g_typed_registry.count] = NULL;
            return;
        }
    }
}

size_t typed_ast_registry_checkpoint(void) {
    return g_typed_registry.count;
}

void typed_ast_release_from_checkpoint(size_t checkpoint) {
    if (!g_typed_registry.nodes) {
        return;
    }

    if (checkpoint > g_typed_registry.count) {
        checkpoint = 0;
    }

    while (g_typed_registry.count > checkpoint) {
        size_t index = g_typed_registry.count - 1;
        TypedASTNode* node = g_typed_registry.nodes[index];
        if (!node) {
            g_typed_registry.count--;
            continue;
        }
        free_typed_ast_node(node);
    }

    if (checkpoint == 0 && g_typed_registry.count == 0) {
        free(g_typed_registry.nodes);
        g_typed_registry.nodes = NULL;
        g_typed_registry.capacity = 0;
    }
}

void typed_ast_release_orphans(void) {
    typed_ast_release_from_checkpoint(0);
}

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

    typed_ast_registry_register(typed);

    // Initialize union based on node type
    switch (original->type) {
        case NODE_PROGRAM:
            typed->typed.program.declarations = NULL;
            typed->typed.program.count = 0;
            typed->typed.program.moduleName = original->program.moduleName;
            break;
        case NODE_VAR_DECL:
            typed->typed.varDecl.initializer = NULL;
            typed->typed.varDecl.typeAnnotation = NULL;
            typed->typed.varDecl.isGlobal = original->varDecl.isGlobal;
            typed->typed.varDecl.isPublic = original->varDecl.isPublic;
            break;
        case NODE_IMPORT:
            typed->typed.import.moduleName = original->import.moduleName;
            typed->typed.import.moduleAlias = original->import.moduleAlias;
            typed->typed.import.symbolCount = original->import.symbolCount;
            typed->typed.import.importAll = original->import.importAll;
            typed->typed.import.importModule = original->import.importModule;
            typed->typed.import.symbols = NULL;
            if (original->import.symbolCount > 0 && original->import.symbols) {
                typed->typed.import.symbols = malloc(sizeof(TypedImportSymbol) * (size_t)original->import.symbolCount);
                if (typed->typed.import.symbols) {
                    for (int i = 0; i < original->import.symbolCount; i++) {
                        typed->typed.import.symbols[i].name = original->import.symbols[i].name;
                        typed->typed.import.symbols[i].alias = original->import.symbols[i].alias;
                    }
                }
            }
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
            typed->typed.forRange.varName = NULL;
            typed->typed.forRange.start = NULL;
            typed->typed.forRange.end = NULL;
            typed->typed.forRange.step = NULL;
            typed->typed.forRange.body = NULL;
            typed->typed.forRange.label = NULL;
            break;
        case NODE_FOR_ITER:
            typed->typed.forIter.varName = NULL;
            typed->typed.forIter.iterable = NULL;
            typed->typed.forIter.body = NULL;
            typed->typed.forIter.label = NULL;
            break;
        case NODE_TRY:
            typed->typed.tryStmt.tryBlock = NULL;
            typed->typed.tryStmt.catchBlock = NULL;
            typed->typed.tryStmt.catchVarName = NULL;
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
            typed->typed.function.isPublic = original->function.isPublic;
            typed->typed.function.isMethod = original->function.isMethod;
            typed->typed.function.isInstanceMethod = original->function.isInstanceMethod;
            typed->typed.function.methodStructName = original->function.methodStructName;
            typed->typed.function.isCoreIntrinsic = original->function.hasCoreIntrinsic;
            typed->typed.function.coreIntrinsicSymbol = original->function.coreIntrinsicSymbol;
            typed->typed.function.intrinsicSignature = NULL;
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
        case NODE_ARRAY_FILL:
            typed->typed.arrayFill.value = NULL;
            typed->typed.arrayFill.lengthExpr = NULL;
            typed->typed.arrayFill.resolvedLength =
                original->arrayFill.hasResolvedLength ? original->arrayFill.resolvedLength : 0;
            break;
        case NODE_INDEX_ACCESS:
            typed->typed.indexAccess.array = NULL;
            typed->typed.indexAccess.index = NULL;
            typed->typed.indexAccess.isStringIndex = original->indexAccess.isStringIndex;
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
        case NODE_STRUCT_LITERAL:
            typed->typed.structLiteral.structName = original->structLiteral.structName;
            typed->typed.structLiteral.moduleAlias = original->structLiteral.moduleAlias;
            typed->typed.structLiteral.resolvedModuleName = original->structLiteral.resolvedModuleName;
            typed->typed.structLiteral.fields = original->structLiteral.fields;
            typed->typed.structLiteral.fieldCount = original->structLiteral.fieldCount;
            typed->typed.structLiteral.values = NULL;
            break;
        case NODE_MEMBER_ACCESS:
            typed->typed.member.object = NULL;
            typed->typed.member.member = original->member.member;
            typed->typed.member.isMethod = original->member.isMethod;
            typed->typed.member.isInstanceMethod = original->member.isInstanceMethod;
            typed->typed.member.resolvesToEnum = original->member.resolvesToEnum;
            typed->typed.member.resolvesToEnumVariant = original->member.resolvesToEnumVariant;
            typed->typed.member.enumVariantIndex = original->member.enumVariantIndex;
            typed->typed.member.enumVariantArity = original->member.enumVariantArity;
            typed->typed.member.enumTypeName = original->member.enumTypeName;
            typed->typed.member.resolvesToModule = original->member.resolvesToModule;
            typed->typed.member.moduleName = original->member.moduleName;
            typed->typed.member.moduleAliasBinding = original->member.moduleAliasBinding;
            typed->typed.member.moduleExportKind = original->member.moduleExportKind;
            typed->typed.member.moduleRegisterIndex = original->member.moduleRegisterIndex;
            break;
        case NODE_MEMBER_ASSIGN:
            typed->typed.memberAssign.target = NULL;
            typed->typed.memberAssign.value = NULL;
            break;
        case NODE_ENUM_DECL:
            typed->typed.enumDecl.name = original->enumDecl.name;
            typed->typed.enumDecl.isPublic = original->enumDecl.isPublic;
            typed->typed.enumDecl.variants = NULL;
            typed->typed.enumDecl.variantCount = 0;
            typed->typed.enumDecl.genericParams = (const char**)original->enumDecl.genericParams;
            typed->typed.enumDecl.genericParamCount = original->enumDecl.genericParamCount;
            break;
        case NODE_ENUM_MATCH_TEST:
            typed->typed.enumMatchTest.value = NULL;
            typed->typed.enumMatchTest.enumTypeName = original->enumMatchTest.enumTypeName;
            typed->typed.enumMatchTest.variantName = original->enumMatchTest.variantName;
            typed->typed.enumMatchTest.variantIndex = original->enumMatchTest.variantIndex;
            typed->typed.enumMatchTest.expectedPayloadCount = original->enumMatchTest.expectedPayloadCount;
            break;
        case NODE_ENUM_PAYLOAD:
            typed->typed.enumPayload.value = NULL;
            typed->typed.enumPayload.enumTypeName = original->enumPayload.enumTypeName;
            typed->typed.enumPayload.variantName = original->enumPayload.variantName;
            typed->typed.enumPayload.variantIndex = original->enumPayload.variantIndex;
            typed->typed.enumPayload.fieldIndex = original->enumPayload.fieldIndex;
            break;
        case NODE_ENUM_MATCH_CHECK:
            typed->typed.enumMatchCheck.value = NULL;
            typed->typed.enumMatchCheck.enumTypeName = original->enumMatchCheck.enumTypeName;
            typed->typed.enumMatchCheck.variantNames = NULL;
            typed->typed.enumMatchCheck.variantCount = original->enumMatchCheck.variantCount;
            typed->typed.enumMatchCheck.hasWildcard = original->enumMatchCheck.hasWildcard;
            break;
        case NODE_MATCH_EXPRESSION:
            typed->typed.matchExpr.subject = NULL;
            typed->typed.matchExpr.tempName = original->matchExpr.tempName;
            typed->typed.matchExpr.arms = NULL;
            typed->typed.matchExpr.armCount = original->matchExpr.armCount;
            typed->typed.matchExpr.hasWildcard = original->matchExpr.hasWildcard;
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

    typed_ast_registry_unregister(node);

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
            if (node->typed.forRange.varName) {
                free(node->typed.forRange.varName);
            }
            free_typed_ast_node(node->typed.forRange.start);
            free_typed_ast_node(node->typed.forRange.end);
            free_typed_ast_node(node->typed.forRange.step);
            free_typed_ast_node(node->typed.forRange.body);
            if (node->typed.forRange.label) {
                free(node->typed.forRange.label);
            }
            break;
        case NODE_FOR_ITER:
            if (node->typed.forIter.varName) {
                free(node->typed.forIter.varName);
            }
            free_typed_ast_node(node->typed.forIter.iterable);
            free_typed_ast_node(node->typed.forIter.body);
            if (node->typed.forIter.label) {
                free(node->typed.forIter.label);
            }
            break;
        case NODE_TRY:
            free_typed_ast_node(node->typed.tryStmt.tryBlock);
            free_typed_ast_node(node->typed.tryStmt.catchBlock);
            if (node->typed.tryStmt.catchVarName) {
                free(node->typed.tryStmt.catchVarName);
            }
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
        case NODE_ARRAY_FILL:
            free_typed_ast_node(node->typed.arrayFill.value);
            free_typed_ast_node(node->typed.arrayFill.lengthExpr);
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
        case NODE_STRUCT_LITERAL:
            if (node->typed.structLiteral.values) {
                for (int i = 0; i < node->typed.structLiteral.fieldCount; i++) {
                    free_typed_ast_node(node->typed.structLiteral.values[i]);
                }
                free(node->typed.structLiteral.values);
            }
            break;
        case NODE_MEMBER_ACCESS:
            free_typed_ast_node(node->typed.member.object);
            break;
        case NODE_MEMBER_ASSIGN:
            free_typed_ast_node(node->typed.memberAssign.target);
            free_typed_ast_node(node->typed.memberAssign.value);
            break;
        case NODE_ENUM_DECL:
            if (node->typed.enumDecl.variants) {
                for (int i = 0; i < node->typed.enumDecl.variantCount; i++) {
                    if (node->typed.enumDecl.variants[i].fields) {
                        for (int j = 0; j < node->typed.enumDecl.variants[i].fieldCount; j++) {
                            free_typed_ast_node(node->typed.enumDecl.variants[i].fields[j].typeAnnotation);
                        }
                        free(node->typed.enumDecl.variants[i].fields);
                    }
                }
                free(node->typed.enumDecl.variants);
            }
            break;
        case NODE_ENUM_MATCH_TEST:
            free_typed_ast_node(node->typed.enumMatchTest.value);
            break;
        case NODE_ENUM_PAYLOAD:
            free_typed_ast_node(node->typed.enumPayload.value);
            break;
        case NODE_ENUM_MATCH_CHECK:
            free_typed_ast_node(node->typed.enumMatchCheck.value);
            if (node->typed.enumMatchCheck.variantNames) {
                free((void*)node->typed.enumMatchCheck.variantNames);
            }
            break;
        case NODE_IMPORT:
            if (node->typed.import.symbols) {
                free(node->typed.import.symbols);
            }
            break;
        case NODE_MATCH_EXPRESSION:
            free_typed_ast_node(node->typed.matchExpr.subject);
            if (node->typed.matchExpr.arms) {
                for (int i = 0; i < node->typed.matchExpr.armCount; i++) {
                    TypedMatchArm* arm = &node->typed.matchExpr.arms[i];
                    free_typed_ast_node(arm->valuePattern);
                    free_typed_ast_node(arm->body);
                    free_typed_ast_node(arm->condition);
                    if (arm->payloadAccesses) {
                        for (int j = 0; j < arm->payloadCount; j++) {
                            free_typed_ast_node(arm->payloadAccesses[j]);
                        }
                        free(arm->payloadAccesses);
                    }
                }
                free(node->typed.matchExpr.arms);
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

    if (node->original->type == NODE_MEMBER_ACCESS) {
        copy->typed.member.resolvesToEnum = node->typed.member.resolvesToEnum;
        copy->typed.member.resolvesToEnumVariant = node->typed.member.resolvesToEnumVariant;
        copy->typed.member.enumVariantIndex = node->typed.member.enumVariantIndex;
        copy->typed.member.enumVariantArity = node->typed.member.enumVariantArity;
        copy->typed.member.enumTypeName = node->typed.member.enumTypeName;
        copy->typed.member.resolvesToModule = node->typed.member.resolvesToModule;
        copy->typed.member.moduleName = node->typed.member.moduleName;
        copy->typed.member.moduleAliasBinding = node->typed.member.moduleAliasBinding;
        copy->typed.member.moduleExportKind = node->typed.member.moduleExportKind;
        copy->typed.member.moduleRegisterIndex = node->typed.member.moduleRegisterIndex;
    }

    return copy;
}

static bool visit_child(TypedASTNode* child, const TypedASTVisitor* visitor,
                        void* user_data);
static bool visit_child_array(TypedASTNode** children, int count,
                              const TypedASTVisitor* visitor,
                              void* user_data);
static bool visit_struct_fields(TypedStructField* fields, int count,
                                const TypedASTVisitor* visitor,
                                void* user_data);
static bool visit_enum_variants(TypedEnumVariant* variants, int count,
                                const TypedASTVisitor* visitor,
                                void* user_data);
static bool visit_match_arms(TypedMatchArm* arms, int count,
                             const TypedASTVisitor* visitor,
                             void* user_data);

static bool visit_child(TypedASTNode* child, const TypedASTVisitor* visitor,
                        void* user_data) {
    if (!child) {
        return true;
    }
    return typed_ast_visit(child, visitor, user_data);
}

static bool visit_child_array(TypedASTNode** children, int count,
                              const TypedASTVisitor* visitor,
                              void* user_data) {
    if (!children || count <= 0) {
        return true;
    }

    for (int i = 0; i < count; i++) {
        if (!visit_child(children[i], visitor, user_data)) {
            return false;
        }
    }

    return true;
}

static bool visit_struct_fields(TypedStructField* fields, int count,
                                const TypedASTVisitor* visitor,
                                void* user_data) {
    if (!fields || count <= 0) {
        return true;
    }

    for (int i = 0; i < count; i++) {
        if (!visit_child(fields[i].typeAnnotation, visitor, user_data)) {
            return false;
        }
        if (!visit_child(fields[i].defaultValue, visitor, user_data)) {
            return false;
        }
    }

    return true;
}

static bool visit_enum_variants(TypedEnumVariant* variants, int count,
                                const TypedASTVisitor* visitor,
                                void* user_data) {
    if (!variants || count <= 0) {
        return true;
    }

    for (int i = 0; i < count; i++) {
        TypedEnumVariant* variant = &variants[i];
        if (!variant->fields || variant->fieldCount <= 0) {
            continue;
        }

        for (int j = 0; j < variant->fieldCount; j++) {
            if (!visit_child(variant->fields[j].typeAnnotation, visitor,
                             user_data)) {
                return false;
            }
        }
    }

    return true;
}

static bool visit_match_arms(TypedMatchArm* arms, int count,
                             const TypedASTVisitor* visitor,
                             void* user_data) {
    if (!arms || count <= 0) {
        return true;
    }

    for (int i = 0; i < count; i++) {
        TypedMatchArm* arm = &arms[i];
        if (!visit_child(arm->valuePattern, visitor, user_data)) {
            return false;
        }
        if (!visit_child(arm->body, visitor, user_data)) {
            return false;
        }
        if (!visit_child(arm->condition, visitor, user_data)) {
            return false;
        }
        if (!visit_child_array(arm->payloadAccesses, arm->payloadCount,
                               visitor, user_data)) {
            return false;
        }
    }

    return true;
}

static bool typed_ast_visit_children(TypedASTNode* node,
                                     const TypedASTVisitor* visitor,
                                     void* user_data) {
    if (!node || !node->original) {
        return true;
    }

    switch (node->original->type) {
        case NODE_PROGRAM:
            return visit_child_array(node->typed.program.declarations,
                                     node->typed.program.count, visitor,
                                     user_data);
        case NODE_VAR_DECL:
            if (!visit_child(node->typed.varDecl.initializer, visitor,
                             user_data)) {
                return false;
            }
            return visit_child(node->typed.varDecl.typeAnnotation, visitor,
                               user_data);
        case NODE_BINARY:
            if (!visit_child(node->typed.binary.left, visitor, user_data)) {
                return false;
            }
            return visit_child(node->typed.binary.right, visitor, user_data);
        case NODE_ASSIGN:
            return visit_child(node->typed.assign.value, visitor, user_data);
        case NODE_PRINT:
            return visit_child_array(node->typed.print.values,
                                     node->typed.print.count, visitor,
                                     user_data);
        case NODE_IF:
            if (!visit_child(node->typed.ifStmt.condition, visitor,
                             user_data)) {
                return false;
            }
            if (!visit_child(node->typed.ifStmt.thenBranch, visitor,
                             user_data)) {
                return false;
            }
            return visit_child(node->typed.ifStmt.elseBranch, visitor,
                               user_data);
        case NODE_WHILE:
            if (!visit_child(node->typed.whileStmt.condition, visitor,
                             user_data)) {
                return false;
            }
            return visit_child(node->typed.whileStmt.body, visitor,
                               user_data);
        case NODE_FOR_RANGE:
            if (!visit_child(node->typed.forRange.start, visitor,
                             user_data)) {
                return false;
            }
            if (!visit_child(node->typed.forRange.end, visitor, user_data)) {
                return false;
            }
            if (!visit_child(node->typed.forRange.step, visitor, user_data)) {
                return false;
            }
            return visit_child(node->typed.forRange.body, visitor,
                               user_data);
        case NODE_FOR_ITER:
            if (!visit_child(node->typed.forIter.iterable, visitor,
                             user_data)) {
                return false;
            }
            return visit_child(node->typed.forIter.body, visitor, user_data);
        case NODE_TRY:
            if (!visit_child(node->typed.tryStmt.tryBlock, visitor,
                             user_data)) {
                return false;
            }
            return visit_child(node->typed.tryStmt.catchBlock, visitor,
                               user_data);
        case NODE_BLOCK:
            return visit_child_array(node->typed.block.statements,
                                     node->typed.block.count, visitor,
                                     user_data);
        case NODE_TERNARY:
            if (!visit_child(node->typed.ternary.condition, visitor,
                             user_data)) {
                return false;
            }
            if (!visit_child(node->typed.ternary.trueExpr, visitor,
                             user_data)) {
                return false;
            }
            return visit_child(node->typed.ternary.falseExpr, visitor,
                               user_data);
        case NODE_UNARY:
            return visit_child(node->typed.unary.operand, visitor, user_data);
        case NODE_FUNCTION:
            if (!visit_child(node->typed.function.returnType, visitor,
                             user_data)) {
                return false;
            }
            return visit_child(node->typed.function.body, visitor, user_data);
        case NODE_CALL:
            if (!visit_child(node->typed.call.callee, visitor, user_data)) {
                return false;
            }
            return visit_child_array(node->typed.call.args,
                                     node->typed.call.argCount, visitor,
                                     user_data);
        case NODE_ARRAY_LITERAL:
            return visit_child_array(node->typed.arrayLiteral.elements,
                                     node->typed.arrayLiteral.count, visitor,
                                     user_data);
        case NODE_ARRAY_FILL:
            if (!visit_child(node->typed.arrayFill.value, visitor,
                             user_data)) {
                return false;
            }
            return visit_child(node->typed.arrayFill.lengthExpr, visitor,
                               user_data);
        case NODE_INDEX_ACCESS:
            if (!visit_child(node->typed.indexAccess.array, visitor,
                             user_data)) {
                return false;
            }
            return visit_child(node->typed.indexAccess.index, visitor,
                               user_data);
        case NODE_RETURN:
            return visit_child(node->typed.returnStmt.value, visitor,
                               user_data);
        case NODE_CAST:
            if (!visit_child(node->typed.cast.expression, visitor,
                             user_data)) {
                return false;
            }
            return visit_child(node->typed.cast.targetType, visitor,
                               user_data);
        case NODE_ARRAY_ASSIGN:
            if (!visit_child(node->typed.arrayAssign.target, visitor,
                             user_data)) {
                return false;
            }
            return visit_child(node->typed.arrayAssign.value, visitor,
                               user_data);
        case NODE_ARRAY_SLICE:
            if (!visit_child(node->typed.arraySlice.array, visitor,
                             user_data)) {
                return false;
            }
            if (!visit_child(node->typed.arraySlice.start, visitor,
                             user_data)) {
                return false;
            }
            return visit_child(node->typed.arraySlice.end, visitor, user_data);
        case NODE_STRUCT_DECL:
            return visit_struct_fields(node->typed.structDecl.fields,
                                       node->typed.structDecl.fieldCount,
                                       visitor, user_data);
        case NODE_IMPL_BLOCK:
            return visit_child_array(node->typed.implBlock.methods,
                                     node->typed.implBlock.methodCount,
                                     visitor, user_data);
        case NODE_STRUCT_LITERAL:
            return visit_child_array(node->typed.structLiteral.values,
                                     node->typed.structLiteral.fieldCount,
                                     visitor, user_data);
        case NODE_MEMBER_ACCESS:
            return visit_child(node->typed.member.object, visitor, user_data);
        case NODE_MEMBER_ASSIGN:
            if (!visit_child(node->typed.memberAssign.target, visitor,
                             user_data)) {
                return false;
            }
            return visit_child(node->typed.memberAssign.value, visitor,
                               user_data);
        case NODE_ENUM_DECL:
            return visit_enum_variants(node->typed.enumDecl.variants,
                                       node->typed.enumDecl.variantCount,
                                       visitor, user_data);
        case NODE_ENUM_MATCH_TEST:
            return visit_child(node->typed.enumMatchTest.value, visitor,
                               user_data);
        case NODE_ENUM_PAYLOAD:
            return visit_child(node->typed.enumPayload.value, visitor,
                               user_data);
        case NODE_ENUM_MATCH_CHECK:
            return visit_child(node->typed.enumMatchCheck.value, visitor,
                               user_data);
        case NODE_MATCH_EXPRESSION:
            if (!visit_child(node->typed.matchExpr.subject, visitor,
                             user_data)) {
                return false;
            }
            return visit_match_arms(node->typed.matchExpr.arms,
                                    node->typed.matchExpr.armCount, visitor,
                                    user_data);
        default:
            return true;
    }
}

bool typed_ast_visit(TypedASTNode* root, const TypedASTVisitor* visitor,
                     void* user_data) {
    if (!root) {
        return true;
    }

    bool descend = true;
    if (visitor && visitor->pre) {
        descend = visitor->pre(root, user_data);
    }

    bool success = true;
    if (descend) {
        success = typed_ast_visit_children(root, visitor, user_data);
    }

    if (visitor && visitor->post) {
        bool post_success = visitor->post(root, user_data);
        success = success && post_success;
    }

    return success;
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
            if (!validate_typed_ast(root->typed.arraySlice.array)) {
                return false;
            }
            if (root->typed.arraySlice.start &&
                !validate_typed_ast(root->typed.arraySlice.start)) {
                return false;
            }
            if (root->typed.arraySlice.end &&
                !validate_typed_ast(root->typed.arraySlice.end)) {
                return false;
            }
            return true;
        case NODE_MEMBER_ASSIGN:
            return validate_typed_ast(root->typed.memberAssign.target) &&
                   validate_typed_ast(root->typed.memberAssign.value);
        case NODE_MEMBER_ACCESS:
            return validate_typed_ast(root->typed.member.object);
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
        case NODE_STRUCT_LITERAL:
            if (root->typed.structLiteral.values) {
                for (int i = 0; i < root->typed.structLiteral.fieldCount; i++) {
                    if (!validate_typed_ast(root->typed.structLiteral.values[i])) {
                        return false;
                    }
                }
            }
            break;
        case NODE_ENUM_DECL:
            if (root->typed.enumDecl.variants) {
                for (int i = 0; i < root->typed.enumDecl.variantCount; i++) {
                    if (root->typed.enumDecl.variants[i].fields) {
                        for (int j = 0; j < root->typed.enumDecl.variants[i].fieldCount; j++) {
                            if (root->typed.enumDecl.variants[i].fields[j].typeAnnotation &&
                                !validate_typed_ast(root->typed.enumDecl.variants[i].fields[j].typeAnnotation)) {
                                return false;
                            }
                        }
                    }
                }
            }
            break;
        case NODE_ENUM_MATCH_TEST:
            return validate_typed_ast(root->typed.enumMatchTest.value);
        case NODE_ENUM_PAYLOAD:
            return validate_typed_ast(root->typed.enumPayload.value);
        case NODE_ENUM_MATCH_CHECK:
            return validate_typed_ast(root->typed.enumMatchCheck.value);
        case NODE_IMPORT:
            return true;
        case NODE_MATCH_EXPRESSION:
            if (root->typed.matchExpr.subject &&
                !validate_typed_ast(root->typed.matchExpr.subject)) {
                return false;
            }
            if (root->typed.matchExpr.arms) {
                for (int i = 0; i < root->typed.matchExpr.armCount; i++) {
                    TypedMatchArm* arm = &root->typed.matchExpr.arms[i];
                    if (arm->valuePattern && !validate_typed_ast(arm->valuePattern)) {
                        return false;
                    }
                    if (arm->condition && !validate_typed_ast(arm->condition)) {
                        return false;
                    }
                    if (arm->payloadAccesses) {
                        for (int j = 0; j < arm->payloadCount; j++) {
                            if (arm->payloadAccesses[j] &&
                                !validate_typed_ast(arm->payloadAccesses[j])) {
                                return false;
                            }
                        }
                    }
                    if (arm->body && !validate_typed_ast(arm->body)) {
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
            break;
        case NODE_TRY:
            if (root->typed.tryStmt.tryBlock &&
                !validate_typed_ast(root->typed.tryStmt.tryBlock)) {
                return false;
            }
            if (root->typed.tryStmt.catchBlock &&
                !validate_typed_ast(root->typed.tryStmt.catchBlock)) {
                return false;
            }
            break;
        case NODE_PASS:
            // No children to validate
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
        case TYPE_STRUCT:
            return "struct";
        case TYPE_ENUM:
            return "enum";
        case TYPE_UNKNOWN:
            return "unknown";
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
        case NODE_TRY:
            nodeTypeStr = "Try";
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
        case NODE_STRUCT_LITERAL:
            nodeTypeStr = "StructLiteral";
            break;
        case NODE_ENUM_DECL:
            nodeTypeStr = "EnumDecl";
            break;
        case NODE_IMPL_BLOCK:
            nodeTypeStr = "ImplBlock";
            break;
        case NODE_MEMBER_ACCESS:
            nodeTypeStr = "MemberAccess";
            break;
        case NODE_MEMBER_ASSIGN:
            nodeTypeStr = "MemberAssign";
            break;
        case NODE_BREAK:
            nodeTypeStr = "Break";
            break;
        case NODE_CONTINUE:
            nodeTypeStr = "Continue";
            break;
        case NODE_PASS:
            nodeTypeStr = "Pass";
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

    if (node->original->type == NODE_MEMBER_ACCESS) {
        const char* memberName = node->typed.member.member ? node->typed.member.member : "<unknown>";
        printf(" [member=%s", memberName);
        if (node->typed.member.isMethod) {
            printf(" method%s", node->typed.member.isInstanceMethod ? "(instance)" : "(static)");
        }
        if (node->typed.member.resolvesToEnumVariant) {
            const char* enumName = node->typed.member.enumTypeName ? node->typed.member.enumTypeName : "<anon-enum>";
            printf(" enum=%s variant=%d arity=%d", enumName, node->typed.member.enumVariantIndex,
                   node->typed.member.enumVariantArity);
        } else if (node->typed.member.resolvesToEnum) {
            const char* enumName = node->typed.member.enumTypeName ? node->typed.member.enumTypeName : "<anon-enum>";
            printf(" enum=%s", enumName);
        }
        if (node->typed.member.resolvesToModule) {
            const char* moduleName = node->typed.member.moduleName ? node->typed.member.moduleName : "<module>";
            printf(" module=%s", moduleName);
            if (node->typed.member.moduleAliasBinding) {
                printf(" alias=%s", node->typed.member.moduleAliasBinding);
            }
            printf(" kind=%d", (int)node->typed.member.moduleExportKind);
        }
        printf("]");
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
        case NODE_TRY:
            if (node->typed.tryStmt.tryBlock) {
                print_typed_ast(node->typed.tryStmt.tryBlock, indent + 1);
            }
            if (node->typed.tryStmt.catchBlock) {
                print_typed_ast(node->typed.tryStmt.catchBlock, indent + 1);
            }
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
        case NODE_STRUCT_LITERAL:
            if (node->typed.structLiteral.values) {
                for (int i = 0; i < node->typed.structLiteral.fieldCount; i++) {
                    TypedASTNode* fieldValue = node->typed.structLiteral.values[i];
                    if (!fieldValue) {
                        continue;
                    }
                    if (node->typed.structLiteral.fields &&
                        node->typed.structLiteral.fields[i].name) {
                        for (int j = 0; j < indent + 1; j++) {
                            printf("  ");
                        }
                        printf("Field %s:\n", node->typed.structLiteral.fields[i].name);
                        print_typed_ast(fieldValue, indent + 2);
                    } else {
                        print_typed_ast(fieldValue, indent + 1);
                    }
                }
            }
            break;
        case NODE_ENUM_DECL:
            if (node->typed.enumDecl.variants) {
                for (int i = 0; i < node->typed.enumDecl.variantCount; i++) {
                    if (node->typed.enumDecl.variants[i].fields) {
                        for (int j = 0; j < node->typed.enumDecl.variants[i].fieldCount; j++) {
                            if (node->typed.enumDecl.variants[i].fields[j].typeAnnotation) {
                                for (int k = 0; k < indent + 1; k++) {
                                    printf("  ");
                                }
                                const char* fieldName = node->typed.enumDecl.variants[i].fields[j].name;
                                if (fieldName) {
                                    printf("Variant %s field %s:\n", node->typed.enumDecl.variants[i].name,
                                           fieldName);
                                } else {
                                    printf("Variant %s field %d:\n", node->typed.enumDecl.variants[i].name,
                                           j);
                                }
                                print_typed_ast(node->typed.enumDecl.variants[i].fields[j].typeAnnotation,
                                                indent + 2);
                            }
                        }
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
        case NODE_MEMBER_ACCESS:
            if (node->typed.member.object) {
                print_typed_ast(node->typed.member.object, indent + 1);
            }
            break;
        case NODE_MEMBER_ASSIGN:
            if (node->typed.memberAssign.target) {
                print_typed_ast(node->typed.memberAssign.target, indent + 1);
            }
            if (node->typed.memberAssign.value) {
                print_typed_ast(node->typed.memberAssign.value, indent + 1);
            }
            break;
        default:
            break;
    }
}

// Note: The generate_typed_ast function has been moved to type_inference.c
// to ensure proper integration with Algorithm W type inference.

