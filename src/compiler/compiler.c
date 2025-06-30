#include "../../include/compiler.h"
#include "../../include/common.h"
#include <string.h>

static int emitJump(Compiler* compiler) {
    emitByte(compiler, 0);
    emitByte(compiler, 0);
    return compiler->chunk->count - 2;
}

static void patchJump(Compiler* compiler, int offset) {
    int jump = compiler->chunk->count - offset - 2;
    compiler->chunk->code[offset] = (jump >> 8) & 0xFF;
    compiler->chunk->code[offset + 1] = jump & 0xFF;
}

static void enterScope(Compiler* compiler) { compiler->scopeDepth++; }

static void exitScope(Compiler* compiler) {
    compiler->scopeDepth--;
    while (compiler->localCount > 0 &&
           compiler->locals[compiler->localCount - 1].depth > compiler->scopeDepth) {
        freeRegister(compiler, compiler->locals[compiler->localCount - 1].reg);
        compiler->locals[compiler->localCount - 1].isActive = false;
        compiler->localCount--;
    }
}

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
            // Constant folding for binary operations on literals
            if (node->binary.left->type == NODE_LITERAL &&
                node->binary.right->type == NODE_LITERAL &&
                IS_I32(node->binary.left->literal.value) &&
                IS_I32(node->binary.right->literal.value)) {
                int32_t a = AS_I32(node->binary.left->literal.value);
                int32_t b = AS_I32(node->binary.right->literal.value);
                int32_t result = 0;
                bool boolResult = false;
                const char* op = node->binary.op;
                if (strcmp(op, "+") == 0) {
                    result = a + b;
                } else if (strcmp(op, "-") == 0) {
                    result = a - b;
                } else if (strcmp(op, "*") == 0) {
                    result = a * b;
                } else if (strcmp(op, "/") == 0) {
                    if (b == 0) return -1;
                    result = a / b;
                } else if (strcmp(op, "%") == 0) {
                    if (b == 0) return -1;
                    result = a % b;
                } else if (strcmp(op, "==") == 0) {
                    boolResult = (a == b);
                } else if (strcmp(op, "!=") == 0) {
                    boolResult = (a != b);
                } else if (strcmp(op, "<") == 0) {
                    boolResult = (a < b);
                } else if (strcmp(op, ">") == 0) {
                    boolResult = (a > b);
                } else if (strcmp(op, "<=") == 0) {
                    boolResult = (a <= b);
                } else if (strcmp(op, ">=") == 0) {
                    boolResult = (a >= b);
                } else if (strcmp(op, "and") == 0) {
                    boolResult = (a && b);
                } else if (strcmp(op, "or") == 0) {
                    boolResult = (a || b);
                } else {
                    return -1;
                }
                uint8_t reg = allocateRegister(compiler);
                if (strcmp(op, "+") == 0 || strcmp(op, "-") == 0 || strcmp(op, "*") == 0 ||
                    strcmp(op, "/") == 0 || strcmp(op, "%") == 0) {
                    emitConstant(compiler, reg, I32_VAL(result));
                } else {
                    emitConstant(compiler, reg, BOOL_VAL(boolResult));
                }
                return reg;
            }

            int leftReg = compileExpressionToRegister(node->binary.left, compiler);
            if (leftReg < 0) return -1;
            int rightReg = compileExpressionToRegister(node->binary.right, compiler);
            if (rightReg < 0) return -1;

            bool leftTemp = (node->binary.left->type == NODE_LITERAL ||
                              node->binary.left->type == NODE_BINARY);

            uint8_t resultReg = leftTemp ? (uint8_t)leftReg : allocateRegister(compiler);
            const char* op = node->binary.op;
            if (strcmp(op, "+") == 0) {
                if ((node->binary.left->type == NODE_LITERAL && IS_STRING(node->binary.left->literal.value)) ||
                    (node->binary.right->type == NODE_LITERAL && IS_STRING(node->binary.right->literal.value))) {
                    emitByte(compiler, OP_CONCAT_R);
                } else {
                    emitByte(compiler, OP_ADD_I32_R);
                }
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
            } else if (strcmp(op, "!=") == 0) {
                emitByte(compiler, OP_NE_R);
            } else if (strcmp(op, "<") == 0) {
                emitByte(compiler, OP_LT_I32_R);
            } else if (strcmp(op, ">") == 0) {
                emitByte(compiler, OP_GT_I32_R);
            } else if (strcmp(op, "<=") == 0) {
                emitByte(compiler, OP_LE_I32_R);
            } else if (strcmp(op, ">=") == 0) {
                emitByte(compiler, OP_GE_I32_R);
            } else if (strcmp(op, "and") == 0) {
                emitByte(compiler, OP_AND_BOOL_R);
            } else if (strcmp(op, "or") == 0) {
                emitByte(compiler, OP_OR_BOOL_R);
            } else {
                return -1;
            }
            emitByte(compiler, resultReg);         // destination register
            emitByte(compiler, (uint8_t)leftReg);  // left operand
            emitByte(compiler, (uint8_t)rightReg); // right operand

            freeRegister(compiler, (uint8_t)rightReg);
            if (!leftTemp) freeRegister(compiler, (uint8_t)leftReg);

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
            compiler->locals[localIndex].depth = compiler->scopeDepth;
            compiler->locals[localIndex].isMutable = node->varDecl.isMutable;
            return initReg;
        }
        case NODE_ASSIGN: {
            const char* name = node->assign.name;
            int localIndex = -1;
            for (int i = compiler->localCount - 1; i >= 0; i--) {
                if (compiler->locals[i].isActive && strcmp(compiler->locals[i].name, name) == 0) {
                    localIndex = i;
                    break;
                }
            }
            if (localIndex < 0) return -1;
            if (!compiler->locals[localIndex].isMutable) {
                compiler->hadError = true;
                return -1;
            }
            uint8_t valueReg = compileExpressionToRegister(node->assign.value, compiler);
            if (valueReg < 0) return -1;
            emitByte(compiler, OP_MOVE);
            emitByte(compiler, compiler->locals[localIndex].reg);
            emitByte(compiler, valueReg);
            freeRegister(compiler, valueReg);
            return compiler->locals[localIndex].reg;
        }
        case NODE_PRINT: {
            if (node->print.count == 0) return 0;
            uint8_t regs[32];
            for (int i = 0; i < node->print.count; i++) {
                int src = compileExpressionToRegister(node->print.values[i], compiler);
                if (src < 0) return -1;
                uint8_t dest = allocateRegister(compiler);
                regs[i] = dest;
                if (src != dest) {
                    emitByte(compiler, OP_MOVE);
                    emitByte(compiler, dest);
                    emitByte(compiler, (uint8_t)src);
                    if (src >= compiler->localCount)
                        freeRegister(compiler, (uint8_t)src);
                }
            }
            emitByte(compiler, OP_PRINT_MULTI_R);
            emitByte(compiler, regs[0]);
            emitByte(compiler, (uint8_t)node->print.count);
            emitByte(compiler, node->print.newline ? 1 : 0);
            for (int i = 0; i < node->print.count; i++) {
                freeRegister(compiler, regs[i]);
            }
            return regs[0];
        }
        case NODE_BLOCK: {
            for (int i = 0; i < node->block.count; i++) {
                if (compileExpressionToRegister(node->block.statements[i], compiler) < 0)
                    return -1;
            }
            return 0;
        }
        case NODE_IF: {
            int cond = compileExpressionToRegister(node->ifStmt.condition, compiler);
            if (cond < 0) return -1;
            emitByte(compiler, OP_JUMP_IF_NOT_R);
            emitByte(compiler, (uint8_t)cond);
            int elseJump = emitJump(compiler);
            freeRegister(compiler, (uint8_t)cond);

            enterScope(compiler);
            if (compileExpressionToRegister(node->ifStmt.thenBranch, compiler) < 0) {
                exitScope(compiler);
                return -1;
            }
            exitScope(compiler);

            int endJump = -1;
            if (node->ifStmt.elseBranch) {
                emitByte(compiler, OP_JUMP);
                endJump = emitJump(compiler);
            }

            patchJump(compiler, elseJump);

            if (node->ifStmt.elseBranch) {
                enterScope(compiler);
                if (compileExpressionToRegister(node->ifStmt.elseBranch, compiler) < 0) {
                    exitScope(compiler);
                    return -1;
                }
                exitScope(compiler);
                patchJump(compiler, endJump);
            }
            return 0;
        }
        case NODE_TERNARY: {
            int cond = compileExpressionToRegister(node->ternary.condition, compiler);
            if (cond < 0) return -1;
            uint8_t resultReg = allocateRegister(compiler);
            emitByte(compiler, OP_JUMP_IF_NOT_R);
            emitByte(compiler, (uint8_t)cond);
            int falseJump = emitJump(compiler);

            int trueReg = compileExpressionToRegister(node->ternary.trueExpr, compiler);
            if (trueReg < 0) return -1;
            emitByte(compiler, OP_MOVE);
            emitByte(compiler, resultReg);
            emitByte(compiler, (uint8_t)trueReg);
            freeRegister(compiler, (uint8_t)trueReg);
            emitByte(compiler, OP_JUMP);
            int endJump = emitJump(compiler);
            patchJump(compiler, falseJump);

            int falseReg = compileExpressionToRegister(node->ternary.falseExpr, compiler);
            if (falseReg < 0) return -1;
            emitByte(compiler, OP_MOVE);
            emitByte(compiler, resultReg);
            emitByte(compiler, (uint8_t)falseReg);
            freeRegister(compiler, (uint8_t)falseReg);
            patchJump(compiler, endJump);
            freeRegister(compiler, (uint8_t)cond);
            return resultReg;
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
    compiler->scopeDepth = 0;
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
    if (constant > UINT16_MAX) {
        compiler->hadError = true;
        return;
    }
    emitByte(compiler, OP_LOAD_CONST);
    emitByte(compiler, reg);
    emitByte(compiler, (uint8_t)((constant >> 8) & 0xFF));
    emitByte(compiler, (uint8_t)(constant & 0xFF));
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
            if (!isModule && stmt->type != NODE_VAR_DECL && stmt->type != NODE_PRINT &&
                stmt->type != NODE_IF && stmt->type != NODE_BLOCK &&
                stmt->type != NODE_ASSIGN) {
                emitByte(compiler, OP_PRINT_R);
                emitByte(compiler, (uint8_t)reg);
            }
        }
        return true;
    }

    int resultReg = compileExpressionToRegister(ast, compiler);

    if (resultReg >= 0 && !isModule && ast->type != NODE_VAR_DECL && ast->type != NODE_PRINT &&
        ast->type != NODE_IF && ast->type != NODE_BLOCK) {
        emitByte(compiler, OP_PRINT_R);
        emitByte(compiler, (uint8_t)resultReg);
    }

    return resultReg >= 0;
}
