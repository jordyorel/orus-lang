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
