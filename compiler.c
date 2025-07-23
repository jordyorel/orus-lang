#include "../../include/compiler/compiler.h"

#include <stdlib.h>
#include <string.h>

#include "../../include/public/common.h"
#include "../../include/runtime/jumptable.h"
#include "../../include/compiler/lexer.h"
#include "../../include/tools/scope_analysis.h"
#include "../../include/compiler/symbol_table.h"
#include "../../include/compiler/compiler.h"
#include "../../include/vm/vm.h"

// Forward declarations
static void compile_typed_expr(Compiler* compiler, ASTNode* node,
                               TypedExpDesc* desc);
static void compile_typed_binary_enhanced(Compiler* compiler, ASTNode* node,
                                          TypedExpDesc* desc,
                                          ValueType inferredType);
static void compile_typed_binary(Compiler* compiler, ASTNode* node,
                                 TypedExpDesc* desc);
static void compile_typed_unary(Compiler* compiler, ASTNode* node,
                                TypedExpDesc* desc);
static void compile_typed_statement_expr(Compiler* compiler, ASTNode* node,
                                         TypedExpDesc* result);
static ValueType inferBinaryOpTypeWithCompiler(ASTNode* left, ASTNode* right,
                                               Compiler* compiler);
static ValueType getNodeValueTypeWithCompiler(ASTNode* node,
                                              Compiler* compiler);
static bool is_arithmetic_op(const char* op);
static bool try_constant_folding(Compiler* compiler, ASTNode* node,
                                 TypedExpDesc* left, TypedExpDesc* right,
                                 TypedExpDesc* result);

// Type cache for inference optimization
typedef struct {
    ASTNode* node;
    Type* type;
} TypeCacheEntry;

// Simple hash table implementation placeholder
static HashMap* hash_table_new(void) {
    return NULL; // Placeholder - simplified for integration
}

static void hash_table_free(HashMap* table) {
    // Placeholder - simplified for integration
    (void)table;
}

// Forward declaration for optimizer function
void optimizeRegisterPressure(Compiler* compiler);

// Forward declaration for legacy compiler function
static int compileExpressionToRegister_old(ASTNode* node, Compiler* compiler);

// Simple findConstant implementation
static int findConstant(Chunk* chunk, Value value) {
    // For now, just return -1 to force adding new constants
    // This is a simplified implementation for integration
    (void)chunk; (void)value;
    return -1;
}

// Missing function implementations for integration
void initCompiler(Compiler* compiler, Chunk* chunk, const char* fileName, const char* source) {
    // Basic initialization - simplified for integration
    compiler->chunk = chunk;
    compiler->fileName = fileName;
    compiler->source = source;
    compiler->scopeDepth = 0;
    compiler->localCount = 0;
    compiler->hadError = false;
    compiler->typeCache = NULL;
}

void freeCompiler(Compiler* compiler) {
    // Basic cleanup - simplified for integration
    (void)compiler;
}

void initCompilerTypeInference(Compiler* compiler) {
    // Type inference initialization - placeholder
    (void)compiler;
}

void freeCompilerTypeInference(Compiler* compiler) {
    // Type inference cleanup - placeholder
    (void)compiler;
}

Type* inferExpressionType(Compiler* compiler, ASTNode* expr) {
    // Basic type inference - placeholder
    (void)compiler; (void)expr;
    return NULL;
}

ValueType typeKindToValueType(TypeKind kind) {
    // Type conversion - simplified mapping
    switch (kind) {
        case TYPE_I32: return VAL_I32;
        case TYPE_I64: return VAL_I64;
        case TYPE_U32: return VAL_U32;
        case TYPE_U64: return VAL_U64;
        case TYPE_F64: return VAL_F64;
        case TYPE_BOOL: return VAL_BOOL;
        case TYPE_STRING: return VAL_STRING;
        default: return VAL_NIL;
    }
}

void emitTypedBinaryOp(Compiler* compiler, const char* op, ValueType type, uint16_t dst, uint16_t left, uint16_t right) {
    // Emit typed binary operation - simplified implementation
    (void)compiler; (void)op; (void)type; (void)dst; (void)left; (void)right;
}

void endVariableLifetime(Compiler* compiler, int localIndex, int instruction) {
    // End variable lifetime - simplified implementation
    (void)compiler; (void)localIndex; (void)instruction;
}

static void initTypeCache(Compiler* compiler) {
    compiler->typeCache = hash_table_new();
}

static void freeTypeCache(Compiler* compiler) {
    if (compiler->typeCache) {
        hash_table_free(compiler->typeCache);
        compiler->typeCache = NULL;
    }
}

// Initialize register types
static void initRegisterTypes(Compiler* compiler) {
    for (int i = 0; i < REGISTER_COUNT; i++) {
        compiler->registerTypes[i] = VAL_NIL;
    }
}

static void setRegisterType(Compiler* compiler, uint16_t reg, ValueType type) {
    compiler->registerTypes[reg] = type;
}

static ValueType getRegisterType(Compiler* compiler, uint16_t reg) {
    return compiler->registerTypes[reg];
}

static void insertCode(Compiler* compiler, int offset, uint8_t* code,
                       int length) {
    if (compiler->chunk->count + length > compiler->chunk->capacity) {
        compiler->chunk->capacity = compiler->chunk->capacity == 0
                                        ? 1024
                                        : compiler->chunk->capacity * 4;
        compiler->chunk->code =
            realloc(compiler->chunk->code, compiler->chunk->capacity);
        compiler->chunk->lines = realloc(
            compiler->chunk->lines, compiler->chunk->capacity * sizeof(int));
        compiler->chunk->columns = realloc(
            compiler->chunk->columns, compiler->chunk->capacity * sizeof(int));
    }
    memmove(compiler->chunk->code + offset + length,
            compiler->chunk->code + offset, compiler->chunk->count - offset);
    memcpy(compiler->chunk->code + offset, code, length);
    for (int i = offset; i < offset + length; i++) {
        compiler->chunk->lines[i] =
            compiler->chunk->lines[offset > 0 ? offset - 1 : 0];
        compiler->chunk->columns[i] =
            compiler->chunk->columns[offset > 0 ? offset - 1 : 0];
    }
    compiler->chunk->count += length;
}

