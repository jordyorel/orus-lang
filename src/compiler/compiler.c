#include "compiler.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../include/ast.h"
#include "../../include/chunk.h"
#include "../../include/debug.h"
#include "../../include/memory.h"
#include "../../include/modules.h"
#include "../../include/scanner.h"
#include "../../include/value.h"
#include "../../include/vm.h"

/**
 * @file compiler.c
 * @brief Main bytecode compiler implementation.
 *
 * This module walks the parsed AST, performs type checking and emits
 * bytecode for the Orus virtual machine.
 */
#include "error.h"
#include "lexer.h"

extern VM vm;

/**
 * Look up a struct type from its identifier token.
 *
 * @param token Token containing the struct name.
 * @return Pointer to the matching Type or NULL.
 */
static Type* findStructTypeToken(Token token) {
    char name[token.length + 1];
    memcpy(name, token.start, token.length);
    name[token.length] = '\0';
    return findStructType(name);
}

static bool tokenEquals(Token token, const char* str) {
    size_t len = strlen(str);
    return token.length == (int)len && strncmp(token.start, str, len) == 0;
}

static int tokenColumn(Compiler* compiler, Token* token) {
    const char* lineStart = token->start;
    while (lineStart > compiler->sourceCode && lineStart[-1] != '\n')
        lineStart--;
    return (int)(token->start - lineStart) + 1;
}

static int firstNonWhitespaceColumn(Compiler* compiler, int line) {
    if (!compiler->lineStarts || line <= 0 || line > compiler->lineCount)
        return 1;
    const char* start = compiler->lineStarts[line - 1];
    int column = 1;
    while (*start == ' ' || *start == '\t') {
        start++;
        column++;
    }
    return column;
}

static uint8_t findPrivateGlobal(const char* name, int length) {
    for (int i = 0; i < vm.variableCount; i++) {
        if (!vm.variableNames[i].name) continue;
        if (vm.variableNames[i].length == length &&
            strncmp(vm.variableNames[i].name->chars, name, length) == 0 &&
            vm.variableNames[i].name->chars[length] == '\0') {
            if (!vm.publicGlobals[i]) return (uint8_t)i;
        }
    }
    return UINT8_MAX;
}

static void generateCode(Compiler* compiler, ASTNode* node);
static void addBreakJump(Compiler* compiler, int jumpPos);
static void patchBreakJumps(Compiler* compiler);
static void addContinueJump(Compiler* compiler, int jumpPos);
static void patchContinueJumps(Compiler* compiler);
static void emitForLoop(Compiler* compiler, ASTNode* node);
void disassembleChunk(Chunk* chunk, const char* name);
static void predeclareFunction(Compiler* compiler, ASTNode* node);

static bool isNumericKind(TypeKind kind) {
    return kind == TYPE_I32 || kind == TYPE_I64 || kind == TYPE_U32 ||
           kind == TYPE_U64 || kind == TYPE_F64;
}

// Evaluate an AST node to an integer constant if possible.
// Returns true on success and stores the value in out.
static bool evaluateConstantInt(Compiler* compiler, ASTNode* node,
                                int64_t* out) {
    if (!node) return false;

    if (node->type == AST_LITERAL) {
        Value v = node->data.literal;
        if (IS_I32(v)) {
            *out = AS_I32(v);
            return true;
        }
        if (IS_I64(v)) {
            *out = AS_I64(v);
            return true;
        }
        if (IS_U32(v)) {
            *out = (int64_t)AS_U32(v);
            return true;
        }
        if (IS_U64(v)) {
            *out = (int64_t)AS_U64(v);
            return true;
        }
        return false;
    }

    if (node->type == AST_VARIABLE) {
        char name[node->data.variable.name.length + 1];
        memcpy(name, node->data.variable.name.start,
               node->data.variable.name.length);
        name[node->data.variable.name.length] = '\0';
        Symbol* sym = findSymbol(&compiler->symbols, name);
        if (!sym || !sym->isConst) return false;
        Value v = vm.globals[sym->index];
        if (IS_I32(v)) {
            *out = AS_I32(v);
            return true;
        }
        if (IS_I64(v)) {
            *out = AS_I64(v);
            return true;
        }
        if (IS_U32(v)) {
            *out = (int64_t)AS_U32(v);
            return true;
        }
        if (IS_U64(v)) {
            *out = (int64_t)AS_U64(v);
            return true;
        }
        return false;
    }

    return false;
}

// Determine if a binary operation on two constant integer nodes would overflow
// the 32-bit signed range. Only handles +, -, * operators.
static bool constantBinaryOverflows(Compiler* compiler, ASTNode* left,
                                    ASTNode* right, TokenType op) {
    int64_t a, b, r = 0;
    if (!evaluateConstantInt(compiler, left, &a) ||
        !evaluateConstantInt(compiler, right, &b)) {
        return false;
    }

    bool overflow = false;
    switch (op) {
        case TOKEN_PLUS:
            overflow = __builtin_add_overflow(a, b, &r);
            break;
        case TOKEN_MINUS:
            overflow = __builtin_sub_overflow(a, b, &r);
            break;
        case TOKEN_STAR:
            overflow = __builtin_mul_overflow(a, b, &r);
            break;
        default:
            return false;
    }

    if (!overflow) {
        overflow = (r > INT32_MAX) || (r < INT32_MIN);
    }
    return overflow;
}

static opCode conversionOp(TypeKind from, TypeKind to) {
    if (from == to) return OP_RETURN;  // unused placeholder when no conversion
    if (from == TYPE_I32 && to == TYPE_F64) return OP_I32_TO_F64;
    if (from == TYPE_U32 && to == TYPE_F64) return OP_U32_TO_F64;
    if (from == TYPE_I32 && to == TYPE_U32) return OP_I32_TO_U32;
    if (from == TYPE_U32 && to == TYPE_I32) return OP_U32_TO_I32;
    if (from == TYPE_I32 && to == TYPE_I64) return OP_I32_TO_I64;
    if (from == TYPE_U32 && to == TYPE_I64) return OP_U32_TO_I64;
    if (from == TYPE_I64 && to == TYPE_I32) return OP_I64_TO_I32;
    if (from == TYPE_I64 && to == TYPE_U32) return OP_I64_TO_U32;
    if (from == TYPE_I32 && to == TYPE_U64) return OP_I32_TO_U64;
    if (from == TYPE_U32 && to == TYPE_U64) return OP_U32_TO_U64;
    if (from == TYPE_U64 && to == TYPE_I32) return OP_U64_TO_I32;
    if (from == TYPE_U64 && to == TYPE_U32) return OP_U64_TO_U32;
    if (from == TYPE_U64 && to == TYPE_F64) return OP_U64_TO_F64;
    if (from == TYPE_F64 && to == TYPE_U64) return OP_F64_TO_U64;
    if (from == TYPE_I64 && to == TYPE_U64) return OP_I64_TO_U64;
    if (from == TYPE_U64 && to == TYPE_I64) return OP_U64_TO_I64;
    if (from == TYPE_I64 && to == TYPE_F64) return OP_I64_TO_F64;
    if (from == TYPE_F64 && to == TYPE_I64) return OP_F64_TO_I64;
    return OP_RETURN;  // indicates unsupported
}

static void deduceGenerics(Type* expected, Type* actual, ObjString** names,
                           Type** subs, int count) {
    if (!expected || !actual) return;

    if (expected->kind == TYPE_GENERIC) {
        for (int i = 0; i < count; i++) {
            if (names[i] && strcmp(expected->info.generic.name->chars,
                                   names[i]->chars) == 0) {
                if (actual->kind == TYPE_NIL) return;
                if (!subs[i]) subs[i] = actual;
                return;
            }
        }
        return;
    }

    if (expected->kind == TYPE_ARRAY && actual->kind == TYPE_ARRAY) {
        if (actual->info.array.elementType->kind == TYPE_NIL) return;
        deduceGenerics(expected->info.array.elementType,
                       actual->info.array.elementType, names, subs, count);
        return;
    }

    if (expected->kind != actual->kind) return;
    switch (expected->kind) {
        case TYPE_ARRAY:
            deduceGenerics(expected->info.array.elementType,
                           actual->info.array.elementType, names, subs, count);
            break;
        case TYPE_FUNCTION:
            for (int i = 0; i < expected->info.function.paramCount &&
                            i < actual->info.function.paramCount;
                 i++) {
                deduceGenerics(expected->info.function.paramTypes[i],
                               actual->info.function.paramTypes[i], names, subs,
                               count);
            }
            deduceGenerics(expected->info.function.returnType,
                           actual->info.function.returnType, names, subs,
                           count);
            break;
        case TYPE_STRUCT:
            if (expected->info.structure.fieldCount ==
                actual->info.structure.fieldCount) {
                for (int i = 0; i < expected->info.structure.fieldCount; i++) {
                    deduceGenerics(expected->info.structure.fields[i].type,
                                   actual->info.structure.fields[i].type, names,
                                   subs, count);
                }
            }
            break;
        default:
            break;
    }
}

// Lookup the constraint for a generic parameter by name
static GenericConstraint findConstraint(Compiler* compiler, ObjString* name) {
    for (int i = 0; i < compiler->genericCount; i++) {
        if (compiler->genericNames[i] &&
            strcmp(compiler->genericNames[i]->chars, name->chars) == 0) {
            return compiler->genericConstraints[i];
        }
    }
    return CONSTRAINT_NONE;
}

// Ensure a generic type meets the required constraint
static bool requireConstraint(Compiler* compiler, Type* type,
                              GenericConstraint needed, Token* token) {
    if (type->kind != TYPE_GENERIC) return true;
    GenericConstraint actual =
        findConstraint(compiler, type->info.generic.name);
    if (needed == CONSTRAINT_COMPARABLE && actual == CONSTRAINT_NUMERIC)
        return true;
    if (actual != needed) {
        const char* label =
            needed == CONSTRAINT_NUMERIC ? "Numeric" : "Comparable";
        char buf[96];
        snprintf(buf, sizeof(buf), "Generic parameter '%s' must satisfy %s",
                 type->info.generic.name->chars, label);
        emitTokenError(compiler, token, ERROR_GENERAL, buf);
        return false;
    }
    return true;
}

static void beginScope(Compiler* compiler) { compiler->scopeDepth++; }

static void endScope(Compiler* compiler) {
    removeSymbolsFromScope(&compiler->symbols, compiler->scopeDepth);
    if (compiler->scopeDepth > 0) compiler->scopeDepth--;
}

static void error(Compiler* compiler, const char* message) {
    emitSimpleError(compiler, ERROR_GENERAL, message);
}

static void errorFmt(Compiler* compiler, const char* format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    emitSimpleError(compiler, ERROR_GENERAL, buffer);
}

// Check if any return statement exists within a node tree
static bool containsReturn(ASTNode* node) {
    if (!node) return false;
    switch (node->type) {
        case AST_RETURN:
            return true;
        case AST_BLOCK: {
            ASTNode* stmt = node->data.block.statements;
            while (stmt) {
                if (containsReturn(stmt)) return true;
                stmt = stmt->next;
            }
            return false;
        }
        case AST_IF: {
            if (containsReturn(node->data.ifStmt.thenBranch)) return true;
            ASTNode* cond = node->data.ifStmt.elifBranches;
            while (cond) {
                if (containsReturn(cond)) return true;
                cond = cond->next;
            }
            if (node->data.ifStmt.elseBranch &&
                containsReturn(node->data.ifStmt.elseBranch))
                return true;
            return false;
        }
        case AST_TERNARY: {
            if (containsReturn(node->data.ternary.thenExpr)) return true;
            if (containsReturn(node->data.ternary.elseExpr)) return true;
            return false;
        }
        case AST_WHILE:
            return containsReturn(node->data.whileStmt.body);
        case AST_FOR:
            return containsReturn(node->data.forStmt.body);
        default:
            if (node->left && containsReturn(node->left)) return true;
            if (node->right && containsReturn(node->right)) return true;
            return false;
    }
}

// Determine if all code paths within the statement list return
static bool statementsAlwaysReturn(ASTNode* stmt);

static bool statementAlwaysReturns(ASTNode* node) {
    if (!node) return false;
    switch (node->type) {
        case AST_RETURN:
            return true;
        case AST_BLOCK:
            return statementsAlwaysReturn(node->data.block.statements);
        case AST_IF: {
            bool thenR = statementsAlwaysReturn(node->data.ifStmt.thenBranch);
            bool allElifR = true;
            ASTNode* branch = node->data.ifStmt.elifBranches;
            while (branch) {
                if (!statementsAlwaysReturn(branch)) allElifR = false;
                branch = branch->next;
            }
            bool elseR = node->data.ifStmt.elseBranch &&
                         statementsAlwaysReturn(node->data.ifStmt.elseBranch);
            return thenR && allElifR && elseR;
        }
        case AST_TERNARY:
            return false;
        default:
            return false;
    }
}

static bool statementsAlwaysReturn(ASTNode* stmt) {
    while (stmt) {
        if (statementAlwaysReturns(stmt)) return true;
        stmt = stmt->next;
    }
    return false;
}

static bool convertLiteralForDecl(ASTNode* init, Type* src, Type* dst) {
    if (!init || !src || !dst) return false;

    bool unaryMinus = false;
    ASTNode* lit = NULL;

    if (init->type == AST_LITERAL) {
        lit = init;
    } else if (init->type == AST_UNARY &&
               init->data.operation.operator.type == TOKEN_MINUS &&
               init->left && init->left->type == AST_LITERAL) {
        lit = init->left;
        unaryMinus = true;
    }

    if (!lit) return false;

    if (src->kind == TYPE_I32 && dst->kind == TYPE_U32) {
        if (IS_I32(lit->data.literal)) {
            int32_t v = AS_I32(lit->data.literal);
            lit->data.literal = U32_VAL((uint32_t)v);
            lit->valueType = dst;
            if (unaryMinus) init->valueType = dst;
            return true;
        }
    } else if (src->kind == TYPE_U32 && dst->kind == TYPE_I32) {
        if (IS_U32(lit->data.literal)) {
            uint32_t v = AS_U32(lit->data.literal);
            lit->data.literal = I32_VAL((int32_t)v);
            lit->valueType = dst;
            if (unaryMinus) init->valueType = dst;
            return true;
        }
    } else if (src->kind == TYPE_I32 && dst->kind == TYPE_I64) {
        if (IS_I32(lit->data.literal)) {
            int32_t v = AS_I32(lit->data.literal);
            lit->data.literal = I64_VAL((int64_t)v);
            lit->valueType = dst;
            if (unaryMinus) init->valueType = dst;
            return true;
        }
    } else if (src->kind == TYPE_I64 && dst->kind == TYPE_I32) {
        if (IS_I64(lit->data.literal)) {
            int64_t v = AS_I64(lit->data.literal);
            lit->data.literal = I32_VAL((int32_t)v);
            lit->valueType = dst;
            if (unaryMinus) init->valueType = dst;
            return true;
        }
    } else if (src->kind == TYPE_I64 && dst->kind == TYPE_U32) {
        if (IS_I64(lit->data.literal)) {
            int64_t v = AS_I64(lit->data.literal);
            lit->data.literal = U32_VAL((uint32_t)v);
            lit->valueType = dst;
            if (unaryMinus) init->valueType = dst;
            return true;
        }
    } else if (src->kind == TYPE_I32 && dst->kind == TYPE_U64) {
        if (IS_I32(lit->data.literal)) {
            int32_t v = AS_I32(lit->data.literal);
            lit->data.literal = U64_VAL((uint64_t)v);
            lit->valueType = dst;
            if (unaryMinus) init->valueType = dst;
            return true;
        }
    } else if (src->kind == TYPE_U32 && dst->kind == TYPE_U64) {
        if (IS_U32(lit->data.literal)) {
            uint32_t v = AS_U32(lit->data.literal);
            lit->data.literal = U64_VAL((uint64_t)v);
            lit->valueType = dst;
            if (unaryMinus) init->valueType = dst;
            return true;
        }
    } else if (src->kind == TYPE_U64 && dst->kind == TYPE_I32) {
        if (IS_U64(lit->data.literal)) {
            uint64_t v = AS_U64(lit->data.literal);
            lit->data.literal = I32_VAL((int32_t)v);
            lit->valueType = dst;
            if (unaryMinus) init->valueType = dst;
            return true;
        }
    } else if (src->kind == TYPE_U64 && dst->kind == TYPE_U32) {
        if (IS_U64(lit->data.literal)) {
            uint64_t v = AS_U64(lit->data.literal);
            lit->data.literal = U32_VAL((uint32_t)v);
            lit->valueType = dst;
            if (unaryMinus) init->valueType = dst;
            return true;
        }
    } else if ((src->kind == TYPE_I32 || src->kind == TYPE_U32) &&
               dst->kind == TYPE_F64) {
        double v = (src->kind == TYPE_I32) ? (double)AS_I32(lit->data.literal)
                                           : (double)AS_U32(lit->data.literal);
        lit->data.literal = F64_VAL(v);
        lit->valueType = dst;
        if (unaryMinus) init->valueType = dst;
        return true;
    } else if (src->kind == TYPE_U64 && dst->kind == TYPE_F64) {
        lit->data.literal = F64_VAL((double)AS_U64(lit->data.literal));
        lit->valueType = dst;
        if (unaryMinus) init->valueType = dst;
        return true;
    } else if (src->kind == TYPE_F64 && dst->kind == TYPE_I32) {
        lit->data.literal = I32_VAL((int32_t)AS_F64(lit->data.literal));
        lit->valueType = dst;
        if (unaryMinus) init->valueType = dst;
        return true;
    } else if (src->kind == TYPE_F64 && dst->kind == TYPE_U32) {
        lit->data.literal = U32_VAL((uint32_t)AS_F64(lit->data.literal));
        lit->valueType = dst;
        if (unaryMinus) init->valueType = dst;
        return true;
    } else if (src->kind == TYPE_F64 && dst->kind == TYPE_U64) {
        lit->data.literal = U64_VAL((uint64_t)AS_F64(lit->data.literal));
        lit->valueType = dst;
        if (unaryMinus) init->valueType = dst;
        return true;
    }
    return false;
}

static Value convertLiteralToString(Value value) {
    char buffer[64];
    int length = 0;
    switch (value.type) {
        case VAL_I32:
            length = snprintf(buffer, sizeof(buffer), "%d", AS_I32(value));
            break;
        case VAL_I64:
            length = snprintf(buffer, sizeof(buffer), "%lld",
                              (long long)AS_I64(value));
            break;
        case VAL_U32:
            length = snprintf(buffer, sizeof(buffer), "%u", AS_U32(value));
            break;
        case VAL_U64:
            length = snprintf(buffer, sizeof(buffer), "%llu",
                              (unsigned long long)AS_U64(value));
            break;
        case VAL_F64:
            length = snprintf(buffer, sizeof(buffer), "%g", AS_F64(value));
            break;
        case VAL_BOOL:
            length = snprintf(buffer, sizeof(buffer), "%s",
                              AS_BOOL(value) ? "true" : "false");
            break;
        case VAL_STRING:
            return value;
        default:
            length = snprintf(buffer, sizeof(buffer), "<obj>");
            break;
    }
    ObjString* obj = allocateString(buffer, length);
    return STRING_VAL(obj);
}

static void writeOp(Compiler* compiler, uint8_t op) {
    writeChunk(compiler->chunk, op, compiler->currentLine,
               compiler->currentColumn);
}

static void writeByte(Compiler* compiler, uint8_t byte) {
    writeChunk(compiler->chunk, byte, compiler->currentLine,
               compiler->currentColumn);
}

static int makeConstant(Compiler* compiler, ObjString* string) {
    Value value = STRING_VAL(string);
    int constant = addConstant(compiler->chunk, value);
    return constant;
}

static void emitConstant(Compiler* compiler, Value value) {
    // Ensure constants are emitted with valid values
    if (IS_I32(value) || IS_I64(value) || IS_U32(value) || IS_U64(value) ||
        IS_F64(value) || IS_BOOL(value) || IS_NIL(value) || IS_STRING(value)) {
        if (IS_STRING(value)) {
            ObjString* copy =
                allocateString(value.as.string->chars, value.as.string->length);
            value.as.string = copy;
        }

        if (IS_I64(value)) {
            int constant = addConstant(compiler->chunk, value);
            if (constant < 256) {
                writeOp(compiler, OP_I64_CONST);
                writeByte(compiler, (uint8_t)constant);
            } else {
                // Fallback to generic constant for large pools
                writeOp(compiler, OP_CONSTANT_LONG);
                writeByte(compiler, (constant >> 16) & 0xFF);
                writeByte(compiler, (constant >> 8) & 0xFF);
                writeByte(compiler, constant & 0xFF);
            }
        } else {
            writeConstant(compiler->chunk, value, compiler->currentLine,
                          compiler->currentColumn);
        }
    } else {
        // fprintf(stderr, "ERROR: Invalid constant type\n");
        // Debug log to trace invalid constants
        // fprintf(stderr, "DEBUG: Invalid constant encountered. Value type:
        // %d\n", value.type); fprintf(stderr, "DEBUG: Value details: ");
        // printValue(value);
        // fprintf(stderr, "\n");
        compiler->hadError = true;
    }
}

/**
 * Perform static type checking on a subtree.
 *
 * @param compiler Active compiler context.
 * @param node     Root of the AST subtree.
 */
