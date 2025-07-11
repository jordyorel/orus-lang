/*
 * compiler.c
 * Clean, restructured Orus compiler with proper static typing integration
 */

#include "../../include/compiler.h"
#include "../../include/common.h"
#include "../../include/vm.h"
#include "../../include/ast.h"
#include "../../include/type.h"
#include "../../include/jumptable.h"
#include "../../include/symbol_table.h"
#include "../../include/lexer.h"
#include "../../include/parser.h"
#include "../../include/scope_analysis.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// Forward declarations
static void compileStatement(ASTNode* node, Compiler* compiler);
static uint8_t compileExpression_internal(ASTNode* node, Compiler* compiler);
static ValueType valueKindFromType(ASTNode* typeNode);
// Function is already declared in scope_analysis.h
static uint8_t compilerGetVariable(Compiler* compiler, const char* name);
static void patchJumpPlaceholder(Compiler* compiler);
static int emitJump(Compiler* compiler, uint8_t instr);
static void patchJump(Compiler* compiler, int offset);
static void emitLoop(Compiler* compiler, int start);
static void setRegisterType(Compiler* compiler, uint8_t reg, ValueType t);
static ValueType getRegisterType(Compiler* compiler, uint8_t reg);
// Function is already declared in compiler.h

// Main compile function
bool compile(ASTNode* ast, Compiler* compiler, bool isModule) {
    (void)isModule;
    if (!ast) return false;

    // Initialize compiler state
    compiler->hadError = false;
    compiler->nextRegister = 0;
    
    // Initialize register types
    for (int i = 0; i < REGISTER_COUNT; i++) {
        compiler->registerTypes[i] = VAL_NIL;
    }
    
    // Initialize symbol table
    symbol_table_init(&compiler->symbols);
    
    // Compile all statements
    for (int i = 0; i < ast->program.count; i++) {
        compileStatement(ast->program.declarations[i], compiler);
        if (compiler->hadError) break;
    }
    
    // Emit final return
    emitByte(compiler, OP_RETURN_VOID);
    
    return !compiler->hadError;
}

// Statement compilation
static void compileStatement(ASTNode* node, Compiler* compiler) {
    switch (node->type) {
        case NODE_VAR_DECL: {
            ValueType declared = VAL_I32;
            if (node->varDecl.typeAnnotation) {
                declared = valueKindFromType(node->varDecl.typeAnnotation);
            }
            
            uint8_t initReg = compileExpression_internal(node->varDecl.initializer, compiler);
            compilerDeclareVariable(compiler, node->varDecl.name, declared, initReg);
            break;
        }
        
        // Expression statements are handled by their direct node types
        case NODE_CALL:
        case NODE_ASSIGN:
            compileExpression_internal(node, compiler);
            break;
            
        case NODE_IF: {
            uint8_t cond = compileExpression_internal(node->ifStmt.condition, compiler);
            int elseJump = emitJump(compiler, OP_JUMP_IF_NOT_SHORT);
            compileStatement(node->ifStmt.thenBranch, compiler);
            if (node->ifStmt.elseBranch) {
                int endJump = emitJump(compiler, OP_JUMP_SHORT);
                patchJump(compiler, elseJump);
                compileStatement(node->ifStmt.elseBranch, compiler);
                patchJump(compiler, endJump);
            } else {
                patchJump(compiler, elseJump);
            }
            break;
        }
        
        case NODE_WHILE: {
            int loopStart = compiler->chunk->count;
            compileExpression_internal(node->whileStmt.condition, compiler);
            int exitJump = emitJump(compiler, OP_JUMP_IF_NOT_SHORT);
            compileStatement(node->whileStmt.body, compiler);
            emitLoop(compiler, loopStart);
            patchJump(compiler, exitJump);
            break;
        }
        
        case NODE_RETURN:
            if (node->returnStmt.value) {
                uint8_t rv = compileExpression_internal(node->returnStmt.value, compiler);
                emitByte(compiler, OP_RETURN_R);
                emitByte(compiler, rv);
            } else {
                emitByte(compiler, OP_RETURN_VOID);
            }
            break;
            
        case NODE_BLOCK:
            for (int i = 0; i < node->block.count; i++) {
                compileStatement(node->block.statements[i], compiler);
            }
            break;
            
        default:
            break;
    }
}