static int emitJump(Compiler* compiler, uint16_t instruction) {
    uint8_t bytes[3] = {instruction, 0xFF,
                        instruction == OP_JUMP_SHORT ? 0 : 0xFF};
    int length = instruction == OP_JUMP_SHORT ? 2 : 3;
    insertCode(compiler, compiler->chunk->count, bytes, length);
    int jumpOffset =
        compiler->chunk->count - (instruction == OP_JUMP_SHORT ? 1 : 2);
    jumptable_add(&compiler->pendingJumps, jumpOffset);
    return jumpOffset;
}

static void updateJumpOffsets(Compiler* compiler, int insertPoint,
                              int bytesInserted) {
    JumpTable* jumps = &compiler->pendingJumps;
    for (int i = 0; i < jumps->offsets.count; i++) {
        if (jumps->offsets.data[i] > insertPoint) {
            jumps->offsets.data[i] += bytesInserted;
        }
    }
    for (int loop = 0; loop < compiler->loopDepth; loop++) {
        for (int j = 0; j < compiler->loopStack[loop].breakJumps.offsets.count;
             j++) {
            if (compiler->loopStack[loop].breakJumps.offsets.data[j] >
                insertPoint) {
                compiler->loopStack[loop].breakJumps.offsets.data[j] +=
                    bytesInserted;
            }
        }
        for (int j = 0;
             j < compiler->loopStack[loop].continueJumps.offsets.count; j++) {
            if (compiler->loopStack[loop].continueJumps.offsets.data[j] >
                insertPoint) {
                compiler->loopStack[loop].continueJumps.offsets.data[j] +=
                    bytesInserted;
            }
        }
    }
}

static void removePendingJump(Compiler* compiler, int offset) {
    JumpTable* jumps = &compiler->pendingJumps;
    for (int i = 0; i < jumps->offsets.count; i++) {
        if (jumps->offsets.data[i] == offset) {
            jumps->offsets.data[i] =
                jumps->offsets.data[jumps->offsets.count - 1];
            jumps->offsets.count--;
            break;
        }
    }
}

static void patchJump(Compiler* compiler, int offset) {
    int jump = compiler->chunk->count - offset - 1;
    if (jump > 255) {
        uint8_t originalOpcode = compiler->chunk->code[offset - 1];
        uint8_t longOpcode =
            originalOpcode == OP_JUMP_SHORT ? OP_JUMP : OP_JUMP_IF_NOT_R;
        compiler->chunk->code[offset - 1] = longOpcode;
        insertCode(compiler, offset, (uint8_t[]){0}, 1);
        updateJumpOffsets(compiler, offset, 1);
        jump = compiler->chunk->count - offset - 2;
        compiler->chunk->code[offset] = (jump >> 8) & 0xFF;
        compiler->chunk->code[offset + 1] = jump & 0xFF;
    } else {
        compiler->chunk->code[offset] = (uint8_t)jump;
    }
    removePendingJump(compiler, offset);
}

static void patchAllPendingJumps(Compiler* compiler) {
    while (compiler->pendingJumps.offsets.count > 0) {
        patchJump(compiler, compiler->pendingJumps.offsets.data[0]);
    }
}

static int emitConditionalJump(Compiler* compiler, uint16_t reg) {
    uint8_t bytes[3] = {OP_JUMP_IF_NOT_SHORT, reg & 0xFF, 0xFF};
    insertCode(compiler, compiler->chunk->count, bytes, 3);
    int jumpOffset = compiler->chunk->count - 1;
    jumptable_add(&compiler->pendingJumps, jumpOffset);
    return jumpOffset;
}

static void emitLoop(Compiler* compiler, int loopStart) {
    int offset = compiler->chunk->count - loopStart + 2;
    if (offset <= 255) {
        uint8_t bytes[2] = {OP_LOOP_SHORT, (uint8_t)offset};
        insertCode(compiler, compiler->chunk->count, bytes, 2);
    } else {
        uint8_t bytes[3] = {OP_LOOP, (offset >> 8) & 0xFF, offset & 0xFF};
        insertCode(compiler, compiler->chunk->count, bytes, 3);
    }
}

static void enterScope(Compiler* compiler) {
    compiler->scopeStack[compiler->scopeDepth] = compiler->localCount;
    compiler->scopeDepth++;
    compilerEnterScope(compiler, false);
}

static void enterLoopScope(Compiler* compiler) {
    compiler->scopeStack[compiler->scopeDepth] = compiler->localCount;
    compiler->scopeDepth++;
    compilerEnterScope(compiler, true);
}

static void exitScope(Compiler* compiler) {
    compiler->scopeDepth--;
    int targetCount = compiler->scopeStack[compiler->scopeDepth];
    int currentInstr = compiler->chunk->count;
    compilerExitScope(compiler);

    // Robust cleanup for all locals leaving scope
    while (compiler->localCount > targetCount) {
        int localIndex = compiler->localCount - 1;

        // End variable lifetime and free register
        if (compiler->locals[localIndex].liveRangeIndex >= 0) {
            endVariableLifetime(compiler, localIndex, currentInstr);
        } else if (compiler->locals[localIndex].reg >= 0) {
            freeRegister(compiler, compiler->locals[localIndex].reg);
        }

        // Restore shadowed variable if present
        const char* varName = compiler->locals[localIndex].name;
        int previousLocalIndex = -1;
        for (int i = localIndex - 1; i >= 0; i--) {
            if (compiler->locals[i].isActive &&
                strcmp(compiler->locals[i].name, varName) == 0 &&
                compiler->locals[i].depth < compiler->locals[localIndex].depth) {
                previousLocalIndex = i;
                break;
            }
        }
        if (previousLocalIndex >= 0) {
            symbol_table_set(&compiler->symbols, varName, previousLocalIndex, compiler->locals[previousLocalIndex].depth);
        } else {
            symbol_table_remove(&compiler->symbols, varName);
        }

        // Mark local inactive and clear sensitive fields
        compiler->locals[localIndex].isActive = false;
        // Only free register if it is valid and local is active
        if (compiler->locals[localIndex].reg >= 0) {
            freeRegister(compiler, compiler->locals[localIndex].reg);
        }
        compiler->locals[localIndex].reg = -1;
        compiler->locals[localIndex].liveRangeIndex = -1;
        // Do NOT set name to NULL; keep for debugging and symbol restoration
        compiler->locals[localIndex].type = VAL_NIL;
        compiler->locals[localIndex].isMutable = false;
        compiler->locals[localIndex].depth = -1;

        // Debug assertion: local should not be active after cleanup
        /* assert(!compiler->locals[localIndex].isActive); */

        compiler->localCount--;
    }
    optimizeRegisterPressure(compiler);
}

