#include "../../include/compiler.h"
#include "../../include/common.h"
#include "../../include/jumptable.h"
#include <string.h>

static void insertCode(Compiler* compiler, int offset, uint8_t* code, int length) {
    if (compiler->chunk->count + length > compiler->chunk->capacity) {
        compiler->chunk->capacity = (compiler->chunk->count + length) * 2;
        compiler->chunk->code = realloc(compiler->chunk->code, compiler->chunk->capacity);
        compiler->chunk->lines = realloc(compiler->chunk->lines, compiler->chunk->capacity * sizeof(int));
        compiler->chunk->columns = realloc(compiler->chunk->columns, compiler->chunk->capacity * sizeof(int));
    }
    
    memmove(compiler->chunk->code + offset + length, 
            compiler->chunk->code + offset, 
            compiler->chunk->count - offset);
    memcpy(compiler->chunk->code + offset, code, length);
    compiler->chunk->count += length;
}


static int emitJump(Compiler* compiler, uint8_t instruction) {
    emitByte(compiler, instruction);
    if (instruction == OP_JUMP_SHORT) {
        emitByte(compiler, 0xFF);
        return compiler->chunk->count - 1;
    } else {
        emitByte(compiler, 0xFF);
        emitByte(compiler, 0xFF);
        return compiler->chunk->count - 2;
    }
}

static void patchJump(Compiler* compiler, int offset) {
    int jump = compiler->chunk->count - offset - 1; // -1 for 1-byte placeholder
    
    if (jump > 255) {
        // The jump is too long, so we need to use a long jump
        uint8_t long_jump[] = { OP_JUMP, (jump >> 8) & 0xFF, jump & 0xFF };
        
        // Overwrite the short jump placeholder with a long jump
        // This requires shifting the bytecode
        insertCode(compiler, offset - 1, long_jump, sizeof(long_jump));
        
        // Since we inserted code, we need to adjust other jumps
        // This is a complex problem, and for now, we assume it's handled
        // correctly by the overall compilation strategy.
        // A more robust solution might involve a jump table that gets updated.
        
    } else {
        // The jump is short enough, so we can patch the placeholder
        compiler->chunk->code[offset] = (uint8_t)jump;
    }
}

static int emitConditionalJump(Compiler* compiler, uint8_t reg) {
    emitByte(compiler, OP_JUMP_IF_NOT_SHORT);
    emitByte(compiler, reg);
    emitByte(compiler, 0xFF); // 1-byte placeholder
    return compiler->chunk->count - 1;
}