// Expression compilation
static uint8_t compileExpression_internal(ASTNode* node, Compiler* compiler) {
    switch (node->type) {
        case NODE_LITERAL: {
            uint8_t r = allocateRegister(compiler);
            emitConstant(compiler, r, node->literal.value);
            setRegisterType(compiler, r, node->literal.value.type);
            return r;
        }
        
        case NODE_IDENTIFIER: {
            return compilerGetVariable(compiler, node->identifier.name);
        }
        
        case NODE_UNARY: {
            uint8_t op = compileExpression_internal(node->unary.operand, compiler);
            uint8_t res = allocateRegister(compiler);
            if (strcmp(node->unary.op, "not") == 0) {
                emitByte(compiler, OP_NOT_BOOL_R);
                emitByte(compiler, res);
                emitByte(compiler, op);
                setRegisterType(compiler, res, VAL_BOOL);
            }
            freeRegister(compiler, op);
            return res;
        }
        
        case NODE_BINARY: {
            uint8_t l = compileExpression_internal(node->binary.left, compiler);
            uint8_t r = compileExpression_internal(node->binary.right, compiler);
            ValueType lt = getRegisterType(compiler, l);
            ValueType rt = getRegisterType(compiler, r);
            
            // Use the left type as result type (simplified)
            ValueType out = lt;
            
            uint8_t dst = allocateRegister(compiler);
            emitTypedBinaryOp(compiler, node->binary.op, out, dst, l, r);
            freeRegister(compiler, l);
            freeRegister(compiler, r);
            return dst;
        }
        
        case NODE_ASSIGN: {
            uint8_t valueReg = compileExpression_internal(node->assign.value, compiler);
            // Simple assignment - just return the value register
            return valueReg;
        }
        
        case NODE_CALL: {
            uint8_t fn = compileExpression_internal(node->call.callee, compiler);
            uint8_t base = allocateRegister(compiler);
            for (int i = 0; i < node->call.argCount; i++) {
                uint8_t a = compileExpression_internal(node->call.args[i], compiler);
                emitByte(compiler, OP_MOVE);
                emitByte(compiler, base + i);
                emitByte(compiler, a);
                freeRegister(compiler, a);
            }
            uint8_t ret = allocateRegister(compiler);
            emitByte(compiler, OP_CALL_R);
            emitByte(compiler, fn);
            emitByte(compiler, base);
            emitByte(compiler, (uint8_t)node->call.argCount);
            emitByte(compiler, ret);
            freeRegister(compiler, fn);
            return ret;
        }
        
        default:
            compiler->hadError = true;
            return 0;
    }
}

// Helper implementations
static ValueType valueKindFromType(ASTNode* typeNode) {
    // For now, just return default type
    (void)typeNode;
    return VAL_I32;
}

// Function is already implemented in scope_analysis.c

static uint8_t compilerGetVariable(Compiler* compiler, const char* name) {
    int localIndex;
    if (symbol_table_get(&compiler->symbols, name, &localIndex)) {
        return compiler->locals[localIndex].reg;
    }
    compiler->hadError = true;
    return 0;
}

static void patchJumpPlaceholder(Compiler* compiler) {
    (void)compiler;
}

static int emitJump(Compiler* compiler, uint8_t instr) {
    emitByte(compiler, instr);
    emitByte(compiler, 0xFF);
    return compiler->chunk->count - 1;
}

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

static void setRegisterType(Compiler* compiler, uint8_t reg, ValueType t) {
    compiler->registerTypes[reg] = t;
}

static ValueType getRegisterType(Compiler* compiler, uint8_t reg) {
    return compiler->registerTypes[reg];
}

// Core compiler utility functions
void emitByte(Compiler* compiler, uint8_t byte) {
    writeChunk(compiler->chunk, byte, 1, 1);
}

void emitBytes(Compiler* compiler, uint8_t byte1, uint8_t byte2) {
    emitByte(compiler, byte1);
    emitByte(compiler, byte2);
}

void emitConstant(Compiler* compiler, uint8_t reg, Value value) {
    int constant = addConstant(compiler->chunk, value);
    if (constant > UINT16_MAX) {
        compiler->hadError = true;
        return;
    }
    emitByte(compiler, OP_LOAD_CONSTANT_R);
    emitByte(compiler, reg);
    emitByte(compiler, (uint8_t)(constant & 0xFF));
    emitByte(compiler, (uint8_t)((constant >> 8) & 0xFF));
}

uint8_t allocateRegister(Compiler* compiler) {
    if (compiler->nextRegister >= REGISTER_COUNT) {
        compiler->hadError = true;
        return 0;
    }
    return compiler->nextRegister++;
}

void freeRegister(Compiler* compiler, uint8_t reg) {
    (void)compiler;
    (void)reg;
    // Simple register allocator - just ignore deallocation for now
}

void initCompiler(Compiler* compiler, Chunk* chunk) {
    compiler->chunk = chunk;
    compiler->hadError = false;
    compiler->nextRegister = 0;
    for (int i = 0; i < REGISTER_COUNT; i++) {
        compiler->registerTypes[i] = VAL_NIL;
    }
    symbol_table_init(&compiler->symbols);
}

void freeCompiler(Compiler* compiler) {
    symbol_table_free(&compiler->symbols);
}

// Public API functions to match header
bool compileExpression(ASTNode* node, Compiler* compiler) {
    uint8_t reg = compileExpression_internal(node, compiler);
    return reg != 0 && !compiler->hadError;
}

int compileExpressionToRegister(ASTNode* node, Compiler* compiler) {
    return compileExpression_internal(node, compiler);
}

int compile_typed_expression_to_register(ASTNode* node, Compiler* compiler) {
    return compileExpression_internal(node, compiler);
}

// Stub implementations for TypedExpDesc compatibility
int compile_typed_statement(ASTNode* node, Compiler* compiler) {
    compileStatement(node, compiler);
    return 0;
}

void compile_typed_if_statement(ASTNode* node, Compiler* compiler, TypedExpDesc* result) {
    (void)result;
    compileStatement(node, compiler);
}

void compile_typed_while_statement(ASTNode* node, Compiler* compiler, TypedExpDesc* result) {
    (void)result;
    compileStatement(node, compiler);
}

void compile_typed_for_statement(ASTNode* node, Compiler* compiler, TypedExpDesc* result) {
    (void)result;
    compileStatement(node, compiler);
}

void compile_typed_block_statement(ASTNode* node, Compiler* compiler, TypedExpDesc* result) {
    (void)result;
    compileStatement(node, compiler);
}

void compile_typed_call(Compiler* compiler, ASTNode* node, TypedExpDesc* result) {
    (void)result;
    compileExpression_internal(node, compiler);
}