static void typeCheckNode(Compiler* compiler, ASTNode* node) {
    if (!node) {
        return;
    }

    compiler->currentLine = node->line;
    compiler->currentColumn = firstNonWhitespaceColumn(compiler, node->line);

    switch (node->type) {
        case AST_LITERAL: {
            if (!node->valueType) {
                error(compiler, "Literal node has no type set.");
            }
            break;
        }

        case AST_BINARY: {
            compiler->currentColumn =
                tokenColumn(compiler, &node->data.operation.operator);

            typeCheckNode(compiler, node->left);
            typeCheckNode(compiler, node->right);
            if (compiler->hadError) return;

            Type* leftType = node->left->valueType;
            Type* rightType = node->right->valueType;
            if (!leftType || !rightType) {
                error(compiler, "Binary operand type not set.");
                return;
            }

            TokenType operator = node->data.operation.operator.type;
            switch (operator) {
                case TOKEN_PLUS: {
                    if (!requireConstraint(compiler, leftType,
                                           CONSTRAINT_NUMERIC,
                                           &node->data.operation.operator) ||
                        !requireConstraint(compiler, rightType,
                                           CONSTRAINT_NUMERIC,
                                           &node->data.operation.operator)) {
                        return;
                    }
                    if (leftType->kind == TYPE_STRING ||
                        rightType->kind == TYPE_STRING) {
                        node->valueType = getPrimitiveType(TYPE_STRING);
                        node->data.operation.convertLeft =
                            leftType->kind != TYPE_STRING &&
                            leftType->kind != TYPE_NIL;
                        node->data.operation.convertRight =
                            rightType->kind != TYPE_STRING &&
                            rightType->kind != TYPE_NIL;
                    } else if ((typesEqual(leftType, rightType) &&
                                leftType->kind == TYPE_GENERIC) ||
                               (typesEqual(leftType, rightType) &&
                                (leftType->kind == TYPE_I32 ||
                                 leftType->kind == TYPE_I64 ||
                                 leftType->kind == TYPE_U32 ||
                                 leftType->kind == TYPE_U64 ||
                                 leftType->kind == TYPE_F64))) {
                        node->valueType = leftType;
                        node->data.operation.convertLeft = false;
                        node->data.operation.convertRight = false;
                    } else if ((leftType->kind == TYPE_I64 &&
                                (rightType->kind == TYPE_I32 ||
                                 rightType->kind == TYPE_U32)) ||
                               (rightType->kind == TYPE_I64 &&
                                (leftType->kind == TYPE_I32 ||
                                 leftType->kind == TYPE_U32))) {
                        node->valueType = getPrimitiveType(TYPE_I64);
                        node->data.operation.convertLeft =
                            leftType->kind != TYPE_I64;
                        node->data.operation.convertRight =
                            rightType->kind != TYPE_I64;
                    } else {
                        emitTokenError(compiler, &node->data.operation.operator,
                                       ERROR_GENERAL,
                                       "Type mismatch in addition operation. "
                                       "Use 'as' for explicit casts.");
                        return;
                    }
                    break;
                }
                case TOKEN_MINUS:
                case TOKEN_STAR:
                case TOKEN_SLASH: {
                    if (!requireConstraint(compiler, leftType,
                                           CONSTRAINT_NUMERIC,
                                           &node->data.operation.operator) ||
                        !requireConstraint(compiler, rightType,
                                           CONSTRAINT_NUMERIC,
                                           &node->data.operation.operator)) {
                        return;
                    }
                    if ((typesEqual(leftType, rightType) &&
                         leftType->kind == TYPE_GENERIC) ||
                        (typesEqual(leftType, rightType) &&
                         (leftType->kind == TYPE_I32 ||
                          leftType->kind == TYPE_I64 ||
                          leftType->kind == TYPE_U32 ||
                          leftType->kind == TYPE_U64 ||
                          leftType->kind == TYPE_F64))) {
                        node->valueType = leftType;
                        node->data.operation.convertLeft = false;
                        node->data.operation.convertRight = false;
                    } else if ((leftType->kind == TYPE_I64 &&
                                (rightType->kind == TYPE_I32 ||
                                 rightType->kind == TYPE_U32)) ||
                               (rightType->kind == TYPE_I64 &&
                                (leftType->kind == TYPE_I32 ||
                                 leftType->kind == TYPE_U32))) {
                        node->valueType = getPrimitiveType(TYPE_I64);
                        node->data.operation.convertLeft =
                            leftType->kind != TYPE_I64;
                        node->data.operation.convertRight =
                            rightType->kind != TYPE_I64;
                    } else {
                        error(compiler,
                              "Type mismatch in arithmetic operation. Use "
                              "explicit 'as' casts.");
                        return;
                    }
                    break;
                }

                case TOKEN_MODULO: {
                    if (!requireConstraint(compiler, leftType,
                                           CONSTRAINT_NUMERIC,
                                           &node->data.operation.operator) ||
                        !requireConstraint(compiler, rightType,
                                           CONSTRAINT_NUMERIC,
                                           &node->data.operation.operator)) {
                        return;
                    }
                    if ((typesEqual(leftType, rightType) &&
                         leftType->kind == TYPE_GENERIC) ||
                        (typesEqual(leftType, rightType) &&
                         (leftType->kind == TYPE_I32 ||
                          leftType->kind == TYPE_I64 ||
                          leftType->kind == TYPE_U32 ||
                          leftType->kind == TYPE_U64))) {
                        node->valueType = leftType;
                        node->data.operation.convertLeft = false;
                        node->data.operation.convertRight = false;
                    } else if ((leftType->kind == TYPE_I64 &&
                                (rightType->kind == TYPE_I32 ||
                                 rightType->kind == TYPE_U32)) ||
                               (rightType->kind == TYPE_I64 &&
                                (leftType->kind == TYPE_I32 ||
                                 leftType->kind == TYPE_U32))) {
                        node->valueType = getPrimitiveType(TYPE_I64);
                        node->data.operation.convertLeft =
                            leftType->kind != TYPE_I64;
                        node->data.operation.convertRight =
                            rightType->kind != TYPE_I64;
                    } else {
                        error(compiler,
                              "Modulo operands must be i32, i64, u32 or u64.");
                        return;
                    }
                    break;
                }

                case TOKEN_BIT_AND:
                case TOKEN_BIT_OR:
                case TOKEN_BIT_XOR: {
                    if (!requireConstraint(compiler, leftType,
                                           CONSTRAINT_NUMERIC,
                                           &node->data.operation.operator) ||
                        !requireConstraint(compiler, rightType,
                                           CONSTRAINT_NUMERIC,
                                           &node->data.operation.operator)) {
                        return;
                    }
                    if (!typesEqual(leftType, rightType) ||
                        !(leftType->kind == TYPE_I32 ||
                          leftType->kind == TYPE_I64 ||
                          leftType->kind == TYPE_U32 ||
                          leftType->kind == TYPE_U64)) {
                        error(
                            compiler,
                            "Bitwise operands must be the same integer type.");
                        return;
                    }
                    node->valueType = leftType;
                    node->data.operation.convertLeft = false;
                    node->data.operation.convertRight = false;
                    break;
                }

                case TOKEN_SHIFT_LEFT:
                case TOKEN_SHIFT_RIGHT: {
                    if (!requireConstraint(compiler, leftType,
                                           CONSTRAINT_NUMERIC,
                                           &node->data.operation.operator) ||
                        !requireConstraint(compiler, rightType,
                                           CONSTRAINT_NUMERIC,
                                           &node->data.operation.operator)) {
                        return;
                    }
                    if (!(leftType->kind == TYPE_I32 ||
                          leftType->kind == TYPE_I64 ||
                          leftType->kind == TYPE_U32 ||
                          leftType->kind == TYPE_U64)) {
                        error(compiler,
                              "Left operand of shift must be an integer.");
                        return;
                    }
                    if (!(rightType->kind == TYPE_I32 ||
                          rightType->kind == TYPE_I64 ||
                          rightType->kind == TYPE_U32 ||
                          rightType->kind == TYPE_U64)) {
                        error(compiler, "Shift amount must be an integer.");
                        return;
                    }
                    node->valueType = leftType;
                    node->data.operation.convertLeft = false;
                    node->data.operation.convertRight =
                        rightType->kind != leftType->kind;
                    break;
                }

                case TOKEN_LEFT_BRACKET: {
                    if (leftType->kind != TYPE_ARRAY) {
                        emitTokenError(compiler, &node->data.operation.operator,
                                       ERROR_GENERAL, "Can only index arrays.");
                        return;
                    }
                    if (rightType->kind != TYPE_I32 &&
                        rightType->kind != TYPE_U32) {
                        emitTokenError(compiler, &node->data.operation.operator,
                                       ERROR_GENERAL,
                                       "Array index must be an integer.");
                        return;
                    }
                    node->valueType = leftType->info.array.elementType;
                    break;
                }

                // Logical operators
                case TOKEN_AND:
                case TOKEN_OR: {
                    // Both operands must be boolean
                    if (leftType->kind != TYPE_BOOL) {
                        error(compiler,
                              "Left operand of logical operator must be a "
                              "boolean.");
                        return;
                    }
                    if (rightType->kind != TYPE_BOOL) {
                        error(compiler,
                              "Right operand of logical operator must be a "
                              "boolean.");
                        return;
                    }
                    // Logical operators return a boolean
                    node->valueType = getPrimitiveType(TYPE_BOOL);
                    break;
                }

                // Comparison operators
                case TOKEN_LESS:
                case TOKEN_LESS_EQUAL:
                case TOKEN_GREATER:
                case TOKEN_GREATER_EQUAL:
                case TOKEN_EQUAL_EQUAL:
                case TOKEN_BANG_EQUAL: {
                    if (!requireConstraint(compiler, leftType,
                                           CONSTRAINT_COMPARABLE,
                                           &node->data.operation.operator) ||
                        !requireConstraint(compiler, rightType,
                                           CONSTRAINT_COMPARABLE,
                                           &node->data.operation.operator)) {
                        return;
                    }
                    // Comparison operators always return a boolean
                    node->valueType = getPrimitiveType(TYPE_BOOL);
                    if ((leftType->kind == TYPE_I64 &&
                         (rightType->kind == TYPE_I32 ||
                          rightType->kind == TYPE_U32)) ||
                        (rightType->kind == TYPE_I64 &&
                         (leftType->kind == TYPE_I32 ||
                          leftType->kind == TYPE_U32))) {
                        node->data.operation.convertLeft =
                            leftType->kind != TYPE_I64;
                        node->data.operation.convertRight =
                            rightType->kind != TYPE_I64;
                    }
                    break;
                }

                default:
                    error(compiler,
                          "Unsupported binary operator in type checker.");
                    return;
            }

            if (node->valueType && node->valueType->kind == TYPE_I32 &&
                (operator == TOKEN_PLUS || operator == TOKEN_MINUS ||
                 operator == TOKEN_STAR) &&
                constantBinaryOverflows(compiler, node->left, node->right,
                                        operator)) {
                node->valueType = getPrimitiveType(TYPE_I64);
                node->data.operation.convertLeft = leftType->kind != TYPE_I64;
                node->data.operation.convertRight = rightType->kind != TYPE_I64;
#ifdef DEBUG_PROMOTION_HINTS
                if (vm.promotionHints) {
                    fprintf(
                        stderr,
                        "[hint] promoted binary operation at line %d to i64\n",
                        node->line);
                }
#endif
            }
            break;
        }

        case AST_UNARY: {
            compiler->currentColumn =
                tokenColumn(compiler, &node->data.operation.operator);
            typeCheckNode(compiler, node->left);
            if (compiler->hadError) return;

            Type* operandType = node->left->valueType;
            if (!operandType) {
                error(compiler, "Unary operand type not set.");
                return;
            }

            TokenType operator = node->data.operation.operator.type;
            switch (operator) {
                case TOKEN_MINUS:
                    if (!requireConstraint(compiler, operandType,
                                           CONSTRAINT_NUMERIC,
                                           &node->data.operation.operator)) {
                        return;
                    }
                    if (operandType->kind != TYPE_I32 &&
                        operandType->kind != TYPE_I64 &&
                        operandType->kind != TYPE_U32 &&
                        operandType->kind != TYPE_U64 &&
                        operandType->kind != TYPE_F64 &&
                        operandType->kind != TYPE_GENERIC) {
                        error(compiler,
                              "Unary minus operand must be a number.");
                        return;
                    }
                    node->valueType = operandType;
                    break;

                case TOKEN_NOT:
                    if (operandType->kind != TYPE_BOOL) {
                        error(compiler, "Unary not operand must be a boolean.");
                        return;
                    }
                    node->valueType = getPrimitiveType(TYPE_BOOL);
                    break;
                case TOKEN_BIT_NOT:
                    if (!requireConstraint(compiler, operandType,
                                           CONSTRAINT_NUMERIC,
                                           &node->data.operation.operator)) {
                        return;
                    }
                    if (operandType->kind != TYPE_I32 &&
                        operandType->kind != TYPE_I64 &&
                        operandType->kind != TYPE_U32 &&
                        operandType->kind != TYPE_U64) {
                        error(compiler,
                              "Bitwise not operand must be an integer.");
                        return;
                    }
                    node->valueType = operandType;
                    break;

                default:
                    error(compiler, "Unsupported unary operator.");
                    return;
            }
            break;
        }

        case AST_CAST: {
            typeCheckNode(compiler, node->left);
            if (compiler->hadError) return;
            Type* src = node->left->valueType;
            Type* dst = node->data.cast.type;
            if (!src || !dst) {
                error(compiler, "Invalid cast types.");
                return;
            }

            if (src->kind == TYPE_NIL || src->kind == TYPE_VOID) {
                error(compiler, "Cannot cast from nil or void.");
                return;
            }
            if (src->kind == TYPE_STRING && dst->kind != TYPE_STRING) {
                error(compiler, "Cannot cast a string to other types.");
                return;
            }

            bool allowed = false;
            if (dst->kind == TYPE_STRING) {
                allowed = true;
            } else if ((src->kind == TYPE_I32 &&
                        (dst->kind == TYPE_U32 || dst->kind == TYPE_I64 ||
                         dst->kind == TYPE_F64 || dst->kind == TYPE_U64)) ||
                       (src->kind == TYPE_U32 &&
                        (dst->kind == TYPE_I32 || dst->kind == TYPE_F64 ||
                         dst->kind == TYPE_U64)) ||
                       (src->kind == TYPE_I64 &&
                        (dst->kind == TYPE_I32 || dst->kind == TYPE_U32)) ||
                       (src->kind == TYPE_U64 &&
                        (dst->kind == TYPE_I32 || dst->kind == TYPE_U32 ||
                         dst->kind == TYPE_F64)) ||
                       (src->kind == TYPE_F64 &&
                        (dst->kind == TYPE_I32 || dst->kind == TYPE_U32 ||
                         dst->kind == TYPE_U64 || dst->kind == TYPE_I64)) ||
                       (src->kind == TYPE_I64 &&
                        (dst->kind == TYPE_U64 || dst->kind == TYPE_F64)) ||
                       (src->kind == TYPE_U64 && dst->kind == TYPE_I64) ||
                       ((src->kind == TYPE_I32 || src->kind == TYPE_U32 ||
                         src->kind == TYPE_I64 || src->kind == TYPE_U64) &&
                        dst->kind == TYPE_BOOL) ||
                       (src->kind == TYPE_F64 && dst->kind == TYPE_BOOL) ||
                       (src->kind == TYPE_BOOL &&
                        (dst->kind == TYPE_I32 || dst->kind == TYPE_U32 ||
                         dst->kind == TYPE_I64 || dst->kind == TYPE_U64 ||
                         dst->kind == TYPE_F64)) ||
                       (src->kind == dst->kind)) {
                allowed = true;
            }

            if (!allowed) {
                error(compiler, "Unsupported cast between these types.");
                return;
            }
            if (node->left->type == AST_LITERAL) {
                if (src->kind == TYPE_I32 && dst->kind == TYPE_U32) {
                    if (IS_I32(node->left->data.literal)) {
                        int32_t v = AS_I32(node->left->data.literal);
                        node->left->data.literal = U32_VAL((uint32_t)v);
                        node->left->valueType = dst;
                    }
                } else if (src->kind == TYPE_U32 && dst->kind == TYPE_I32) {
                    if (IS_U32(node->left->data.literal)) {
                        uint32_t v = AS_U32(node->left->data.literal);
                        node->left->data.literal = I32_VAL((int32_t)v);
                        node->left->valueType = dst;
                    }
                } else if (src->kind == TYPE_I32 && dst->kind == TYPE_I64) {
                    if (IS_I32(node->left->data.literal)) {
                        int32_t v = AS_I32(node->left->data.literal);
                        node->left->data.literal = I64_VAL((int64_t)v);
                        node->left->valueType = dst;
                    }
                } else if (src->kind == TYPE_I64 && dst->kind == TYPE_I32) {
                    if (IS_I64(node->left->data.literal)) {
                        int64_t v = AS_I64(node->left->data.literal);
                        node->left->data.literal = I32_VAL((int32_t)v);
                        node->left->valueType = dst;
                    }
                } else if (src->kind == TYPE_I64 && dst->kind == TYPE_U32) {
                    if (IS_I64(node->left->data.literal)) {
                        int64_t v = AS_I64(node->left->data.literal);
                        node->left->data.literal = U32_VAL((uint32_t)v);
                        node->left->valueType = dst;
                    }
                } else if (src->kind == TYPE_I32 && dst->kind == TYPE_U64) {
                    if (IS_I32(node->left->data.literal)) {
                        int32_t v = AS_I32(node->left->data.literal);
                        node->left->data.literal = U64_VAL((uint64_t)v);
                        node->left->valueType = dst;
                    }
                } else if (src->kind == TYPE_U32 && dst->kind == TYPE_U64) {
                    if (IS_U32(node->left->data.literal)) {
                        uint32_t v = AS_U32(node->left->data.literal);
                        node->left->data.literal = U64_VAL((uint64_t)v);
                        node->left->valueType = dst;
                    }
                } else if (src->kind == TYPE_U64 && dst->kind == TYPE_I32) {
                    if (IS_U64(node->left->data.literal)) {
                        uint64_t v = AS_U64(node->left->data.literal);
                        node->left->data.literal = I32_VAL((int32_t)v);
                        node->left->valueType = dst;
                    }
                } else if (src->kind == TYPE_U64 && dst->kind == TYPE_U32) {
                    if (IS_U64(node->left->data.literal)) {
                        uint64_t v = AS_U64(node->left->data.literal);
                        node->left->data.literal = U32_VAL((uint32_t)v);
                        node->left->valueType = dst;
                    }
                } else if (src->kind == TYPE_U64 && dst->kind == TYPE_F64) {
                    if (IS_U64(node->left->data.literal)) {
                        node->left->data.literal =
                            F64_VAL((double)AS_U64(node->left->data.literal));
                        node->left->valueType = dst;
                    }
                } else if (src->kind == TYPE_F64 && dst->kind == TYPE_U64) {
                    node->left->data.literal =
                        U64_VAL((uint64_t)AS_F64(node->left->data.literal));
                    node->left->valueType = dst;
                } else if ((src->kind == TYPE_I32 || src->kind == TYPE_U32) &&
                           dst->kind == TYPE_F64) {
                    double v = (src->kind == TYPE_I32)
                                   ? (double)AS_I32(node->left->data.literal)
                                   : (double)AS_U32(node->left->data.literal);
                    node->left->data.literal = F64_VAL(v);
                    node->left->valueType = dst;
                } else if (src->kind == TYPE_F64 && dst->kind == TYPE_I32) {
                    node->left->data.literal =
                        I32_VAL((int32_t)AS_F64(node->left->data.literal));
                    node->left->valueType = dst;
                } else if (src->kind == TYPE_F64 && dst->kind == TYPE_U32) {
                    node->left->data.literal =
                        U32_VAL((uint32_t)AS_F64(node->left->data.literal));
                    node->left->valueType = dst;
                } else if (dst->kind == TYPE_STRING) {
                    node->left->data.literal =
                        convertLiteralToString(node->left->data.literal);
                    node->left->valueType = dst;
                }
            }
            node->valueType = dst;
            break;
        }

        case AST_VARIABLE: {
            compiler->currentColumn =
                tokenColumn(compiler, &node->data.variable.name);
            uint8_t index = resolveVariable(compiler, node->data.variable.name);
            if (index == UINT8_MAX) {
                char tempName[node->data.variable.name.length + 1];
                memcpy(tempName, node->data.variable.name.start,
                       node->data.variable.name.length);
                tempName[node->data.variable.name.length] = '\0';
                uint8_t priv = findPrivateGlobal(
                    tempName, node->data.variable.name.length);
                if (priv != UINT8_MAX) {
                    emitPrivateVariableError(compiler,
                                             &node->data.variable.name);
                    return;
                }
                Symbol* any = findAnySymbol(&compiler->symbols, tempName);
                if (any && !any->active) {
                    emitUndefinedVarError(compiler, &node->data.variable.name,
                                          &any->token, tempName);
                } else {
                    emitUndefinedVarError(compiler, &node->data.variable.name,
                                          NULL, tempName);
                }
                return;
            }
            node->data.variable.index = index;

            node->valueType = vm.globalTypes[index];
            if (!node->valueType) {
                error(compiler, "Variable has no type defined.");
                return;
            }
            break;
        }

        case AST_LET: {
            // First type check the initializer if present
            if (node->data.let.initializer) {
                typeCheckNode(compiler, node->data.let.initializer);
                if (compiler->hadError) return;
            }

            Type* initType = NULL;
            Type* declType = node->data.let.type;

            if (node->data.let.initializer) {
                initType = node->data.let.initializer->valueType;
                if (!initType) {
                    error(compiler, "Could not determine initializer type");
                    return;
                }
            }

            if (declType) {
                if (initType) {
                    if (!typesEqual(declType, initType)) {
                        if (initType->kind == TYPE_ARRAY &&
                            initType->info.array.elementType->kind ==
                                TYPE_NIL &&
                            declType->kind == TYPE_ARRAY) {
                            node->data.let.initializer->valueType = declType;
                            initType = declType;
                        } else if (node->data.let.initializer->type ==
                                       AST_ARRAY &&
                                   declType->kind == TYPE_ARRAY) {
                            ASTNode* el =
                                node->data.let.initializer->data.array.elements;
                            while (el) {
                                convertLiteralForDecl(
                                    el, el->valueType,
                                    declType->info.array.elementType);
                                if (declType->info.array.elementType->kind ==
                                        TYPE_ARRAY &&
                                    declType->info.array.elementType->info.array
                                            .length < 0 &&
                                    el->valueType &&
                                    el->valueType->kind == TYPE_ARRAY &&
                                    typesEqual(
                                        el->valueType->info.array.elementType,
                                        declType->info.array.elementType->info
                                            .array.elementType)) {
                                    el->valueType =
                                        declType->info.array.elementType;
                                } else if (!typesEqual(el->valueType,
                                                       declType->info.array
                                                           .elementType)) {
                                    error(compiler,
                                          "Type mismatch in let declaration.");
                                    return;
                                }
                                el = el->next;
                            }
                            node->data.let.initializer->valueType = declType;
                            initType = declType;
                        } else if (node->data.let.initializer->type ==
                                       AST_ARRAY_FILL &&
                                   declType->kind == TYPE_ARRAY) {
                            ASTNode* val = node->data.let.initializer->data
                                               .arrayFill.value;
                            convertLiteralForDecl(
                                val, val->valueType,
                                declType->info.array.elementType);
                            if (declType->info.array.elementType->kind ==
                                    TYPE_ARRAY &&
                                declType->info.array.elementType->info.array
                                        .length < 0 &&
                                val->valueType &&
                                val->valueType->kind == TYPE_ARRAY &&
                                typesEqual(
                                    val->valueType->info.array.elementType,
                                    declType->info.array.elementType->info.array
                                        .elementType)) {
                                val->valueType =
                                    declType->info.array.elementType;
                            } else if (!typesEqual(
                                           val->valueType,
                                           declType->info.array.elementType)) {
                                error(compiler,
                                      "Type mismatch in let declaration.");
                                return;
                            }
                            node->data.let.initializer->valueType = declType;
                            initType = declType;
                        } else if (convertLiteralForDecl(
                                       node->data.let.initializer, initType,
                                       declType)) {
                            initType = declType;
                        } else {
                            error(compiler,
                                  "Type mismatch in let declaration.");
                            return;
                        }
                    }
                }
                node->valueType = declType;
            } else if (initType) {
                node->valueType = initType;
            } else {
                error(compiler, "Cannot determine variable type");
                return;
            }

            // Add the variable to the symbol table
            uint8_t index =
                addLocal(compiler, node->data.let.name, node->valueType,
                         node->data.let.isMutable, false);
            node->data.let.index = index;
            vm.publicGlobals[index] = node->data.let.isPublic;
            {
                char nameBuf[node->data.let.name.length + 1];
                memcpy(nameBuf, node->data.let.name.start,
                       node->data.let.name.length);
                nameBuf[node->data.let.name.length] = '\0';
                Symbol* sym = findSymbol(&compiler->symbols, nameBuf);
                if (sym) {
                    bool fixed = false;
                    if (node->data.let.initializer) {
                        ASTNode* init = node->data.let.initializer;
                        if (init->type == AST_ARRAY &&
                            node->data.let.type == NULL &&
                            init->data.array.elementCount > 0)
                            fixed = true;
                        else if (init->type == AST_ARRAY_FILL &&
                                 init->data.arrayFill.lengthValue >= 0)
                            fixed = true;
                    }
                    sym->fixedArray = fixed;
                }
            }
            break;
        }

        case AST_STATIC: {
            if (node->data.staticVar.initializer) {
                typeCheckNode(compiler, node->data.staticVar.initializer);
                if (compiler->hadError) return;
            }

            Type* initType = NULL;
            Type* declType = node->data.staticVar.type;

            if (node->data.staticVar.initializer) {
                initType = node->data.staticVar.initializer->valueType;
                if (!initType) {
                    error(compiler, "Could not determine initializer type");
                    return;
                }
            }

            if (declType) {
                if (initType) {
                    if (!typesEqual(declType, initType)) {
                        if (initType->kind == TYPE_ARRAY &&
                            initType->info.array.elementType->kind ==
                                TYPE_NIL &&
                            declType->kind == TYPE_ARRAY) {
                            node->data.staticVar.initializer->valueType =
                                declType;
                            initType = declType;
                        } else if (node->data.staticVar.initializer->type ==
                                       AST_ARRAY &&
                                   declType->kind == TYPE_ARRAY) {
                            ASTNode* el = node->data.staticVar.initializer->data
                                              .array.elements;
                            while (el) {
                                convertLiteralForDecl(
                                    el, el->valueType,
                                    declType->info.array.elementType);
                                if (!typesEqual(
                                        el->valueType,
                                        declType->info.array.elementType)) {
                                    error(
                                        compiler,
                                        "Type mismatch in static declaration.");
                                    return;
                                }
                                el = el->next;
                            }
                            node->data.staticVar.initializer->valueType =
                                declType;
                            initType = declType;
                        } else if (node->data.staticVar.initializer->type ==
                                       AST_ARRAY_FILL &&
                                   declType->kind == TYPE_ARRAY) {
                            ASTNode* val = node->data.staticVar.initializer
                                               ->data.arrayFill.value;
                            convertLiteralForDecl(
                                val, val->valueType,
                                declType->info.array.elementType);
                            if (declType->info.array.elementType->kind ==
                                    TYPE_ARRAY &&
                                declType->info.array.elementType->info.array
                                        .length < 0 &&
                                val->valueType &&
                                val->valueType->kind == TYPE_ARRAY &&
                                typesEqual(
                                    val->valueType->info.array.elementType,
                                    declType->info.array.elementType->info.array
                                        .elementType)) {
                                val->valueType =
                                    declType->info.array.elementType;
                            } else if (!typesEqual(
                                           val->valueType,
                                           declType->info.array.elementType)) {
                                error(compiler,
                                      "Type mismatch in static declaration.");
                                return;
                            }
                            node->data.staticVar.initializer->valueType =
                                declType;
                            initType = declType;
                        } else if (convertLiteralForDecl(
                                       node->data.staticVar.initializer,
                                       initType, declType)) {
                            initType = declType;
                        } else {
                            error(compiler,
                                  "Type mismatch in static declaration.");
                            return;
                        }
                    }
                }
                node->valueType = declType;
            } else if (initType) {
                node->valueType = initType;
            } else {
                error(compiler, "Cannot determine variable type");
                return;
            }

            uint8_t index =
                addLocal(compiler, node->data.staticVar.name, node->valueType,
                         node->data.staticVar.isMutable, false);
            node->data.staticVar.index = index;
            {
                char nameBuf[node->data.staticVar.name.length + 1];
                memcpy(nameBuf, node->data.staticVar.name.start,
                       node->data.staticVar.name.length);
                nameBuf[node->data.staticVar.name.length] = '\0';
                Symbol* sym = findSymbol(&compiler->symbols, nameBuf);
                if (sym) {
                    bool fixed = false;
                    if (node->data.staticVar.initializer) {
                        ASTNode* init = node->data.staticVar.initializer;
                        if (init->type == AST_ARRAY &&
                            node->data.staticVar.type == NULL &&
                            init->data.array.elementCount > 0)
                            fixed = true;
                        else if (init->type == AST_ARRAY_FILL &&
                                 init->data.arrayFill.lengthValue >= 0)
                            fixed = true;
                    }
                    sym->fixedArray = fixed;
                }
            }
            break;
        }

        case AST_CONST: {
            if (node->data.constant.initializer) {
                typeCheckNode(compiler, node->data.constant.initializer);
                if (compiler->hadError) return;
            }

            if (!node->data.constant.initializer ||
                node->data.constant.initializer->type != AST_LITERAL) {
                error(compiler, "Constant expressions must be literals.");
                return;
            }

            Type* initType = node->data.constant.initializer->valueType;
            Type* declType = node->data.constant.type;

            if (declType) {
                if (initType && !typesEqual(declType, initType)) {
                    if (!convertLiteralForDecl(node->data.constant.initializer,
                                               initType, declType)) {
                        error(compiler, "Type mismatch in const declaration.");
                        return;
                    }
                }
                node->valueType = declType;
            } else if (initType) {
                node->valueType = initType;
            } else {
                error(compiler, "Cannot determine constant type");
                return;
            }

            uint8_t index = addLocal(compiler, node->data.constant.name,
                                     node->valueType, false, true);
            node->data.constant.index = index;
            vm.globals[index] = node->data.constant.initializer->data.literal;
            vm.globalTypes[index] = node->valueType;
            vm.publicGlobals[index] = node->data.constant.isPublic;
            break;
        }

        case AST_PRINT: {
            ASTNode* format = node->data.print.format;
            ASTNode* arg = node->data.print.arguments;

            // Type check the format expression first
            typeCheckNode(compiler, format);
            if (compiler->hadError) return;

            if (arg != NULL) {
                // This is a formatted print with interpolation
                // Verify format is a string
                if (format->valueType == NULL ||
                    format->valueType->kind != TYPE_STRING) {
                    error(compiler,
                          "First argument to print must evaluate to a string "
                          "for interpolation.");
                    return;
                }

                // Count arguments safely and validate linked list
                ASTNode* current = arg;
                while (current != NULL) {
                    if (current == current->next) {
                        compiler->hadError = true;
                        return;
                    }

                    typeCheckNode(compiler, current);  // Perform type check
                    if (compiler->hadError) return;

                    current = current->next;
                }
            } else {
                // This is a simple print, format can be any type
                // No additional type checking needed
            }

            break;
        }

        case AST_ASSIGNMENT: {
            // First type check the value expression
            if (node->left) {
                typeCheckNode(compiler, node->left);
                if (compiler->hadError) return;
            } else {
                error(compiler, "Assignment requires a value expression");
                return;
            }

            // Resolve the variable being assigned to
            uint8_t index = resolveVariable(compiler, node->data.variable.name);
            if (index == UINT8_MAX) {
                char tempName[node->data.variable.name.length + 1];
                memcpy(tempName, node->data.variable.name.start,
                       node->data.variable.name.length);
                tempName[node->data.variable.name.length] = '\0';
                errorFmt(compiler, "Cannot assign to undefined variable '%s'.",
                         tempName);
                return;
            }
            node->data.variable.index = index;

            char tempName[node->data.variable.name.length + 1];
            memcpy(tempName, node->data.variable.name.start,
                   node->data.variable.name.length);
            tempName[node->data.variable.name.length] = '\0';
            Symbol* sym = findSymbol(&compiler->symbols, tempName);
            if (sym && !sym->isMutable) {
                emitImmutableAssignmentError(
                    compiler, &node->data.variable.name, tempName);
                return;
            }

            // Check that the types are compatible
            Type* varType = vm.globalTypes[index];
            Type* valueType = node->left->valueType;

            if (!varType) {
                error(compiler, "Variable has no type defined.");
                return;
            }

            if (!valueType) {
                error(compiler, "Could not determine value type.");
                return;
            }

            // Allow i32 literals to be used for u32 variables if the value is
            // non-negative
            if (varType->kind == TYPE_U32 && valueType->kind == TYPE_I32 &&
                node->left->type == AST_LITERAL) {
                if (IS_I32(node->left->data.literal) &&
                    AS_I32(node->left->data.literal) >= 0) {
                    int32_t value = AS_I32(node->left->data.literal);
                    node->left->data.literal = U32_VAL((uint32_t)value);
                    node->left->valueType = varType;
                    valueType = varType;
                }
            }

            // Implicitly cast i64 literals to i32 when safe
            if (varType->kind == TYPE_I32 && valueType->kind == TYPE_I64 &&
                node->left->type == AST_LITERAL) {
                if (IS_I64(node->left->data.literal)) {
                    int64_t v = AS_I64(node->left->data.literal);
                    if (v >= INT32_MIN && v <= INT32_MAX) {
                        node->left->data.literal = I32_VAL((int32_t)v);
                        node->left->valueType = varType;
                        valueType = varType;
                    }
                }
            }

            // Persist type if the variable was previously nil
            if (varType->kind == TYPE_NIL && valueType->kind != TYPE_NIL) {
                vm.globalTypes[index] = valueType;
                vm.globalTypes[index] = valueType;
                char tempName[node->data.variable.name.length + 1];
                memcpy(tempName, node->data.variable.name.start,
                       node->data.variable.name.length);
                tempName[node->data.variable.name.length] = '\0';
                Symbol* sym = findSymbol(&compiler->symbols, tempName);
                if (sym) sym->type = valueType;
                varType = valueType;
            } else if (varType->kind == TYPE_ARRAY &&
                       varType->info.array.elementType->kind == TYPE_NIL &&
                       valueType->kind == TYPE_ARRAY) {
                vm.globalTypes[index] = valueType;
                vm.globalTypes[index] = valueType;
                char tempName[node->data.variable.name.length + 1];
                memcpy(tempName, node->data.variable.name.start,
                       node->data.variable.name.length);
                tempName[node->data.variable.name.length] = '\0';
                Symbol* sym = findSymbol(&compiler->symbols, tempName);
                if (sym) sym->type = valueType;
                varType = valueType;
            }

            if (!typesEqual(varType, valueType)) {
                if (valueType->kind == TYPE_ARRAY &&
                    valueType->info.array.elementType->kind == TYPE_NIL &&
                    varType->kind == TYPE_ARRAY) {
                    node->left->valueType = varType;
                    valueType = varType;
                } else {
                    error(compiler, "Type mismatch in assignment.");
                    return;
                }
            }

            // The assignment expression has the same type as the variable
            node->valueType = varType;
            break;
        }

        case AST_IF: {
            // Type check the condition
            typeCheckNode(compiler, node->data.ifStmt.condition);
            if (compiler->hadError) return;

            // Ensure the condition is a boolean
            Type* condType = node->data.ifStmt.condition->valueType;
            if (!condType || condType->kind != TYPE_BOOL) {
                error(compiler, "If condition must be a boolean expression.");
                return;
            }

            // Type check the then branch
            typeCheckNode(compiler, node->data.ifStmt.thenBranch);
            if (compiler->hadError) return;

            // Type check the elif conditions and branches
            ASTNode* elifCondition = node->data.ifStmt.elifConditions;
            ASTNode* elifBranch = node->data.ifStmt.elifBranches;
            while (elifCondition != NULL && elifBranch != NULL) {
                // Type check the elif condition
                typeCheckNode(compiler, elifCondition);
                if (compiler->hadError) return;

                // Ensure the elif condition is a boolean
                Type* elifCondType = elifCondition->valueType;
                if (!elifCondType || elifCondType->kind != TYPE_BOOL) {
                    error(compiler,
                          "Elif condition must be a boolean expression.");
                    return;
                }

                // Type check the elif branch
                typeCheckNode(compiler, elifBranch);
                if (compiler->hadError) return;

                // Move to the next elif condition and branch
                elifCondition = elifCondition->next;
                elifBranch = elifBranch->next;
            }

            // Type check the else branch if it exists
            if (node->data.ifStmt.elseBranch) {
                typeCheckNode(compiler, node->data.ifStmt.elseBranch);
                if (compiler->hadError) return;
            }

            // If statements don't have a value type
            node->valueType = NULL;
            break;
        }

        case AST_TERNARY: {
            typeCheckNode(compiler, node->data.ternary.condition);
            if (compiler->hadError) return;

            Type* condType = node->data.ternary.condition->valueType;
            if (!condType || condType->kind != TYPE_BOOL) {
                error(compiler,
                      "Conditional expression must use a boolean condition.");
                return;
            }

            typeCheckNode(compiler, node->data.ternary.thenExpr);
            if (compiler->hadError) return;
            typeCheckNode(compiler, node->data.ternary.elseExpr);
            if (compiler->hadError) return;

            Type* thenType = node->data.ternary.thenExpr->valueType;
            Type* elseType = node->data.ternary.elseExpr->valueType;
            if (!thenType || !elseType || !typesEqual(thenType, elseType)) {
                error(compiler, "Both branches of ?: must have the same type.");
                return;
            }
            node->valueType = thenType;
            break;
        }

        case AST_BLOCK: {
            if (node->data.block.scoped) {
                beginScope(compiler);
            }

            // Type check each statement in the block
            ASTNode* stmt = node->data.block.statements;
            while (stmt) {
                typeCheckNode(compiler, stmt);
                if (compiler->hadError) {
                    if (node->data.block.scoped) endScope(compiler);
                    return;
                }
                stmt = stmt->next;
            }

            if (node->data.block.scoped) {
                endScope(compiler);
            }

            // Blocks don't have a value type
            node->valueType = NULL;
            break;
        }

        case AST_WHILE: {
            // Type check the condition
            typeCheckNode(compiler, node->data.whileStmt.condition);
            if (compiler->hadError) return;

            // Ensure the condition is a boolean
            Type* condType = node->data.whileStmt.condition->valueType;
            if (!condType || condType->kind != TYPE_BOOL) {
                error(compiler,
                      "While condition must be a boolean expression.");
                return;
            }

            beginScope(compiler);
            // Type check the body
            typeCheckNode(compiler, node->data.whileStmt.body);
            if (compiler->hadError) {
                endScope(compiler);
                return;
            }
            endScope(compiler);

            // While statements don't have a value type
            node->valueType = NULL;
            break;
        }

        case AST_FOR: {
            // Type check the range start expression
            typeCheckNode(compiler, node->data.forStmt.startExpr);
            if (compiler->hadError) return;

            // Type check the range end expression
            typeCheckNode(compiler, node->data.forStmt.endExpr);
            if (compiler->hadError) return;

            // Type check the step expression if it exists
            if (node->data.forStmt.stepExpr) {
                typeCheckNode(compiler, node->data.forStmt.stepExpr);
                if (compiler->hadError) return;
            }

            // Ensure the range expressions are integers
            Type* startType = node->data.forStmt.startExpr->valueType;
            Type* endType = node->data.forStmt.endExpr->valueType;
            Type* stepType = node->data.forStmt.stepExpr
                                 ? node->data.forStmt.stepExpr->valueType
                                 : NULL;

            if (!startType ||
                (startType->kind != TYPE_I32 && startType->kind != TYPE_I64 &&
                 startType->kind != TYPE_U32 && startType->kind != TYPE_U64)) {
                error(compiler, "For loop range start must be an integer.");
                return;
            }

            if (!endType ||
                (endType->kind != TYPE_I32 && endType->kind != TYPE_I64 &&
                 endType->kind != TYPE_U32 && endType->kind != TYPE_U64)) {
                error(compiler, "For loop range end must be an integer.");
                return;
            }

            if (stepType &&
                (stepType->kind != TYPE_I32 && stepType->kind != TYPE_I64 &&
                 stepType->kind != TYPE_U32 && stepType->kind != TYPE_U64)) {
                error(compiler, "For loop step must be an integer.");
                return;
            }

            // Analyse constant loop bounds for potential overflow
            int64_t startVal, endVal, stepVal = 1;
            bool startConst = evaluateConstantInt(
                compiler, node->data.forStmt.startExpr, &startVal);
            bool endConst = evaluateConstantInt(
                compiler, node->data.forStmt.endExpr, &endVal);
            bool stepConst =
                node->data.forStmt.stepExpr
                    ? evaluateConstantInt(compiler, node->data.forStmt.stepExpr,
                                          &stepVal)
                    : true;
            bool promoteIter = false;
            if ((startConst &&
                 (startVal > INT32_MAX || startVal < INT32_MIN)) ||
                (endConst && (endVal > INT32_MAX || endVal < INT32_MIN))) {
                promoteIter = true;
            }
            if (!promoteIter && startConst && endConst && stepConst) {
                long double diff = (long double)endVal - (long double)startVal;
                long double steps =
                    stepVal != 0 ? diff / (long double)stepVal : diff;
                long double last =
                    (long double)startVal + steps * (long double)stepVal;
                if (last > INT32_MAX || last < INT32_MIN) promoteIter = true;
            }
#ifdef DEBUG_PROMOTION_HINTS
            if (promoteIter && vm.promotionHints) {
                fprintf(
                    stderr,
                    "[hint] promoting for-loop iterator at line %d to i64\n",
                    node->line);
            }
#endif

            beginScope(compiler);
            // Define the iterator variable, promoting to i64 if needed
            Type* iterType =
                promoteIter ? getPrimitiveType(TYPE_I64) : startType;
            uint8_t index = defineVariable(
                compiler, node->data.forStmt.iteratorName, iterType);
            node->data.forStmt.iteratorIndex = index;

            // Type check the body
            typeCheckNode(compiler, node->data.forStmt.body);
            if (compiler->hadError) {
                endScope(compiler);
                return;
            }
            endScope(compiler);

            // For statements don't have a value type
            node->valueType = NULL;
            break;
        }

        case AST_FUNCTION: {
            uint8_t index = node->data.function.index;
            if (index == UINT8_MAX) {
                predeclareFunction(compiler, node);
                index = node->data.function.index;
            }

            Type* prevReturn = compiler->currentReturnType;
            bool prevGenericFlag = compiler->currentFunctionHasGenerics;
            ObjString** prevNames = compiler->genericNames;
            GenericConstraint* prevCons = compiler->genericConstraints;
            int prevCount = compiler->genericCount;
            compiler->currentReturnType = node->data.function.returnType;
            compiler->currentFunctionHasGenerics =
                node->data.function.genericCount > 0;
            compiler->genericNames = node->data.function.genericParams;
            compiler->genericConstraints =
                node->data.function.genericConstraints;
            compiler->genericCount = node->data.function.genericCount;

            beginScope(compiler);
            // Type check parameters
            ASTNode* param = node->data.function.parameters;
            while (param != NULL) {
                typeCheckNode(compiler, param);
                if (compiler->hadError) {
                    endScope(compiler);
                    compiler->currentReturnType = prevReturn;
                    compiler->currentFunctionHasGenerics = prevGenericFlag;
                    return;
                }
                param = param->next;
            }

            // Type check the function body
            typeCheckNode(compiler, node->data.function.body);
            if (compiler->hadError) {
                endScope(compiler);
                compiler->currentReturnType = prevReturn;
                compiler->currentFunctionHasGenerics = prevGenericFlag;
                return;
            }
            endScope(compiler);

            if (node->data.function.genericCount == 0 &&
                node->data.function.returnType &&
                node->data.function.returnType->kind != TYPE_VOID) {
                bool hasRet = containsReturn(node->data.function.body);
                bool allRet = statementsAlwaysReturn(
                    node->data.function.body->data.block.statements);
                if (!hasRet) {
                    char msg[128];
                    snprintf(msg, sizeof(msg),
                             "Error: Missing return statement in function "
                             "'%.*s', expected return type '%s'.",
                             node->data.function.name.length,
                             node->data.function.name.start,
                             getTypeName(node->data.function.returnType->kind));
                    emitSimpleError(compiler, ERROR_GENERAL, msg);
                } else if (!allRet) {
                    char msg[128];
                    snprintf(msg, sizeof(msg),
                             "Error: Not all code paths return a value in "
                             "function '%.*s'.",
                             node->data.function.name.length,
                             node->data.function.name.start);
                    emitSimpleError(compiler, ERROR_GENERAL, msg);
                }
            }

            compiler->currentReturnType = prevReturn;
            compiler->currentFunctionHasGenerics = prevGenericFlag;
            compiler->genericNames = prevNames;
            compiler->genericConstraints = prevCons;
            compiler->genericCount = prevCount;

            // Function declarations don't have a value type
            node->valueType = NULL;
            break;
        }

        case AST_CALL: {
            compiler->currentColumn =
                tokenColumn(compiler, &node->data.call.name);

            bool fromModule = false;
            Module* mod = NULL;
            if (node->data.call.staticType == NULL &&
                node->data.call.arguments != NULL &&
                node->data.call.arguments->type == AST_VARIABLE) {
                ASTNode* recv = node->data.call.arguments;
                char tempName[recv->data.variable.name.length + 1];
                memcpy(tempName, recv->data.variable.name.start,
                       recv->data.variable.name.length);
                tempName[recv->data.variable.name.length] = '\0';
                Symbol* sym = findSymbol(&compiler->symbols, tempName);
                if (sym && sym->isModule) {
                    fromModule = true;
                    mod = sym->module;
                    node->data.call.arguments = recv->next;
                    node->data.call.argCount--;
                }
            }

            // Attempt to resolve the function name
            ObjString* nameObj = allocateString(node->data.call.name.start,
                                                node->data.call.name.length);
            int nativeIdx = findNative(nameObj);
            node->data.call.nativeIndex = nativeIdx;
            node->data.call.builtinOp = -1;
            // Built-in functions
            if (!fromModule && tokenEquals(node->data.call.name, "len")) {
                if (node->data.call.argCount != 1) {
                    emitBuiltinArgCountError(compiler, &node->data.call.name,
                                             "len", 1,
                                             node->data.call.argCount);
                    return;
                }
                ASTNode* arg = node->data.call.arguments;
                typeCheckNode(compiler, arg);
                if (compiler->hadError) return;
                if (!arg->valueType || (arg->valueType->kind != TYPE_ARRAY &&
                                        arg->valueType->kind != TYPE_STRING)) {
                    const char* actualType =
                        arg->valueType ? getTypeName(arg->valueType->kind)
                                       : "unknown";
                    emitLenInvalidTypeError(compiler, &node->data.call.name,
                                            actualType);
                    return;
                }
                if (arg->valueType->kind == TYPE_ARRAY)
                    node->data.call.builtinOp = OP_LEN_ARRAY;
                else if (arg->valueType->kind == TYPE_STRING)
                    node->data.call.builtinOp = OP_LEN_STRING;
                node->valueType = getPrimitiveType(TYPE_I32);
                break;
            } else if (!fromModule &&
                       tokenEquals(node->data.call.name, "substring")) {
                if (node->data.call.argCount != 3) {
                    emitBuiltinArgCountError(compiler, &node->data.call.name,
                                             "substring", 3,
                                             node->data.call.argCount);
                    return;
                }
                ASTNode* strArg = node->data.call.arguments;
                ASTNode* startArg = strArg->next;
                ASTNode* lenArg = startArg->next;
                typeCheckNode(compiler, strArg);
                typeCheckNode(compiler, startArg);
                typeCheckNode(compiler, lenArg);
                if (compiler->hadError) return;
                if (!strArg->valueType ||
                    strArg->valueType->kind != TYPE_STRING) {
                    error(compiler,
                          "substring() first argument must be a string.");
                    return;
                }
                if (!startArg->valueType ||
                    startArg->valueType->kind != TYPE_I32) {
                    error(compiler, "substring() second argument must be i32.");
                    return;
                }
                if (!lenArg->valueType || lenArg->valueType->kind != TYPE_I32) {
                    error(compiler, "substring() third argument must be i32.");
                    return;
                }
                node->valueType = getPrimitiveType(TYPE_STRING);
                break;
            } else if (!fromModule &&
                       tokenEquals(node->data.call.name, "type_of")) {
                if (node->data.call.argCount != 1) {
                    // Special handling for zero arguments to ensure the error
                    // message is consistent
                    emitBuiltinArgCountError(compiler, &node->data.call.name,
                                             "type_of", 1,
                                             node->data.call.argCount);
                    return;
                }
                ASTNode* valArg = node->data.call.arguments;
                typeCheckNode(compiler, valArg);
                if (compiler->hadError) return;
                if (valArg->valueType) {
                    switch (valArg->valueType->kind) {
                        case TYPE_I32:
                            node->data.call.builtinOp = OP_TYPE_OF_I32;
                            break;
                        case TYPE_I64:
                            node->data.call.builtinOp = OP_TYPE_OF_I64;
                            break;
                        case TYPE_U32:
                            node->data.call.builtinOp = OP_TYPE_OF_U32;
                            break;
                        case TYPE_U64:
                            node->data.call.builtinOp = OP_TYPE_OF_U64;
                            break;
                        case TYPE_F64:
                            node->data.call.builtinOp = OP_TYPE_OF_F64;
                            break;
                        case TYPE_BOOL:
                            node->data.call.builtinOp = OP_TYPE_OF_BOOL;
                            break;
                        case TYPE_STRING:
                            node->data.call.builtinOp = OP_TYPE_OF_STRING;
                            break;
                        case TYPE_ARRAY:
                            node->data.call.builtinOp = OP_TYPE_OF_ARRAY;
                            break;
                        default:
                            break;
                    }
                }
                node->valueType = getPrimitiveType(TYPE_STRING);
                break;
            } else if (!fromModule &&
                       tokenEquals(node->data.call.name, "is_type")) {
                if (node->data.call.argCount != 2) {
                    emitBuiltinArgCountError(compiler, &node->data.call.name,
                                             "is_type", 2,
                                             node->data.call.argCount);
                    return;
                }
                ASTNode* valArg = node->data.call.arguments;
                ASTNode* typeArg = valArg->next;
                typeCheckNode(compiler, valArg);
                typeCheckNode(compiler, typeArg);
                if (compiler->hadError) return;
                if (!typeArg->valueType ||
                    typeArg->valueType->kind != TYPE_STRING) {
                    const char* actualType =
                        typeArg->valueType
                            ? getTypeName(typeArg->valueType->kind)
                            : "unknown";
                    emitIsTypeSecondArgError(compiler, &node->data.call.name,
                                             actualType);
                    return;
                }
                node->valueType = getPrimitiveType(TYPE_BOOL);
                break;
            } else if (!fromModule &&
                       tokenEquals(node->data.call.name, "input")) {
                if (node->data.call.argCount != 1) {
                    emitBuiltinArgCountError(compiler, &node->data.call.name,
                                             "input", 1,
                                             node->data.call.argCount);
                    return;
                }
                ASTNode* promptArg = node->data.call.arguments;
                typeCheckNode(compiler, promptArg);
                if (compiler->hadError) return;
                if (!promptArg->valueType ||
                    promptArg->valueType->kind != TYPE_STRING) {
                    error(compiler, "input() argument must be a string.");
                    return;
                }
                node->valueType = getPrimitiveType(TYPE_STRING);
                break;
            } else if (!fromModule &&
                       tokenEquals(node->data.call.name, "int")) {
                if (node->data.call.argCount != 1) {
                    emitBuiltinArgCountError(compiler, &node->data.call.name,
                                             "int", 1,
                                             node->data.call.argCount);
                    return;
                }
                ASTNode* arg = node->data.call.arguments;
                typeCheckNode(compiler, arg);
                if (compiler->hadError) return;
                if (!arg->valueType || arg->valueType->kind != TYPE_STRING) {
                    error(compiler, "int() argument must be a string.");
                    return;
                }
                node->valueType = getPrimitiveType(TYPE_I32);
                break;
            } else if (!fromModule &&
                       tokenEquals(node->data.call.name, "float")) {
                if (node->data.call.argCount != 1) {
                    emitBuiltinArgCountError(compiler, &node->data.call.name,
                                             "float", 1,
                                             node->data.call.argCount);
                    return;
                }
                ASTNode* arg = node->data.call.arguments;
                typeCheckNode(compiler, arg);
                if (compiler->hadError) return;
                if (!arg->valueType || arg->valueType->kind != TYPE_STRING) {
                    error(compiler, "float() argument must be a string.");
                    return;
                }
                node->valueType = getPrimitiveType(TYPE_F64);
                break;
            } else if (!fromModule &&
                       tokenEquals(node->data.call.name, "timestamp")) {
                if (node->data.call.argCount != 0) {
                    emitBuiltinArgCountError(compiler, &node->data.call.name,
                                             "timestamp", 0,
                                             node->data.call.argCount);
                    return;
                }
                node->valueType = getPrimitiveType(TYPE_F64);
                break;
            } else if (!fromModule &&
                       tokenEquals(node->data.call.name, "push")) {
                if (node->data.call.argCount != 2) {
                    emitBuiltinArgCountError(compiler, &node->data.call.name,
                                             "push", 2,
                                             node->data.call.argCount);
                    return;
                }
                // argCount == 2
                ASTNode* arr = node->data.call.arguments;
                ASTNode* val = arr->next;
                typeCheckNode(compiler, arr);
                typeCheckNode(compiler, val);
                if (compiler->hadError) return;
                if (arr->valueType && arr->valueType->kind == TYPE_ARRAY) {
                    if (arr->type == AST_VARIABLE) {
                        char buf[arr->data.variable.name.length + 1];
                        memcpy(buf, arr->data.variable.name.start,
                               arr->data.variable.name.length);
                        buf[arr->data.variable.name.length] = '\0';
                        Symbol* sym = findSymbol(&compiler->symbols, buf);
                        if (sym && sym->fixedArray) {
                            error(compiler,
                                  "push() cannot modify fixed-size array.");
                            return;
                        }
                    }
                    Type* elemType = arr->valueType->info.array.elementType;
                    if (elemType->kind == TYPE_NIL) {
                        if (arr->valueType->info.array.length >= 0) {
                            arr->valueType = createSizedArrayType(
                                val->valueType,
                                arr->valueType->info.array.length);
                        } else {
                            arr->valueType = createArrayType(val->valueType);
                        }
                        elemType = val->valueType;
                        if (arr->type == AST_VARIABLE) {
                            vm.globalTypes[arr->data.variable.index] =
                                arr->valueType;
                        }
                    }
                    if (!typesEqual(elemType, val->valueType)) {
                        error(compiler, "push() value type mismatch.");
                        return;
                    }
                    node->valueType = arr->valueType;
                    node->data.call.builtinOp = OP_ARRAY_PUSH;
                    break;
                }
                // Not an array: likely a method call, fall through
            } else if (!fromModule &&
                       tokenEquals(node->data.call.name, "pop")) {
                if (node->data.call.argCount != 1) {
                    emitBuiltinArgCountError(compiler, &node->data.call.name,
                                             "pop", 1,
                                             node->data.call.argCount);
                    return;
                }
                // argCount == 1
                ASTNode* arr = node->data.call.arguments;
                typeCheckNode(compiler, arr);
                if (compiler->hadError) return;
                if (arr->valueType && arr->valueType->kind == TYPE_ARRAY) {
                    if (arr->type == AST_VARIABLE) {
                        char buf[arr->data.variable.name.length + 1];
                        memcpy(buf, arr->data.variable.name.start,
                               arr->data.variable.name.length);
                        buf[arr->data.variable.name.length] = '\0';
                        Symbol* sym = findSymbol(&compiler->symbols, buf);
                        if (sym && sym->fixedArray) {
                            error(compiler,
                                  "pop() cannot modify fixed-size array.");
                            return;
                        }
                    }
                    node->valueType = arr->valueType->info.array.elementType;
                    node->data.call.builtinOp = OP_ARRAY_POP;
                    break;
                }
                // Not an array: treat as normal call
            } else if (!fromModule &&
                       tokenEquals(node->data.call.name, "reserve")) {
                if (node->data.call.argCount != 2) {
                    emitBuiltinArgCountError(compiler, &node->data.call.name,
                                             "reserve", 2,
                                             node->data.call.argCount);
                    return;
                }
                ASTNode* arr = node->data.call.arguments;
                ASTNode* cap = arr->next;
                typeCheckNode(compiler, arr);
                typeCheckNode(compiler, cap);
                if (compiler->hadError) return;
                if (arr->valueType && arr->valueType->kind == TYPE_ARRAY) {
                    if (arr->type == AST_VARIABLE) {
                        char buf[arr->data.variable.name.length + 1];
                        memcpy(buf, arr->data.variable.name.start,
                               arr->data.variable.name.length);
                        buf[arr->data.variable.name.length] = '\0';
                        Symbol* sym = findSymbol(&compiler->symbols, buf);
                        if (sym && sym->fixedArray) {
                            error(compiler,
                                  "reserve() cannot modify fixed-size array.");
                            return;
                        }
                    }
                    if (cap->valueType && (cap->valueType->kind == TYPE_I32 ||
                                           cap->valueType->kind == TYPE_I64 ||
                                           cap->valueType->kind == TYPE_U32 ||
                                           cap->valueType->kind == TYPE_U64)) {
                        node->valueType = arr->valueType;
                        node->data.call.builtinOp = OP_ARRAY_RESERVE;
                        break;
                    }
                }
                // Not an array or invalid capacity type: treat as normal call
            } else if (!fromModule &&
                       tokenEquals(node->data.call.name, "sorted")) {
                if (node->data.call.argCount < 1 ||
                    node->data.call.argCount > 3) {
                    error(compiler,
                          "sorted() takes between 1 and 3 arguments.");
                    return;
                }

                ASTNode* arr = node->data.call.arguments;
                typeCheckNode(compiler, arr);
                if (compiler->hadError) return;
                if (!arr->valueType || arr->valueType->kind != TYPE_ARRAY) {
                    error(compiler, "sorted() first argument must be array.");
                    return;
                }

                if (node->data.call.argCount == 2) {
                    ASTNode* second = arr->next;
                    typeCheckNode(compiler, second);
                    if (compiler->hadError) return;
                    if (!second->valueType) return;  // safety
                    if (second->valueType->kind == TYPE_BOOL) {
                        // reverse flag only
                    } else if (second->valueType->kind != TYPE_NIL) {
                        error(compiler,
                              "sorted() key function not supported yet.");
                        return;
                    }
                } else if (node->data.call.argCount == 3) {
                    ASTNode* key = arr->next;
                    typeCheckNode(compiler, key);
                    if (compiler->hadError) return;
                    if (!key->valueType || key->valueType->kind != TYPE_NIL) {
                        error(compiler,
                              "sorted() key function not supported yet.");
                        return;
                    }

                    ASTNode* rev = key->next;
                    typeCheckNode(compiler, rev);
                    if (compiler->hadError) return;
                    if (!rev->valueType || rev->valueType->kind != TYPE_BOOL) {
                        error(compiler,
                              "sorted() third argument must be bool.");
                        return;
                    }
                }

                node->valueType = arr->valueType;
                break;
            }

            uint8_t index;
            if (fromModule) {
                char fname[node->data.call.name.length + 1];
                memcpy(fname, node->data.call.name.start,
                       node->data.call.name.length);
                fname[node->data.call.name.length] = '\0';
                Export* ex = get_export(mod, fname);
                if (!ex) {
                    errorFmt(compiler, "Symbol `%s` not found in module `%s`",
                             fname, mod->module_name);
                    return;
                }
                index = ex->index;
            } else {
                index = resolveVariable(compiler, node->data.call.name);
            }

            // If the function name matches a built-in but wasn't defined in
            // the current scope, provide a helpful argument count error instead
            // of reporting it as an undefined function.
            if (index == UINT8_MAX && node->data.call.nativeIndex != -1) {
                int expected =
                    vm.nativeFunctions[node->data.call.nativeIndex].arity;
                const char* builtinName =
                    vm.nativeFunctions[node->data.call.nativeIndex].name->chars;
                if (expected >= 0 && node->data.call.argCount != expected) {
                    emitBuiltinArgCountError(compiler, &node->data.call.name,
                                             builtinName, expected,
                                             node->data.call.argCount);
                    return;
                }
            }

            // Type check arguments first to know the type of the receiver
            ASTNode* arg = node->data.call.arguments;
            while (arg != NULL) {
                typeCheckNode(compiler, arg);
                if (compiler->hadError) return;
                arg = arg->next;
            }

            // If call specifies a static struct type, try mangled name first
            if (node->data.call.staticType != NULL) {
                const char* structName =
                    node->data.call.staticType->info.structure.name->chars;
                size_t structLen = strlen(structName);
                size_t nameLen = node->data.call.name.length;
                char* temp = (char*)malloc(structLen + 1 + nameLen + 1);
                memcpy(temp, structName, structLen);
                temp[structLen] = '_';
                memcpy(temp + structLen + 1, node->data.call.name.start,
                       nameLen);
                temp[structLen + 1 + nameLen] = '\0';
                Symbol* sym = findSymbol(&compiler->symbols, temp);
                if (sym) {
                    index = sym->index;
                    ObjString* fullStr =
                        allocateString(temp, structLen + 1 + nameLen);
                    node->data.call.name.start = fullStr->chars;
                    node->data.call.name.length = structLen + 1 + nameLen;
                    node->data.call.mangledName = fullStr;
                }
                free(temp);
            } else if (index == UINT8_MAX &&
                       node->data.call.arguments != NULL) {
                // If not found, try mangled method name based on first argument
                // (instance method)
                ASTNode* recv = node->data.call.arguments;
                Type* recvType = recv->valueType;
                if (recvType && recvType->kind == TYPE_STRUCT) {
                    const char* structName =
                        recvType->info.structure.name->chars;
                    size_t structLen = strlen(structName);
                    size_t nameLen = node->data.call.name.length;
                    char* temp = (char*)malloc(structLen + 1 + nameLen + 1);
                    memcpy(temp, structName, structLen);
                    temp[structLen] = '_';
                    memcpy(temp + structLen + 1, node->data.call.name.start,
                           nameLen);
                    temp[structLen + 1 + nameLen] = '\0';
                    Symbol* sym = findSymbol(&compiler->symbols, temp);
                    if (sym) {
                        index = sym->index;
                        ObjString* fullStr =
                            allocateString(temp, structLen + 1 + nameLen);
                        node->data.call.name.start = fullStr->chars;
                        node->data.call.name.length = structLen + 1 + nameLen;
                        node->data.call.mangledName = fullStr;
                    }
                    free(temp);
                }
            }

            if (index == UINT8_MAX) {
                char tempName[node->data.call.name.length + 1];
                memcpy(tempName, node->data.call.name.start,
                       node->data.call.name.length);
                tempName[node->data.call.name.length] = '\0';
                uint8_t priv =
                    findPrivateGlobal(tempName, node->data.call.name.length);
                if (priv != UINT8_MAX && vm.globalTypes[priv] &&
                    vm.globalTypes[priv]->kind == TYPE_FUNCTION) {
                    emitPrivateFunctionError(compiler, &node->data.call.name);
                    return;
                }
                if (node->data.call.nativeIndex != -1 &&
                    (tokenEquals(node->data.call.name, "sum") ||
                     tokenEquals(node->data.call.name, "min") ||
                     tokenEquals(node->data.call.name, "max"))) {
                    const char* fname = node->data.call.name.start;
                    ASTNode* arr = node->data.call.arguments;
                    if (!arr->valueType || arr->valueType->kind != TYPE_ARRAY) {
                        char msg[32];
                        snprintf(msg, sizeof(msg), "%s() expects array.",
                                 fname);
                        error(compiler, msg);
                        return;
                    }
                    Type* elem = arr->valueType->info.array.elementType;
                    if (elem->kind != TYPE_I32 && elem->kind != TYPE_U32 &&
                        elem->kind != TYPE_F64) {
                        char msg[64];
                        snprintf(msg, sizeof(msg),
                                 "%s() array must contain numbers.", fname);
                        error(compiler, msg);
                        return;
                    }
                    node->valueType = elem;
                    break;
                }
                emitUndefinedFunctionError(compiler, &node->data.call.name);
                return;
            }

            node->data.call.index = index;
            node->data.call.nativeIndex = -1;  // prefer user-defined function

            // Get the function's return type
            Type* funcType = vm.globalTypes[index];
            if (!funcType || funcType->kind != TYPE_FUNCTION) {
                error(compiler, "Called object is not a function.");
                return;
            }

            ASTNode* fnNode = vm.functionDecls[index];
            ObjString** gnames = NULL;
            int gcount = 0;
            if (fnNode) {
                gnames = fnNode->data.function.genericParams;
                gcount = fnNode->data.function.genericCount;
            }
            Type** gsubs = NULL;
            if (gcount > 0) {
                gsubs = (Type**)calloc(gcount, sizeof(Type*));
                if (node->data.call.genericArgCount > 0) {
                    if (node->data.call.genericArgCount != gcount) {
                        char msgBuffer[128];
                        snprintf(msgBuffer, sizeof(msgBuffer),
                                 "generic argument count mismatch: expected "
                                 "%d, found %d",
                                 gcount, node->data.call.genericArgCount);
                        char helpBuffer[128];
                        snprintf(helpBuffer, sizeof(helpBuffer),
                                 "function expects %d generic type "
                                 "parameter(s), but %d were provided",
                                 gcount, node->data.call.genericArgCount);
                        const char* note =
                            "Check the function definition and provide the "
                            "correct number of generic arguments.";
                        emitGenericTypeError(compiler, &node->data.call.name,
                                             msgBuffer, helpBuffer, note);
                        return;
                    }
                    for (int i = 0; i < gcount; i++)
                        gsubs[i] = node->data.call.genericArgs[i];
                }
            }

            ASTNode* argIt = node->data.call.arguments;
            ASTNode* argNodes[256];
            int acount = 0;
            while (argIt != NULL && acount < 256) {
                argNodes[acount++] = argIt;
                argIt = argIt->next;
            }

            node->data.call.convertArgs =
                (bool*)calloc(node->data.call.argCount, sizeof(bool));

            for (int i = 0; i < funcType->info.function.paramCount; i++) {
                Type* expected = funcType->info.function.paramTypes[i];
                if (gcount > 0 && i < acount) {
                    deduceGenerics(expected, argNodes[i]->valueType, gnames,
                                   gsubs, gcount);
                }
                if (gcount > 0) {
                    expected =
                        substituteGenerics(expected, gnames, gsubs, gcount);
                }
                if (i < acount && argNodes[i]->valueType &&
                    argNodes[i]->valueType->kind == TYPE_ARRAY &&
                    argNodes[i]->valueType->info.array.elementType->kind ==
                        TYPE_NIL &&
                    expected->kind == TYPE_ARRAY) {
                    argNodes[i]->valueType = expected;
                    if (argNodes[i]->type == AST_VARIABLE) {
                        vm.globalTypes[argNodes[i]->data.variable.index] =
                            expected;
                    }
                }
                if (i >= acount) {
                    const char* expectedType = getTypeName(expected->kind);
                    const char* actualType = "(none)";
                    emitTypeMismatchError(compiler, &node->data.call.name,
                                          expectedType, actualType);
                    return;
                }

                if (!typesEqual(expected, argNodes[i]->valueType)) {
                    opCode op = conversionOp(argNodes[i]->valueType->kind,
                                             expected->kind);
                    if (op != OP_RETURN && isNumericKind(expected->kind) &&
                        isNumericKind(argNodes[i]->valueType->kind)) {
                        node->data.call.convertArgs[i] = true;
                    } else {
                        const char* expectedType = getTypeName(expected->kind);
                        const char* actualType =
                            argNodes[i] && argNodes[i]->valueType
                                ? getTypeName(argNodes[i]->valueType->kind)
                                : "(none)";
                        emitTypeMismatchError(compiler, &node->data.call.name,
                                              expectedType, actualType);
                        return;
                    }
                }
            }

            Type* returnType = substituteGenerics(
                funcType->info.function.returnType, gnames, gsubs, gcount);
            node->valueType = returnType;
            break;
        }

        case AST_ARRAY: {
            ASTNode* elem = node->data.array.elements;
            Type* elementType = NULL;
            while (elem) {
                typeCheckNode(compiler, elem);
                if (compiler->hadError) return;
                if (!elementType)
                    elementType = elem->valueType;
                else if (!typesEqual(elementType, elem->valueType)) {
                    error(compiler, "Array elements must have the same type.");
                    return;
                }
                elem = elem->next;
            }
            if (!elementType) {
                if (node->data.array.elementCount == 0) {
                    node->valueType =
                        createArrayType(getPrimitiveType(TYPE_NIL));
                } else {
                    node->valueType =
                        createSizedArrayType(getPrimitiveType(TYPE_NIL),
                                             node->data.array.elementCount);
                }
            } else {
                if (node->data.array.elementCount == 0) {
                    node->valueType = createArrayType(elementType);
                } else {
                    node->valueType = createSizedArrayType(
                        elementType, node->data.array.elementCount);
                }
            }
            break;
        }

        case AST_ARRAY_FILL: {
            typeCheckNode(compiler, node->data.arrayFill.value);
            if (compiler->hadError) return;
            typeCheckNode(compiler, node->data.arrayFill.length);
            if (compiler->hadError) return;

            int64_t len;
            if (!evaluateConstantInt(compiler, node->data.arrayFill.length,
                                     &len)) {
                error(compiler,
                      "Array fill length must be a compile-time constant.");
                return;
            }
            node->data.arrayFill.lengthValue = (int)len;

            if (!node->data.arrayFill.value->valueType ||
                node->data.arrayFill.value->valueType->kind == TYPE_NIL) {
                error(compiler, "Cannot infer array element type.");
                return;
            }

            node->valueType =
                createSizedArrayType(node->data.arrayFill.value->valueType,
                                     node->data.arrayFill.lengthValue);
            break;
        }

        case AST_STRUCT_LITERAL: {
            compiler->currentColumn =
                tokenColumn(compiler, &node->data.structLiteral.name);
            Type* structType =
                findStructTypeToken(node->data.structLiteral.name);
            if (!structType) {
                error(compiler, "Unknown struct type.");
                return;
            }
            if (node->data.structLiteral.genericArgCount > 0) {
                structType = instantiateStructType(
                    structType, node->data.structLiteral.genericArgs,
                    node->data.structLiteral.genericArgCount);
            }
            if (structType->info.structure.fieldCount !=
                node->data.structLiteral.fieldCount) {
                error(compiler, "Struct literal field count mismatch.");
                return;
            }
            ASTNode* value = node->data.structLiteral.values;
            for (int i = 0; i < node->data.structLiteral.fieldCount; i++) {
                if (!value) {
                    error(compiler, "Missing struct field value.");
                    return;
                }
                typeCheckNode(compiler, value);
                if (compiler->hadError) return;
                Type* expected = structType->info.structure.fields[i].type;
                if (value->type == AST_ARRAY && value->valueType &&
                    value->valueType->kind == TYPE_ARRAY &&
                    value->valueType->info.array.elementType->kind ==
                        TYPE_NIL &&
                    expected->kind == TYPE_ARRAY) {
                    value->valueType = expected;
                }
                if (expected->kind == TYPE_ARRAY &&
                    expected->info.array.length < 0 && value->valueType &&
                    value->valueType->kind == TYPE_ARRAY &&
                    typesEqual(expected->info.array.elementType,
                               value->valueType->info.array.elementType)) {
                    value->valueType = expected;
                } else if (expected->kind == TYPE_ARRAY &&
                           value->type == AST_ARRAY_FILL && value->valueType &&
                           value->valueType->kind == TYPE_ARRAY) {
                    ASTNode* val = value->data.arrayFill.value;
                    convertLiteralForDecl(val, val->valueType,
                                          expected->info.array.elementType);
                    if (typesEqual(val->valueType,
                                   expected->info.array.elementType)) {
                        value->valueType = expected;
                    }
                }
                if (!typesEqual(expected, value->valueType)) {
                    if (expected->kind == TYPE_U32 &&
                        value->type == AST_LITERAL && value->valueType &&
                        value->valueType->kind == TYPE_I32 &&
                        IS_I32(value->data.literal) &&
                        AS_I32(value->data.literal) >= 0) {
                        int32_t v = AS_I32(value->data.literal);
                        value->data.literal = U32_VAL((uint32_t)v);
                        value->valueType = expected;
                    } else if (expected->kind == TYPE_I32 &&
                               value->type == AST_LITERAL && value->valueType &&
                               value->valueType->kind == TYPE_U32 &&
                               IS_U32(value->data.literal) &&
                               AS_U32(value->data.literal) <= INT32_MAX) {
                        uint32_t v = AS_U32(value->data.literal);
                        value->data.literal = I32_VAL((int32_t)v);
                        value->valueType = expected;
                    }
                }
                if (!typesEqual(expected, value->valueType)) {
                    const char* structName =
                        structType->info.structure.name->chars;
                    const char* fieldName =
                        structType->info.structure.fields[i].name->chars;
                    const char* expectedType = getTypeName(expected->kind);
                    const char* actualType =
                        value->valueType ? getTypeName(value->valueType->kind)
                                         : "(none)";
                    emitStructFieldTypeMismatchError(
                        compiler, &node->data.structLiteral.name, structName,
                        fieldName, expectedType, actualType);
                    return;
                }
                value = value->next;
            }
            node->valueType = structType;
            break;
        }

        case AST_FIELD: {
            compiler->currentColumn =
                tokenColumn(compiler, &node->data.field.fieldName);

            // Support module.field access
            if (node->left->type == AST_VARIABLE) {
                char tempName[node->left->data.variable.name.length + 1];
                memcpy(tempName, node->left->data.variable.name.start,
                       node->left->data.variable.name.length);
                tempName[node->left->data.variable.name.length] = '\0';
                Symbol* sym = findSymbol(&compiler->symbols, tempName);
                if (sym && sym->isModule) {
                    char fieldName[node->data.field.fieldName.length + 1];
                    memcpy(fieldName, node->data.field.fieldName.start,
                           node->data.field.fieldName.length);
                    fieldName[node->data.field.fieldName.length] = '\0';
                    Export* ex = get_export(sym->module, fieldName);
                    if (!ex) {
                        errorFmt(compiler,
                                 "Symbol `%s` not found in module `%s`",
                                 fieldName, sym->module->module_name);
                        return;
                    }
                    node->type = AST_VARIABLE;
                    node->data.variable.name = node->data.field.fieldName;
                    node->data.variable.index = ex->index;
                    node->left = NULL;
                    node->valueType = vm.globalTypes[ex->index];
                    break;
                }
            }

            typeCheckNode(compiler, node->left);
            if (compiler->hadError) return;
            Type* structType = node->left->valueType;
            if (!structType || structType->kind != TYPE_STRUCT) {
                const char* actualType =
                    structType ? getTypeName(structType->kind) : "(none)";
                emitFieldAccessNonStructError(
                    compiler, &node->data.field.fieldName, actualType);
                return;
            }
            int index = -1;
            for (int i = 0; i < structType->info.structure.fieldCount; i++) {
                if (strncmp(structType->info.structure.fields[i].name->chars,
                            node->data.field.fieldName.start,
                            node->data.field.fieldName.length) == 0 &&
                    structType->info.structure.fields[i]
                            .name->chars[node->data.field.fieldName.length] ==
                        '\0') {
                    index = i;
                    break;
                }
            }
            if (index < 0) {
                emitTokenError(compiler, &node->data.field.fieldName,
                               ERROR_GENERAL, "Unknown field name.");
                return;
            }
            node->data.field.index = index;
            node->valueType = structType->info.structure.fields[index].type;
            break;
        }

        case AST_FIELD_SET: {
            compiler->currentColumn =
                tokenColumn(compiler, &node->data.fieldSet.fieldName);
            typeCheckNode(compiler, node->right);  // object
            if (compiler->hadError) return;
            Type* structType = node->right->valueType;
            if (!structType || structType->kind != TYPE_STRUCT) {
                error(compiler, "Can only set fields on structs.");
                return;
            }
            int index = -1;
            for (int i = 0; i < structType->info.structure.fieldCount; i++) {
                if (strncmp(structType->info.structure.fields[i].name->chars,
                            node->data.fieldSet.fieldName.start,
                            node->data.fieldSet.fieldName.length) == 0 &&
                    structType->info.structure.fields[i]
                            .name
                            ->chars[node->data.fieldSet.fieldName.length] ==
                        '\0') {
                    index = i;
                    break;
                }
            }
            if (index < 0) {
                emitTokenError(compiler, &node->data.fieldSet.fieldName,
                               ERROR_GENERAL, "Unknown field name.");
                return;
            }
            node->data.fieldSet.index = index;
            typeCheckNode(compiler, node->left);  // value
            if (compiler->hadError) return;
            if (!typesEqual(structType->info.structure.fields[index].type,
                            node->left->valueType)) {
                error(compiler, "Type mismatch in field assignment.");
                return;
            }
            node->valueType = structType->info.structure.fields[index].type;
            break;
        }

        case AST_ARRAY_SET: {
            typeCheckNode(compiler, node->right);  // array expression
            if (compiler->hadError) return;
            typeCheckNode(compiler, node->data.arraySet.index);
            if (compiler->hadError) return;
            typeCheckNode(compiler, node->left);  // value
            if (compiler->hadError) return;

            Type* arrayType = node->right->valueType;
            Type* indexType = node->data.arraySet.index->valueType;
            Type* valueType = node->left->valueType;
            if (!arrayType || arrayType->kind != TYPE_ARRAY) {
                error(compiler, "Can only assign to array elements.");
                return;
            }
            if (!indexType ||
                (indexType->kind != TYPE_I32 && indexType->kind != TYPE_U32)) {
                error(compiler, "Array index must be an integer.");
                return;
            }
            Type* elementType = arrayType->info.array.elementType;
            if (!typesEqual(elementType, valueType)) {
                error(compiler, "Type mismatch in array assignment.");
                return;
            }
            node->valueType = elementType;
            break;
        }

        case AST_SLICE: {
            typeCheckNode(compiler, node->left);  // array
            if (node->data.slice.start)
                typeCheckNode(compiler, node->data.slice.start);
            if (node->data.slice.end)
                typeCheckNode(compiler, node->data.slice.end);
            if (compiler->hadError) return;
            Type* arrayType = node->left->valueType;
            if (!arrayType || arrayType->kind != TYPE_ARRAY) {
                error(compiler, "Can only slice arrays.");
                return;
            }
            if (node->data.slice.start) {
                Type* startType = node->data.slice.start->valueType;
                if (!startType || (startType->kind != TYPE_I32 &&
                                   startType->kind != TYPE_U32)) {
                    error(compiler, "Slice start index must be an integer.");
                    return;
                }
            }
            if (node->data.slice.end) {
                Type* endType = node->data.slice.end->valueType;
                if (!endType ||
                    (endType->kind != TYPE_I32 && endType->kind != TYPE_U32)) {
                    error(compiler, "Slice end index must be an integer.");
                    return;
                }
            }
            node->valueType = node->left->valueType;
            break;
        }

        case AST_RETURN: {
            Type* expected = compiler->currentReturnType;
            if (node->data.returnStmt.value != NULL) {
                typeCheckNode(compiler, node->data.returnStmt.value);
                if (compiler->hadError) return;
                if (!expected || expected->kind == TYPE_VOID) {
                    error(compiler, "Return value provided in void function.");
                } else if (!compiler->currentFunctionHasGenerics &&
                           expected->kind != TYPE_GENERIC &&
                           node->data.returnStmt.value->valueType &&
                           node->data.returnStmt.value->valueType->kind !=
                               TYPE_GENERIC &&
                           !typesEqual(
                               expected,
                               node->data.returnStmt.value->valueType)) {
                    const char* expName = getTypeName(expected->kind);
                    const char* actName =
                        node->data.returnStmt.value->valueType
                            ? getTypeName(
                                  node->data.returnStmt.value->valueType->kind)
                            : "unknown";
                    char msg[128];
                    snprintf(msg, sizeof(msg),
                             "Error: Return type mismatch in function. "
                             "Expected '%s', found '%s'.",
                             expName, actName);
                    emitSimpleError(compiler, ERROR_GENERAL, msg);
                }
            } else if (expected && expected->kind != TYPE_VOID) {
                char msg[128];
                snprintf(msg, sizeof(msg),
                         "Error: Expected return value of type '%s', but found "
                         "empty return.",
                         getTypeName(expected->kind));
                emitSimpleError(compiler, ERROR_GENERAL, msg);
            }

            node->valueType = NULL;
            break;
        }

        case AST_BREAK: {
            // Break statements don't have a value type
            node->valueType = NULL;
            break;
        }

        case AST_CONTINUE: {
            // Continue statements don't have a value type
            node->valueType = NULL;
            break;
        }

        case AST_IMPORT: {
            node->valueType = NULL;
            break;
        }
        case AST_USE: {
            // Load the module and bind a module alias symbol
            const char* path = node->data.useStmt.path->chars;
            InterpretResult r = compile_module_only(path);
            if (r != INTERPRET_OK) {
                if (moduleError) {
                    error(compiler, moduleError);
                } else if (r == INTERPRET_RUNTIME_ERROR) {
                    errorFmt(compiler, "Module `%s` not found", path);
                } else {
                    errorFmt(compiler, "Failed to load module `%s`", path);
                }
                compiler->hadError = true;
                break;
            }

            Module* mod = get_module(path);
            if (!mod) {
                errorFmt(compiler, "Module `%s` not found", path);
                break;
            }

            const char* aliasName = node->data.useStmt.alias
                                        ? node->data.useStmt.alias->chars
                                        : mod->name;
            Token t;
            t.start = aliasName;
            t.length = (int)strlen(aliasName);
            t.line = node->line;
            addSymbol(&compiler->symbols, aliasName, t, NULL,
                      compiler->scopeDepth, UINT8_MAX, false, false, true, mod);

            node->valueType = NULL;
            break;
        }

        case AST_TRY: {
            beginScope(compiler);
            Type* strType = getPrimitiveType(TYPE_STRING);
            uint8_t idx = addLocal(compiler, node->data.tryStmt.errorName,
                                   strType, true, false);
            node->data.tryStmt.errorIndex = idx;
            typeCheckNode(compiler, node->data.tryStmt.tryBlock);
            if (compiler->hadError) {
                endScope(compiler);
                return;
            }
            typeCheckNode(compiler, node->data.tryStmt.catchBlock);
            endScope(compiler);
            node->valueType = NULL;
            break;
        }

        default:
            error(compiler, "Unsupported AST node type in type checker.");
            break;
    }
}

