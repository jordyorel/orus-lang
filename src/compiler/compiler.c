#include "../../include/compiler.h"
#include "../../include/common.h"
#include <string.h>

// Convert an expression AST to a register and emit bytecode
int compileExpressionToRegister(ASTNode* node, Compiler* compiler) {
    if (!node) return -1;

    switch (node->type) {
        case NODE_LITERAL: {
            uint8_t reg = allocateRegister(compiler);
            emitConstant(compiler, reg, node->literal.value);
            return reg;
        }
        case NODE_BINARY: {
            int leftReg = compileExpressionToRegister(node->binary.left, compiler);
            if (leftReg < 0) return -1;
            int rightReg = compileExpressionToRegister(node->binary.right, compiler);
            if (rightReg < 0) return -1;
            uint8_t resultReg = allocateRegister(compiler);
            const char* op = node->binary.op;
            if (strcmp(op, "+") == 0) {
                emitByte(compiler, OP_ADD_I32_R);
            } else if (strcmp(op, "-") == 0) {
                emitByte(compiler, OP_SUB_I32_R);
            } else if (strcmp(op, "*") == 0) {
                emitByte(compiler, OP_MUL_I32_R);
            } else if (strcmp(op, "/") == 0) {
                emitByte(compiler, OP_DIV_I32_R);
            } else if (strcmp(op, "%") == 0) {
                emitByte(compiler, OP_MOD_I32_R);
            } else if (strcmp(op, "==") == 0) {
                emitByte(compiler, OP_EQ_R);
            } else if (strcmp(op, "<") == 0) {
                emitByte(compiler, OP_LT_I32_R);
            } else {
                return -1;
            }
            emitByte(compiler, resultReg);
            emitByte(compiler, (uint8_t)leftReg);
            emitByte(compiler, (uint8_t)rightReg);
            return resultReg;
        }
        case NODE_IDENTIFIER: {
            const char* name = node->identifier.name;
            for (int i = compiler->localCount - 1; i >= 0; i--) {
                if (compiler->locals[i].isActive &&
                    strcmp(compiler->locals[i].name, name) == 0) {
                    return compiler->locals[i].reg;
                }
            }
            uint8_t reg = allocateRegister(compiler);
            emitConstant(compiler, reg, I32_VAL(0));
            return reg;
        }
        case NODE_VAR_DECL: {
            int initReg = compileExpressionToRegister(node->varDecl.initializer, compiler);
            if (initReg < 0) return -1;
            if (compiler->localCount >= REGISTER_COUNT) {
                return -1;
            }
            int localIndex = compiler->localCount++;
            compiler->locals[localIndex].name = node->varDecl.name;
            compiler->locals[localIndex].reg = (uint8_t)initReg;
            compiler->locals[localIndex].isActive = true;
            return initReg;
        }
        default:
            return -1;
    }
}

bool compileExpression(ASTNode* node, Compiler* compiler) {
    return compileExpressionToRegister(node, compiler) >= 0;
}

// ---------------------------------------------------------------------------
// Compiler setup and register allocation
// ---------------------------------------------------------------------------

void initCompiler(Compiler* compiler, Chunk* chunk, const char* fileName,
                  const char* source) {
    compiler->chunk = chunk;
    compiler->fileName = fileName;
    compiler->source = source;
    compiler->nextRegister = 0;
    compiler->maxRegisters = 0;
    compiler->localCount = 0;
    compiler->hadError = false;
}

uint8_t allocateRegister(Compiler* compiler) {
    if (compiler->nextRegister >= (REGISTER_COUNT - 1)) {
        compiler->hadError = true;
        return 0;
    }

    uint8_t reg = compiler->nextRegister++;
    if (compiler->nextRegister > compiler->maxRegisters) {
        compiler->maxRegisters = compiler->nextRegister;
    }

    return reg;
}

void freeRegister(Compiler* compiler, uint8_t reg) {
    if (reg == compiler->nextRegister - 1) {
        compiler->nextRegister--;
    }
}

// ---------------------------------------------------------------------------
// Bytecode emission helpers
// ---------------------------------------------------------------------------

void emitByte(Compiler* compiler, uint8_t byte) {
    writeChunk(compiler->chunk, byte, 1, 1);
}

__attribute__((unused))
void emitBytes(Compiler* compiler, uint8_t byte1, uint8_t byte2) {
    emitByte(compiler, byte1);
    emitByte(compiler, byte2);
}

void emitConstant(Compiler* compiler, uint8_t reg, Value value) {
    int constant = addConstant(compiler->chunk, value);
    if (constant > UINT8_MAX) {
        compiler->hadError = true;
        return;
    }
    emitByte(compiler, OP_LOAD_CONST);
    emitByte(compiler, reg);
    emitByte(compiler, (uint8_t)constant);
}

// ---------------------------------------------------------------------------
// Top-level compilation entry
// ---------------------------------------------------------------------------

bool compile(ASTNode* ast, Compiler* compiler, bool isModule) {
    UNUSED(isModule);

    if (!ast) {
        return false;
    }

    if (ast->type == NODE_PROGRAM) {
        for (int i = 0; i < ast->program.count; i++) {
            ASTNode* stmt = ast->program.declarations[i];
            int reg = compileExpressionToRegister(stmt, compiler);
            if (reg < 0) return false;
            if (!isModule && stmt->type != NODE_VAR_DECL) {
                emitByte(compiler, OP_PRINT_R);
                emitByte(compiler, (uint8_t)reg);
            }
        }
        return true;
    }

    int resultReg = compileExpressionToRegister(ast, compiler);

    if (resultReg >= 0 && !isModule && ast->type != NODE_VAR_DECL) {
        emitByte(compiler, OP_PRINT_R);
        emitByte(compiler, (uint8_t)resultReg);
    }

    return resultReg >= 0;
}
