/*
 * compiler.c
 * Clean, restructured Orus compiler with static typing integration
 */

#include "../../include/compiler.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "../../include/ast.h"
#include "../../include/common.h"
#include "../../include/lexer.h"
#include "../../include/memory.h"
#include "../../include/parser.h"
#include "../../include/scope_analysis.h"
#include "../../include/symbol_table.h"
#include "../../include/type.h"
#include "../../include/vm.h"

// Forward declarations
static void compileStatement(ASTNode* node, Compiler* compiler);
static uint8_t compileExpressionInternal(ASTNode* node, Compiler* compiler);
static ValueType valueKindFromType(ASTNode* typeNode);
static void compileVarDecl(ASTNode* node, Compiler* compiler);
static void compileAssign(ASTNode* node, Compiler* compiler);
static void compileFunctionDecl(ASTNode* node, Compiler* compiler);
static void compileForRange(ASTNode* node, Compiler* compiler);
static void compileForIter(ASTNode* node, Compiler* compiler);
static void compileIf(ASTNode* node, Compiler* compiler);
static void compileWhile(ASTNode* node, Compiler* compiler);
static void compileBreak(ASTNode* node, Compiler* compiler);
static void compileContinue(ASTNode* node, Compiler* compiler);
static void compilePrint(ASTNode* node, Compiler* compiler);
static uint8_t compileGetVariable(ASTNode* node, Compiler* compiler);
static void patchJumpPlaceholder(Compiler* compiler);
static int emitJump(Compiler* compiler, uint8_t instr);
static void patchJump(Compiler* compiler, int offset);
static void emitLoop(Compiler* compiler, int start);
static void setRegisterType(Compiler* compiler, uint8_t reg, ValueType t);
static ValueType getRegisterType(Compiler* compiler, uint8_t reg);

// ---------------------------------------------------------------------------

// Patch jump placeholder logic for loops
static void patchJumpPlaceholder(Compiler* compiler) {
    // Placeholder, implemented per loop or continue target
    (void)compiler;
}

// Emit a short or long jump instruction placeholder
static int emitJump(Compiler* compiler, uint8_t instr) {
    emitByte(compiler, instr);
    emitByte(compiler, 0xFF);
    return compiler->chunk->count - 1;
}

// Replace the placeholder with real offset
static void patchJump(Compiler* compiler, int offset) {
    int jump = compiler->chunk->count - offset - 1;
    if (jump <= 255) {
        compiler->chunk->code[offset] = (uint8_t)jump;
    }
}

static void emitLoop(Compiler* compiler, int start) {
    int offset = compiler->chunk->count - start + 2;
    if (offset <= 255) {
        emitByte(compiler, OP_LOOP_SHORT);
        emitByte(compiler, (uint8_t)offset);
    } else {
        emitByte(compiler, OP_LOOP);
        emitByte(compiler, (offset >> 8) & 0xFF);
        emitByte(compiler, offset & 0xFF);
    }
}

// Set and retrieve register types
static void setRegisterType(Compiler* compiler, uint8_t reg, ValueType t) {
    compiler->registerTypes[reg] = t;
}

static ValueType getRegisterType(Compiler* compiler, uint8_t reg) {
    return compiler->registerTypes[reg];
}

bool compile(ASTNode* ast, Compiler* compiler, bool isModule) {
    (void)isModule;
    if (!ast) return false;

    compiler->hadError = false;
    compiler->nextRegister = 0;
    for (int i = 0; i < REGISTER_COUNT; i++) {
        compiler->registerTypes[i] = VAL_NIL;
    }
    symbol_table_init(&compiler->symbols);

    for (int i = 0; i < ast->program.count; i++) {
        compileStatement(ast->program.declarations[i], compiler);
        if (compiler->hadError) break;
    }

    emitByte(compiler, OP_RETURN_VOID);
    return !compiler->hadError;
}

static void compileStatement(ASTNode* node, Compiler* compiler) {
    switch (node->type) {
        case NODE_VAR_DECL:
            compileVarDecl(node, compiler);
            break;
        case NODE_ASSIGN:
            compileAssign(node, compiler);
            break;
        case NODE_FUNCTION:
            compileFunctionDecl(node, compiler);
            break;
        case NODE_FOR_RANGE:
            compileForRange(node, compiler);
            break;
        case NODE_FOR_ITER:
            compileForIter(node, compiler);
            break;
        case NODE_IF:
            compileIf(node, compiler);
            break;
        case NODE_WHILE:
            compileWhile(node, compiler);
            break;
        case NODE_BREAK:
            compileBreak(node, compiler);
            break;
        case NODE_CONTINUE:
            compileContinue(node, compiler);
            break;
        case NODE_PRINT:
            compilePrint(node, compiler);
            break;
        case NODE_BLOCK:
            for (int i = 0; i < node->block.count; i++) {
                compileStatement(node->block.statements[i], compiler);
            }
            break;
        default:
            compileExpressionInternal(node, compiler);
            break;
    }
}