/**
 * Emit bytecode instructions for a single AST node.
 *
 * @param compiler Active compiler instance.
 * @param node     AST node to translate.
 */
static void generateCode(Compiler* compiler, ASTNode* node) {
    if (!node || compiler->hadError) {
        return;
    }

    // Record the line number of the node to annotate emitted bytecode
    compiler->currentLine = node->line;

    switch (node->type) {
        case AST_LITERAL: {
            // Debug log to trace AST nodes generating constants
            // fprintf(stderr, "DEBUG: Generating constant from AST node of
            // type: %d\n", node->type);
            if (node->type == AST_LITERAL) {
                // fprintf(stderr, "DEBUG: Literal value: ");
                // printValue(node->data.literal);
                // fprintf(stderr, "\n");
            }
            emitConstant(compiler, node->data.literal);
            break;
        }

        case AST_BINARY: {
            // Generate code for the left operand
            generateCode(compiler, node->left);
            if (compiler->hadError) return;

            // Convert left operand to result type before generating the right
            if (node->data.operation.convertLeft) {
                Type* leftType = node->left->valueType;
                TypeKind resultType = node->valueType->kind;

                switch (resultType) {
                    case TYPE_F64:
                        if (leftType->kind == TYPE_I32)
                            writeOp(compiler, OP_I32_TO_F64);
                        else if (leftType->kind == TYPE_U32)
                            writeOp(compiler, OP_U32_TO_F64);
                        else if (leftType->kind == TYPE_I64)
                            ; /* already i64 -> f64 handled later if needed */
                        else {
                            char msgBuffer[256];
                            const char* leftTypeName =
                                getTypeName(leftType->kind);
                            snprintf(msgBuffer, sizeof(msgBuffer),
                                     "Unsupported left operand conversion for "
                                     "binary operation. Left type: '%s', "
                                     "operation at line %d",
                                     leftTypeName,
                                     node->data.operation.operator.line);
                            error(compiler, msgBuffer);
                            return;
                        }
                        break;
                    case TYPE_I64:
                        if (leftType->kind == TYPE_I32)
                            writeOp(compiler, OP_I32_TO_I64);
                        else if (leftType->kind == TYPE_U32)
                            writeOp(compiler, OP_U32_TO_I64);
                        break;
                    case TYPE_STRING:
                        switch (leftType->kind) {
                            case TYPE_I32:
                                writeOp(compiler, OP_I32_TO_STRING);
                                break;
                            case TYPE_U32:
                                writeOp(compiler, OP_U32_TO_STRING);
                                break;
                            case TYPE_F64:
                                writeOp(compiler, OP_F64_TO_STRING);
                                break;
                            case TYPE_BOOL:
                                writeOp(compiler, OP_BOOL_TO_STRING);
                                break;
                            case TYPE_ARRAY:
                                writeOp(compiler, OP_ARRAY_TO_STRING);
                                break;
                            case TYPE_STRUCT:
                                writeOp(compiler, OP_ARRAY_TO_STRING);
                                break;
                            default:
                                error(compiler,
                                      "Unsupported left operand conversion for "
                                      "binary operation.");
                                return;
                        }
                        break;
                    default:
                        error(compiler,
                              "Unsupported result type for binary operation.");
                        return;
                }
            }

            // Generate code for the right operand
            generateCode(compiler, node->right);
            if (compiler->hadError) return;

            // Get operand and result types
            Type* leftType = node->left->valueType;
            Type* rightType = node->right->valueType;
            TypeKind resultType = node->valueType->kind;

            // Convert right operand to result type if needed
            if (node->data.operation.convertRight) {
                switch (resultType) {
                    case TYPE_F64:
                        if (rightType->kind == TYPE_I32)
                            writeOp(compiler, OP_I32_TO_F64);
                        else if (rightType->kind == TYPE_U32)
                            writeOp(compiler, OP_U32_TO_F64);
                        else {
                            error(compiler,
                                  "Unsupported right operand conversion for "
                                  "binary operation.");
                            return;
                        }
                        break;
                    case TYPE_I64:
                        if (rightType->kind == TYPE_I32)
                            writeOp(compiler, OP_I32_TO_I64);
                        else if (rightType->kind == TYPE_U32)
                            writeOp(compiler, OP_U32_TO_I64);
                        break;
                    case TYPE_U64:
                        if (rightType->kind == TYPE_I32)
                            writeOp(compiler, OP_I32_TO_U64);
                        else if (rightType->kind == TYPE_U32)
                            writeOp(compiler, OP_U32_TO_U64);
                        else if (rightType->kind == TYPE_I64)
                            writeOp(compiler, OP_I64_TO_U64);
                        break;
                    case TYPE_STRING:
                        switch (rightType->kind) {
                            case TYPE_I32:
                                writeOp(compiler, OP_I32_TO_STRING);
                                break;
                            case TYPE_U32:
                                writeOp(compiler, OP_U32_TO_STRING);
                                break;
                            case TYPE_F64:
                                writeOp(compiler, OP_F64_TO_STRING);
                                break;
                            case TYPE_BOOL:
                                writeOp(compiler, OP_BOOL_TO_STRING);
                                break;
                            case TYPE_ARRAY:
                                writeOp(compiler, OP_ARRAY_TO_STRING);
                                break;
                            case TYPE_STRUCT:
                                writeOp(compiler, OP_ARRAY_TO_STRING);
                                break;
                            default: {
                                char msgBuffer[256];
                                const char* rightTypeName =
                                    getTypeName(rightType->kind);
                                snprintf(msgBuffer, sizeof(msgBuffer),
                                         "Unsupported right operand conversion "
                                         "for binary operation. Right type: "
                                         "'%s', operation at line %d",
                                         rightTypeName,
                                         node->data.operation.operator.line);
                                error(compiler, msgBuffer);
                                return;
                            }
                        }
                        break;
                    default: {
                        const char* leftTypeName =
                            leftType ? getTypeName(leftType->kind) : "(none)";
                        const char* rightTypeName =
                            rightType ? getTypeName(rightType->kind) : "(none)";
                        char msgBuffer[256];
                        snprintf(msgBuffer, sizeof(msgBuffer),
                                 "unsupported right operand conversion for "
                                 "binary operation: left type '%s', right type "
                                 "'%s', attempted result type '%s'",
                                 leftTypeName, rightTypeName,
                                 getTypeName(resultType));
                        char helpBuffer[128];
                        snprintf(helpBuffer, sizeof(helpBuffer),
                                 "try converting the right operand to a "
                                 "compatible type or use explicit string "
                                 "conversion (e.g., str(x))");
                        const char* note =
                            "Orus does not support implicit conversion between "
                            "these types in this operation";
                        emitGenericTypeError(compiler,
                                             &node->data.operation.operator,
                                             msgBuffer, helpBuffer, note);
                        return;
                    }
                }
            }

            // Emit the operator instruction
            TokenType operator = node->data.operation.operator.type;

            switch (operator) {
                case TOKEN_PLUS:
                    switch (resultType) {
                        case TYPE_STRING:
                            writeOp(compiler, OP_CONCAT);
                            break;
                        case TYPE_I32:
                            writeOp(compiler, OP_ADD_I32);
                            break;
                        case TYPE_I64:
                            writeOp(compiler, OP_ADD_I64);
                            break;
                        case TYPE_U32:
                            writeOp(compiler, OP_ADD_U32);
                            break;
                        case TYPE_U64:
                            writeOp(compiler, OP_ADD_U64);
                            break;
                        case TYPE_F64:
                            writeOp(compiler, OP_ADD_F64);
                            break;
                        case TYPE_GENERIC:
                            writeOp(compiler, OP_ADD_NUMERIC);
                            break;
                        default:
                            error(compiler,
                                  "Addition not supported for this type.");
                            return;
                    }
                    break;

                case TOKEN_MINUS:
                    switch (resultType) {
                        case TYPE_I32:
                            writeOp(compiler, OP_SUBTRACT_I32);
                            break;
                        case TYPE_I64:
                            writeOp(compiler, OP_SUBTRACT_I64);
                            break;
                        case TYPE_U32:
                            writeOp(compiler, OP_SUBTRACT_U32);
                            break;
                        case TYPE_U64:
                            writeOp(compiler, OP_SUBTRACT_U64);
                            break;
                        case TYPE_F64:
                            writeOp(compiler, OP_SUBTRACT_F64);
                            break;
                        case TYPE_GENERIC:
                            writeOp(compiler, OP_SUBTRACT_NUMERIC);
                            break;
                        default:
                            error(compiler,
                                  "Subtraction not supported for this type.");
                            return;
                    }
                    break;

                case TOKEN_STAR:
                    switch (resultType) {
                        case TYPE_I32:
                            writeOp(compiler, OP_MULTIPLY_I32);
                            break;
                        case TYPE_I64:
                            writeOp(compiler, OP_MULTIPLY_I64);
                            break;
                        case TYPE_U32:
                            writeOp(compiler, OP_MULTIPLY_U32);
                            break;
                        case TYPE_U64:
                            writeOp(compiler, OP_MULTIPLY_U64);
                            break;
                        case TYPE_F64:
                            writeOp(compiler, OP_MULTIPLY_F64);
                            break;
                        case TYPE_GENERIC:
                            writeOp(compiler, OP_MULTIPLY_NUMERIC);
                            break;
                        default:
                            error(
                                compiler,
                                "Multiplication not supported for this type.");
                            return;
                    }
                    break;
                case TOKEN_SLASH:
                    switch (resultType) {
                        case TYPE_I32:
                            writeOp(compiler, OP_DIVIDE_I32);
                            break;
                        case TYPE_I64:
                            writeOp(compiler, OP_DIVIDE_I64);
                            break;
                        case TYPE_U32:
                            writeOp(compiler, OP_DIVIDE_U32);
                            break;
                        case TYPE_U64:
                            writeOp(compiler, OP_DIVIDE_U64);
                            break;
                        case TYPE_F64:
                            writeOp(compiler, OP_DIVIDE_F64);
                            break;
                        case TYPE_GENERIC:
                            writeOp(compiler, OP_DIVIDE_NUMERIC);
                            break;
                        default:
                            error(compiler,
                                  "Division not supported for this type.");
                            return;
                    }
                    break;
                case TOKEN_MODULO:
                    switch (resultType) {
                        case TYPE_I32:
                            writeOp(compiler, OP_MODULO_I32);
                            break;
                        case TYPE_I64:
                            writeOp(compiler, OP_MODULO_I64);
                            break;
                        case TYPE_U32:
                            writeOp(compiler, OP_MODULO_U32);
                            break;
                        case TYPE_U64:
                            writeOp(compiler, OP_MODULO_U64);
                            break;
                        case TYPE_GENERIC:
                            writeOp(compiler, OP_MODULO_NUMERIC);
                            break;
                        default:
                            error(compiler,
                                  "Modulo not supported for this type.");
                            return;
                    }
                    break;

                case TOKEN_BIT_AND:
                    switch (resultType) {
                        case TYPE_I32:
                            writeOp(compiler, OP_BIT_AND_I32);
                            break;
                        case TYPE_I64:
                            writeOp(compiler, OP_BIT_AND_I64);
                            break;
                        case TYPE_U32:
                            writeOp(compiler, OP_BIT_AND_U32);
                            break;
                        case TYPE_U64:
                            writeOp(compiler, OP_BIT_AND_U64);
                            break;
                        default:
                            error(compiler,
                                  "Bitwise AND not supported for this type.");
                            return;
                    }
                    break;
                case TOKEN_BIT_OR:
                    switch (resultType) {
                        case TYPE_I32:
                            writeOp(compiler, OP_BIT_OR_I32);
                            break;
                        case TYPE_I64:
                            writeOp(compiler, OP_BIT_OR_I64);
                            break;
                        case TYPE_U32:
                            writeOp(compiler, OP_BIT_OR_U32);
                            break;
                        case TYPE_U64:
                            writeOp(compiler, OP_BIT_OR_U64);
                            break;
                        default:
                            error(compiler,
                                  "Bitwise OR not supported for this type.");
                            return;
                    }
                    break;
                case TOKEN_BIT_XOR:
                    switch (resultType) {
                        case TYPE_I32:
                            writeOp(compiler, OP_BIT_XOR_I32);
                            break;
                        case TYPE_I64:
                            writeOp(compiler, OP_BIT_XOR_I64);
                            break;
                        case TYPE_U32:
                            writeOp(compiler, OP_BIT_XOR_U32);
                            break;
                        case TYPE_U64:
                            writeOp(compiler, OP_BIT_XOR_U64);
                            break;
                        default:
                            error(compiler,
                                  "Bitwise XOR not supported for this type.");
                            return;
                    }
                    break;
                case TOKEN_SHIFT_LEFT:
                    switch (resultType) {
                        case TYPE_I32:
                            writeOp(compiler, OP_SHIFT_LEFT_I32);
                            break;
                        case TYPE_I64:
                            writeOp(compiler, OP_SHIFT_LEFT_I64);
                            break;
                        case TYPE_U32:
                            writeOp(compiler, OP_SHIFT_LEFT_U32);
                            break;
                        case TYPE_U64:
                            writeOp(compiler, OP_SHIFT_LEFT_U64);
                            break;
                        default:
                            error(compiler,
                                  "Left shift not supported for this type.");
                            return;
                    }
                    break;
                case TOKEN_SHIFT_RIGHT:
                    switch (resultType) {
                        case TYPE_I32:
                            writeOp(compiler, OP_SHIFT_RIGHT_I32);
                            break;
                        case TYPE_I64:
                            writeOp(compiler, OP_SHIFT_RIGHT_I64);
                            break;
                        case TYPE_U32:
                            writeOp(compiler, OP_SHIFT_RIGHT_U32);
                            break;
                        case TYPE_U64:
                            writeOp(compiler, OP_SHIFT_RIGHT_U64);
                            break;
                        default:
                            error(compiler,
                                  "Right shift not supported for this type.");
                            return;
                    }
                    break;

                case TOKEN_LEFT_BRACKET:
                    writeOp(compiler, OP_ARRAY_GET);
                    break;

                // Comparison operators
                case TOKEN_LESS:
                    switch (leftType->kind) {
                        case TYPE_I32:
                            writeOp(compiler, OP_LESS_I32);
                            break;
                        case TYPE_I64:
                            writeOp(compiler, OP_LESS_I64);
                            break;
                        case TYPE_U32:
                            writeOp(compiler, OP_LESS_U32);
                            break;
                        case TYPE_U64:
                            writeOp(compiler, OP_LESS_U64);
                            break;
                        case TYPE_F64:
                            writeOp(compiler, OP_LESS_F64);
                            break;
                        case TYPE_GENERIC:
                            // Fallback to numeric comparison with conversion
                            writeOp(compiler, OP_LESS_F64);
                            break;
                        default:
                            error(compiler,
                                  "Less than not supported for this type.");
                            return;
                    }
                    break;

                case TOKEN_LESS_EQUAL:
                    switch (leftType->kind) {
                        case TYPE_I32:
                            writeOp(compiler, OP_LESS_EQUAL_I32);
                            break;
                        case TYPE_I64:
                            writeOp(compiler, OP_LESS_EQUAL_I64);
                            break;
                        case TYPE_U32:
                            writeOp(compiler, OP_LESS_EQUAL_U32);
                            break;
                        case TYPE_U64:
                            writeOp(compiler, OP_LESS_EQUAL_U64);
                            break;
                        case TYPE_F64:
                            writeOp(compiler, OP_LESS_EQUAL_F64);
                            break;
                        case TYPE_GENERIC:
                            writeOp(compiler, OP_LESS_EQUAL_F64);
                            break;
                        default:
                            error(compiler,
                                  "Less than or equal not supported for this "
                                  "type.");
                            return;
                    }
                    break;

                case TOKEN_GREATER:
                    switch (leftType->kind) {
                        case TYPE_I32:
                            writeOp(compiler, OP_GREATER_I32);
                            break;
                        case TYPE_I64:
                            writeOp(compiler, OP_GREATER_I64);
                            break;
                        case TYPE_U32:
                            writeOp(compiler, OP_GREATER_U32);
                            break;
                        case TYPE_U64:
                            writeOp(compiler, OP_GREATER_U64);
                            break;
                        case TYPE_F64:
                            writeOp(compiler, OP_GREATER_F64);
                            break;
                        case TYPE_GENERIC:
                            writeOp(compiler, OP_GREATER_F64);
                            break;
                        default:
                            error(compiler,
                                  "Greater than not supported for this type.");
                            return;
                    }
                    break;

                case TOKEN_GREATER_EQUAL:
                    switch (leftType->kind) {
                        case TYPE_I32:
                            writeOp(compiler, OP_GREATER_EQUAL_I32);
                            break;
                        case TYPE_I64:
                            writeOp(compiler, OP_GREATER_EQUAL_I64);
                            break;
                        case TYPE_U32:
                            writeOp(compiler, OP_GREATER_EQUAL_U32);
                            break;
                        case TYPE_U64:
                            writeOp(compiler, OP_GREATER_EQUAL_U64);
                            break;
                        case TYPE_F64:
                            writeOp(compiler, OP_GREATER_EQUAL_F64);
                            break;
                        case TYPE_GENERIC:
                            writeOp(compiler, OP_GREATER_EQUAL_F64);
                            break;
                        default:
                            error(compiler,
                                  "Greater than or equal not supported for "
                                  "this type.");
                            return;
                    }
                    break;

                case TOKEN_EQUAL_EQUAL:
                    if (leftType->kind == TYPE_I64 ||
                        rightType->kind == TYPE_I64) {
                        writeOp(compiler, OP_EQUAL_I64);
                    } else {
                        writeOp(compiler, OP_EQUAL);
                    }
                    break;

                case TOKEN_BANG_EQUAL:
                    if (leftType->kind == TYPE_I64 ||
                        rightType->kind == TYPE_I64) {
                        writeOp(compiler, OP_NOT_EQUAL_I64);
                    } else {
                        writeOp(compiler, OP_NOT_EQUAL);
                    }
                    break;

                // Logical operators
                case TOKEN_AND:
                    writeOp(compiler, OP_AND);
                    break;

                case TOKEN_OR:
                    writeOp(compiler, OP_OR);
                    break;

                default:
                    error(compiler, "Unsupported binary operator.");
                    return;
            }
            break;
        }

        case AST_UNARY: {
            generateCode(compiler, node->left);
            if (compiler->hadError) return;

            TypeKind operandType = node->valueType->kind;
            TokenType operator = node->data.operation.operator.type;

            switch (operator) {
                case TOKEN_MINUS:
                    switch (operandType) {
                        case TYPE_I32:
                            writeOp(compiler, OP_NEGATE_I32);
                            break;
                        case TYPE_I64:
                            writeOp(compiler, OP_NEGATE_I64);
                            break;
                        case TYPE_U32:
                            writeOp(compiler, OP_NEGATE_U32);
                            break;
                        case TYPE_U64:
                            writeOp(compiler, OP_NEGATE_U64);
                            break;
                        case TYPE_F64:
                            writeOp(compiler, OP_NEGATE_F64);
                            break;
                        case TYPE_GENERIC:
                            writeOp(compiler, OP_NEGATE_NUMERIC);
                            break;
                        default:
                            error(compiler,
                                  "Negation not supported for this type.");
                            return;
                    }
                    break;
                case TOKEN_NOT:
                    writeOp(compiler, OP_NOT);
                    break;
                case TOKEN_BIT_NOT:
                    switch (operandType) {
                        case TYPE_I32:
                            writeOp(compiler, OP_BIT_NOT_I32);
                            break;
                        case TYPE_I64:
                            writeOp(compiler, OP_BIT_NOT_I64);
                            break;
                        case TYPE_U32:
                            writeOp(compiler, OP_BIT_NOT_U32);
                            break;
                        case TYPE_U64:
                            writeOp(compiler, OP_BIT_NOT_U64);
                            break;
                        default:
                            error(compiler,
                                  "Bitwise not not supported for this type.");
                            return;
                    }
                    break;
                default:
                    error(compiler, "Unsupported unary operator.");
                    return;
            }
            break;
        }

        case AST_CAST: {
            generateCode(compiler, node->left);
            if (compiler->hadError) return;
            TypeKind from =
                node->left->valueType ? node->left->valueType->kind : TYPE_I32;
            TypeKind to = node->data.cast.type->kind;
            if (from == to) {
                break;
            }
            if (from == TYPE_I32 && to == TYPE_F64) {
                writeOp(compiler, OP_I32_TO_F64);
            } else if (from == TYPE_U32 && to == TYPE_F64) {
                writeOp(compiler, OP_U32_TO_F64);
            } else if (from == TYPE_I32 && to == TYPE_U32) {
                writeOp(compiler, OP_I32_TO_U32);
            } else if (from == TYPE_U32 && to == TYPE_I32) {
                writeOp(compiler, OP_U32_TO_I32);
            } else if (from == TYPE_I32 && to == TYPE_I64) {
                writeOp(compiler, OP_I32_TO_I64);
            } else if (from == TYPE_U32 && to == TYPE_I64) {
                writeOp(compiler, OP_U32_TO_I64);
            } else if (from == TYPE_I64 && to == TYPE_I32) {
                writeOp(compiler, OP_I64_TO_I32);
            } else if (from == TYPE_I64 && to == TYPE_U32) {
                writeOp(compiler, OP_I64_TO_U32);
            } else if (from == TYPE_I32 && to == TYPE_U64) {
                writeOp(compiler, OP_I32_TO_U64);
            } else if (from == TYPE_U32 && to == TYPE_U64) {
                writeOp(compiler, OP_U32_TO_U64);
            } else if (from == TYPE_U64 && to == TYPE_I32) {
                writeOp(compiler, OP_U64_TO_I32);
            } else if (from == TYPE_U64 && to == TYPE_U32) {
                writeOp(compiler, OP_U64_TO_U32);
            } else if (from == TYPE_U64 && to == TYPE_F64) {
                writeOp(compiler, OP_U64_TO_F64);
            } else if (from == TYPE_F64 && to == TYPE_U64) {
                writeOp(compiler, OP_F64_TO_U64);
            } else if (from == TYPE_I64 && to == TYPE_U64) {
                writeOp(compiler, OP_I64_TO_U64);
            } else if (from == TYPE_U64 && to == TYPE_I64) {
                writeOp(compiler, OP_U64_TO_I64);
            } else if (from == TYPE_I64 && to == TYPE_F64) {
                writeOp(compiler, OP_I64_TO_F64);
            } else if (from == TYPE_F64 && to == TYPE_I64) {
                writeOp(compiler, OP_F64_TO_I64);
            } else if (from == TYPE_I32 && to == TYPE_BOOL) {
                writeOp(compiler, OP_I32_TO_BOOL);
            } else if (from == TYPE_U32 && to == TYPE_BOOL) {
                writeOp(compiler, OP_U32_TO_BOOL);
            } else if (from == TYPE_I64 && to == TYPE_BOOL) {
                writeOp(compiler, OP_I64_TO_BOOL);
            } else if (from == TYPE_U64 && to == TYPE_BOOL) {
                writeOp(compiler, OP_U64_TO_BOOL);
            } else if (from == TYPE_BOOL && to == TYPE_I32) {
                writeOp(compiler, OP_BOOL_TO_I32);
            } else if (from == TYPE_BOOL && to == TYPE_U32) {
                writeOp(compiler, OP_BOOL_TO_U32);
            } else if (from == TYPE_BOOL && to == TYPE_I64) {
                writeOp(compiler, OP_BOOL_TO_I64);
            } else if (from == TYPE_BOOL && to == TYPE_U64) {
                writeOp(compiler, OP_BOOL_TO_U64);
            } else if (from == TYPE_BOOL && to == TYPE_F64) {
                writeOp(compiler, OP_BOOL_TO_F64);
            } else if (from == TYPE_F64 && to == TYPE_BOOL) {
                writeOp(compiler, OP_F64_TO_BOOL);
            } else if (from == TYPE_F64 && to == TYPE_I32) {
                writeOp(compiler, OP_F64_TO_I32);
            } else if (from == TYPE_F64 && to == TYPE_U32) {
                writeOp(compiler, OP_F64_TO_U32);
            } else if (to == TYPE_STRING) {
                switch (from) {
                    case TYPE_I32:
                        writeOp(compiler, OP_I32_TO_STRING);
                        break;
                    case TYPE_U32:
                        writeOp(compiler, OP_U32_TO_STRING);
                        break;
                    case TYPE_I64:
                        writeOp(compiler, OP_I64_TO_STRING);
                        break;
                    case TYPE_U64:
                        writeOp(compiler, OP_U64_TO_STRING);
                        break;
                    case TYPE_F64:
                        writeOp(compiler, OP_F64_TO_STRING);
                        break;
                    case TYPE_BOOL:
                        writeOp(compiler, OP_BOOL_TO_STRING);
                        break;
                    case TYPE_ARRAY:
                    case TYPE_STRUCT:
                        writeOp(compiler, OP_ARRAY_TO_STRING);
                        break;
                    default:
                        break;
                }
            }
            break;
        }

        case AST_VARIABLE: {
            writeOp(compiler, OP_GET_GLOBAL);
            writeOp(compiler, node->data.variable.index);
            break;
        }

        case AST_LET: {
            if (node->data.let.initializer) {
                generateCode(compiler,
                             node->data.let.initializer);  // push value
            } else {
                writeOp(compiler, OP_NIL);  // no initializer → nil
            }

            writeOp(compiler, OP_DEFINE_GLOBAL);
            writeByte(compiler, node->data.let.index);  // correct index
            break;
        }

        case AST_STATIC: {
            if (node->data.staticVar.initializer) {
                generateCode(compiler,
                             node->data.staticVar.initializer);  // push value
            } else {
                writeOp(compiler, OP_NIL);
            }

            writeOp(compiler, OP_DEFINE_GLOBAL);
            writeByte(compiler, node->data.staticVar.index);
            break;
        }

        case AST_CONST: {
            // constants are evaluated at compile time and already stored
            break;
        }

        case AST_PRINT: {
            if (node->data.print.arguments != NULL &&
                node->data.print.format->type == AST_LITERAL &&
                IS_STRING(node->data.print.format->data.literal)) {
                // Constant format string with arguments. Print the prefix
                // before evaluating arguments so side effects occur after it.

                ObjString* fmt =
                    AS_STRING(node->data.print.format->data.literal);
                const char* chars = fmt->chars;
                int length = fmt->length;

                int prefixIndex = -1;
                for (int i = 0; i < length - 1; i++) {
                    if (chars[i] == '{' && chars[i + 1] == '}') {
                        prefixIndex = i;
                        break;
                    }
                }

                bool placeholderAtEnd =
                    prefixIndex >= 0 && prefixIndex + 2 == length;
                bool singleVoidArg = false;
                if (placeholderAtEnd && node->data.print.argCount == 1 &&
                    node->data.print.arguments &&
                    node->data.print.arguments->valueType) {
                    TypeKind k = node->data.print.arguments->valueType->kind;
                    singleVoidArg = (k == TYPE_VOID || k == TYPE_NIL);
                }

                if (prefixIndex > 0) {
                    ObjString* prefix = allocateString(chars, prefixIndex);
                    emitConstant(compiler, STRING_VAL(prefix));
                    if (singleVoidArg)
                        writeOp(compiler, OP_PRINT);
                    else
                        writeOp(compiler, OP_PRINT_NO_NL);
                }

                ObjString* rest = allocateString(
                    chars + (prefixIndex >= 0 ? prefixIndex : 0),
                    length - (prefixIndex >= 0 ? prefixIndex : 0));

                // Push the remaining format string
                emitConstant(compiler, STRING_VAL(rest));

                // Generate arguments after the prefix
                ASTNode* arg = node->data.print.arguments;
                while (arg != NULL) {
                    generateCode(compiler, arg);
                    if (compiler->hadError) return;
                    arg = arg->next;
                }

                emitConstant(compiler, I32_VAL(node->data.print.argCount));

                if (singleVoidArg) {
                    writeOp(compiler, OP_FORMAT_PRINT_NO_NL);
                } else {
                    if (node->data.print.newline)
                        writeOp(compiler, OP_FORMAT_PRINT);
                    else
                        writeOp(compiler, OP_FORMAT_PRINT_NO_NL);
                }
            } else if (node->data.print.arguments != NULL) {
                // Generic formatted print with interpolation

                // 1. Generate code for the format string first
                generateCode(compiler, node->data.print.format);
                if (compiler->hadError) return;

                // 2. Then generate code for each argument (in order)
                ASTNode* arg = node->data.print.arguments;
                while (arg != NULL) {
                    generateCode(compiler, arg);
                    if (compiler->hadError) return;
                    arg = arg->next;
                }

                // 3. Push the argument count as constant
                emitConstant(compiler, I32_VAL(node->data.print.argCount));

                // 4. Emit formatted print instruction
                if (node->data.print.newline)
                    writeOp(compiler, OP_FORMAT_PRINT);
                else
                    writeOp(compiler, OP_FORMAT_PRINT_NO_NL);
            } else {
                // This is a simple print without interpolation
                generateCode(compiler, node->data.print.format);
                if (compiler->hadError) return;

                // Automatically call `to_string` for struct values if available
                if (node->data.print.format->valueType &&
                    node->data.print.format->valueType->kind == TYPE_STRUCT) {
                    const char* structName = node->data.print.format->valueType
                                                 ->info.structure.name->chars;
                    size_t len = strlen(structName);
                    const char* suffix = "_to_string";
                    char* temp = (char*)malloc(len + strlen(suffix) + 1);
                    memcpy(temp, structName, len);
                    memcpy(temp + len, suffix, strlen(suffix) + 1);
                    Symbol* sym = findSymbol(&compiler->symbols, temp);
                    uint8_t callIndex = UINT8_MAX;
                    if (sym) {
                        callIndex = sym->index;
                    } else {
                        for (int si = 0; si < compiler->symbols.count; si++) {
                            Symbol* modSym = &compiler->symbols.symbols[si];
                            if (!modSym->active || !modSym->isModule ||
                                !modSym->module)
                                continue;
                            Export* ex = get_export(modSym->module, temp);
                            if (ex) {
                                callIndex = ex->index;
                                break;
                            }
                        }
                    }
                    if (callIndex != UINT8_MAX) {
                        writeOp(compiler, OP_CALL);
                        writeOp(compiler, callIndex);
                        writeOp(compiler, 1);
                    }
                    free(temp);
                }

                Type* t = node->data.print.format->valueType;
                if (t) {
                    opCode op = OP_PRINT;
                    opCode opNoNl = OP_PRINT_NO_NL;
                    switch (t->kind) {
                        case TYPE_I32:
                            op = OP_PRINT_I32;
                            opNoNl = OP_PRINT_I32_NO_NL;
                            break;
                        case TYPE_I64:
                            op = OP_PRINT_I64;
                            opNoNl = OP_PRINT_I64_NO_NL;
                            break;
                        case TYPE_U32:
                            op = OP_PRINT_U32;
                            opNoNl = OP_PRINT_U32_NO_NL;
                            break;
                        case TYPE_U64:
                            op = OP_PRINT_U64;
                            opNoNl = OP_PRINT_U64_NO_NL;
                            break;
                        case TYPE_F64:
                            op = OP_PRINT_F64;
                            opNoNl = OP_PRINT_F64_NO_NL;
                            break;
                        case TYPE_BOOL:
                            op = OP_PRINT_BOOL;
                            opNoNl = OP_PRINT_BOOL_NO_NL;
                            break;
                        case TYPE_STRING:
                            op = OP_PRINT_STRING;
                            opNoNl = OP_PRINT_STRING_NO_NL;
                            break;
                        default:
                            break;
                    }
                    writeOp(compiler, node->data.print.newline ? op : opNoNl);
                } else {
                    if (node->data.print.newline)
                        writeOp(compiler, OP_PRINT);
                    else
                        writeOp(compiler, OP_PRINT_NO_NL);
                }
            }
            break;
        }

        case AST_ASSIGNMENT: {
            generateCode(compiler, node->left);
            if (compiler->hadError) return;
            writeOp(compiler, OP_SET_GLOBAL);
            writeOp(compiler, node->data.variable.index);
            // Discard the assigned value to keep the stack balanced
            writeOp(compiler, OP_POP);
            break;
        }

        case AST_ARRAY_SET: {
            generateCode(compiler, node->right);  // array
            if (compiler->hadError) return;
            generateCode(compiler, node->data.arraySet.index);  // index
            if (compiler->hadError) return;
            generateCode(compiler, node->left);  // value
            if (compiler->hadError) return;
            writeOp(compiler, OP_ARRAY_SET);
            break;
        }

        case AST_SLICE: {
            generateCode(compiler, node->left);
            if (compiler->hadError) return;
            if (node->data.slice.start) {
                generateCode(compiler, node->data.slice.start);
            } else {
                emitConstant(compiler, NIL_VAL);
            }
            if (compiler->hadError) return;
            if (node->data.slice.end) {
                generateCode(compiler, node->data.slice.end);
            } else {
                emitConstant(compiler, NIL_VAL);
            }
            if (compiler->hadError) return;
            writeOp(compiler, OP_SLICE);
            break;
        }

        case AST_FIELD_SET: {
            compiler->currentColumn =
                tokenColumn(compiler, &node->data.fieldSet.fieldName);
            generateCode(compiler, node->right);  // object
            if (compiler->hadError) return;
            emitConstant(compiler, I32_VAL(node->data.fieldSet.index));
            generateCode(compiler, node->left);  // value
            if (compiler->hadError) return;
            writeOp(compiler, OP_ARRAY_SET);
            break;
        }

        case AST_ARRAY: {
            int count = 0;
            ASTNode* elem = node->data.array.elements;
            while (elem) {
                generateCode(compiler, elem);
                if (compiler->hadError) return;
                count++;
                elem = elem->next;
            }
            writeOp(compiler, OP_MAKE_ARRAY);
            writeOp(compiler, count);
            break;
        }

        case AST_ARRAY_FILL: {
            generateCode(compiler, node->data.arrayFill.value);
            if (compiler->hadError) return;
            writeOp(compiler, OP_ARRAY_FILL);
            writeOp(compiler, node->data.arrayFill.lengthValue);
            break;
        }

        case AST_STRUCT_LITERAL: {
            compiler->currentColumn =
                tokenColumn(compiler, &node->data.structLiteral.name);
            int count = 0;
            ASTNode* val = node->data.structLiteral.values;
            while (val) {
                generateCode(compiler, val);
                if (compiler->hadError) return;
                count++;
                val = val->next;
            }
            writeOp(compiler, OP_MAKE_ARRAY);
            writeOp(compiler, count);
            break;
        }

        case AST_FIELD: {
            compiler->currentColumn =
                tokenColumn(compiler, &node->data.field.fieldName);
            generateCode(compiler, node->left);
            if (compiler->hadError) return;
            emitConstant(compiler, I32_VAL(node->data.field.index));
            writeOp(compiler, OP_ARRAY_GET);
            break;
        }

        case AST_IF: {
            // Generate code for the condition
            generateCode(compiler, node->data.ifStmt.condition);
            if (compiler->hadError) return;

            // Emit a jump-if-false instruction
            // We'll patch this jump later
            int thenJump = compiler->chunk->count;
            writeOp(compiler, OP_JUMP_IF_FALSE);
            writeChunk(compiler->chunk, 0xFF, 0,
                       1);  // Placeholder for jump offset
            writeChunk(compiler->chunk, 0xFF, 0,
                       1);  // Placeholder for jump offset

            // Pop the condition value from the stack
            writeOp(compiler, OP_POP);

            // Generate code for the then branch
            generateCode(compiler, node->data.ifStmt.thenBranch);
            if (compiler->hadError) return;

            // Emit a jump instruction to skip the else branch
            // We'll patch this jump later
            int elseJump = compiler->chunk->count;
            writeOp(compiler, OP_JUMP);
            writeChunk(compiler->chunk, 0xFF, 0,
                       1);  // Placeholder for jump offset
            writeChunk(compiler->chunk, 0xFF, 0,
                       1);  // Placeholder for jump offset

            // Patch the then jump and emit a pop so the condition is removed
            // when the "then" branch is skipped
            int thenEnd = compiler->chunk->count;
            compiler->chunk->code[thenJump + 1] = (thenEnd - thenJump - 3) >> 8;
            compiler->chunk->code[thenJump + 2] =
                (thenEnd - thenJump - 3) & 0xFF;
            // When jumping to the else/elif section the condition hasn't been
            // popped yet. Emit a POP here so the stack stays balanced for the
            // false branch as well.
            writeOp(compiler, OP_POP);

            // Generate code for elif branches if any
            ASTNode* elifCondition = node->data.ifStmt.elifConditions;
            ASTNode* elifBranch = node->data.ifStmt.elifBranches;
            ObjIntArray* elifJumpsObj = NULL;
            int64_t* elifJumps = NULL;
            int elifCount = 0;

            // Count the number of elif branches
            ASTNode* tempCondition = elifCondition;
            while (tempCondition != NULL) {
                elifCount++;
                tempCondition = tempCondition->next;
            }

            // Allocate memory for elif jumps
            if (elifCount > 0) {
                elifJumpsObj = allocateIntArray(elifCount);
                elifJumps = elifJumpsObj->elements;
            }

            // Generate code for each elif branch
            int elifIndex = 0;
            while (elifCondition != NULL && elifBranch != NULL) {
                // Generate code for the elif condition
                generateCode(compiler, elifCondition);
                if (compiler->hadError) {
                    return;
                }

                // Emit a jump-if-false instruction
                // We'll patch this jump later
                int elifThenJump = compiler->chunk->count;
                writeOp(compiler, OP_JUMP_IF_FALSE);
                writeChunk(compiler->chunk, 0xFF, 0,
                           1);  // Placeholder for jump offset
                writeChunk(compiler->chunk, 0xFF, 0,
                           1);  // Placeholder for jump offset

                // Pop the condition value from the stack
                writeOp(compiler, OP_POP);

                // Generate code for the elif branch
                generateCode(compiler, elifBranch);
                if (compiler->hadError) {
                    return;
                }

                // Emit a jump instruction to skip the remaining branches
                // We'll patch this jump later
                elifJumps[elifIndex] = compiler->chunk->count;
                writeOp(compiler, OP_JUMP);
                writeChunk(compiler->chunk, 0xFF, 0,
                           1);  // Placeholder for jump offset
                writeChunk(compiler->chunk, 0xFF, 0,
                           1);  // Placeholder for jump offset

                // Patch the elif jump to the code after this branch. As with
                // the main if/else case, emit a POP so the condition is
                // discarded when the branch is skipped.
                int elifEnd = compiler->chunk->count;
                compiler->chunk->code[elifThenJump + 1] =
                    (elifEnd - elifThenJump - 3) >> 8;
                compiler->chunk->code[elifThenJump + 2] =
                    (elifEnd - elifThenJump - 3) & 0xFF;
                writeOp(compiler, OP_POP);

                // Move to the next elif condition and branch
                elifCondition = elifCondition->next;
                elifBranch = elifBranch->next;
                elifIndex++;
            }

            // Generate code for the else branch if it exists
            if (node->data.ifStmt.elseBranch) {
                generateCode(compiler, node->data.ifStmt.elseBranch);
                if (compiler->hadError) {
                    return;
                }
            }

            // Patch the else jump
            int end = compiler->chunk->count;
            compiler->chunk->code[elseJump + 1] = (end - elseJump - 3) >> 8;
            compiler->chunk->code[elseJump + 2] = (end - elseJump - 3) & 0xFF;

            // Patch all elif jumps
            for (int i = 0; i < elifCount; i++) {
                int elifJump = (int)elifJumps[i];
                compiler->chunk->code[elifJump + 1] = (end - elifJump - 3) >> 8;
                compiler->chunk->code[elifJump + 2] =
                    (end - elifJump - 3) & 0xFF;
            }

            (void)elifJumpsObj;  // GC-managed

            break;
        }

        case AST_TERNARY: {
            generateCode(compiler, node->data.ternary.condition);
            if (compiler->hadError) return;

            int elseJump = compiler->chunk->count;
            writeOp(compiler, OP_JUMP_IF_FALSE);
            writeChunk(compiler->chunk, 0xFF, 0, 1);
            writeChunk(compiler->chunk, 0xFF, 0, 1);

            writeOp(compiler, OP_POP);
            generateCode(compiler, node->data.ternary.thenExpr);
            if (compiler->hadError) return;

            int endJump = compiler->chunk->count;
            writeOp(compiler, OP_JUMP);
            writeChunk(compiler->chunk, 0xFF, 0, 1);
            writeChunk(compiler->chunk, 0xFF, 0, 1);

            int elseStart = compiler->chunk->count;
            compiler->chunk->code[elseJump + 1] =
                (elseStart - elseJump - 3) >> 8;
            compiler->chunk->code[elseJump + 2] =
                (elseStart - elseJump - 3) & 0xFF;

            writeOp(compiler, OP_POP);
            generateCode(compiler, node->data.ternary.elseExpr);
            if (compiler->hadError) return;

            int end = compiler->chunk->count;
            compiler->chunk->code[endJump + 1] = (end - endJump - 3) >> 8;
            compiler->chunk->code[endJump + 2] = (end - endJump - 3) & 0xFF;
            break;
        }

        case AST_BLOCK: {
            if (node->data.block.scoped) {
                beginScope(compiler);
            }

            // Generate code for each statement in the block
            ASTNode* stmt = node->data.block.statements;
            while (stmt) {
                generateCode(compiler, stmt);
                if (compiler->hadError) {
                    if (node->data.block.scoped) endScope(compiler);
                    return;
                }
                stmt = stmt->next;
            }
            if (node->data.block.scoped) {
                endScope(compiler);
            }
            break;
        }

        case AST_WHILE: {
            // Save the enclosing loop context
            int enclosingLoopStart = compiler->loopStart;
            int enclosingLoopEnd = compiler->loopEnd;
            int enclosingLoopContinue = compiler->loopContinue;
            int enclosingLoopDepth = compiler->loopDepth;

            // Store the current position to jump back to for the loop condition
            compiler->loopStart = compiler->chunk->count;
            compiler->loopDepth++;

            // Generate code for the condition
            generateCode(compiler, node->data.whileStmt.condition);
            if (compiler->hadError) return;

            // Emit a jump-if-false instruction
            // We'll patch this jump later
            int exitJump = compiler->chunk->count;
            writeOp(compiler, OP_JUMP_IF_FALSE);
            writeChunk(compiler->chunk, 0xFF, 0,
                       1);  // Placeholder for jump offset
            writeChunk(compiler->chunk, 0xFF, 0,
                       1);  // Placeholder for jump offset

            // Pop the condition value from the stack
            writeOp(compiler, OP_POP);

            // Set the continue position to the start of the loop
            compiler->loopContinue = compiler->loopStart;

            beginScope(compiler);
            // Generate code for the body
            generateCode(compiler, node->data.whileStmt.body);
            if (compiler->hadError) {
                endScope(compiler);
                return;
            }
            endScope(compiler);

            // Emit a loop instruction to jump back to the condition
            writeOp(compiler, OP_LOOP);
            int offset = compiler->chunk->count - compiler->loopStart + 2;
            writeChunk(compiler->chunk, (offset >> 8) & 0xFF, 0, 1);
            writeChunk(compiler->chunk, offset & 0xFF, 0, 1);

            // Patch the exit jump
            int exitDest = compiler->chunk->count;
            compiler->chunk->code[exitJump + 1] =
                (exitDest - exitJump - 3) >> 8;
            compiler->chunk->code[exitJump + 2] =
                (exitDest - exitJump - 3) & 0xFF;

            // Set the loop end position
            compiler->loopEnd = exitDest;

            // Patch all break jumps to jump to the current position
            patchBreakJumps(compiler);

            // When the loop exits via the jump-if-false above, the condition
            // value remains on the stack because the OP_POP immediately after
            // the jump is skipped. Emit a pop here so that the stack is
            // balanced on loop exit.
            writeOp(compiler, OP_POP);

            // Restore the enclosing loop context
            compiler->loopStart = enclosingLoopStart;
            compiler->loopEnd = enclosingLoopEnd;
            compiler->loopContinue = enclosingLoopContinue;
            compiler->loopDepth = enclosingLoopDepth;

            break;
        }

        case AST_FOR: {
            emitForLoop(compiler, node);
            break;
        }
        case AST_FUNCTION: {
            beginScope(compiler);
            // Count and collect parameters
            ASTNode* paramList[256];  // Max 256 params
            int paramCount = 0;

            ASTNode* param = node->data.function.parameters;
            while (param != NULL && paramCount < 256) {
                paramList[paramCount++] = param;
                param = param->next;
            }

            // Reserve jump over function body
            int jumpOverFunction = compiler->chunk->count;
            writeOp(compiler, OP_JUMP);
            writeChunk(compiler->chunk, 0xFF, 0, 1);
            writeChunk(compiler->chunk, 0xFF, 0, 1);

            // Record function body start
            int functionStart = compiler->chunk->count;
            // fprintf(stderr, "DEBUG: Function bytecode starts at offset %d\n",
            //         functionStart);

            // Bind parameters to globals
            for (int i = paramCount - 1; i >= 0; i--) {
                writeOp(compiler, OP_SET_GLOBAL);
                writeOp(compiler, paramList[i]->data.let.index);
                // Pop argument after storing
                writeOp(compiler, OP_POP);
            }

            // Emit body and return
            generateCode(compiler, node->data.function.body);
            if (node->data.function.returnType &&
                node->data.function.returnType->kind != TYPE_VOID) {
                writeOp(compiler, OP_NIL);
            }
            writeOp(compiler, OP_RETURN);

            // Patch jump over function
            int afterFunction = compiler->chunk->count;
            compiler->chunk->code[jumpOverFunction + 1] =
                (afterFunction - jumpOverFunction - 3) >> 8;
            compiler->chunk->code[jumpOverFunction + 2] =
                (afterFunction - jumpOverFunction - 3) & 0xFF;

            // Create function entry and store its index in the global variable
            if (vm.functionCount >= UINT8_COUNT) {
                error(compiler, "Too many functions defined.");
                return;
            }
            uint8_t funcIndex = vm.functionCount++;
            vm.functions[funcIndex].start = functionStart;
            vm.functions[funcIndex].arity = (uint8_t)paramCount;
            vm.functions[funcIndex].chunk = compiler->chunk;

            // Store function index in globals for lookup at runtime
            vm.globals[node->data.function.index] = I32_VAL(funcIndex);

            endScope(compiler);
            break;
        }
        case AST_CALL: {
            compiler->currentColumn =
                tokenColumn(compiler, &node->data.call.name);
            // Emit specialized builtin opcode if available
            if (node->data.call.builtinOp != -1) {
                ASTNode* arg = node->data.call.arguments;
                while (arg) {
                    generateCode(compiler, arg);
                    if (compiler->hadError) return;
                    arg = arg->next;
                }
                writeOp(compiler, (opCode)node->data.call.builtinOp);
                break;
            }

            // Built-in implementations using native registry
            if (node->data.call.nativeIndex != -1) {
                ASTNode* arg = node->data.call.arguments;
                while (arg) {
                    generateCode(compiler, arg);
                    if (compiler->hadError) return;
                    arg = arg->next;
                }
                writeOp(compiler, OP_CALL_NATIVE);
                writeOp(compiler, (uint8_t)node->data.call.nativeIndex);
                writeOp(compiler, (uint8_t)node->data.call.argCount);
                break;
            }

            // Generate code for each argument in source order
            int argCount = 0;
            ASTNode* arg = node->data.call.arguments;

            ASTNode* args[256];
            while (arg != NULL) {
                args[argCount++] = arg;
                arg = arg->next;
            }

            for (int i = 0; i < argCount; i++) {
                generateCode(compiler, args[i]);
                if (compiler->hadError) return;

                if (node->data.call.convertArgs[i]) {
                    Type* funcType = vm.globalTypes[node->data.call.index];
                    if (!funcType || funcType->kind != TYPE_FUNCTION) continue;
                    Type* expected = funcType->info.function.paramTypes[i];
                    opCode op =
                        conversionOp(args[i]->valueType->kind, expected->kind);
                    if (op != OP_RETURN) writeOp(compiler, op);
                }
            }

            writeOp(compiler, OP_CALL);
            writeOp(compiler, node->data.call.index);
            writeOp(compiler, argCount);

            break;
        }

        case AST_RETURN: {
            // Generate code for the return value if present
            if (node->data.returnStmt.value != NULL) {
                generateCode(compiler, node->data.returnStmt.value);
                if (compiler->hadError) return;
            }

            // Return from the function
            writeOp(compiler, OP_RETURN);

            break;
        }

        case AST_BREAK: {
            if (compiler->loopDepth == 0) {
                error(compiler, "Cannot use 'break' outside of a loop.");
                return;
            }

            int jumpPos = compiler->chunk->count;
            writeOp(compiler, OP_JUMP);
            writeChunk(compiler->chunk, 0xFF, 0, 1);
            writeChunk(compiler->chunk, 0xFF, 0, 1);
            addBreakJump(compiler, jumpPos);
            break;
        }

        case AST_CONTINUE: {
            if (compiler->loopDepth == 0) {
                error(compiler, "Cannot use 'continue' outside of a loop.");
                return;
            }

            bool isForLoop = compiler->loopContinue != compiler->loopStart;

            if (compiler->loopContinue < 0 && isForLoop) {
                // For-loop continue before increment section is emitted.
                int jumpPos = compiler->chunk->count;
                writeOp(compiler, OP_JUMP);
                writeChunk(compiler->chunk, 0xFF, 0, 1);
                writeChunk(compiler->chunk, 0xFF, 0, 1);
                addContinueJump(compiler, jumpPos);
            } else {
                if (!isForLoop) {
                    // While loops need to pop the condition value
                    writeOp(compiler, OP_POP);
                }

                writeOp(compiler, OP_LOOP);
                int offset =
                    compiler->chunk->count - compiler->loopContinue + 2;
                writeChunk(compiler->chunk, (offset >> 8) & 0xFF, 0, 1);
                writeChunk(compiler->chunk, offset & 0xFF, 0, 1);
            }

            break;
        }

        case AST_TRY: {
            beginScope(compiler);
            uint8_t index = node->data.tryStmt.errorIndex;
            int setup = compiler->chunk->count;
            writeOp(compiler, OP_SETUP_EXCEPT);
            writeChunk(compiler->chunk, 0xFF, 0, 1);
            writeChunk(compiler->chunk, 0xFF, 0, 1);
            writeOp(compiler, index);

            generateCode(compiler, node->data.tryStmt.tryBlock);
            if (compiler->hadError) {
                endScope(compiler);
                return;
            }

            writeOp(compiler, OP_POP_EXCEPT);
            int jumpOver = compiler->chunk->count;
            writeOp(compiler, OP_JUMP);
            writeChunk(compiler->chunk, 0xFF, 0, 1);
            writeChunk(compiler->chunk, 0xFF, 0, 1);

            int handler = compiler->chunk->count;
            compiler->chunk->code[setup + 1] = (handler - setup - 4) >> 8;
            compiler->chunk->code[setup + 2] = (handler - setup - 4) & 0xFF;

            generateCode(compiler, node->data.tryStmt.catchBlock);
            if (compiler->hadError) {
                endScope(compiler);
                return;
            }

            int end = compiler->chunk->count;
            compiler->chunk->code[jumpOver + 1] = (end - jumpOver - 3) >> 8;
            compiler->chunk->code[jumpOver + 2] = (end - jumpOver - 3) & 0xFF;

            endScope(compiler);
            break;
        }

        case AST_IMPORT: {
            // Emit an import instruction with module path constant
            int constant = makeConstant(compiler, node->data.importStmt.path);
            writeOp(compiler, OP_IMPORT);
            writeOp(compiler, (uint8_t)constant);
            break;
        }
        case AST_USE: {
            int constant = makeConstant(compiler, node->data.useStmt.path);
            writeOp(compiler, OP_IMPORT);
            writeOp(compiler, (uint8_t)constant);
            break;
        }

        default:
            error(compiler, "Unsupported AST node type in code generator.");
            break;
    }
}