// Smart loop emission for backward jumps
static void emitLoop(Compiler* compiler, int loopStart) {
    int offset = compiler->chunk->count - loopStart + 2; // +2 for short, +3 for long
    
    if (offset <= 255) {
        emitByte(compiler, OP_LOOP_SHORT);
        emitByte(compiler, (uint8_t)offset);
    } else {
        emitByte(compiler, OP_LOOP);
        emitByte(compiler, (offset >> 8) & 0xFF);
        emitByte(compiler, offset & 0xFF);
    }
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





static void enterLoop(Compiler* compiler, int continueTarget, const char* label) {
    if (compiler->loopDepth >= 16) return;
    LoopContext* loop = &compiler->loopStack[compiler->loopDepth];
    loop->continueTarget = continueTarget;
    loop->breakJumps = jumptable_new();
    loop->continueJumps = jumptable_new();
    loop->scopeDepth = compiler->scopeDepth;
    loop->label = label;
    compiler->loopDepth++;
}

static void exitLoop(Compiler* compiler) {
    if (compiler->loopDepth <= 0) return;
    compiler->loopDepth--;
    LoopContext* loop = &compiler->loopStack[compiler->loopDepth];
    
    // Patch all break jumps to point to current position
    for (int i = 0; i < loop->breakJumps.offsets.count; i++) {
        patchJump(compiler, loop->breakJumps.offsets.data[i]);
    }
    jumptable_free(&loop->breakJumps);
    jumptable_free(&loop->continueJumps);
    loop->label = NULL;
}

static void patchContinueJumps(Compiler* compiler, LoopContext* loop, int target) {
    if (!loop) return;

    // Continue jumps are emitted using OP_JUMP_SHORT with a single-byte
    // placeholder. Patch each jump to point to the given target.
    for (int i = 0; i < loop->continueJumps.offsets.count; i++) {
        int offset = loop->continueJumps.offsets.data[i];
        int jump = target - offset - 1; // placeholder is 1 byte
        // Current implementation assumes short jumps are sufficient.
        if (jump < 0 || jump > 255) {
            // Fallback: if jump is out of range, leave as is to avoid corrupting
            // bytecode. A future improvement could insert long jumps here.
            compiler->chunk->code[offset] = 0; // safest no-op jump
        } else {
            compiler->chunk->code[offset] = (uint8_t)jump;
        }
    }
}

static LoopContext* getCurrentLoop(Compiler* compiler) {
    if (compiler->loopDepth <= 0) return NULL;
    return &compiler->loopStack[compiler->loopDepth - 1];
}

static LoopContext* getLoopByLabel(Compiler* compiler, const char* label) {
    if (!label) return getCurrentLoop(compiler);
    for (int i = compiler->loopDepth - 1; i >= 0; i--) {
        LoopContext* loop = &compiler->loopStack[i];
        if (loop->label && strcmp(loop->label, label) == 0) {
            return loop;
        }
    }
    return NULL;
}

static ValueType inferBinaryOpTypeWithCompiler(ASTNode* left, ASTNode* right, Compiler* compiler);

// Helper function to get the value type from an AST node
static ValueType getNodeValueType(ASTNode* node) {
    if (node->type == NODE_LITERAL) {
        return node->literal.value.type;
    }
    // Default to i32 for now (can be extended for type inference)
    return VAL_I32;
}

// Helper function to get the value type from an AST node with compiler context
static ValueType getNodeValueTypeWithCompiler(ASTNode* node, Compiler* compiler) {
    if (node->type == NODE_LITERAL) {
        return node->literal.value.type;
    } else if (node->type == NODE_IDENTIFIER) {
        // Look up variable type in locals
        const char* name = node->identifier.name;
        for (int i = compiler->localCount - 1; i >= 0; i--) {
            if (compiler->locals[i].isActive && strcmp(compiler->locals[i].name, name) == 0) {
                return compiler->locals[i].type;
            }
        }
        // If not found in locals, default to i32
        return VAL_I32;
    } else if (node->type == NODE_BINARY) {
        return inferBinaryOpTypeWithCompiler(node->binary.left, node->binary.right, compiler);
    }
    return VAL_I32;
}

// Helper function to determine operation type from operands
static ValueType inferBinaryOpTypeWithCompiler(ASTNode* left, ASTNode* right, Compiler* compiler) {
    ValueType leftType = getNodeValueTypeWithCompiler(left, compiler);
    ValueType rightType = getNodeValueTypeWithCompiler(right, compiler);

    // If both operands are the same type, result is that type
    if (leftType == rightType) {
        return leftType;
    }

    // Type promotion rules for mixed types:
    
    // f64 has highest priority - any f64 operand makes result f64
    if (leftType == VAL_F64 || rightType == VAL_F64) {
        return VAL_F64;
    }

    // u64 and i64 - both are 64-bit, but we need exact type matches for arithmetic
    // If one is u64 and other is i64, this is a type error, but we'll return i64 for now
    if (leftType == VAL_U64 || rightType == VAL_U64) {
        if (leftType == VAL_I64 || rightType == VAL_I64) {
            return VAL_I64;  // Mixed 64-bit defaults to signed
        }
        return VAL_U64;
    }

    if (leftType == VAL_I64 || rightType == VAL_I64) {
        return VAL_I64;
    }

    // u32 and i32 - similar rules
    if (leftType == VAL_U32 || rightType == VAL_U32) {
        if (leftType == VAL_I32 || rightType == VAL_I32) {
            return VAL_I32;  // Mixed 32-bit defaults to signed
        }
        return VAL_U32;
    }

    // Default to i32
    return VAL_I32;
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

            // Constant folding for u32 operations
            if (node->binary.left->type == NODE_LITERAL &&
                node->binary.right->type == NODE_LITERAL &&
                IS_U32(node->binary.left->literal.value) &&
                IS_U32(node->binary.right->literal.value)) {
                uint32_t a = AS_U32(node->binary.left->literal.value);
                uint32_t b = AS_U32(node->binary.right->literal.value);
                uint32_t result = 0;
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
                } else {
                    return -1;
                }
                uint8_t reg = allocateRegister(compiler);
                if (strcmp(op, "+") == 0 || strcmp(op, "-") == 0 || strcmp(op, "*") == 0 ||
                    strcmp(op, "/") == 0 || strcmp(op, "%") == 0) {
                    emitConstant(compiler, reg, U32_VAL(result));
                } else {
                    emitConstant(compiler, reg, BOOL_VAL(boolResult));
                }
                return reg;
            }

            // Constant folding for i64 operations
            if (node->binary.left->type == NODE_LITERAL &&
                node->binary.right->type == NODE_LITERAL &&
                IS_I64(node->binary.left->literal.value) &&
                IS_I64(node->binary.right->literal.value)) {
                int64_t a = AS_I64(node->binary.left->literal.value);
                int64_t b = AS_I64(node->binary.right->literal.value);
                int64_t result = 0;
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
                } else {
                    return -1;
                }
                uint8_t reg = allocateRegister(compiler);
                if (strcmp(op, "+") == 0 || strcmp(op, "-") == 0 || strcmp(op, "*") == 0 ||
                    strcmp(op, "/") == 0 || strcmp(op, "%") == 0) {
                    emitConstant(compiler, reg, I64_VAL(result));
                } else {
                    emitConstant(compiler, reg, BOOL_VAL(boolResult));
                }
                return reg;
            }

            // Constant folding for f64 operations
            if (node->binary.left->type == NODE_LITERAL &&
                node->binary.right->type == NODE_LITERAL &&
                IS_F64(node->binary.left->literal.value) &&
                IS_F64(node->binary.right->literal.value)) {
                double a = AS_F64(node->binary.left->literal.value);
                double b = AS_F64(node->binary.right->literal.value);
                double result = 0.0;
                bool boolResult = false;
                const char* op = node->binary.op;
                if (strcmp(op, "+") == 0) {
                    result = a + b;
                } else if (strcmp(op, "-") == 0) {
                    result = a - b;
                } else if (strcmp(op, "*") == 0) {
                    result = a * b;
                } else if (strcmp(op, "/") == 0) {
                    if (b == 0.0) return -1;
                    result = a / b;
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
                } else {
                    return -1;
                }
                uint8_t reg = allocateRegister(compiler);
                if (strcmp(op, "+") == 0 || strcmp(op, "-") == 0 || strcmp(op, "*") == 0 ||
                    strcmp(op, "/") == 0) {
                    emitConstant(compiler, reg, F64_VAL(result));
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
            
            // Determine operation type and handle type conversion
            ValueType leftType = getNodeValueTypeWithCompiler(node->binary.left, compiler);
            ValueType rightType = getNodeValueTypeWithCompiler(node->binary.right, compiler);
            ValueType opType = inferBinaryOpTypeWithCompiler(node->binary.left, node->binary.right, compiler);
            
            // Handle type conversion for mixed operations
            if (opType == VAL_F64) {
                if (leftType == VAL_I32) {
                    uint8_t convertReg = allocateRegister(compiler);
                    emitByte(compiler, OP_I32_TO_F64_R);
                    emitByte(compiler, convertReg);
                    emitByte(compiler, (uint8_t)leftReg);
                    freeRegister(compiler, (uint8_t)leftReg);
                    leftReg = convertReg;
                } else if (leftType == VAL_I64) {
                    uint8_t convertReg = allocateRegister(compiler);
                    emitByte(compiler, OP_I64_TO_F64_R);
                    emitByte(compiler, convertReg);
                    emitByte(compiler, (uint8_t)leftReg);
                    freeRegister(compiler, (uint8_t)leftReg);
                    leftReg = convertReg;
                }
                if (rightType == VAL_I32) {
                    uint8_t convertReg = allocateRegister(compiler);
                    emitByte(compiler, OP_I32_TO_F64_R);
                    emitByte(compiler, convertReg);
                    emitByte(compiler, (uint8_t)rightReg);
                    freeRegister(compiler, (uint8_t)rightReg);
                    rightReg = convertReg;
                } else if (rightType == VAL_I64) {
                    uint8_t convertReg = allocateRegister(compiler);
                    emitByte(compiler, OP_I64_TO_F64_R);
                    emitByte(compiler, convertReg);
                    emitByte(compiler, (uint8_t)rightReg);
                    freeRegister(compiler, (uint8_t)rightReg);
                    rightReg = convertReg;
                }
            } else if (opType == VAL_I64) {
                if (leftType == VAL_I32) {
                    // Convert left operand from i32 to i64
                    uint8_t convertReg = allocateRegister(compiler);
                    emitByte(compiler, OP_I32_TO_I64_R);
                    emitByte(compiler, convertReg);
                    emitByte(compiler, (uint8_t)leftReg);
                    freeRegister(compiler, (uint8_t)leftReg);
                    leftReg = convertReg;
                }
                if (rightType == VAL_I32) {
                    // Convert right operand from i32 to i64
                    uint8_t convertReg = allocateRegister(compiler);
                    emitByte(compiler, OP_I32_TO_I64_R);
                    emitByte(compiler, convertReg);
                    emitByte(compiler, (uint8_t)rightReg);
                    freeRegister(compiler, (uint8_t)rightReg);
                    rightReg = convertReg;
                }
            }
            
            const char* op = node->binary.op;
            if (strcmp(op, "+") == 0) {
                if ((node->binary.left->type == NODE_LITERAL && IS_STRING(node->binary.left->literal.value)) ||
                    (node->binary.right->type == NODE_LITERAL && IS_STRING(node->binary.right->literal.value))) {
                    emitByte(compiler, OP_CONCAT_R);
                } else if (opType == VAL_I64) {
                    emitByte(compiler, OP_ADD_I64_R);
                } else if (opType == VAL_F64) {
                    emitByte(compiler, OP_ADD_F64_R);
                } else if (opType == VAL_U32) {
                    emitByte(compiler, OP_ADD_U32_R);
                } else if (opType == VAL_U64) {
                    emitByte(compiler, OP_ADD_U64_R);
                } else {
                    emitByte(compiler, OP_ADD_I32_R);
                }
            } else if (strcmp(op, "-") == 0) {
                if (opType == VAL_I64) {
                    emitByte(compiler, OP_SUB_I64_R);
                } else if (opType == VAL_F64) {
                    emitByte(compiler, OP_SUB_F64_R);
                } else if (opType == VAL_U32) {
                    emitByte(compiler, OP_SUB_U32_R);
                } else if (opType == VAL_U64) {
                    emitByte(compiler, OP_SUB_U64_R);
                } else {
                    emitByte(compiler, OP_SUB_I32_R);
                }
            } else if (strcmp(op, "*") == 0) {
                if (opType == VAL_I64) {
                    emitByte(compiler, OP_MUL_I64_R);
                } else if (opType == VAL_F64) {
                    emitByte(compiler, OP_MUL_F64_R);
                } else if (opType == VAL_U32) {
                    emitByte(compiler, OP_MUL_U32_R);
                } else if (opType == VAL_U64) {
                    emitByte(compiler, OP_MUL_U64_R);
                } else {
                    emitByte(compiler, OP_MUL_I32_R);
                }
            } else if (strcmp(op, "/") == 0) {
                if (opType == VAL_I64) {
                    emitByte(compiler, OP_DIV_I64_R);
                } else if (opType == VAL_F64) {
                    emitByte(compiler, OP_DIV_F64_R);
                } else if (opType == VAL_U32) {
                    emitByte(compiler, OP_DIV_U32_R);
                } else if (opType == VAL_U64) {
                    emitByte(compiler, OP_DIV_U64_R);
                } else {
                    emitByte(compiler, OP_DIV_I32_R);
                }
            } else if (strcmp(op, "%") == 0) {
                if (opType == VAL_I64) {
                    emitByte(compiler, OP_MOD_I64_R);
                } else if (opType == VAL_U32) {
                    emitByte(compiler, OP_MOD_U32_R);
                } else if (opType == VAL_U64) {
                    emitByte(compiler, OP_MOD_U64_R);
                } else {
                    emitByte(compiler, OP_MOD_I32_R);
                }
            } else if (strcmp(op, "==") == 0) {
                emitByte(compiler, OP_EQ_R);
            } else if (strcmp(op, "!=") == 0) {
                emitByte(compiler, OP_NE_R);
            } else if (strcmp(op, "<") == 0) {
                if (opType == VAL_I64) {
                    emitByte(compiler, OP_LT_I64_R);
                } else if (opType == VAL_F64) {
                    emitByte(compiler, OP_LT_F64_R);
                } else if (opType == VAL_U32) {
                    emitByte(compiler, OP_LT_U32_R);
                } else if (opType == VAL_U64) {
                    emitByte(compiler, OP_LT_U64_R);
                } else {
                    emitByte(compiler, OP_LT_I32_R);
                }
            } else if (strcmp(op, ">") == 0) {
                if (opType == VAL_I64) {
                    emitByte(compiler, OP_GT_I64_R);
                } else if (opType == VAL_F64) {
                    emitByte(compiler, OP_GT_F64_R);
                } else if (opType == VAL_U32) {
                    emitByte(compiler, OP_GT_U32_R);
                } else if (opType == VAL_U64) {
                    emitByte(compiler, OP_GT_U64_R);
                } else {
                    emitByte(compiler, OP_GT_I32_R);
                }
            } else if (strcmp(op, "<=") == 0) {
                if (opType == VAL_I64) {
                    emitByte(compiler, OP_LE_I64_R);
                } else if (opType == VAL_F64) {
                    emitByte(compiler, OP_LE_F64_R);
                } else if (opType == VAL_U32) {
                    emitByte(compiler, OP_LE_U32_R);
                } else if (opType == VAL_U64) {
                    emitByte(compiler, OP_LE_U64_R);
                } else {
                    emitByte(compiler, OP_LE_I32_R);
                }
            } else if (strcmp(op, ">=") == 0) {
                if (opType == VAL_I64) {
                    emitByte(compiler, OP_GE_I64_R);
                } else if (opType == VAL_F64) {
                    emitByte(compiler, OP_GE_F64_R);
                } else if (opType == VAL_U32) {
                    emitByte(compiler, OP_GE_U32_R);
                } else if (opType == VAL_U64) {
                    emitByte(compiler, OP_GE_U64_R);
                } else {
                    emitByte(compiler, OP_GE_I32_R);
                }
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
            // Determine variable type from type annotation FIRST (before compiling expression)
            ValueType declaredType;
            bool hasTypeAnnotation = node->varDecl.typeAnnotation && node->varDecl.typeAnnotation->typeAnnotation.name;
            
            if (hasTypeAnnotation) {
                const char* typeName = node->varDecl.typeAnnotation->typeAnnotation.name;
                if (strcmp(typeName, "i64") == 0) {
                    declaredType = VAL_I64;
                } else if (strcmp(typeName, "i32") == 0) {
                    declaredType = VAL_I32;
                } else if (strcmp(typeName, "u32") == 0) {
                    declaredType = VAL_U32;
                } else if (strcmp(typeName, "u64") == 0) {
                    declaredType = VAL_U64;
                } else if (strcmp(typeName, "f64") == 0) {
                    declaredType = VAL_F64;
                } else if (strcmp(typeName, "bool") == 0) {
                    declaredType = VAL_BOOL;
                } else {
                    declaredType = VAL_I32; // default
                }
                
                // CRITICAL FIX: Coerce literal types BEFORE compiling the expression
                // This ensures the literal is compiled with the correct type
                if (node->varDecl.initializer && node->varDecl.initializer->type == NODE_LITERAL) {
                    ASTNode* literal = node->varDecl.initializer;
                    ValueType literalType = literal->literal.value.type;
                    
                    // Check if this is a plain integer literal that can be coerced
                    if ((literalType == VAL_I32 || literalType == VAL_I64) && 
                        (declaredType == VAL_U32 || declaredType == VAL_U64 || declaredType == VAL_I32 || declaredType == VAL_I64)) {
                        
                        // Get the integer value and coerce it to the declared type
                        int64_t intValue;
                        if (literalType == VAL_I32) {
                            intValue = AS_I32(literal->literal.value);
                        } else {
                            intValue = AS_I64(literal->literal.value);
                        }
                        
                        // Coerce to declared type if value is in range
                        if (declaredType == VAL_U32) {
                            if (intValue >= 0 && intValue <= UINT32_MAX) {
                                literal->literal.value = U32_VAL((uint32_t)intValue);
                            }
                        } else if (declaredType == VAL_U64) {
                            if (intValue >= 0) {
                                literal->literal.value = U64_VAL((uint64_t)intValue);
                            }
                        } else if (declaredType == VAL_I64) {
                            literal->literal.value = I64_VAL(intValue);
                        }
                        // VAL_I32 doesn't need coercion from VAL_I32
                    }
                }
            } else {
                // Infer type from initializer using enhanced type inference
                declaredType = getNodeValueTypeWithCompiler(node->varDecl.initializer, compiler);
            }
            
            // NOW compile the expression with the correctly typed literal
            int initReg = compileExpressionToRegister(node->varDecl.initializer, compiler);
            if (initReg < 0) return -1;
            
            if (compiler->localCount >= REGISTER_COUNT) {
                return -1;
            }
            int localIndex = compiler->localCount++;
            compiler->locals[localIndex].name = node->varDecl.name;
            compiler->locals[localIndex].isActive = true;
            compiler->locals[localIndex].depth = compiler->scopeDepth;
            compiler->locals[localIndex].isMutable = node->varDecl.isMutable;
            compiler->locals[localIndex].type = declaredType;
            
            // Handle type conversion if needed
            // For complex expressions, we need to determine the actual result type
            ValueType initType;
            if (node->varDecl.initializer->type == NODE_LITERAL) {
                initType = node->varDecl.initializer->literal.value.type;
            } else if (node->varDecl.initializer->type == NODE_BINARY) {
                // For binary expressions, use the inferred operation type
                initType = inferBinaryOpTypeWithCompiler(
                    node->varDecl.initializer->binary.left,
                    node->varDecl.initializer->binary.right,
                    compiler);
            } else {
                // For other expressions, default to i32 (can be improved)
                initType = VAL_I32;
            }
            
            if (declaredType != initType) {
                if (declaredType == VAL_I64 && initType == VAL_I32) {
                    uint8_t convertReg = allocateRegister(compiler);
                    emitByte(compiler, OP_I32_TO_I64_R);
                    emitByte(compiler, convertReg);
                    emitByte(compiler, (uint8_t)initReg);
                    freeRegister(compiler, (uint8_t)initReg);
                    initReg = convertReg;
                } else if (declaredType == VAL_F64 && initType == VAL_I32) {
                    uint8_t convertReg = allocateRegister(compiler);
                    emitByte(compiler, OP_I32_TO_F64_R);
                    emitByte(compiler, convertReg);
                    emitByte(compiler, (uint8_t)initReg);
                    freeRegister(compiler, (uint8_t)initReg);
                    initReg = convertReg;
                } else if (declaredType == VAL_F64 && initType == VAL_I64) {
                    uint8_t convertReg = allocateRegister(compiler);
                    emitByte(compiler, OP_I64_TO_F64_R);
                    emitByte(compiler, convertReg);
                    emitByte(compiler, (uint8_t)initReg);
                    freeRegister(compiler, (uint8_t)initReg);
                    initReg = convertReg;
                } else if (declaredType == VAL_U32 && initType == VAL_I32) {
                    uint8_t convertReg = allocateRegister(compiler);
                    emitByte(compiler, OP_I32_TO_U32_R);
                    emitByte(compiler, convertReg);
                    emitByte(compiler, (uint8_t)initReg);
                    freeRegister(compiler, (uint8_t)initReg);
                    initReg = convertReg;
                }
            }
            
            compiler->locals[localIndex].reg = (uint8_t)initReg;
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
            int elseJump = emitConditionalJump(compiler, (uint8_t)cond);
            freeRegister(compiler, (uint8_t)cond);

            enterScope(compiler);
            if (compileExpressionToRegister(node->ifStmt.thenBranch, compiler) < 0) {
                exitScope(compiler);
                return -1;
            }
            exitScope(compiler);

            int endJump = -1;
            if (node->ifStmt.elseBranch) {
                endJump = emitJump(compiler, OP_JUMP_SHORT);
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
        case NODE_WHILE: {
            int loopStart = compiler->chunk->count;
            enterLoop(compiler, loopStart, node->whileStmt.label);
            
            int condReg = compileExpressionToRegister(node->whileStmt.condition, compiler);
            if (condReg < 0) {
                exitLoop(compiler);
                return -1;
            }
            int exitJump = emitConditionalJump(compiler, (uint8_t)condReg);
            freeRegister(compiler, (uint8_t)condReg);

            enterScope(compiler);
            if (compileExpressionToRegister(node->whileStmt.body, compiler) < 0) {
                exitScope(compiler);
                exitLoop(compiler);
                return -1;
            }
            exitScope(compiler);

            // Patch any `continue` statements inside the loop body to jump here
            patchContinueJumps(compiler, getCurrentLoop(compiler), compiler->chunk->count);

            emitLoop(compiler, loopStart);

            patchJump(compiler, exitJump);
            exitLoop(compiler);
            return 0;
        }
        case NODE_FOR_RANGE: {
            int startReg = compileExpressionToRegister(node->forRange.start, compiler);
            if (startReg < 0) return -1;
            int endReg = compileExpressionToRegister(node->forRange.end, compiler);
            if (endReg < 0) { freeRegister(compiler, (uint8_t)startReg); return -1; }

            int stepReg = -1;
            bool stepConstOne = true;
            if (node->forRange.step) {
                stepReg = compileExpressionToRegister(node->forRange.step, compiler);
                if (stepReg < 0) { freeRegister(compiler, (uint8_t)startReg); freeRegister(compiler, (uint8_t)endReg); return -1; }
                stepConstOne = false;
            }

            enterScope(compiler);

            uint8_t loopVar = allocateRegister(compiler);
            if (compiler->localCount >= REGISTER_COUNT) {
                freeRegister(compiler, (uint8_t)startReg);
                freeRegister(compiler, (uint8_t)endReg);
                if (stepReg >= 0) freeRegister(compiler, (uint8_t)stepReg);
                exitScope(compiler);
                return -1;
            }
            int localIndex = compiler->localCount++;
            compiler->locals[localIndex].name = node->forRange.varName;
            compiler->locals[localIndex].reg = loopVar;
            compiler->locals[localIndex].isActive = true;
            compiler->locals[localIndex].depth = compiler->scopeDepth;
            compiler->locals[localIndex].isMutable = true;
            compiler->locals[localIndex].type = VAL_I32; // for range loops use i32

            emitByte(compiler, OP_MOVE);
            emitByte(compiler, loopVar);
            emitByte(compiler, (uint8_t)startReg);

            int loopStart = compiler->chunk->count;

            uint8_t condReg = allocateRegister(compiler);
            emitByte(compiler, node->forRange.inclusive ? OP_LE_I32_R : OP_LT_I32_R);
            emitByte(compiler, condReg);
            emitByte(compiler, loopVar);
            emitByte(compiler, (uint8_t)endReg);

            int exitJump = emitConditionalJump(compiler, condReg);
            freeRegister(compiler, condReg);

            enterLoop(compiler, loopStart, node->forRange.label);

            if (compileExpressionToRegister(node->forRange.body, compiler) < 0) {
                exitLoop(compiler);
                exitScope(compiler);
                return -1;
            }

            // Patch continue jumps to point to increment section
            patchContinueJumps(compiler, getCurrentLoop(compiler), compiler->chunk->count);

            if (stepConstOne) {
                emitByte(compiler, OP_INC_I32_R);
                emitByte(compiler, loopVar);
            } else {
                emitByte(compiler, OP_ADD_I32_R);
                emitByte(compiler, loopVar);
                emitByte(compiler, loopVar);
                emitByte(compiler, (uint8_t)stepReg);
                freeRegister(compiler, (uint8_t)stepReg);
            }

            emitLoop(compiler, loopStart);

            patchJump(compiler, exitJump);
            exitLoop(compiler);

            exitScope(compiler);

            freeRegister(compiler, (uint8_t)startReg);
            freeRegister(compiler, (uint8_t)endReg);
            freeRegister(compiler, loopVar);

            return 0;
        }
        case NODE_FOR_ITER: {
            int iterSrc = compileExpressionToRegister(node->forIter.iterable, compiler);
            if (iterSrc < 0) return -1;

            enterScope(compiler);

            uint8_t iterator = allocateRegister(compiler);
            emitByte(compiler, OP_GET_ITER_R);
            emitByte(compiler, iterator);
            emitByte(compiler, (uint8_t)iterSrc);

            uint8_t loopVar = allocateRegister(compiler);
            int localIndex = compiler->localCount++;
            compiler->locals[localIndex].name = node->forIter.varName;
            compiler->locals[localIndex].reg = loopVar;
            compiler->locals[localIndex].isActive = true;
            compiler->locals[localIndex].depth = compiler->scopeDepth;
            compiler->locals[localIndex].isMutable = true;
            compiler->locals[localIndex].type = VAL_I64; // iterator values are i64

            int loopStart = compiler->chunk->count;
            enterLoop(compiler, loopStart, node->forIter.label);

            uint8_t hasReg = allocateRegister(compiler);
            emitByte(compiler, OP_ITER_NEXT_R);
            emitByte(compiler, loopVar);
            emitByte(compiler, iterator);
            emitByte(compiler, hasReg);

            int exitJump = emitConditionalJump(compiler, hasReg);
            freeRegister(compiler, hasReg);

            if (compileExpressionToRegister(node->forIter.body, compiler) < 0) {
                exitLoop(compiler);
                exitScope(compiler);
                return -1;
            }

            // Patch `continue` statements inside the loop body
            patchContinueJumps(compiler, getCurrentLoop(compiler), compiler->chunk->count);

            emitLoop(compiler, loopStart);

            patchJump(compiler, exitJump);
            exitLoop(compiler);

            exitScope(compiler);

            freeRegister(compiler, (uint8_t)iterSrc);
            freeRegister(compiler, iterator);
            freeRegister(compiler, loopVar);

            return 0;
        }
        case NODE_BREAK: {
            LoopContext* currentLoop = getLoopByLabel(compiler, node->breakStmt.label);
            if (!currentLoop) {
                // Break statement outside of loop - compile-time error
                compiler->hadError = true;
                return -1;
            }
            
            // Add this break jump to the list to patch later
            jumptable_add(&currentLoop->breakJumps, emitJump(compiler, OP_JUMP_SHORT));
            return 0;
        }
        case NODE_CONTINUE: {
            LoopContext* currentLoop = getLoopByLabel(compiler, node->continueStmt.label);
            if (!currentLoop) {
                // Continue statement outside of loop - compile-time error
                compiler->hadError = true;
                return -1;
            }
            
            // Add this continue jump to the list to patch later
            jumptable_add(&currentLoop->continueJumps, emitJump(compiler, OP_JUMP_SHORT));
            return 0;
        }
        case NODE_TERNARY: {
            int cond = compileExpressionToRegister(node->ternary.condition, compiler);
            if (cond < 0) return -1;
            uint8_t resultReg = allocateRegister(compiler);
            int falseJump = emitConditionalJump(compiler, (uint8_t)cond);

            int trueReg = compileExpressionToRegister(node->ternary.trueExpr, compiler);
            if (trueReg < 0) return -1;
            emitByte(compiler, OP_MOVE);
            emitByte(compiler, resultReg);
            emitByte(compiler, (uint8_t)trueReg);
            freeRegister(compiler, (uint8_t)trueReg);
            int endJump = emitJump(compiler, OP_JUMP_SHORT);
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
    compiler->loopDepth = 0;
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
                stmt->type != NODE_IF && stmt->type != NODE_WHILE && stmt->type != NODE_FOR_RANGE && stmt->type != NODE_FOR_ITER && stmt->type != NODE_BLOCK &&
                stmt->type != NODE_ASSIGN) {
                emitByte(compiler, OP_PRINT_R);
                emitByte(compiler, (uint8_t)reg);
            }
        }
        return true;
    }

    int resultReg = compileExpressionToRegister(ast, compiler);

    if (resultReg >= 0 && !isModule && ast->type != NODE_VAR_DECL && ast->type != NODE_PRINT &&
        ast->type != NODE_IF && ast->type != NODE_WHILE && ast->type != NODE_FOR_RANGE && ast->type != NODE_FOR_ITER && ast->type != NODE_BLOCK) {
        emitByte(compiler, OP_PRINT_R);
        emitByte(compiler, (uint8_t)resultReg);
    }

    return resultReg >= 0;
}