// ---------------------------------------------------------------------------
// Function declaration (NODE_FUNCTION)
// ---------------------------------------------------------------------------
static void compileFunctionDecl(ASTNode* node, Compiler* compiler) {
    Compiler fnCompiler;
    Chunk* fnChunk = (Chunk*)malloc(sizeof(Chunk));
    initChunk(fnChunk);
    initCompiler(&fnCompiler, fnChunk, compiler->fileName, compiler->source);

    // Register parameters
    for (int i = 0; i < node->function.paramCount; i++) {
        FunctionParam* param = &node->function.params[i];
        uint8_t reg = allocateRegister(&fnCompiler);
        symbol_table_set(&fnCompiler.symbols, param->name, reg);
    }

    ASTNode* body = node->function.body;

    if (body->type == NODE_BLOCK && body->block.count > 0) {
        // Compile all but last statement
        for (int i = 0; i < body->block.count - 1; i++) {
            compileStatement(body->block.statements[i], &fnCompiler);
        }

        // Compile last statement and return it
        ASTNode* last = body->block.statements[body->block.count - 1];
        if (last->type == NODE_BINARY ||
            last->type == NODE_CALL || last->type == NODE_LITERAL) {
            uint8_t result = compileExpressionInternal(last, &fnCompiler);
            emitByte(&fnCompiler, OP_RETURN_R);
            emitByte(&fnCompiler, result);
        } else {
            compileStatement(last, &fnCompiler);
            emitByte(&fnCompiler, OP_RETURN_VOID);
        }
    } else {
        compileStatement(body, &fnCompiler);
        emitByte(&fnCompiler, OP_RETURN_VOID);
    }

    ObjFunction* fn = allocateFunction();
    fn->chunk = fnChunk;
    fn->name = allocateString(node->function.name, strlen(node->function.name));
    fn->arity = node->function.paramCount;
    
    Value fnVal = FUNCTION_VAL(fn);
    int constant = addConstant(compiler->chunk, fnVal);

    uint8_t dst = allocateRegister(compiler);
    emitByte(compiler, OP_CLOSURE_R);
    emitByte(compiler, dst);
    emitByte(compiler, constant & 0xFF);
    emitByte(compiler, (constant >> 8) & 0xFF);

    symbol_table_set(&compiler->symbols, node->function.name, dst);
}

// ---------------------------------------------------------------------------
// For-range loop (NODE_FOR_RANGE)
// ---------------------------------------------------------------------------
static void compileForRange(ASTNode* node, Compiler* compiler) {
    char* varName = node->forRange.varName;
    ASTNode* startExpr = node->forRange.start;
    ASTNode* endExpr = node->forRange.end;
    ASTNode* stepExpr = node->forRange.step;

    uint8_t start = compileExpressionInternal(startExpr, compiler);
    uint8_t end = compileExpressionInternal(endExpr, compiler);
    uint8_t step = stepExpr ? compileExpressionInternal(stepExpr, compiler) : 0;

    uint8_t i = allocateRegister(compiler);
    emitByte(compiler, OP_MOVE);
    emitByte(compiler, i);
    emitByte(compiler, start);
    symbol_table_set(&compiler->symbols, varName, i);

    int loopStart = compiler->chunk->count;

    uint8_t cond = allocateRegister(compiler);
    emitByte(compiler, OP_LT_I32_R);
    emitByte(compiler, cond);
    emitByte(compiler, i);
    emitByte(compiler, end);

    int exitJump = emitJump(compiler, OP_JUMP_IF_NOT_SHORT);
    emitByte(compiler, cond);

    compileStatement(node->forRange.body, compiler);

    if (stepExpr) {
        emitByte(compiler, OP_ADD_I32_R);
        emitByte(compiler, i);
        emitByte(compiler, i);
        emitByte(compiler, step);
    } else {
        emitByte(compiler, OP_INC_I32_R);
        emitByte(compiler, i);
        emitByte(compiler, i);
        emitByte(compiler, 1);  // immediate value
    }

    emitLoop(compiler, loopStart);
    patchJump(compiler, exitJump);
}