static void emitForLoop(Compiler* compiler, ASTNode* node) {
    beginScope(compiler);
    // Save the enclosing loop context
    int enclosingLoopStart = compiler->loopStart;
    int enclosingLoopEnd = compiler->loopEnd;
    int enclosingLoopContinue = compiler->loopContinue;
    int enclosingLoopDepth = compiler->loopDepth;
    bool useTypedJump = false;

    Type* iterType = node->data.forStmt.startExpr->valueType;
    bool numericLoop =
        iterType && (iterType->kind == TYPE_I32 || iterType->kind == TYPE_I64 ||
                     iterType->kind == TYPE_U32 || iterType->kind == TYPE_U64);
    if (numericLoop) {
        writeOp(compiler, OP_GC_PAUSE);
    }

    // Generate code for the range start expression and store it in the iterator
    // variable
    generateCode(compiler, node->data.forStmt.startExpr);
    if (compiler->hadError) return;

    // Define and initialize the iterator variable
    writeOp(compiler, OP_DEFINE_GLOBAL);
    writeOp(compiler, node->data.forStmt.iteratorIndex);

    // Store the current position to jump back to for the loop condition
    int loopStart = compiler->chunk->count;
    compiler->loopStart = loopStart;
    compiler->loopDepth++;

    // Get the iterator value for comparison
    writeOp(compiler, OP_GET_GLOBAL);
    writeOp(compiler, node->data.forStmt.iteratorIndex);

    // Get the end value for comparison
    generateCode(compiler, node->data.forStmt.endExpr);
    if (compiler->hadError) return;

    // Compare the iterator with the end value
    int exitJump;
    if (iterType->kind == TYPE_I32) {
        writeOp(compiler, OP_LESS_I32);
        exitJump = compiler->chunk->count;
        writeOp(compiler, OP_JUMP_IF_FALSE);
        writeChunk(compiler->chunk, 0xFF, 0, 1);  // Placeholder for jump offset
        writeChunk(compiler->chunk, 0xFF, 0, 1);  // Placeholder for jump offset
        writeOp(compiler, OP_POP);
    } else if (iterType->kind == TYPE_I64) {
        useTypedJump = true;
        exitJump = compiler->chunk->count;
        writeOp(compiler, OP_JUMP_IF_LT_I64);
        writeChunk(compiler->chunk, 0xFF, 0, 1);  // Placeholder for jump offset
        writeChunk(compiler->chunk, 0xFF, 0, 1);
    } else if (iterType->kind == TYPE_U32) {
        writeOp(compiler, OP_LESS_U32);
        exitJump = compiler->chunk->count;
        writeOp(compiler, OP_JUMP_IF_FALSE);
        writeChunk(compiler->chunk, 0xFF, 0, 1);
        writeChunk(compiler->chunk, 0xFF, 0, 1);
        writeOp(compiler, OP_POP);
    } else if (iterType->kind == TYPE_U64) {
        writeOp(compiler, OP_LESS_U64);
        exitJump = compiler->chunk->count;
        writeOp(compiler, OP_JUMP_IF_FALSE);
        writeChunk(compiler->chunk, 0xFF, 0, 1);
        writeChunk(compiler->chunk, 0xFF, 0, 1);
        writeOp(compiler, OP_POP);
    } else {
        error(compiler, "Unsupported iterator type for for loop.");
        return;
    }

    // Generate code for the body
    generateCode(compiler, node->data.forStmt.body);
    if (compiler->hadError) return;

    // Store the current position where we're about to emit the increment code
    // This is where continue statements will jump to
    compiler->loopContinue = compiler->chunk->count;
    patchContinueJumps(compiler);

    // After the body, increment the iterator
    // Get the current iterator value
    writeOp(compiler, OP_GET_GLOBAL);
    writeOp(compiler, node->data.forStmt.iteratorIndex);

    // Add the step value
    bool useIncI64 = false;
    if (node->data.forStmt.stepExpr) {
        generateCode(compiler, node->data.forStmt.stepExpr);
        if (compiler->hadError) return;
        Type* stepType = node->data.forStmt.stepExpr->valueType;
        if (stepType && stepType->kind != iterType->kind) {
            if (iterType->kind == TYPE_I64) {
                if (stepType->kind == TYPE_I32) {
                    writeOp(compiler, OP_I32_TO_I64);
                } else if (stepType->kind == TYPE_U32) {
                    writeOp(compiler, OP_U32_TO_I64);
                } else if (stepType->kind == TYPE_U64) {
                    writeOp(compiler, OP_U64_TO_I64);
                }
            } else if (iterType->kind == TYPE_U64) {
                if (stepType->kind == TYPE_I32) {
                    writeOp(compiler, OP_I32_TO_U64);
                } else if (stepType->kind == TYPE_U32) {
                    writeOp(compiler, OP_U32_TO_U64);
                }
            }
        }
    } else {
        // Default step value is 1
        if (iterType->kind == TYPE_I32) {
            emitConstant(compiler, I32_VAL(1));
        } else if (iterType->kind == TYPE_I64) {
            useIncI64 = true;
        } else if (iterType->kind == TYPE_U32) {
            emitConstant(compiler, U32_VAL(1));
        } else if (iterType->kind == TYPE_U64) {
            emitConstant(compiler, U64_VAL(1));
        }
    }

    // Add the step to the iterator
    if (iterType->kind == TYPE_I32) {
        writeOp(compiler, OP_ADD_I32);
    } else if (iterType->kind == TYPE_I64) {
        if (useIncI64) {
            writeOp(compiler, OP_INC_I64);
        } else {
            writeOp(compiler, OP_ADD_I64);
        }
    } else if (iterType->kind == TYPE_U32) {
        writeOp(compiler, OP_ADD_U32);
    } else if (iterType->kind == TYPE_U64) {
        writeOp(compiler, OP_ADD_U64);
    }

    // Store the incremented value back in the iterator variable
    writeOp(compiler, OP_SET_GLOBAL);
    writeOp(compiler, node->data.forStmt.iteratorIndex);

    // Pop the value from the stack after SET_GLOBAL (important for stack
    // cleanliness!)
    writeOp(compiler, OP_POP);

    // Jump back to the condition check
    writeOp(compiler, OP_LOOP);
    int offset = compiler->chunk->count - loopStart + 2;
    writeChunk(compiler->chunk, (offset >> 8) & 0xFF, 0, 1);
    writeChunk(compiler->chunk, offset & 0xFF, 0, 1);

    // Patch the exit jump
    int exitDest = compiler->chunk->count;
    compiler->chunk->code[exitJump + 1] = (exitDest - exitJump - 3) >> 8;
    compiler->chunk->code[exitJump + 2] = (exitDest - exitJump - 3) & 0xFF;

    // Set the loop end position to the destination of the exit jump
    compiler->loopEnd = exitDest;

    // Patch all break jumps to jump to the current position
    patchBreakJumps(compiler);

    // Like while loops, the condition value remains on the stack when the loop
    // exits via the jump-if-false above because the OP_POP directly after the
    // jump is skipped. Emit a pop here to keep the stack balanced on exit for
    // generic comparisons.
    if (!useTypedJump) {
        writeOp(compiler, OP_POP);
    }

    if (numericLoop) {
        writeOp(compiler, OP_GC_RESUME);
    }

    endScope(compiler);

    // Restore the enclosing loop context
    compiler->loopStart = enclosingLoopStart;
    compiler->loopEnd = enclosingLoopEnd;
    compiler->loopContinue = enclosingLoopContinue;
    compiler->loopDepth = enclosingLoopDepth;
}