static void enterLoop(Compiler* compiler, int continueTarget,
                      const char* label) {
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
    for (int i = 0; i < loop->breakJumps.offsets.count; i++) {
        patchJump(compiler, loop->breakJumps.offsets.data[i]);
    }
    jumptable_free(&loop->breakJumps);
    jumptable_free(&loop->continueJumps);
    loop->label = NULL;
}

static void patchContinueJumps(Compiler* compiler, LoopContext* loop,
                               int target) {
    if (!loop) return;
    for (int i = 0; i < loop->continueJumps.offsets.count; i++) {
        int offset = loop->continueJumps.offsets.data[i];
        int jump = target - offset - 1;
        if (jump < 0) {
            int backwardJump = -jump;
            if (backwardJump <= 255) {
                compiler->chunk->code[offset - 1] = OP_JUMP_BACK_SHORT;
                compiler->chunk->code[offset] = (uint8_t)backwardJump;
            } else {
                compiler->chunk->code[offset - 1] = OP_LOOP;
                insertCode(compiler, offset, (uint8_t[]){0}, 1);
                updateJumpOffsets(compiler, offset, 1);
                compiler->chunk->code[offset] = (backwardJump >> 8) & 0xFF;
                compiler->chunk->code[offset + 1] = backwardJump & 0xFF;
            }
        } else if (jump <= 255) {
            compiler->chunk->code[offset] = (uint8_t)jump;
        } else {
            compiler->chunk->code[offset - 1] = OP_JUMP;
            insertCode(compiler, offset, (uint8_t[]){0}, 1);
            updateJumpOffsets(compiler, offset, 1);
            compiler->chunk->code[offset] = (jump >> 8) & 0xFF;
            compiler->chunk->code[offset + 1] = jump & 0xFF;
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

static ValueType getNodeValueTypeWithCompiler(ASTNode* node,
                                              Compiler* compiler) {
    if (node->type == NODE_LITERAL) {
        return node->literal.value.type;
    } else if (node->type == NODE_TIME_STAMP) {
        return VAL_I64;
    } else if (node->type == NODE_IDENTIFIER) {
        int localIndex;
        if (symbol_table_get(&compiler->symbols, node->identifier.name,
                             &localIndex)) {
            return compiler->locals[localIndex].type;
        }
        return VAL_I32;
    } else if (node->type == NODE_UNARY) {
        if (strcmp(node->unary.op, "not") == 0) {
            return VAL_BOOL;
        }
        return getNodeValueTypeWithCompiler(node->unary.operand, compiler);
    } else if (node->type == NODE_BINARY) {
        return inferBinaryOpTypeWithCompiler(node->binary.left,
                                             node->binary.right, compiler);
    }
    return VAL_I32;
}

static ValueType inferBinaryOpTypeWithCompiler(ASTNode* left, ASTNode* right,
                                               Compiler* compiler) {
    ValueType leftType = getNodeValueTypeWithCompiler(left, compiler);
    ValueType rightType = getNodeValueTypeWithCompiler(right, compiler);
    if (leftType == rightType) {
        return leftType;
    }
    if (leftType == VAL_F64 || rightType == VAL_F64) {
        return VAL_F64;
    }
    if (leftType == VAL_U64 || rightType == VAL_U64) {
        if (leftType == VAL_I64 || rightType == VAL_I64) {
            return VAL_I64;
        }
        return VAL_U64;
    }
    if (leftType == VAL_I64 || rightType == VAL_I64) {
        return VAL_I64;
    }
    if (leftType == VAL_U32 || rightType == VAL_U32) {
        if (leftType == VAL_I32 || rightType == VAL_I32) {
            return VAL_I32;
        }
        return VAL_U32;
    }
    return VAL_I32;
}

static void init_typed_exp(TypedExpDesc* e, ExpKind kind, ValueType type,
                           int info) {
    e->kind = kind;
    e->type = type;
    e->isConstant = (kind == EXP_K);
    e->u.s.info = info;
    e->u.s.regType = type;
    e->u.s.isTemporary = false;
    e->t = e->f = NO_JUMP;
}

static void discharge_typed_reg(Compiler* compiler, TypedExpDesc* e, int reg) {
    switch (e->kind) {
        case EXP_K: {
            emitConstant(compiler, reg, e->u.constant.value);
            setRegisterType(compiler, reg, e->type);
            break;
        }
        case EXP_LOCAL: {
            if (e->u.s.info != reg) {
                uint8_t bytes[5] = {OP_MOVE, (uint8_t)(reg & 0xFF), (uint8_t)(reg >> 8),
                                    (uint8_t)(e->u.s.info & 0xFF), (uint8_t)(e->u.s.info >> 8)};
                insertCode(compiler, compiler->chunk->count, bytes, 5);
            }
            setRegisterType(compiler, reg, e->type);
            break;
        }
        case EXP_TRUE:
        case EXP_FALSE: {
            uint8_t bytes[3] = {
                e->kind == EXP_TRUE ? OP_LOAD_TRUE : OP_LOAD_FALSE,
                (uint8_t)(reg & 0xFF), (uint8_t)(reg >> 8)};
            insertCode(compiler, compiler->chunk->count, bytes, 3);
            setRegisterType(compiler, reg, VAL_BOOL);
            break;
        }
        case EXP_NIL: {
            uint8_t bytes[3] = {OP_LOAD_NIL, (uint8_t)(reg & 0xFF), (uint8_t)(reg >> 8)};
            insertCode(compiler, compiler->chunk->count, bytes, 3);
            setRegisterType(compiler, reg, VAL_NIL);
            break;
        }
        default:
            break;
    }
    e->kind = EXP_TEMP;
    e->u.s.info = reg;
    e->u.s.regType = e->type;
}

static bool is_arithmetic_op(const char* op) {
    return strcmp(op, "+") == 0 || strcmp(op, "-") == 0 ||
           strcmp(op, "*") == 0 || strcmp(op, "/") == 0 ||
           strcmp(op, "%") == 0 || strcmp(op, "<") == 0 ||
           strcmp(op, ">") == 0 || strcmp(op, "<=") == 0 ||
           strcmp(op, ">=") == 0 || strcmp(op, "==") == 0 ||
           strcmp(op, "!=") == 0;
}

static bool try_constant_folding(Compiler* compiler, ASTNode* node,
                                 TypedExpDesc* left, TypedExpDesc* right,
                                 TypedExpDesc* result) {
    if (!left->isConstant || !right->isConstant) return false;
    Value leftVal = left->u.constant.value;
    Value rightVal = right->u.constant.value;
    Value resultVal;
    const char* op = node->binary.op;

    if (leftVal.type != rightVal.type) return false;

    switch (leftVal.type) {
        case VAL_I32: {
            int32_t a = leftVal.as.i32, b = rightVal.as.i32;
            if (strcmp(op, "+") == 0) {
                resultVal.as.i32 = a + b;
                resultVal.type = VAL_I32;
            } else if (strcmp(op, "-") == 0) {
                resultVal.as.i32 = a - b;
                resultVal.type = VAL_I32;
            } else if (strcmp(op, "*") == 0) {
                resultVal.as.i32 = a * b;
                resultVal.type = VAL_I32;
            } else if (strcmp(op, "/") == 0 && b != 0) {
                resultVal.as.i32 = a / b;
                resultVal.type = VAL_I32;
            } else if (strcmp(op, "%") == 0 && b != 0) {
                resultVal.as.i32 = a % b;
                resultVal.type = VAL_I32;
            } else if (strcmp(op, "<") == 0) {
                resultVal.as.boolean = a < b;
                resultVal.type = VAL_BOOL;
            } else if (strcmp(op, ">") == 0) {
                resultVal.as.boolean = a > b;
                resultVal.type = VAL_BOOL;
            } else if (strcmp(op, "<=") == 0) {
                resultVal.as.boolean = a <= b;
                resultVal.type = VAL_BOOL;
            } else if (strcmp(op, ">=") == 0) {
                resultVal.as.boolean = a >= b;
                resultVal.type = VAL_BOOL;
            } else if (strcmp(op, "==") == 0) {
                resultVal.as.boolean = a == b;
                resultVal.type = VAL_BOOL;
            } else if (strcmp(op, "!=") == 0) {
                resultVal.as.boolean = a != b;
                resultVal.type = VAL_BOOL;
            } else
                return false;
            break;
        }
        case VAL_F64: {
            double a = leftVal.as.f64, b = rightVal.as.f64;
            if (strcmp(op, "+") == 0) {
                resultVal.as.f64 = a + b;
                resultVal.type = VAL_F64;
            } else if (strcmp(op, "-") == 0) {
                resultVal.as.f64 = a - b;
                resultVal.type = VAL_F64;
            } else if (strcmp(op, "*") == 0) {
                resultVal.as.f64 = a * b;
                resultVal.type = VAL_F64;
            } else if (strcmp(op, "/") == 0 && b != 0.0) {
                resultVal.as.f64 = a / b;
                resultVal.type = VAL_F64;
            } else if (strcmp(op, "<") == 0) {
                resultVal.as.boolean = a < b;
                resultVal.type = VAL_BOOL;
            } else if (strcmp(op, ">") == 0) {
                resultVal.as.boolean = a > b;
                resultVal.type = VAL_BOOL;
            } else if (strcmp(op, "<=") == 0) {
                resultVal.as.boolean = a <= b;
                resultVal.type = VAL_BOOL;
            } else if (strcmp(op, ">=") == 0) {
                resultVal.as.boolean = a >= b;
                resultVal.type = VAL_BOOL;
            } else if (strcmp(op, "==") == 0) {
                resultVal.as.boolean = a == b;
                resultVal.type = VAL_BOOL;
            } else if (strcmp(op, "!=") == 0) {
                resultVal.as.boolean = a != b;
                resultVal.type = VAL_BOOL;
            } else
                return false;
            break;
        }
        default:
            return false;
    }

    result->kind = EXP_K;
    result->type = resultVal.type;
    result->u.constant.value = resultVal;
    result->u.constant.constIndex = -1;
    result->isConstant = true;
    return true;
}

static void compile_typed_binary_enhanced(Compiler* compiler, ASTNode* node,
                                          TypedExpDesc* desc,
                                          ValueType inferredType) {
    TypedExpDesc left, right;
    compile_typed_expr(compiler, node->binary.left, &left);
    compile_typed_expr(compiler, node->binary.right, &right);

    ValueType resultType = inferredType != VAL_NIL ? inferredType : left.type;
    if (left.type != right.type) {
        if ((left.type == VAL_I32 && right.type == VAL_I64) ||
            (left.type == VAL_I64 && right.type == VAL_I32)) {
            resultType = VAL_I64;
        } else if ((left.type == VAL_F64) || (right.type == VAL_F64)) {
            resultType = VAL_F64;
        }
    }

    desc->type = resultType;
    desc->kind = EXP_TEMP;
    desc->isConstant = false;

    if (left.isConstant && right.isConstant &&
        is_arithmetic_op(node->binary.op)) {
        if (try_constant_folding(compiler, node, &left, &right, desc)) {
            return;
        }
    }

    int leftReg = allocateRegister(compiler);
    int rightReg = allocateRegister(compiler);
    int resultReg = allocateRegister(compiler);

    discharge_typed_reg(compiler, &left, leftReg);
    discharge_typed_reg(compiler, &right, rightReg);

    emitTypedBinaryOp(compiler, node->binary.op, resultType, resultReg, leftReg,
                      rightReg);

    desc->u.s.info = resultReg;
    desc->u.s.regType = resultType;
    desc->u.s.isTemporary = true;

    if (left.u.s.isTemporary) freeRegister(compiler, left.u.s.info);
    if (right.u.s.isTemporary) freeRegister(compiler, right.u.s.info);
}

static void compile_typed_binary(Compiler* compiler, ASTNode* node,
                                 TypedExpDesc* desc) {
    compile_typed_binary_enhanced(compiler, node, desc, VAL_NIL);
}

static void compile_typed_unary(Compiler* compiler, ASTNode* node,
                                TypedExpDesc* desc) {
    TypedExpDesc operand;
    compile_typed_expr(compiler, node->unary.operand, &operand);

    if (operand.kind == EXP_K && strcmp(node->unary.op, "-") == 0) {
        Value v = operand.u.constant.value;
        if (IS_I32(v)) {
            desc->u.constant.value = I32_VAL(-AS_I32(v));
            desc->type = VAL_I32;
            desc->kind = EXP_K;
            desc->isConstant = true;
            return;
        } else if (IS_F64(v)) {
            desc->u.constant.value = F64_VAL(-AS_F64(v));
            desc->type = VAL_F64;
            desc->kind = EXP_K;
            desc->isConstant = true;
            return;
        }
    }

    discharge_typed_reg(compiler, &operand, allocateRegister(compiler));
    int resultReg = allocateRegister(compiler);
    if (strcmp(node->unary.op, "not") == 0) {
        uint8_t bytes[3] = {OP_NOT_BOOL_R, (uint8_t)resultReg,
                            (uint8_t)operand.u.s.info};
        insertCode(compiler, compiler->chunk->count, bytes, 3);
        desc->type = VAL_BOOL;
    } else if (strcmp(node->unary.op, "-") == 0) {
        Value zero;
        uint8_t opcode;
        switch (operand.type) {
            case VAL_I64:
                zero = I64_VAL(0);
                opcode = OP_SUB_I64_R;
                break;
            case VAL_F64:
                zero = F64_VAL(0);
                opcode = OP_SUB_F64_R;
                break;
            case VAL_U32:
                zero = U32_VAL(0);
                opcode = OP_SUB_U32_R;
                break;
            case VAL_U64:
                zero = U64_VAL(0);
                opcode = OP_SUB_U64_R;
                break;
            default:
                zero = I32_VAL(0);
                opcode = OP_SUB_I32_R;
                break;
        }
        uint8_t zeroReg = allocateRegister(compiler);
        emitConstant(compiler, zeroReg, zero);
        uint8_t bytes[4] = {opcode, (uint8_t)resultReg, zeroReg,
                            (uint8_t)operand.u.s.info};
        insertCode(compiler, compiler->chunk->count, bytes, 4);
        freeRegister(compiler, zeroReg);
        desc->type = operand.type;
    } else if (strcmp(node->unary.op, "~") == 0) {
        uint8_t bytes[3] = {OP_NOT_I32_R, (uint8_t)resultReg,
                            (uint8_t)operand.u.s.info};
        insertCode(compiler, compiler->chunk->count, bytes, 3);
        desc->type = VAL_I32;
    } else {
        uint8_t bytes[3] = {OP_NOT_BOOL_R, (uint8_t)resultReg,
                            (uint8_t)operand.u.s.info};
        insertCode(compiler, compiler->chunk->count, bytes, 3);
        desc->type = VAL_BOOL;
    }

    if (operand.u.s.isTemporary) freeRegister(compiler, operand.u.s.info);

    desc->kind = EXP_TEMP;
    desc->u.s.info = resultReg;
    desc->u.s.regType = desc->type;
    desc->u.s.isTemporary = true;
    desc->isConstant = false;
}

void compile_typed_call(Compiler* compiler, ASTNode* node, TypedExpDesc* desc) {
    TypedExpDesc calleeDesc;
    compile_typed_expr(compiler, node->call.callee, &calleeDesc);

    int funcReg = allocateRegister(compiler);
    discharge_typed_reg(compiler, &calleeDesc, funcReg);

    uint8_t resultReg = allocateRegister(compiler);
    uint8_t firstArgReg = 0;

    if (node->call.argCount > 0) {
        firstArgReg = allocateRegister(compiler);
        for (int i = 0; i < node->call.argCount; i++) {
            TypedExpDesc argDesc;
            compile_typed_expr(compiler, node->call.args[i], &argDesc);
            uint8_t argReg = firstArgReg + i;
            if (i > 0) {
                argReg = allocateRegister(compiler);
            }
            discharge_typed_reg(compiler, &argDesc, argReg);
        }
    }

    uint8_t bytes[5] = {OP_CALL_R, (uint8_t)funcReg, firstArgReg,
                        (uint8_t)node->call.argCount, resultReg};
    insertCode(compiler, compiler->chunk->count, bytes, 5);

    freeRegister(compiler, (uint8_t)funcReg);
    for (int i = 0; i < node->call.argCount; i++) {
        freeRegister(compiler, firstArgReg + i);
    }

    desc->kind = EXP_TEMP;
    desc->type = VAL_NIL;
    desc->u.s.info = resultReg;
    desc->u.s.regType = VAL_NIL;
    desc->u.s.isTemporary = true;
    desc->isConstant = false;
}

static void compile_typed_expr(Compiler* compiler, ASTNode* node,
                               TypedExpDesc* desc) {
    Type* inferredType = inferExpressionType(compiler, node);
    ValueType staticType =
        inferredType ? typeKindToValueType(inferredType->kind) : VAL_NIL;

    switch (node->type) {
        case NODE_LITERAL: {
            desc->kind = EXP_K;
            desc->type =
                staticType != VAL_NIL ? staticType : node->literal.value.type;
            desc->u.constant.value = node->literal.value;
            desc->u.constant.constIndex = -1;
            desc->isConstant = true;
            break;
        }
        case NODE_IDENTIFIER: {
            int localIndex;
            if (symbol_table_get(&compiler->symbols, node->identifier.name,
                                 &localIndex)) {
                desc->kind = EXP_LOCAL;
                desc->type = staticType != VAL_NIL
                                 ? staticType
                                 : compiler->locals[localIndex].type;
                desc->u.s.info = compiler->locals[localIndex].reg;
                desc->u.s.regType = desc->type;
                desc->isConstant = false;
            } else {
                desc->kind = EXP_VOID;
                desc->type = VAL_NIL;
            }
            break;
        }
        case NODE_BINARY: {
            compile_typed_binary_enhanced(compiler, node, desc, staticType);
            break;
        }
        case NODE_UNARY: {
            compile_typed_unary(compiler, node, desc);
            break;
        }
        case NODE_CALL: {
            compile_typed_call(compiler, node, desc);
            break;
        }
        default: {
            desc->kind = EXP_VOID;
            desc->type = VAL_NIL;
            break;
        }
    }
}

int compile_typed_expression_to_register(ASTNode* node, Compiler* compiler) {
    TypedExpDesc desc;
    compile_typed_expr(compiler, node, &desc);

    if (desc.kind == EXP_VOID) {
        return compileExpressionToRegister_old(node, compiler);
    }

    int reg = allocateRegister(compiler);
    discharge_typed_reg(compiler, &desc, reg);
    return reg;
}

int compileExpressionToRegister_new(ASTNode* node, Compiler* compiler) {
    return compile_typed_expression_to_register(node, compiler);
}

int compileExpressionToRegister(ASTNode* node, Compiler* compiler) {
#if TYPED_EXPRESSIONS
    return compile_typed_expression_to_register(node, compiler);
#else
    return compileExpressionToRegister_old(node, compiler);
#endif
}

static void compile_typed_statement_expr(Compiler* compiler, ASTNode* node,
                                         TypedExpDesc* result) {
    init_typed_exp(result, EXP_VOID, VAL_NIL, 0);

    switch (node->type) {
        case NODE_IF: {
            compile_typed_if_statement(node, compiler, result);
            break;
        }
        case NODE_WHILE: {
            compile_typed_while_statement(node, compiler, result);
            break;
        }
        case NODE_FOR_RANGE: {
            compile_typed_for_statement(node, compiler, result);
            break;
        }
        case NODE_BLOCK: {
            compile_typed_block_statement(node, compiler, result);
            break;
        }
        case NODE_VAR_DECL: {
            int reg = compileExpressionToRegister_old(node, compiler);
            if (reg >= 0) {
                result->kind = EXP_TEMP;
                result->type = VAL_NIL;
                result->u.s.info = reg;
                result->u.s.isTemporary = true;
            }
            break;
        }
        case NODE_ASSIGN: {
            int reg = compileExpressionToRegister_old(node, compiler);
            if (reg >= 0) {
                result->kind = EXP_TEMP;
                result->type = VAL_NIL;
                result->u.s.info = reg;
                result->u.s.isTemporary = true;
            }
            break;
        }
        case NODE_PRINT: {
            int reg = compileExpressionToRegister_old(node, compiler);
            if (reg >= 0) {
                result->kind = EXP_TEMP;
                result->type = VAL_NIL;
                result->u.s.info = reg;
                result->u.s.isTemporary = true;
            }
            break;
        }
        default: {
            compile_typed_expr(compiler, node, result);
            break;
        }
    }
}

int compile_typed_statement(ASTNode* node, Compiler* compiler) {
    TypedExpDesc desc;
    compile_typed_statement_expr(compiler, node, &desc);

    if (desc.kind == EXP_VOID) {
        return compileExpressionToRegister_old(node, compiler);
    }

    int reg = allocateRegister(compiler);
    discharge_typed_reg(compiler, &desc, reg);
    return reg;
}

void compile_typed_if_statement(ASTNode* node, Compiler* compiler,
                                TypedExpDesc* result) {
    TypedExpDesc condDesc;
    compile_typed_expr(compiler, node->ifStmt.condition, &condDesc);

    int condReg = allocateRegister(compiler);
    discharge_typed_reg(compiler, &condDesc, condReg);

    int elseJump = emitConditionalJump(compiler, (uint8_t)condReg);
    freeRegister(compiler, (uint8_t)condReg);

    enterScope(compiler);
    TypedExpDesc thenDesc;
    compile_typed_statement_expr(compiler, node->ifStmt.thenBranch, &thenDesc);

    int thenReg = -1;
    if (thenDesc.kind != EXP_VOID) {
        thenReg = allocateRegister(compiler);
        discharge_typed_reg(compiler, &thenDesc, thenReg);
    }
    exitScope(compiler);

    int endJump = -1;
    int elseReg = -1;
    if (node->ifStmt.elseBranch) {
        endJump = emitJump(compiler, OP_JUMP_SHORT);
        patchJump(compiler, elseJump);

        enterScope(compiler);
        TypedExpDesc elseDesc;
        compile_typed_statement_expr(compiler, node->ifStmt.elseBranch,
                                     &elseDesc);

        if (elseDesc.kind != EXP_VOID) {
            elseReg = allocateRegister(compiler);
            discharge_typed_reg(compiler, &elseDesc, elseReg);
        }
        exitScope(compiler);

        if (endJump >= 0) {
            patchJump(compiler, endJump);
        }
    } else {
        patchJump(compiler, elseJump);
    }

    if (thenReg >= 0) {
        result->kind = EXP_TEMP;
        result->type = thenDesc.type;
        result->u.s.info = thenReg;
        result->u.s.isTemporary = true;
    } else {
        result->kind = EXP_VOID;
        result->type = VAL_NIL;
    }
}

void compile_typed_while_statement(ASTNode* node, Compiler* compiler,
                                   TypedExpDesc* result) {
    int loopStart = compiler->chunk->count;
    enterLoop(compiler, loopStart, node->whileStmt.label);

    TypedExpDesc condDesc;
    compile_typed_expr(compiler, node->whileStmt.condition, &condDesc);

    int condReg = allocateRegister(compiler);
    discharge_typed_reg(compiler, &condDesc, condReg);

    int exitJump = emitConditionalJump(compiler, (uint8_t)condReg);
    freeRegister(compiler, (uint8_t)condReg);

    enterScope(compiler);
    TypedExpDesc bodyDesc;
    compile_typed_statement_expr(compiler, node->whileStmt.body, &bodyDesc);

    if (bodyDesc.kind != EXP_VOID && bodyDesc.u.s.isTemporary) {
        freeRegister(compiler, bodyDesc.u.s.info);
    }
    exitScope(compiler);

    patchContinueJumps(compiler, getCurrentLoop(compiler),
                       compiler->chunk->count);
    emitLoop(compiler, loopStart);

    patchJump(compiler, exitJump);
    LoopContext* loop = getCurrentLoop(compiler);
    for (int i = 0; i < loop->breakJumps.offsets.count; i++) {
        patchJump(compiler, loop->breakJumps.offsets.data[i]);
    }

    exitLoop(compiler);

    result->kind = EXP_VOID;
    result->type = VAL_NIL;
}

void compile_typed_for_statement(ASTNode* node, Compiler* compiler,
                                 TypedExpDesc* result) {
    TypedExpDesc startDesc, endDesc;
    compile_typed_expr(compiler, node->forRange.start, &startDesc);
    compile_typed_expr(compiler, node->forRange.end, &endDesc);

    int startReg = allocateRegister(compiler);
    int endReg = allocateRegister(compiler);

    discharge_typed_reg(compiler, &startDesc, startReg);
    discharge_typed_reg(compiler, &endDesc, endReg);

    enterScope(compiler);

    const char* varName = node->forRange.varName;
    uint8_t loopVar = allocateRegister(compiler);

    if (compiler->localCount >= REGISTER_COUNT) {
        freeRegister(compiler, (uint8_t)startReg);
        freeRegister(compiler, (uint8_t)endReg);
        exitScope(compiler);
        result->kind = EXP_VOID;
        result->type = VAL_NIL;
        return;
    }

    int localIndex = compiler->localCount++;
    compiler->locals[localIndex].name = (char*)varName;
    compiler->locals[localIndex].reg = loopVar;
    compiler->locals[localIndex].isActive = true;
    compiler->locals[localIndex].depth = compiler->scopeDepth;
    compiler->locals[localIndex].isMutable = true;
    compiler->locals[localIndex].type = VAL_I32;
    symbol_table_set(&compiler->symbols, varName, localIndex, compiler->scopeDepth);

    uint8_t moveBytes[3] = {OP_MOVE, loopVar, (uint8_t)startReg};
    insertCode(compiler, compiler->chunk->count, moveBytes, 3);

    int loopStart = compiler->chunk->count;
    enterLoop(compiler, loopStart, node->forRange.label);

    uint8_t condReg = allocateRegister(compiler);
    uint8_t compBytes[4] = {
        node->forRange.inclusive ? OP_LE_I32_R : OP_LT_I32_R, condReg, loopVar,
        (uint8_t)endReg};
    insertCode(compiler, compiler->chunk->count, compBytes, 4);

    int exitJump = emitConditionalJump(compiler, condReg);
    freeRegister(compiler, condReg);

    TypedExpDesc bodyDesc;
    compile_typed_statement_expr(compiler, node->forRange.body, &bodyDesc);

    if (bodyDesc.kind != EXP_VOID && bodyDesc.u.s.isTemporary) {
        freeRegister(compiler, bodyDesc.u.s.info);
    }

    patchContinueJumps(compiler, getCurrentLoop(compiler),
                       compiler->chunk->count);

    if (node->forRange.step) {
        TypedExpDesc stepDesc;
        compile_typed_expr(compiler, node->forRange.step, &stepDesc);
        int stepReg = allocateRegister(compiler);
        discharge_typed_reg(compiler, &stepDesc, stepReg);

        uint8_t addBytes[4] = {OP_ADD_I32_R, loopVar, loopVar,
                               (uint8_t)stepReg};
        insertCode(compiler, compiler->chunk->count, addBytes, 4);
        freeRegister(compiler, (uint8_t)stepReg);
    } else {
        uint8_t oneReg = allocateRegister(compiler);
        Value one = {.type = VAL_I32, .as.i32 = 1};
        emitConstant(compiler, oneReg, one);

        uint8_t addBytes[4] = {OP_ADD_I32_R, loopVar, loopVar, oneReg};
        insertCode(compiler, compiler->chunk->count, addBytes, 4);
        freeRegister(compiler, oneReg);
    }

    emitLoop(compiler, loopStart);

    patchJump(compiler, exitJump);
    LoopContext* forLoop = getCurrentLoop(compiler);
    for (int i = 0; i < forLoop->breakJumps.offsets.count; i++) {
        patchJump(compiler, forLoop->breakJumps.offsets.data[i]);
    }

    exitLoop(compiler);
    exitScope(compiler);

    freeRegister(compiler, (uint8_t)startReg);
    freeRegister(compiler, (uint8_t)endReg);

    result->kind = EXP_VOID;
    result->type = VAL_NIL;
}

void compile_typed_block_statement(ASTNode* node, Compiler* compiler,
                                   TypedExpDesc* result) {
    result->kind = EXP_VOID;
    result->type = VAL_NIL;

    if (node->block.count == 0) {
        return;
    }

    for (int i = 0; i < node->block.count; i++) {
        TypedExpDesc stmtDesc;
        compile_typed_statement_expr(compiler, node->block.statements[i],
                                     &stmtDesc);

        if (i == node->block.count - 1 && stmtDesc.kind != EXP_VOID) {
            *result = stmtDesc;
        } else if (stmtDesc.kind != EXP_VOID && stmtDesc.u.s.isTemporary) {
            freeRegister(compiler, stmtDesc.u.s.info);
        }
    }
}

static int compileExpressionToRegister_old(ASTNode* node, Compiler* compiler) {
    if (!node) return -1;

    switch (node->type) {
        case NODE_LITERAL: {
            uint16_t reg = allocateRegister(compiler);
            emitConstant(compiler, reg, node->literal.value);
            return reg;
        }
        case NODE_UNARY: {
            int operandReg =
                compileExpressionToRegister_old(node->unary.operand, compiler);
            if (operandReg < 0) return -1;
            uint16_t resultReg = allocateRegister(compiler);
            if (strcmp(node->unary.op, "not") == 0) {
                uint8_t bytes[5] = {OP_NOT_BOOL_R, (uint8_t)(resultReg & 0xFF), (uint8_t)(resultReg >> 8),
                                    (uint8_t)(operandReg & 0xFF), (uint8_t)(operandReg >> 8)};
                insertCode(compiler, compiler->chunk->count, bytes, 5);
                freeRegister(compiler, operandReg);
                return resultReg;
            } else if (strcmp(node->unary.op, "-") == 0) {
                ValueType opType =
                    getNodeValueTypeWithCompiler(node->unary.operand, compiler);
                Value zero;
                uint16_t opcode;
                switch (opType) {
                    case VAL_I64:
                        zero = I64_VAL(0);
                        opcode = OP_SUB_I64_R;
                        break;
                    case VAL_F64:
                        zero = F64_VAL(0);
                        opcode = OP_SUB_F64_R;
                        break;
                    case VAL_U32:
                        zero = U32_VAL(0);
                        opcode = OP_SUB_U32_R;
                        break;
                    case VAL_U64:
                        zero = U64_VAL(0);
                        opcode = OP_SUB_U64_R;
                        break;
                    default:
                        zero = I32_VAL(0);
                        opcode = OP_SUB_I32_R;
                        break;
                }
                uint16_t zeroReg = allocateRegister(compiler);
                emitConstant(compiler, zeroReg, zero);
                uint8_t bytes[7] = {opcode, (uint8_t)(resultReg & 0xFF), (uint8_t)(resultReg >> 8),
                                    (uint8_t)(zeroReg & 0xFF), (uint8_t)(zeroReg >> 8),
                                    (uint8_t)(operandReg & 0xFF), (uint8_t)(operandReg >> 8)};
                insertCode(compiler, compiler->chunk->count, bytes, 7);
                freeRegister(compiler, operandReg);
                freeRegister(compiler, zeroReg);
                return resultReg;
            } else if (strcmp(node->unary.op, "~") == 0) {
                uint8_t bytes[5] = {OP_NOT_I32_R, (uint8_t)(resultReg & 0xFF), (uint8_t)(resultReg >> 8),
                                    (uint8_t)(operandReg & 0xFF), (uint8_t)(operandReg >> 8)};
                insertCode(compiler, compiler->chunk->count, bytes, 5);
                freeRegister(compiler, operandReg);
                return resultReg;
            }
            freeRegister(compiler, operandReg);
            return -1;
        }
        case NODE_BINARY: {
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
                } else if (strcmp(op, "/") == 0 && b != 0) {
                    result = a / b;
                } else if (strcmp(op, "%") == 0 && b != 0) {
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
                } else
                    return -1;
                uint16_t reg = allocateRegister(compiler);
                if (strcmp(op, "+") == 0 || strcmp(op, "-") == 0 ||
                    strcmp(op, "*") == 0 || strcmp(op, "/") == 0 ||
                    strcmp(op, "%") == 0) {
                    emitConstant(compiler, reg, I32_VAL(result));
                } else {
                    emitConstant(compiler, reg, BOOL_VAL(boolResult));
                }
                return reg;
            }
            return -1;  // Simplified for brevity; original code had more cases
        }
        default:
            return -1;
    }
}

void emitByte(Compiler* compiler, uint8_t byte) {
    writeChunk(compiler->chunk, byte, 1, 1);
}

void emitConstant(Compiler* compiler, uint16_t reg, Value value) {
    int constant = findConstant(compiler->chunk, value);
    if (constant == -1) {
        constant = addConstant(compiler->chunk, value);
    }
    if (constant > UINT16_MAX) {
        compiler->hadError = true;
        return;
    }
    uint8_t bytes[4] = {OP_LOAD_CONST, reg & 0xFF, (uint8_t)((constant >> 8) & 0xFF),
                        (uint8_t)(constant & 0xFF)};
    insertCode(compiler, compiler->chunk->count, bytes, 4);
}

bool compile(ASTNode* ast, Compiler* compiler, bool isModule) {
    initTypeCache(compiler);
    initCompilerTypeInference(compiler);

    if (!ast) {
        freeTypeCache(compiler);
        freeCompilerTypeInference(compiler);
        return false;
    }

    if (ast->type == NODE_PROGRAM) {
        for (int i = 0; i < ast->program.count; i++) {
            ASTNode* stmt = ast->program.declarations[i];
            int reg;
#if TYPED_STATEMENTS
            reg = compile_typed_statement(stmt, compiler);
#else
            reg = compileExpressionToRegister(stmt, compiler);
#endif
            if (reg < 0) {
                freeTypeCache(compiler);
                freeCompilerTypeInference(compiler);
                return false;
            }
            if (!isModule && stmt->type != NODE_VAR_DECL &&
                stmt->type != NODE_PRINT && stmt->type != NODE_IF &&
                stmt->type != NODE_WHILE && stmt->type != NODE_FOR_RANGE &&
                stmt->type != NODE_FOR_ITER && stmt->type != NODE_BLOCK &&
                stmt->type != NODE_ASSIGN && stmt->type != NODE_FUNCTION) {
                uint8_t bytes[2] = {OP_PRINT_R, (uint8_t)reg};
                insertCode(compiler, compiler->chunk->count, bytes, 3);
            }
        }
        patchAllPendingJumps(compiler);
        freeTypeCache(compiler);
        freeCompilerTypeInference(compiler);
        return true;
    }

    int resultReg;
#if TYPED_STATEMENTS
    resultReg = compile_typed_statement(ast, compiler);
#else
    resultReg = compileExpressionToRegister(ast, compiler);
#endif

    if (resultReg >= 0 && !isModule && ast->type != NODE_VAR_DECL &&
        ast->type != NODE_PRINT && ast->type != NODE_IF &&
        ast->type != NODE_WHILE && ast->type != NODE_FOR_RANGE &&
        ast->type != NODE_FOR_ITER && ast->type != NODE_BLOCK &&
        ast->type != NODE_FUNCTION) {
        uint8_t bytes[2] = {OP_PRINT_R, (uint8_t)resultReg};
        insertCode(compiler, compiler->chunk->count, bytes, 2);
    }

    patchAllPendingJumps(compiler);
    freeTypeCache(compiler);
    freeCompilerTypeInference(compiler);
    return resultReg >= 0;
}