// ---------------------------------------------------------------------------
// For-iter loop (NODE_FOR_ITER)
// ---------------------------------------------------------------------------
static void compileForIter(ASTNode* node, Compiler* compiler) {
    char* varName = node->forIter.varName;
    ASTNode* iterable = node->forIter.iterable;

    uint8_t arrayReg = compileExpressionInternal(iterable, compiler);

    uint8_t indexReg = allocateRegister(compiler);
    emitByte(compiler, OP_LOAD_I32_CONST);
    emitByte(compiler, indexReg);
    emitByte(compiler, 0);

    uint8_t lenReg = allocateRegister(compiler);
    emitByte(compiler, OP_ARRAY_LEN_R);
    emitByte(compiler, lenReg);
    emitByte(compiler, arrayReg);

    int loopStart = compiler->chunk->count;

    uint8_t condReg = allocateRegister(compiler);
    emitByte(compiler, OP_LT_I32_R);
    emitByte(compiler, condReg);
    emitByte(compiler, indexReg);
    emitByte(compiler, lenReg);

    int exitJump = emitJump(compiler, OP_JUMP_IF_NOT_SHORT);
    emitByte(compiler, condReg);

    uint8_t elemReg = allocateRegister(compiler);
    emitByte(compiler, OP_ARRAY_GET_R);
    emitByte(compiler, elemReg);
    emitByte(compiler, arrayReg);
    emitByte(compiler, indexReg);

    symbol_table_set(&compiler->symbols, varName, elemReg);

    compileStatement(node->forIter.body, compiler);

    emitByte(compiler, OP_INC_I32_R);
    emitByte(compiler, indexReg);
    emitByte(compiler, indexReg);
    emitByte(compiler, 1);  // immediate value

    emitLoop(compiler, loopStart);
    patchJump(compiler, exitJump);
}

// ---------------------------------------------------------------------------
// If statement (NODE_IF)
// ---------------------------------------------------------------------------
static void compileIf(ASTNode* node, Compiler* compiler) {
    uint8_t cond = compileExpressionInternal(node->ifStmt.condition, compiler);

    int elseJump = emitJump(compiler, OP_JUMP_IF_NOT_SHORT);
    emitByte(compiler, cond);

    compileStatement(node->ifStmt.thenBranch, compiler);

    if (node->ifStmt.elseBranch) {
        int endJump = emitJump(compiler, OP_JUMP_SHORT);
        patchJump(compiler, elseJump);
        compileStatement(node->ifStmt.elseBranch, compiler);
        patchJump(compiler, endJump);
    } else {
        patchJump(compiler, elseJump);
    }
}

// ---------------------------------------------------------------------------
// While loop (NODE_WHILE)
// ---------------------------------------------------------------------------
static void compileWhile(ASTNode* node, Compiler* compiler) {
    int loopStart = compiler->chunk->count;

    uint8_t cond =
        compileExpressionInternal(node->whileStmt.condition, compiler);

    int exitJump = emitJump(compiler, OP_JUMP_IF_NOT_SHORT);
    emitByte(compiler, cond);

    compileStatement(node->whileStmt.body, compiler);

    emitLoop(compiler, loopStart);
    patchJump(compiler, exitJump);
}

// ---------------------------------------------------------------------------
// Break statement (NODE_BREAK)
// ---------------------------------------------------------------------------
static void compileBreak(ASTNode* node, Compiler* compiler) {
    (void)node;
    emitJump(compiler, OP_JUMP);
    emitByte(compiler,
             0xFF);  // Placeholder — implement jump stack for patching
}

// ---------------------------------------------------------------------------
// Continue statement (NODE_CONTINUE)
// ---------------------------------------------------------------------------
static void compileContinue(ASTNode* node, Compiler* compiler) {
    (void)node;
    emitJump(compiler, OP_JUMP);
    emitByte(compiler, 0xFE);  // Placeholder offset
}

// ---------------------------------------------------------------------------
// Print statement (NODE_PRINT)
// ---------------------------------------------------------------------------
static void compilePrint(ASTNode* node, Compiler* compiler) {
    // Compile all print arguments into registers
    uint8_t firstReg = compiler->nextRegister;
    for (int i = 0; i < node->print.count; i++) {
        compileExpressionInternal(node->print.values[i], compiler);
    }
    
    // Emit print instruction
    emitByte(compiler, OP_PRINT_MULTI_R);
    emitByte(compiler, firstReg);
    emitByte(compiler, (uint8_t)node->print.count);
    emitByte(compiler, node->print.newline ? 1 : 0);
}