uint8_t defineVariable(Compiler* compiler, Token name, Type* type) {
    return addLocal(compiler, name, type, false, false);
}

uint8_t addLocal(Compiler* compiler, Token name, Type* type, bool isMutable,
                 bool isConst) {
    char tempName[name.length + 1];
    memcpy(tempName, name.start, name.length);
    tempName[name.length] = '\0';
    Symbol* existing = findSymbol(&compiler->symbols, tempName);
    if (existing) {
        if (existing->isConst) {
            emitRedeclarationError(compiler, &name, tempName);
            return UINT8_MAX;
        }
        if (existing->scope == compiler->scopeDepth) {
            emitRedeclarationError(compiler, &name, tempName);
            return UINT8_MAX;
        }
    }

    if (vm.variableCount >= UINT8_COUNT) {
        error(compiler, "Too many variables.");
        return 0;
    }
    uint8_t index = vm.variableCount++;
    ObjString* nameObj = allocateString(name.start, name.length);
    if (nameObj == NULL) {
        error(compiler, "Memory allocation failed for variable name.");
        return 0;
    }
    vm.variableNames[index].name = nameObj;

    vm.variableNames[index].length = name.length;
    vm.globalTypes[index] = type;  // Should be getPrimitiveType result
    vm.globalTypes[index] = type;
    vm.globals[index] = NIL_VAL;
    vm.publicGlobals[index] = false;

    addSymbol(&compiler->symbols, nameObj->chars, name, type,
              compiler->scopeDepth, index, isMutable, isConst, false, NULL);
    Symbol* sym = findSymbol(&compiler->symbols, nameObj->chars);
    if (sym) sym->fixedArray = false;

    return index;
}