// ---------------------------------------------------------------------------
// Variable declaration with static type checking
// ---------------------------------------------------------------------------
static void compileVarDecl(ASTNode* node, Compiler* compiler) {
    // Determine declared type (default to i32)
    ValueType declared = VAL_I32;
    if (node->varDecl.typeAnnotation) {
        declared = valueKindFromType(node->varDecl.typeAnnotation);
    }
    // Compile initializer
    uint8_t initReg =
        compileExpressionInternal(node->varDecl.initializer, compiler);
    // Type check
    ValueType actual = getRegisterType(compiler, initReg);
    if (actual != declared) {
        // TODO: Add proper error reporting with line number from node location
        compiler->hadError = true;
        return;
    }
    // Bind in symbol table
    symbol_table_set(&compiler->symbols, node->varDecl.name, initReg);
}

// ---------------------------------------------------------------------------
// Assignment
// ---------------------------------------------------------------------------
static void compileAssign(ASTNode* node, Compiler* compiler) {
    uint8_t valueReg = compileExpressionInternal(node->assign.value, compiler);
    int targetReg;
    if (!symbol_table_get(&compiler->symbols, node->assign.name, &targetReg)) {
        compiler->hadError = true;
        return;
    }
    emitByte(compiler, OP_MOVE);
    emitByte(compiler, (uint8_t)targetReg);
    emitByte(compiler, valueReg);
    freeRegister(compiler, valueReg);
}

// ---------------------------------------------------------------------------
// Load variable
// ---------------------------------------------------------------------------
static uint8_t compileGetVariable(ASTNode* node, Compiler* compiler) {
    int srcReg;
    if (!symbol_table_get(&compiler->symbols, node->identifier.name, &srcReg)) {
        compiler->hadError = true;
        return 0;
    }
    uint8_t dst = allocateRegister(compiler);
    emitByte(compiler, OP_MOVE);
    emitByte(compiler, dst);
    emitByte(compiler, (uint8_t)srcReg);
    setRegisterType(compiler, dst, getRegisterType(compiler, (uint8_t)srcReg));
    return dst;
}

/// ---------------------------------------------------------------------------
// Expression nodes
// ---------------------------------------------------------------------------
static uint8_t compileLiteral(ASTNode* node, Compiler* compiler) {
    uint8_t reg = compiler->nextRegister++;
    
    // Use the value directly from the parser
    Value value = node->literal.value;
    
    emitConstant(compiler, reg, value);
    setRegisterType(compiler, reg, value.type);
    return reg;
}

static uint8_t compileUnary(ASTNode* node, Compiler* compiler) {
    uint8_t operand = compileExpressionInternal(node->unary.operand, compiler);
    uint8_t dest = compiler->nextRegister++;
    
    ValueType type = getRegisterType(compiler, operand);
    const char* op = node->unary.op;
    
    if (strcmp(op, "-") == 0) {
        // Negate operation - multiply by -1
        switch (type) {
            case VAL_I32:
                emitByte(compiler, OP_SUB_I32_R);
                emitByte(compiler, dest);
                emitByte(compiler, 0);  // Use register 0 which should contain 0
                emitByte(compiler, operand);
                break;
            case VAL_I64:
                emitByte(compiler, OP_SUB_I64_R);
                emitByte(compiler, dest);
                emitByte(compiler, 0);
                emitByte(compiler, operand);
                break;
            case VAL_F64:
                emitByte(compiler, OP_SUB_F64_R);
                emitByte(compiler, dest);
                emitByte(compiler, 0);
                emitByte(compiler, operand);
                break;
            default:
                compiler->hadError = true;
                return 0;
        }
    } else if (strcmp(op, "!") == 0) {
        // Logical NOT
        emitByte(compiler, OP_NOT_BOOL_R);
        emitByte(compiler, dest);
        emitByte(compiler, operand);
        type = VAL_BOOL;
    } else {
        compiler->hadError = true;
        return 0;
    }
    
    setRegisterType(compiler, dest, type);
    return dest;
}