uint8_t resolveVariable(Compiler* compiler, Token name) {
    char tempName[name.length + 1];
    memcpy(tempName, name.start, name.length);
    tempName[name.length] = '\0';
    Symbol* sym = findSymbol(&compiler->symbols, tempName);
    if (sym) return sym->index;
    return UINT8_MAX;  // Not found
}

/**
 * Record the location of a `break` jump to patch later.
 */
static void addBreakJump(Compiler* compiler, int jumpPos) {
    if (compiler->breakJumps == NULL) {
        compiler->breakJumpCapacity = 8;
        compiler->breakJumps = allocateIntArray(compiler->breakJumpCapacity);
    } else if (compiler->breakJumpCount >= compiler->breakJumpCapacity) {
        int oldCapacity = compiler->breakJumpCapacity;
        compiler->breakJumpCapacity = oldCapacity * 2;
        compiler->breakJumps->elements =
            realloc(compiler->breakJumps->elements,
                    sizeof(int64_t) * compiler->breakJumpCapacity);
        compiler->breakJumps->length = compiler->breakJumpCapacity;
    }
    compiler->breakJumps->elements[compiler->breakJumpCount++] = jumpPos;
}

/**
 * Record the location of a `continue` jump to patch later.
 */
static void addContinueJump(Compiler* compiler, int jumpPos) {
    if (compiler->continueJumps == NULL) {
        compiler->continueJumpCapacity = 8;
        compiler->continueJumps =
            allocateIntArray(compiler->continueJumpCapacity);
    } else if (compiler->continueJumpCount >= compiler->continueJumpCapacity) {
        int oldCapacity = compiler->continueJumpCapacity;
        compiler->continueJumpCapacity = oldCapacity * 2;
        compiler->continueJumps->elements =
            realloc(compiler->continueJumps->elements,
                    sizeof(int64_t) * compiler->continueJumpCapacity);
        compiler->continueJumps->length = compiler->continueJumpCapacity;
    }
    compiler->continueJumps->elements[compiler->continueJumpCount++] = jumpPos;
}

/**
 * Patch all recorded `continue` jumps with the correct destination.
 */
static void patchContinueJumps(Compiler* compiler) {
    int continueDest = compiler->loopContinue;
    for (int i = 0; i < compiler->continueJumpCount; i++) {
        int jumpPos = (int)compiler->continueJumps->elements[i];
        int offset = continueDest - jumpPos - 3;
        compiler->chunk->code[jumpPos + 1] = (offset >> 8) & 0xFF;
        compiler->chunk->code[jumpPos + 2] = offset & 0xFF;
    }
    compiler->continueJumpCount = 0;
}

/**
 * Patch all recorded `break` jumps with the end of the loop.
 */
static void patchBreakJumps(Compiler* compiler) {
    int breakDest = compiler->chunk->count;

    // Patch all break jumps to jump to the current position
    for (int i = 0; i < compiler->breakJumpCount; i++) {
        int jumpPos = (int)compiler->breakJumps->elements[i];
        int offset = breakDest - jumpPos - 3;
        compiler->chunk->code[jumpPos + 1] = (offset >> 8) & 0xFF;
        compiler->chunk->code[jumpPos + 2] = offset & 0xFF;
    }

    // Reset the break jumps array
    compiler->breakJumpCount = 0;
}

/**
 * Record a function declaration so it may be referenced before definition.
 */