static uint8_t compileBinary(ASTNode* node, Compiler* compiler) {
    uint8_t left = compileExpressionInternal(node->binary.left, compiler);
    uint8_t right = compileExpressionInternal(node->binary.right, compiler);
    uint8_t dest = compiler->nextRegister++;

    ValueType type = getRegisterType(compiler, left);
    const char* op = node->binary.op;

    if (strcmp(op, "+") == 0)
        emitByte(compiler, OP_ADD_I32_R);
    else if (strcmp(op, "-") == 0)
        emitByte(compiler, OP_SUB_I32_R);
    else if (strcmp(op, "*") == 0)
        emitByte(compiler, OP_MUL_I32_R);
    else if (strcmp(op, "/") == 0)
        emitByte(compiler, OP_DIV_I32_R);
    else {
        compiler->hadError = true;
        return 0;
    }

    emitByte(compiler, dest);
    emitByte(compiler, left);
    emitByte(compiler, right);
    setRegisterType(compiler, dest, type);
    return dest;
}

static uint8_t compileCall(ASTNode* node, Compiler* compiler) {
    uint8_t callee = compileExpressionInternal(node->call.callee, compiler);
    uint8_t base = compiler->nextRegister++;
    for (int i = 0; i < node->call.argCount; i++) {
        uint8_t arg = compileExpressionInternal(node->call.args[i], compiler);
        emitByte(compiler, OP_MOVE);
        emitByte(compiler, base + i);
        emitByte(compiler, arg);
    }
    uint8_t result = compiler->nextRegister++;
    emitByte(compiler, OP_CALL_R);
    emitByte(compiler, callee);
    emitByte(compiler, base);
    emitByte(compiler, (uint8_t)node->call.argCount);
    emitByte(compiler, result);
    return result;
}

static uint8_t compileExpressionInternal(ASTNode* node, Compiler* compiler) {
    switch (node->type) {
        case NODE_LITERAL:
            return compileLiteral(node, compiler);
        case NODE_BINARY:
            return compileBinary(node, compiler);
        case NODE_UNARY:
            return compileUnary(node, compiler);
        case NODE_CALL:
            return compileCall(node, compiler);
        case NODE_IDENTIFIER:
            return compileGetVariable(node, compiler);
        default:
            compiler->hadError = true;
            return 0;
    }
}


// Core emission
void emitByte(Compiler* compiler, uint8_t byte) {
    writeChunk(compiler->chunk, byte, compiler->chunk->count, 0);
}

// Convert AST type node to ValueType
static ValueType valueKindFromType(ASTNode* typeNode) {
    if (!typeNode || typeNode->type != NODE_TYPE) {
        return VAL_I32;  // Default type
    }
    
    const char* typeName = typeNode->typeAnnotation.name;
    if (strcmp(typeName, "i32") == 0) return VAL_I32;
    if (strcmp(typeName, "i64") == 0) return VAL_I64;
    if (strcmp(typeName, "u32") == 0) return VAL_U32;
    if (strcmp(typeName, "u64") == 0) return VAL_U64;
    if (strcmp(typeName, "f64") == 0) return VAL_F64;
    if (strcmp(typeName, "bool") == 0) return VAL_BOOL;
    if (strcmp(typeName, "string") == 0) return VAL_STRING;
    
    return VAL_I32;  // Default fallback
}

void emitConstant(Compiler* compiler, uint8_t reg, Value value) {
    int constant = addConstant(compiler->chunk, value);
    emitByte(compiler, OP_LOAD_CONST);
    emitByte(compiler, reg);
    emitByte(compiler, (uint8_t)(constant & 0xFF));
    emitByte(compiler, (uint8_t)((constant >> 8) & 0xFF));
}

uint8_t allocateRegister(Compiler* compiler) {
    if (compiler->nextRegister >= (uint8_t)REGISTER_COUNT) {
        compiler->hadError = true;
        return 0;
    }
    return compiler->nextRegister++;
}

void freeRegister(Compiler* compiler, uint8_t reg) {
    // No-op for now or implement simple free-list
    (void)compiler;
    (void)reg;
}

void initCompiler(Compiler* compiler, Chunk* chunk, const char* fileName,
                  const char* source) {
    compiler->chunk = chunk;
    compiler->fileName = fileName;
    compiler->source = source;
    compiler->hadError = false;
    compiler->nextRegister = 0;
    for (int i = 0; i < REGISTER_COUNT; i++)
        compiler->registerTypes[i] = VAL_NIL;
    symbol_table_init(&compiler->symbols);
}

void freeCompiler(Compiler* compiler) { symbol_table_free(&compiler->symbols); }