static void predeclareFunction(Compiler* compiler, ASTNode* node) {
    char tempName[node->data.function.name.length + 1];
    memcpy(tempName, node->data.function.name.start,
           node->data.function.name.length);
    tempName[node->data.function.name.length] = '\0';
    Symbol* existing = findSymbol(&compiler->symbols, tempName);
    uint8_t index;
    if (existing && existing->scope == compiler->scopeDepth &&
        node->data.function.implType) {
        const char* structName =
            node->data.function.implType->info.structure.name->chars;
        size_t structLen = strlen(structName);
        size_t funcLen = node->data.function.name.length;
        char* temp = (char*)malloc(structLen + 1 + funcLen + 1);
        memcpy(temp, structName, structLen);
        temp[structLen] = '_';
        memcpy(temp + structLen + 1, node->data.function.name.start, funcLen);
        temp[structLen + 1 + funcLen] = '\0';

        ObjString* fullStr = allocateString(temp, structLen + 1 + funcLen);
        free(temp);

        Token newTok = node->data.function.name;
        newTok.start = fullStr->chars;
        newTok.length = structLen + 1 + funcLen;
        node->data.function.name = newTok;
        node->data.function.mangledName = fullStr;
        index =
            defineVariable(compiler, newTok, node->data.function.returnType);
    } else {
        index = defineVariable(compiler, node->data.function.name,
                               node->data.function.returnType);
    }
    node->data.function.index = index;
    vm.publicGlobals[index] = node->data.function.isPublic;
    vm.functionDecls[index] = node;

    int pcount = 0;
    ASTNode* p = node->data.function.parameters;
    while (p) {
        pcount++;
        p = p->next;
    }
    Type** paramTypes = NULL;
    if (pcount > 0) {
        paramTypes = (Type**)malloc(sizeof(Type*) * pcount);
        p = node->data.function.parameters;
        for (int i = 0; i < pcount; i++) {
            paramTypes[i] = p->data.let.type;
            p = p->next;
        }
    }
    Type* funcType =
        createFunctionType(node->data.function.returnType, paramTypes, pcount);
    vm.globalTypes[index] = funcType;
    vm.globalTypes[index] = funcType;
}

static void recordFunctionDeclarations(ASTNode* ast, Compiler* compiler) {
    ASTNode* current = ast;
    while (current) {
        if (current->type == AST_FUNCTION) {
            predeclareFunction(compiler, current);
        } else if (current->type == AST_BLOCK && !current->data.block.scoped) {
            recordFunctionDeclarations(current->data.block.statements,
                                       compiler);
        }
        current = current->next;
    }
}

void initCompiler(Compiler* compiler, Chunk* chunk, const char* filePath,
                  const char* sourceCode) {
    compiler->loopStart = -1;
    compiler->loopEnd = -1;
    compiler->loopContinue = -1;
    compiler->loopDepth = 0;

    // Initialize break jumps array
    compiler->breakJumps = NULL;
    compiler->breakJumpCount = 0;
    compiler->breakJumpCapacity = 0;

    // Initialize continue jumps array
    compiler->continueJumps = NULL;
    compiler->continueJumpCount = 0;
    compiler->continueJumpCapacity = 0;

    initSymbolTable(&compiler->symbols);
    compiler->scopeDepth = 0;
    compiler->chunk = chunk;
    compiler->hadError = false;
    compiler->panicMode = false;

    compiler->filePath = filePath;
    compiler->sourceCode = sourceCode;
    compiler->currentLine = 0;
    compiler->currentColumn = 1;
    compiler->currentReturnType = NULL;
    compiler->currentFunctionHasGenerics = false;
    compiler->genericNames = NULL;
    compiler->genericConstraints = NULL;
    compiler->genericCount = 0;

    // Count lines in sourceCode and record start pointers for each line
    if (sourceCode) {
        int lines = 1;
        const char* p = sourceCode;
        while (*p) {
            if (*p == '\n') lines++;
            p++;
        }
        compiler->lineCount = lines;
        compiler->lineStarts = malloc(sizeof(const char*) * lines);
        compiler->lineStarts[0] = sourceCode;
        p = sourceCode;
        int line = 1;
        while (*p && line < lines) {
            if (*p == '\n') compiler->lineStarts[line++] = p + 1;
            p++;
        }
    } else {
        compiler->lineStarts = NULL;
        compiler->lineCount = 0;
    }
}

// Free resources used by the compiler
static void freeCompiler(Compiler* compiler) {
    // Allow GC to reclaim jump arrays
    compiler->breakJumps = NULL;
    compiler->breakJumpCount = 0;
    compiler->breakJumpCapacity = 0;

    compiler->continueJumps = NULL;
    compiler->continueJumpCount = 0;
    compiler->continueJumpCapacity = 0;

    freeSymbolTable(&compiler->symbols);

    compiler->genericNames = NULL;
    compiler->genericConstraints = NULL;
    compiler->genericCount = 0;

    if (compiler->lineStarts) {
        free((void*)compiler->lineStarts);
        compiler->lineStarts = NULL;
    }
}

bool compile(ASTNode* ast, Compiler* compiler, bool requireMain) {
    initTypeSystem();
    recordFunctionDeclarations(ast, compiler);
    ASTNode* current = ast;
    // Removed unused index variable
    while (current) {
        typeCheckNode(compiler, current);
        if (!compiler->hadError) {
            generateCode(compiler, current);
        }
        current = current->next;
    }

    // Automatically invoke `main` if it exists or report an error
    Token mainTok;
    mainTok.type = TOKEN_IDENTIFIER;
    mainTok.start = "main";
    mainTok.length = 4;
    mainTok.line = 0;
    uint8_t mainIndex = resolveVariable(compiler, mainTok);

    if (mainIndex != UINT8_MAX) {
        writeOp(compiler, OP_CALL);
        writeOp(compiler, mainIndex);
        writeOp(compiler, 0);  // no arguments
        Type* mainType = vm.globalTypes[mainIndex];
        if (!mainType || mainType->kind != TYPE_FUNCTION ||
            !mainType->info.function.returnType ||
            mainType->info.function.returnType->kind != TYPE_VOID) {
            writeOp(compiler, OP_POP);  // discard return value
        }
    } else if (requireMain) {
        error(compiler, "No 'main' function defined.");
    }

    writeOp(compiler, OP_RETURN);

    if (vm.trace) {
#ifdef DEBUG_TRACE_EXECUTION
        disassembleChunk(compiler->chunk, "code");
#endif
    }

    freeCompiler(compiler);
    return !compiler->hadError;
}
