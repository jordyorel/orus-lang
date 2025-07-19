// Author: Hierat
// Date: 2023-10-01
// Description: A single pass compiler for the Orus language, handling AST compilation to bytecode.



#include "compiler/compiler.h"
#include "runtime/memory.h"
#include "compiler/symbol_table.h"
#include "type/type.h"
#include "internal/error_reporting.h"
#include "errors/features/type_errors.h"
#include "runtime/jumptable.h"
#include "compiler/loop_optimization.h"
#include <string.h>

bool compileNode(ASTNode* node, Compiler* compiler);
static int compileExpr(ASTNode* node, Compiler* compiler);
static Type* getExprType(ASTNode* node, Compiler* compiler);

// Optimization functions
static Value evaluateConstantExpression(ASTNode* node);
static bool isConstantExpression(ASTNode* node);
static bool isAlwaysTrue(ASTNode* node);
static bool isAlwaysFalse(ASTNode* node);

// Expression compilation handlers
static int compileLiteral(ASTNode* node, Compiler* compiler);
static int compileIdentifier(ASTNode* node, Compiler* compiler);
static int compileBinaryOp(ASTNode* node, Compiler* compiler);
static int compileCast(ASTNode* node, Compiler* compiler);
static int compileUnary(ASTNode* node, Compiler* compiler);
static int compileTernary(ASTNode* node, Compiler* compiler);

// Cast compilation handlers
static bool compileCastFromI32(Type* targetType, Compiler* compiler, uint8_t dstReg, int srcReg);
static bool compileCastFromI64(Type* targetType, Compiler* compiler, uint8_t dstReg, int srcReg);
static bool compileCastFromU32(Type* targetType, Compiler* compiler, uint8_t dstReg, int srcReg);
static bool compileCastFromU64(Type* targetType, Compiler* compiler, uint8_t dstReg, int srcReg);
static bool compileCastFromF64(Type* targetType, Compiler* compiler, uint8_t dstReg, int srcReg);
static bool compileCastFromBool(Type* targetType, Compiler* compiler, uint8_t dstReg, int srcReg);

void initCompiler(Compiler* compiler, Chunk* chunk, const char* fileName,
                   const char* source) {
    compiler->chunk = chunk;
    compiler->fileName = fileName;
    compiler->source = source;
    compiler->nextRegister = 0;
    compiler->maxRegisters = 0;
    compiler->localCount = 0;
    compiler->scopeDepth = 0;
    compiler->loopDepth = 0;       // Initialize loop depth tracking
    compiler->hadError = false;
    compiler->currentLine = 1;     // Initialize line tracking
    compiler->currentColumn = 1;   // Initialize column tracking
    symbol_table_init(&compiler->symbols);
    
    // Initialize loop optimizer
    compiler->optimizer.enabled = true;
    compiler->optimizer.unrollCount = 0;
    compiler->optimizer.strengthReductionCount = 0;
    compiler->optimizer.boundsEliminationCount = 0;
    compiler->optimizer.totalOptimizations = 0;
}

void freeCompiler(Compiler* compiler) {
    symbol_table_free(&compiler->symbols);
}

uint8_t allocateRegister(Compiler* compiler) {
    uint8_t r = compiler->nextRegister++;
    if (compiler->nextRegister > compiler->maxRegisters)
        compiler->maxRegisters = compiler->nextRegister;
    return r;
}

void freeRegister(Compiler* compiler, uint8_t reg) {
    if (reg + 1 == compiler->nextRegister && compiler->nextRegister > 0)
        compiler->nextRegister--;
    (void)compiler;
}

void emitByte(Compiler* compiler, uint8_t byte) {
    writeChunk(compiler->chunk, byte, compiler->currentLine, compiler->currentColumn);
}

void emitBytes(Compiler* compiler, uint8_t b1, uint8_t b2) {
    emitByte(compiler, b1);
    emitByte(compiler, b2);
}

// Control flow helper functions
int emitJump(Compiler* compiler) {
    // Emit placeholder bytes for jump offset
    emitByte(compiler, 0xff);
    emitByte(compiler, 0xff);
    return compiler->chunk->count - 2;
}

void patchJump(Compiler* compiler, int offset) {
    // Calculate jump distance
    int jump = compiler->chunk->count - offset - 2;
    
    if (jump > UINT16_MAX) {
        // Jump too far - could implement long jumps here
        compiler->hadError = true;
        return;
    }
    
    // Patch the placeholder bytes
    compiler->chunk->code[offset] = (jump >> 8) & 0xff;
    compiler->chunk->code[offset + 1] = jump & 0xff;
}

void emitLoop(Compiler* compiler, int loopStart) {
    emitByte(compiler, OP_LOOP);
    
    int offset = compiler->chunk->count - loopStart + 2;
    if (offset > UINT16_MAX) {
        compiler->hadError = true;
        return;
    }
    
    emitByte(compiler, (offset >> 8) & 0xff);
    emitByte(compiler, offset & 0xff);
}

void emitConstant(Compiler* compiler, uint8_t reg, Value value) {
    int idx = addConstant(compiler->chunk, value);
    emitByte(compiler, OP_LOAD_CONST);
    emitByte(compiler, reg);
    emitByte(compiler, (uint8_t)((idx >> 8) & 0xFF));
    emitByte(compiler, (uint8_t)(idx & 0xFF));
}

// Optimization functions - Single-pass constant folding and dead code elimination
// These functions evaluate expressions at compile time for optimization

static bool isConstantExpression(ASTNode* node) {
    if (!node) return false;
    
    switch (node->type) {
        case NODE_LITERAL:
            return true;
            
        case NODE_BINARY: {
            // Binary expressions are constant if both operands are constant
            return isConstantExpression(node->binary.left) && 
                   isConstantExpression(node->binary.right);
        }
        
        case NODE_UNARY: {
            // Unary expressions are constant if operand is constant
            return isConstantExpression(node->unary.operand);
        }
        
        case NODE_CAST: {
            // Cast expressions are constant if operand is constant
            return isConstantExpression(node->cast.expression);
        }
        
        default:
            return false;
    }
}

static Value evaluateConstantExpression(ASTNode* node) {
    if (!node) return NIL_VAL;
    
    switch (node->type) {
        case NODE_LITERAL:
            return node->literal.value;
            
        case NODE_BINARY: {
            Value left = evaluateConstantExpression(node->binary.left);
            Value right = evaluateConstantExpression(node->binary.right);
            
            // Perform constant folding for supported operations
            const char* op = node->binary.op;
            
            // Arithmetic operations
            if (strcmp(op, "+") == 0) {
                if (IS_I32(left) && IS_I32(right)) {
                    return I32_VAL(AS_I32(left) + AS_I32(right));
                }
                if (IS_F64(left) && IS_F64(right)) {
                    return F64_VAL(AS_F64(left) + AS_F64(right));
                }
            } else if (strcmp(op, "-") == 0) {
                if (IS_I32(left) && IS_I32(right)) {
                    return I32_VAL(AS_I32(left) - AS_I32(right));
                }
                if (IS_F64(left) && IS_F64(right)) {
                    return F64_VAL(AS_F64(left) - AS_F64(right));
                }
            } else if (strcmp(op, "*") == 0) {
                if (IS_I32(left) && IS_I32(right)) {
                    return I32_VAL(AS_I32(left) * AS_I32(right));
                }
                if (IS_F64(left) && IS_F64(right)) {
                    return F64_VAL(AS_F64(left) * AS_F64(right));
                }
            } else if (strcmp(op, "/") == 0) {
                if (IS_I32(left) && IS_I32(right) && AS_I32(right) != 0) {
                    return I32_VAL(AS_I32(left) / AS_I32(right));
                }
                if (IS_F64(left) && IS_F64(right) && AS_F64(right) != 0.0) {
                    return F64_VAL(AS_F64(left) / AS_F64(right));
                }
            }
            
            // Comparison operations
            else if (strcmp(op, "==") == 0) {
                if (IS_I32(left) && IS_I32(right)) {
                    return BOOL_VAL(AS_I32(left) == AS_I32(right));
                }
                if (IS_F64(left) && IS_F64(right)) {
                    return BOOL_VAL(AS_F64(left) == AS_F64(right));
                }
                if (IS_BOOL(left) && IS_BOOL(right)) {
                    return BOOL_VAL(AS_BOOL(left) == AS_BOOL(right));
                }
            } else if (strcmp(op, "!=") == 0) {
                if (IS_I32(left) && IS_I32(right)) {
                    return BOOL_VAL(AS_I32(left) != AS_I32(right));
                }
                if (IS_F64(left) && IS_F64(right)) {
                    return BOOL_VAL(AS_F64(left) != AS_F64(right));
                }
                if (IS_BOOL(left) && IS_BOOL(right)) {
                    return BOOL_VAL(AS_BOOL(left) != AS_BOOL(right));
                }
            } else if (strcmp(op, "<") == 0) {
                if (IS_I32(left) && IS_I32(right)) {
                    return BOOL_VAL(AS_I32(left) < AS_I32(right));
                }
                if (IS_F64(left) && IS_F64(right)) {
                    return BOOL_VAL(AS_F64(left) < AS_F64(right));
                }
            } else if (strcmp(op, "<=") == 0) {
                if (IS_I32(left) && IS_I32(right)) {
                    return BOOL_VAL(AS_I32(left) <= AS_I32(right));
                }
                if (IS_F64(left) && IS_F64(right)) {
                    return BOOL_VAL(AS_F64(left) <= AS_F64(right));
                }
            } else if (strcmp(op, ">") == 0) {
                if (IS_I32(left) && IS_I32(right)) {
                    return BOOL_VAL(AS_I32(left) > AS_I32(right));
                }
                if (IS_F64(left) && IS_F64(right)) {
                    return BOOL_VAL(AS_F64(left) > AS_F64(right));
                }
            } else if (strcmp(op, ">=") == 0) {
                if (IS_I32(left) && IS_I32(right)) {
                    return BOOL_VAL(AS_I32(left) >= AS_I32(right));
                }
                if (IS_F64(left) && IS_F64(right)) {
                    return BOOL_VAL(AS_F64(left) >= AS_F64(right));
                }
            }
            
            // Logical operations
            else if (strcmp(op, "and") == 0) {
                if (IS_BOOL(left) && IS_BOOL(right)) {
                    return BOOL_VAL(AS_BOOL(left) && AS_BOOL(right));
                }
            } else if (strcmp(op, "or") == 0) {
                if (IS_BOOL(left) && IS_BOOL(right)) {
                    return BOOL_VAL(AS_BOOL(left) || AS_BOOL(right));
                }
            }
            
            return NIL_VAL;
        }
        
        case NODE_UNARY: {
            Value operand = evaluateConstantExpression(node->unary.operand);
            const char* op = node->unary.op;
            
            if (strcmp(op, "not") == 0 && IS_BOOL(operand)) {
                return BOOL_VAL(!AS_BOOL(operand));
            } else if (strcmp(op, "-") == 0) {
                if (IS_I32(operand)) {
                    return I32_VAL(-AS_I32(operand));
                }
                if (IS_F64(operand)) {
                    return F64_VAL(-AS_F64(operand));
                }
            }
            
            return NIL_VAL;
        }
        
        default:
            return NIL_VAL;
    }
}

static bool isAlwaysTrue(ASTNode* node) {
    if (!isConstantExpression(node)) return false;
    
    Value result = evaluateConstantExpression(node);
    return IS_BOOL(result) && AS_BOOL(result);
}

static bool isAlwaysFalse(ASTNode* node) {
    if (!isConstantExpression(node)) return false;
    
    Value result = evaluateConstantExpression(node);
    return IS_BOOL(result) && !AS_BOOL(result);
}

// Helper function to get the type of an expression
static Type* getExprType(ASTNode* node, Compiler* compiler) {
    if (!node) return getPrimitiveType(TYPE_UNKNOWN);
    
    switch (node->type) {
        case NODE_LITERAL:
            return infer_literal_type_extended(&node->literal.value);
        case NODE_IDENTIFIER: {
            int index;
            if (symbol_table_get_in_scope(&compiler->symbols, node->identifier.name, compiler->scopeDepth, &index)) {
                if (index < 0) {
                    // Register-based variable (loop variable) - always i32 for now
                    return getPrimitiveType(TYPE_I32);
                } else {
                    // Global variable
                    return vm.globalTypes[index];
                }
            }
            return getPrimitiveType(TYPE_UNKNOWN);
        }
        case NODE_BINARY: {
            // Check if this is a comparison operator - they return bool
            if (strcmp(node->binary.op, "==") == 0 || strcmp(node->binary.op, "!=") == 0 ||
                strcmp(node->binary.op, "<") == 0 || strcmp(node->binary.op, "<=") == 0 ||
                strcmp(node->binary.op, ">") == 0 || strcmp(node->binary.op, ">=") == 0) {
                return getPrimitiveType(TYPE_BOOL);
            }
            
            // Check if this is a logical operator - they return bool
            if (strcmp(node->binary.op, "and") == 0 || strcmp(node->binary.op, "or") == 0) {
                return getPrimitiveType(TYPE_BOOL);
            }
            
            Type* leftType = getExprType(node->binary.left, compiler);
            Type* rightType = getExprType(node->binary.right, compiler);
            
            // For arithmetic operators, return left type if both are same, otherwise unknown
            if (type_equals_extended(leftType, rightType)) {
                return leftType;
            }
            return getPrimitiveType(TYPE_UNKNOWN);
        }
        case NODE_CAST:
            // Cast result type is the target type
            if (node->cast.targetType && node->cast.targetType->type == NODE_TYPE) {
                const char* typeName = node->cast.targetType->typeAnnotation.name;
                if (strcmp(typeName, "i32") == 0) return getPrimitiveType(TYPE_I32);
                if (strcmp(typeName, "i64") == 0) return getPrimitiveType(TYPE_I64);
                if (strcmp(typeName, "u32") == 0) return getPrimitiveType(TYPE_U32);
                if (strcmp(typeName, "u64") == 0) return getPrimitiveType(TYPE_U64);
                if (strcmp(typeName, "f64") == 0) return getPrimitiveType(TYPE_F64);
                if (strcmp(typeName, "bool") == 0) return getPrimitiveType(TYPE_BOOL);
                if (strcmp(typeName, "string") == 0) return getPrimitiveType(TYPE_STRING);
            }
            return getPrimitiveType(TYPE_UNKNOWN);
        case NODE_UNARY: {
            // Handle unary expressions
            if (strcmp(node->unary.op, "not") == 0) {
                return getPrimitiveType(TYPE_BOOL);
            } else if (strcmp(node->unary.op, "-") == 0) {
                // Negation preserves the operand type (mostly)
                Type* operandType = getExprType(node->unary.operand, compiler);
                
                // For unsigned types, negation converts to signed
                if (operandType->kind == TYPE_U32) {
                    return getPrimitiveType(TYPE_I32);
                } else if (operandType->kind == TYPE_U64) {
                    return getPrimitiveType(TYPE_I64);
                } else {
                    return operandType; // i32, i64, f64 stay the same
                }
            }
            return getPrimitiveType(TYPE_UNKNOWN);
        }
        default:
            return getPrimitiveType(TYPE_UNKNOWN);
    }
}

// Expression compilation handlers

static int compileLiteral(ASTNode* node, Compiler* compiler) {
    uint8_t r = allocateRegister(compiler);
    emitConstant(compiler, r, node->literal.value);
    return r;
}

static int compileIdentifier(ASTNode* node, Compiler* compiler) {
    int index;
    if (!symbol_table_get_in_scope(&compiler->symbols, node->identifier.name, compiler->scopeDepth, &index)) {
        compiler->hadError = true;
        return allocateRegister(compiler);
    }
    
    if (index < 0) {
        // Register-based variable (loop variable) - negative index encodes register number
        int regNum = -(index + 1);
        uint8_t r = allocateRegister(compiler);
        emitByte(compiler, OP_MOVE);
        emitByte(compiler, r);
        emitByte(compiler, (uint8_t)regNum);
        return r;
    } else {
        // Global variable
        uint8_t r = allocateRegister(compiler);
        emitByte(compiler, OP_LOAD_GLOBAL);
        emitByte(compiler, r);
        emitByte(compiler, (uint8_t)index);
        return r;
    }
}

static int compileBinaryOp(ASTNode* node, Compiler* compiler) {
    // Optimization: Constant folding for binary expressions
    if (isConstantExpression(node)) {
        Value result = evaluateConstantExpression(node);
        if (!IS_NIL(result)) {
            uint8_t r = allocateRegister(compiler);
            emitConstant(compiler, r, result);
            return r;
        }
    }
    
    // Phase 4: Simple type checking for binary operations
    // Only check for obvious mismatches, let VM handle runtime type dispatch
    Type* leftType = getExprType(node->binary.left, compiler);
    Type* rightType = getExprType(node->binary.right, compiler);
    
    // Handle string concatenation with +
    if (strcmp(node->binary.op, "+") == 0 && 
        leftType->kind == TYPE_STRING && rightType->kind == TYPE_STRING) {
        // String concatenation
        int left = compileExpr(node->binary.left, compiler);
        int right = compileExpr(node->binary.right, compiler);
        uint8_t dst = allocateRegister(compiler);
        
        emitByte(compiler, OP_CONCAT_R);
        emitByte(compiler, dst);
        emitByte(compiler, (uint8_t)left);
        emitByte(compiler, (uint8_t)right);
        freeRegister(compiler, right);
        freeRegister(compiler, left);
        return dst;
    }
    
    // Simple type mismatch check - only error on clearly incompatible types
    if (leftType->kind != TYPE_UNKNOWN && rightType->kind != TYPE_UNKNOWN &&
        leftType->kind != TYPE_ANY && rightType->kind != TYPE_ANY &&
        !type_equals_extended(leftType, rightType)) {
        SrcLocation location = {vm.filePath, node->location.line, node->location.column};
        report_mixed_arithmetic(location, getTypeName(leftType->kind), getTypeName(rightType->kind));
        compiler->hadError = true;
    }
    
    int left = compileExpr(node->binary.left, compiler);
    int right = compileExpr(node->binary.right, compiler);
    uint8_t dst = allocateRegister(compiler);
    
    // Keep existing simple opcode dispatch - let VM handle type-specific operations
    if (strcmp(node->binary.op, "+") == 0) {
        emitByte(compiler, OP_ADD_I32_R);
    } else if (strcmp(node->binary.op, "-") == 0) {
        emitByte(compiler, OP_SUB_I32_R);
    } else if (strcmp(node->binary.op, "*") == 0) {
        emitByte(compiler, OP_MUL_I32_R);
    } else if (strcmp(node->binary.op, "/") == 0) {
        emitByte(compiler, OP_DIV_I32_R);
    } else if (strcmp(node->binary.op, "%") == 0) {
        emitByte(compiler, OP_MOD_I32_R);
    } else if (strcmp(node->binary.op, "and") == 0) {
        emitByte(compiler, OP_AND_BOOL_R);
    } else if (strcmp(node->binary.op, "or") == 0) {
        emitByte(compiler, OP_OR_BOOL_R);
    } else if (strcmp(node->binary.op, "==") == 0) {
        emitByte(compiler, OP_EQ_R);
    } else if (strcmp(node->binary.op, "!=") == 0) {
        emitByte(compiler, OP_NE_R);
    } else if (strcmp(node->binary.op, "<") == 0) {
        emitByte(compiler, OP_LT_I32_R);
    } else if (strcmp(node->binary.op, "<=") == 0) {
        emitByte(compiler, OP_LE_I32_R);
    } else if (strcmp(node->binary.op, ">") == 0) {
        emitByte(compiler, OP_GT_I32_R);
    } else if (strcmp(node->binary.op, ">=") == 0) {
        emitByte(compiler, OP_GE_I32_R);
    } else {
        compiler->hadError = true;
        emitByte(compiler, OP_ADD_I32_R); // Fallback
    }
    
    emitByte(compiler, dst);
    emitByte(compiler, (uint8_t)left);
    emitByte(compiler, (uint8_t)right);
    freeRegister(compiler, right);
    freeRegister(compiler, left);
    return dst;
}

// Cast compilation handlers - each handles one source type
static bool compileCastFromI32(Type* targetType, Compiler* compiler, uint8_t dstReg, int srcReg) {
    (void)dstReg; (void)srcReg;  // Unused for simple opcodes
    if (targetType->kind == TYPE_I64) {
        emitByte(compiler, OP_I32_TO_I64_R);
    } else if (targetType->kind == TYPE_U32) {
        emitByte(compiler, OP_I32_TO_U32_R);
    } else if (targetType->kind == TYPE_U64) {
        emitByte(compiler, OP_I32_TO_U64_R);
    } else if (targetType->kind == TYPE_F64) {
        emitByte(compiler, OP_I32_TO_F64_R);
    } else if (targetType->kind == TYPE_BOOL) {
        emitByte(compiler, OP_I32_TO_BOOL_R);
    } else if (targetType->kind == TYPE_STRING) {
        emitByte(compiler, OP_TO_STRING_R);
    } else {
        return false;
    }
    return true;
}

static bool compileCastFromI64(Type* targetType, Compiler* compiler, uint8_t dstReg, int srcReg) {
    if (targetType->kind == TYPE_I32) {
        emitByte(compiler, OP_I64_TO_I32_R);
    } else if (targetType->kind == TYPE_U32) {
        // Chain: i64 → i32 → u32 (use existing opcodes)
        emitByte(compiler, OP_I64_TO_I32_R);
        emitByte(compiler, dstReg);
        emitByte(compiler, (uint8_t)srcReg);
        srcReg = dstReg;
        dstReg = allocateRegister(compiler);
        emitByte(compiler, OP_I32_TO_U32_R);
    } else if (targetType->kind == TYPE_U64) {
        emitByte(compiler, OP_I64_TO_U64_R);
    } else if (targetType->kind == TYPE_F64) {
        emitByte(compiler, OP_I64_TO_F64_R);
    } else if (targetType->kind == TYPE_BOOL) {
        // Chain: i64 → i32 → bool
        emitByte(compiler, OP_I64_TO_I32_R);
        emitByte(compiler, dstReg);
        emitByte(compiler, (uint8_t)srcReg);
        srcReg = dstReg;
        dstReg = allocateRegister(compiler);
        emitByte(compiler, OP_I32_TO_BOOL_R);
    } else if (targetType->kind == TYPE_STRING) {
        emitByte(compiler, OP_TO_STRING_R);
    } else {
        return false;
    }
    return true;
}

static bool compileCastFromU32(Type* targetType, Compiler* compiler, uint8_t dstReg, int srcReg) {
    if (targetType->kind == TYPE_I32) {
        (void)dstReg; (void)srcReg;  // Unused for simple opcodes
        emitByte(compiler, OP_U32_TO_I32_R);
    } else if (targetType->kind == TYPE_I64) {
        // Chain: u32 → i32 → i64
        emitByte(compiler, OP_U32_TO_I32_R);
        emitByte(compiler, dstReg);
        emitByte(compiler, (uint8_t)srcReg);
        srcReg = dstReg;
        dstReg = allocateRegister(compiler);
        emitByte(compiler, OP_I32_TO_I64_R);
    } else if (targetType->kind == TYPE_U64) {
        emitByte(compiler, OP_U32_TO_U64_R);
    } else if (targetType->kind == TYPE_F64) {
        emitByte(compiler, OP_U32_TO_F64_R);
    } else if (targetType->kind == TYPE_BOOL) {
        // Chain: u32 → i32 → bool
        emitByte(compiler, OP_U32_TO_I32_R);
        emitByte(compiler, dstReg);
        emitByte(compiler, (uint8_t)srcReg);
        srcReg = dstReg;
        dstReg = allocateRegister(compiler);
        emitByte(compiler, OP_I32_TO_BOOL_R);
    } else if (targetType->kind == TYPE_STRING) {
        emitByte(compiler, OP_TO_STRING_R);
    } else {
        return false;
    }
    return true;
}

static bool compileCastFromU64(Type* targetType, Compiler* compiler, uint8_t dstReg, int srcReg) {
    (void)dstReg; (void)srcReg;  // May be unused for simple opcodes
    if (targetType->kind == TYPE_I32) {
        emitByte(compiler, OP_U64_TO_I32_R);
    } else if (targetType->kind == TYPE_I64) {
        emitByte(compiler, OP_U64_TO_I64_R);
    } else if (targetType->kind == TYPE_U32) {
        emitByte(compiler, OP_U64_TO_U32_R);
    } else if (targetType->kind == TYPE_F64) {
        emitByte(compiler, OP_U64_TO_F64_R);
    } else if (targetType->kind == TYPE_BOOL) {
        // Chain: u64 → i32 → bool
        emitByte(compiler, OP_U64_TO_I32_R);
        emitByte(compiler, dstReg);
        emitByte(compiler, (uint8_t)srcReg);
        srcReg = dstReg;
        dstReg = allocateRegister(compiler);
        emitByte(compiler, OP_I32_TO_BOOL_R);
    } else if (targetType->kind == TYPE_STRING) {
        emitByte(compiler, OP_TO_STRING_R);
    } else {
        return false;
    }
    return true;
}

static bool compileCastFromF64(Type* targetType, Compiler* compiler, uint8_t dstReg, int srcReg) {
    (void)dstReg; (void)srcReg;  // May be unused for simple opcodes
    if (targetType->kind == TYPE_I32) {
        emitByte(compiler, OP_F64_TO_I32_R);
    } else if (targetType->kind == TYPE_I64) {
        emitByte(compiler, OP_F64_TO_I64_R);
    } else if (targetType->kind == TYPE_U32) {
        emitByte(compiler, OP_F64_TO_U32_R);
    } else if (targetType->kind == TYPE_U64) {
        emitByte(compiler, OP_F64_TO_U64_R);
    } else if (targetType->kind == TYPE_BOOL) {
        // Chain: f64 → i32 → bool
        emitByte(compiler, OP_F64_TO_I32_R);
        emitByte(compiler, dstReg);
        emitByte(compiler, (uint8_t)srcReg);
        srcReg = dstReg;
        dstReg = allocateRegister(compiler);
        emitByte(compiler, OP_I32_TO_BOOL_R);
    } else if (targetType->kind == TYPE_STRING) {
        emitByte(compiler, OP_TO_STRING_R);
    } else {
        return false;
    }
    return true;
}

static bool compileCastFromBool(Type* targetType, Compiler* compiler, uint8_t dstReg, int srcReg) {
    (void)dstReg; (void)srcReg;  // May be unused for some opcodes
    if (targetType->kind == TYPE_I32) {
        emitByte(compiler, OP_BOOL_TO_I32_R);
    } else if (targetType->kind == TYPE_I64) {
        // Chain: bool → i32 → i64
        emitByte(compiler, OP_BOOL_TO_I32_R);
        emitByte(compiler, dstReg);
        emitByte(compiler, (uint8_t)srcReg);
        srcReg = dstReg;
        dstReg = allocateRegister(compiler);
        emitByte(compiler, OP_I32_TO_I64_R);
    } else if (targetType->kind == TYPE_U32) {
        // Chain: bool → i32 → u32
        emitByte(compiler, OP_BOOL_TO_I32_R);
        emitByte(compiler, dstReg);
        emitByte(compiler, (uint8_t)srcReg);
        srcReg = dstReg;
        dstReg = allocateRegister(compiler);
        emitByte(compiler, OP_I32_TO_U32_R);
    } else if (targetType->kind == TYPE_U64) {
        // Chain: bool → i32 → u64
        emitByte(compiler, OP_BOOL_TO_I32_R);
        emitByte(compiler, dstReg);
        emitByte(compiler, (uint8_t)srcReg);
        srcReg = dstReg;
        dstReg = allocateRegister(compiler);
        emitByte(compiler, OP_I32_TO_U64_R);
    } else if (targetType->kind == TYPE_F64) {
        // Chain: bool → i32 → f64
        emitByte(compiler, OP_BOOL_TO_I32_R);
        emitByte(compiler, dstReg);
        emitByte(compiler, (uint8_t)srcReg);
        srcReg = dstReg;
        dstReg = allocateRegister(compiler);
        emitByte(compiler, OP_I32_TO_F64_R);
    } else if (targetType->kind == TYPE_STRING) {
        emitByte(compiler, OP_TO_STRING_R);
    } else {
        return false;
    }
    return true;
}

static int compileCast(ASTNode* node, Compiler* compiler) {
    // Phase 5: Comprehensive casting rules implementation
    
    // Get source type using our type inference
    Type* sourceType = getExprType(node->cast.expression, compiler);
    
    // Get target type name
    const char* targetTypeName = node->cast.targetType->typeAnnotation.name;
    Type* targetType = NULL;
    
    // Parse target type
    if (strcmp(targetTypeName, "i32") == 0) {
        targetType = getPrimitiveType(TYPE_I32);
    } else if (strcmp(targetTypeName, "i64") == 0) {
        targetType = getPrimitiveType(TYPE_I64);
    } else if (strcmp(targetTypeName, "u32") == 0) {
        targetType = getPrimitiveType(TYPE_U32);
    } else if (strcmp(targetTypeName, "u64") == 0) {
        targetType = getPrimitiveType(TYPE_U64);
    } else if (strcmp(targetTypeName, "f64") == 0) {
        targetType = getPrimitiveType(TYPE_F64);
    } else if (strcmp(targetTypeName, "bool") == 0) {
        targetType = getPrimitiveType(TYPE_BOOL);
    } else if (strcmp(targetTypeName, "string") == 0) {
        targetType = getPrimitiveType(TYPE_STRING);
    } else {
        SrcLocation location = {vm.filePath, node->location.line, node->location.column};
        report_undefined_type(location, targetTypeName);
        compiler->hadError = true;
        return allocateRegister(compiler);
    }
    
    // Phase 5: Allow casting TO string, but not FROM string to other types
    if (sourceType->kind == TYPE_STRING && targetType->kind != TYPE_STRING) {
        // Use new friendly error reporting
        SrcLocation location = {vm.filePath, node->location.line, node->location.column};
        report_invalid_cast(location, getTypeName(targetType->kind), getTypeName(sourceType->kind));
        compiler->hadError = true;
        return allocateRegister(compiler);
    }
    
    // Check if this is a same-type cast (no-op)
    if (type_equals_extended(sourceType, targetType)) {
        // Same type, just compile the expression
        return compileExpr(node->cast.expression, compiler);
    }
    
    // Validate cast compatibility (numeric, bool, and string types allowed)
    if (sourceType->kind != TYPE_I32 && sourceType->kind != TYPE_I64 && 
        sourceType->kind != TYPE_U32 && sourceType->kind != TYPE_U64 && 
        sourceType->kind != TYPE_F64 && sourceType->kind != TYPE_BOOL && 
        sourceType->kind != TYPE_STRING) {
        SrcLocation location = {vm.filePath, node->location.line, node->location.column};
        report_invalid_cast(location, "supported type", getTypeName(sourceType->kind));
        compiler->hadError = true;
        return allocateRegister(compiler);
    }
    
    if (targetType->kind != TYPE_I32 && targetType->kind != TYPE_I64 && 
        targetType->kind != TYPE_U32 && targetType->kind != TYPE_U64 && 
        targetType->kind != TYPE_F64 && targetType->kind != TYPE_BOOL && 
        targetType->kind != TYPE_STRING) {
        SrcLocation location = {vm.filePath, node->location.line, node->location.column};
        report_invalid_cast(location, "supported type", getTypeName(targetType->kind));
        compiler->hadError = true;
        return allocateRegister(compiler);
    }
    
    // Compile source expression and allocate destination
    int srcReg = compileExpr(node->cast.expression, compiler);
    uint8_t dstReg = allocateRegister(compiler);
    
    // Delegate to type-specific cast handlers
    bool validCast = false;
    
    switch (sourceType->kind) {
        case TYPE_I32:
            validCast = compileCastFromI32(targetType, compiler, dstReg, srcReg);
            break;
        case TYPE_I64:
            validCast = compileCastFromI64(targetType, compiler, dstReg, srcReg);
            break;
        case TYPE_U32:
            validCast = compileCastFromU32(targetType, compiler, dstReg, srcReg);
            break;
        case TYPE_U64:
            validCast = compileCastFromU64(targetType, compiler, dstReg, srcReg);
            break;
        case TYPE_F64:
            validCast = compileCastFromF64(targetType, compiler, dstReg, srcReg);
            break;
        case TYPE_BOOL:
            validCast = compileCastFromBool(targetType, compiler, dstReg, srcReg);
            break;
        default:
            validCast = false;
    }
    
    if (!validCast) {
        SrcLocation location = {vm.filePath, node->location.line, node->location.column};
        report_invalid_cast(location, getTypeName(targetType->kind), getTypeName(sourceType->kind));
        compiler->hadError = true;
        freeRegister(compiler, srcReg);
        return allocateRegister(compiler);
    }
    
    emitByte(compiler, dstReg);
    emitByte(compiler, (uint8_t)srcReg);
    freeRegister(compiler, srcReg);
    return dstReg;
}

static int compileUnary(ASTNode* node, Compiler* compiler) {
    // Optimization: Constant folding for unary expressions
    if (isConstantExpression(node)) {
        Value result = evaluateConstantExpression(node);
        if (!IS_NIL(result)) {
            uint8_t r = allocateRegister(compiler);
            emitConstant(compiler, r, result);
            return r;
        }
    }
    
    int operand = compileExpr(node->unary.operand, compiler);
    uint8_t dst = allocateRegister(compiler);
    
    if (strcmp(node->unary.op, "not") == 0) {
        emitByte(compiler, OP_NOT_BOOL_R);
        emitByte(compiler, dst);
        emitByte(compiler, (uint8_t)operand);
    } else if (strcmp(node->unary.op, "-") == 0) {
        // Use the original generic negation opcode that handles all types
        emitByte(compiler, OP_MOVE);  // First copy operand to destination
        emitByte(compiler, dst);
        emitByte(compiler, (uint8_t)operand);
        emitByte(compiler, OP_NEG_I32_R);  // Generic negation (handles all numeric types)
        emitByte(compiler, dst);
    } else {
        // For now, unsupported unary operations
        compiler->hadError = true;
        emitByte(compiler, OP_MOVE); // Fallback
        emitByte(compiler, dst);
        emitByte(compiler, (uint8_t)operand);
    }
    
    freeRegister(compiler, operand);
    return dst;
}

static int compileTernary(ASTNode* node, Compiler* compiler) {
    // Apply constant folding optimization first
    if (isConstantExpression(node->ternary.condition)) {
        Value conditionValue = evaluateConstantExpression(node->ternary.condition);
        if (IS_BOOL(conditionValue)) {
            // Dead code elimination - compile only the taken branch
            if (AS_BOOL(conditionValue)) {
                return compileExpr(node->ternary.trueExpr, compiler);
            } else {
                return compileExpr(node->ternary.falseExpr, compiler);
            }
        }
    }
    
    // Non-constant condition - compile with conditional jumps
    int conditionReg = compileExpr(node->ternary.condition, compiler);
    
    // Allocate result register
    int resultReg = allocateRegister(compiler);
    
    // Emit conditional jump - if condition is false, jump to false expression
    emitByte(compiler, OP_JUMP_IF_NOT_R);
    emitByte(compiler, (uint8_t)conditionReg);
    int falseJump = emitJump(compiler);
    
    freeRegister(compiler, conditionReg);
    
    // Compile true expression and store in result register
    int trueReg = compileExpr(node->ternary.trueExpr, compiler);
    emitByte(compiler, OP_MOVE);
    emitByte(compiler, (uint8_t)resultReg);
    emitByte(compiler, (uint8_t)trueReg);
    freeRegister(compiler, trueReg);
    
    // Jump over false expression
    emitByte(compiler, OP_JUMP);
    int endJump = emitJump(compiler);
    
    // Patch the false jump to point here (start of false expression)
    patchJump(compiler, falseJump);
    
    // Compile false expression and store in result register
    int falseReg = compileExpr(node->ternary.falseExpr, compiler);
    emitByte(compiler, OP_MOVE);
    emitByte(compiler, (uint8_t)resultReg);
    emitByte(compiler, (uint8_t)falseReg);
    freeRegister(compiler, falseReg);
    
    // Patch the end jump
    patchJump(compiler, endJump);
    
    return resultReg;
}

static int compileExpr(ASTNode* node, Compiler* compiler) {
    // Update current line/column for error reporting
    if (node) {
        compiler->currentLine = node->location.line;
        compiler->currentColumn = node->location.column;
    }
    
    // ✨ LICM: Check if this expression should be replaced with a hoisted temporary variable
    int tempVarIdx;
    if (tryReplaceInvariantExpression(node, &tempVarIdx)) {
        // Expression was hoisted - use the register directly (tempVarIdx is a register number)
        printf("🔄 LICM: Using hoisted register %d for expression (should be 15 or 21)\n", tempVarIdx);
        return tempVarIdx;
    }
    
    switch (node->type) {
        case NODE_LITERAL:
            return compileLiteral(node, compiler);
        case NODE_IDENTIFIER:
            return compileIdentifier(node, compiler);
        case NODE_BINARY:
            return compileBinaryOp(node, compiler);
        case NODE_TIME_STAMP: {
            uint8_t r = allocateRegister(compiler);
            emitByte(compiler, OP_TIME_STAMP);
            emitByte(compiler, r);
            return r;
        }
        case NODE_CAST:
            return compileCast(node, compiler);
        case NODE_UNARY:
            return compileUnary(node, compiler);
        case NODE_TERNARY:
            return compileTernary(node, compiler);
        default:
            compiler->hadError = true;
            return allocateRegister(compiler);
    }
}

bool compileNode(ASTNode* node, Compiler* compiler) {
    // Update current line/column for error reporting
    if (node) {
        compiler->currentLine = node->location.line;
        compiler->currentColumn = node->location.column;
    }
    
    switch (node->type) {
        case NODE_PROGRAM:
            for (int i = 0; i < node->program.count; i++) {
                if (!compileNode(node->program.declarations[i], compiler))
                    return false;
            }
            return true;

        case NODE_VAR_DECL: {
            int reg = compileExpr(node->varDecl.initializer, compiler);
            int idx = vm.variableCount++;
            ObjString* name = allocateString(node->varDecl.name,
                                             (int)strlen(node->varDecl.name));
            vm.variableNames[idx].name = name;
            vm.variableNames[idx].length = name->length;
            vm.globals[idx] = NIL_VAL;
            
            // Phase 3: Type Resolution
            Type* inferredType = NULL;
            Type* annotatedType = NULL;
            
            // Get inferred type from initializer expression
            if (node->varDecl.initializer) {
                if (node->varDecl.initializer->type == NODE_LITERAL) {
                    // Direct literal inference for the most common case
                    inferredType = infer_literal_type_extended(&node->varDecl.initializer->literal.value);
                } else {
                    // For other expressions, try to infer type
                    inferredType = getExprType(node->varDecl.initializer, compiler);
                }
            }
            
            // Get annotated type if provided
            if (node->varDecl.typeAnnotation) {
                const char* typeName = node->varDecl.typeAnnotation->typeAnnotation.name;
                if (strcmp(typeName, "i32") == 0) {
                    annotatedType = getPrimitiveType(TYPE_I32);
                } else if (strcmp(typeName, "i64") == 0) {
                    annotatedType = getPrimitiveType(TYPE_I64);
                } else if (strcmp(typeName, "u32") == 0) {
                    annotatedType = getPrimitiveType(TYPE_U32);
                } else if (strcmp(typeName, "u64") == 0) {
                    annotatedType = getPrimitiveType(TYPE_U64);
                } else if (strcmp(typeName, "f64") == 0) {
                    annotatedType = getPrimitiveType(TYPE_F64);
                } else if (strcmp(typeName, "bool") == 0) {
                    annotatedType = getPrimitiveType(TYPE_BOOL);
                } else if (strcmp(typeName, "string") == 0) {
                    annotatedType = getPrimitiveType(TYPE_STRING);
                } else {
                    annotatedType = getPrimitiveType(TYPE_ANY);
                }
            }
            
            // Type resolution logic
            if (annotatedType) {
                // Check if inferred type is compatible with annotated type
                if (inferredType && !type_assignable_to_extended(inferredType, annotatedType)) {
                    // Only allow compatible assignments or require explicit casting
                    if (!type_equals_extended(inferredType, annotatedType)) {
                        SrcLocation location = {vm.filePath, node->location.line, node->location.column};
                        report_type_mismatch(location, getTypeName(annotatedType->kind), getTypeName(inferredType->kind));
                        compiler->hadError = true;
                        vm.globalTypes[idx] = getPrimitiveType(TYPE_ANY);
                    } else {
                        vm.globalTypes[idx] = annotatedType;
                    }
                } else {
                    vm.globalTypes[idx] = annotatedType;
                }
            } else {
                // No annotation - use inferred type
                vm.globalTypes[idx] = inferredType ? inferredType : getPrimitiveType(TYPE_ANY);
            }
            
            // Set mutability based on declaration
            vm.mutableGlobals[idx] = node->varDecl.isMutable;
            
            symbol_table_set(&compiler->symbols, node->varDecl.name, idx, compiler->scopeDepth);
            emitByte(compiler, OP_STORE_GLOBAL);
            emitByte(compiler, (uint8_t)idx);
            emitByte(compiler, (uint8_t)reg);
            freeRegister(compiler, reg);
            return true;
        }

        case NODE_ASSIGN: {
            int reg = compileExpr(node->assign.value, compiler);
            int idx;
            
            // Check if variable exists - use different lookup based on context
            bool foundInCurrentScope = false;
            // ✨ FIX: Always use normal scope lookup to find variables in outer scopes
            // The previous logic broke assignment to outer-scope variables inside loops
            foundInCurrentScope = symbol_table_get_in_scope(&compiler->symbols, node->assign.name, compiler->scopeDepth, &idx);
            
            if (!foundInCurrentScope) {
                // Variable doesn't exist in current scope - create new one
                idx = vm.variableCount++;
                ObjString* name = allocateString(node->assign.name,
                                                 (int)strlen(node->assign.name));
                vm.variableNames[idx].name = name;
                vm.variableNames[idx].length = name->length;
                vm.globals[idx] = NIL_VAL;
                
                // For assignments without declaration, infer type from assigned expression
                Type* inferredType = getExprType(node->assign.value, compiler);
                vm.globalTypes[idx] = inferredType ? inferredType : getPrimitiveType(TYPE_ANY);
                
                // ✨ Auto-mutable inside loops for elegant syntax
                if (compiler->loopDepth > 0) {
                    // Inside loop: Variables are auto-mutable for convenience
                    vm.mutableGlobals[idx] = true;
                } else {
                    // Outside loop: Plain assignments create immutable variables
                    vm.mutableGlobals[idx] = false;
                }
                
                symbol_table_set(&compiler->symbols, node->assign.name, idx, compiler->scopeDepth);
            } else {
                // Variable exists in current scope - check if it's mutable
                if (!vm.mutableGlobals[idx]) {
                    SrcLocation location = {vm.filePath, node->location.line, node->location.column};
                    report_immutable_assignment(location, node->assign.name);
                    compiler->hadError = true;
                    freeRegister(compiler, reg);
                    return false;
                }
            }
            emitByte(compiler, OP_STORE_GLOBAL);
            emitByte(compiler, (uint8_t)idx);
            emitByte(compiler, (uint8_t)reg);
            freeRegister(compiler, reg);
            return true;
        }

        case NODE_PRINT: {
            for (int i = 0; i < node->print.count; i++) {
                int r = compileExpr(node->print.values[i], compiler);
                if (i == node->print.count - 1 && node->print.newline) {
                    emitByte(compiler, OP_PRINT_R);
                } else {
                    emitByte(compiler, OP_PRINT_NO_NL_R);
                }
                emitByte(compiler, (uint8_t)r);
                freeRegister(compiler, r);
            }
            if (node->print.count == 0 && node->print.newline) {
                uint8_t r = allocateRegister(compiler);
                emitByte(compiler, OP_LOAD_NIL);
                emitByte(compiler, r);
                emitByte(compiler, OP_PRINT_R);
                emitByte(compiler, r);
                freeRegister(compiler, r);
            }
            return true;
        }

        case NODE_IF: {
            // Optimization: Constant folding and dead code elimination
            if (isAlwaysTrue(node->ifStmt.condition)) {
                // Always true condition - compile only then branch, eliminate else
                return compileNode(node->ifStmt.thenBranch, compiler);
            } else if (isAlwaysFalse(node->ifStmt.condition)) {
                // Always false condition - compile only else branch, eliminate then
                if (node->ifStmt.elseBranch) {
                    return compileNode(node->ifStmt.elseBranch, compiler);
                }
                return true; // Empty else branch
            }
            
            // Non-constant condition - compile normally
            int conditionReg = compileExpr(node->ifStmt.condition, compiler);
            
            // Emit conditional jump - if condition is false, jump to else/end
            emitByte(compiler, OP_JUMP_IF_NOT_R);
            emitByte(compiler, (uint8_t)conditionReg);
            int thenJump = emitJump(compiler);
            
            freeRegister(compiler, conditionReg);
            
            // Compile then branch
            if (!compileNode(node->ifStmt.thenBranch, compiler)) return false;
            
            // Jump over else branch if we executed then branch
            int elseJump = -1;
            if (node->ifStmt.elseBranch) {
                emitByte(compiler, OP_JUMP);
                elseJump = emitJump(compiler);
            }
            
            // Patch the then jump to point here (start of else or end)
            patchJump(compiler, thenJump);
            
            // Compile else branch if it exists
            if (node->ifStmt.elseBranch) {
                if (!compileNode(node->ifStmt.elseBranch, compiler)) return false;
                patchJump(compiler, elseJump);
            }
            
            return true;
        }

        case NODE_WHILE: {
            // Enter loop context
            LoopContext loopCtx;
            loopCtx.breakJumps = jumptable_new();
            loopCtx.continueJumps = jumptable_new();
            loopCtx.scopeDepth = compiler->scopeDepth;
            loopCtx.label = node->whileStmt.label;
            loopCtx.loopVarIndex = -1;
            loopCtx.loopVarStartInstr = compiler->chunk->count;
            
            // Push loop context
            if (compiler->loopDepth >= 16) {
                // TODO: Use structured error reporting
                fprintf(stderr, "Error: Maximum loop nesting depth exceeded\n");
                return false;
            }
            compiler->loopStack[compiler->loopDepth++] = loopCtx;
            
            // Mark loop start for continue statements
            int loopStart = compiler->chunk->count;
            compiler->loopStack[compiler->loopDepth - 1].continueTarget = loopStart;
            
            // Compile condition
            int conditionReg = compileExpr(node->whileStmt.condition, compiler);
            
            // Emit conditional jump to exit loop if condition is false
            emitByte(compiler, OP_JUMP_IF_NOT_R);
            emitByte(compiler, (uint8_t)conditionReg);
            int exitJump = emitJump(compiler);
            
            freeRegister(compiler, conditionReg);
            
            // Compile body
            if (!compileNode(node->whileStmt.body, compiler)) {
                compiler->loopDepth--;
                return false;
            }
            
            // Jump back to loop start
            emitLoop(compiler, loopStart);
            
            // Patch exit jump to point here
            patchJump(compiler, exitJump);
            
            // Patch all break jumps BEFORE freeing jumptable and popping loop context
            LoopContext* patchLoop = &compiler->loopStack[compiler->loopDepth - 1];
            for (int i = 0; i < patchLoop->breakJumps.offsets.count; i++) {
                patchJump(compiler, patchLoop->breakJumps.offsets.data[i]);
            }
            
            // Cleanup
            jumptable_free(&patchLoop->breakJumps);
            jumptable_free(&patchLoop->continueJumps);
            compiler->loopDepth--;
            
            return true;
        }

        case NODE_FOR_RANGE: {
            // Try loop optimization first
            if (optimizeLoop(node, compiler)) {
                // Loop was optimized (e.g., unrolled), no need for regular compilation
                return true;
            }
            
            // Enter loop context
            LoopContext loopCtx;
            loopCtx.breakJumps = jumptable_new();
            loopCtx.continueJumps = jumptable_new();
            loopCtx.scopeDepth = compiler->scopeDepth;
            loopCtx.label = node->forRange.label;
            loopCtx.loopVarIndex = -1;
            loopCtx.loopVarStartInstr = compiler->chunk->count;
            
            // Push loop context
            if (compiler->loopDepth >= 16) {
                // TODO: Use structured error reporting
                fprintf(stderr, "Error: Maximum loop nesting depth exceeded\n");
                return false;
            }
            compiler->loopStack[compiler->loopDepth++] = loopCtx;
            
            // Begin new scope for loop variables
            compiler->scopeDepth++;
            symbol_table_begin_scope(&compiler->symbols, compiler->scopeDepth);
            
            // Allocate loop variable
            int loopVarReg = allocateRegister(compiler);
            
            // Initialize loop variable with start value
            int startReg = compileExpr(node->forRange.start, compiler);
            emitByte(compiler, OP_MOVE);
            emitByte(compiler, loopVarReg);
            emitByte(compiler, (uint8_t)startReg);
            freeRegister(compiler, startReg);
            
            // Get end value and adjust for inclusive ranges
            int endReg = compileExpr(node->forRange.end, compiler);
            
            // Adjust end value for inclusive ranges
            if (node->forRange.inclusive) {
                int adjustedEndReg = allocateRegister(compiler);
                int oneReg = allocateRegister(compiler);
                
                // Check if we have a negative step to determine adjustment direction
                bool isNegativeStep = false;
                if (node->forRange.step) {
                    if (node->forRange.step->type == NODE_LITERAL && 
                        node->forRange.step->literal.value.type == VAL_I32 &&
                        node->forRange.step->literal.value.as.i32 < 0) {
                        isNegativeStep = true;
                    } else if (node->forRange.step->type == NODE_UNARY &&
                               strcmp(node->forRange.step->unary.op, "-") == 0) {
                        // Unary minus operation (e.g., -1, -2)
                        isNegativeStep = true;
                    }
                }
                
                if (isNegativeStep) {
                    // For negative step: end = end - 1
                    emitConstant(compiler, oneReg, I32_VAL(1));
                    emitByte(compiler, OP_SUB_I32_R);
                } else {
                    // For positive step: end = end + 1
                    emitConstant(compiler, oneReg, I32_VAL(1));
                    emitByte(compiler, OP_ADD_I32_R);
                }
                
                emitByte(compiler, adjustedEndReg);
                emitByte(compiler, (uint8_t)endReg);
                emitByte(compiler, oneReg);
                
                freeRegister(compiler, endReg);
                freeRegister(compiler, oneReg);
                endReg = adjustedEndReg;
            }
            
            // Mark loop start for condition check
            int loopStart = compiler->chunk->count;
            
            // Check condition: loopVar < end (for positive step) or loopVar > end (for negative step)
            int conditionReg = allocateRegister(compiler);
            
            // Determine comparison operator based on step direction
            // We need to check if step is positive or negative
            if (node->forRange.step) {
                // Custom step - check if it's a negative value
                bool isNegativeStep = false;
                if (node->forRange.step->type == NODE_LITERAL && 
                    node->forRange.step->literal.value.type == VAL_I32 &&
                    node->forRange.step->literal.value.as.i32 < 0) {
                    isNegativeStep = true;
                } else if (node->forRange.step->type == NODE_UNARY &&
                           strcmp(node->forRange.step->unary.op, "-") == 0) {
                    // Unary minus operation (e.g., -1, -2)
                    isNegativeStep = true;
                }
                
                if (isNegativeStep) {
                    // For negative step: loopVar > end
                    emitByte(compiler, OP_GT_I32_R);
                } else {
                    // For positive step: loopVar < end
                    emitByte(compiler, OP_LT_I32_R);
                }
            } else {
                // Default step of 1 is always positive
                emitByte(compiler, OP_LT_I32_R);
            }
            
            emitByte(compiler, conditionReg);
            emitByte(compiler, loopVarReg);
            emitByte(compiler, (uint8_t)endReg);
            
            // Exit if condition is false
            emitByte(compiler, OP_JUMP_IF_NOT_R);
            emitByte(compiler, conditionReg);
            int exitJump = emitJump(compiler);
            
            freeRegister(compiler, conditionReg);
            
            // Use register-based local variable for loop variable instead of global
            // Add to symbol table with negative index to indicate register-based variable
            symbol_table_set(&compiler->symbols, node->forRange.varName, -(loopVarReg + 1), compiler->scopeDepth);
            
            // No need to store to global - loop variable stays in register
            // The loop variable register (loopVarReg) already contains the current value
            
            // Set continueTarget to increment code (to be emitted after body)
            int continueTarget = -1;
            compiler->loopStack[compiler->loopDepth - 1].continueTarget = -1; // will set after body
            
            // Compile body
            if (!compileNode(node->forRange.body, compiler)) {
                compiler->loopDepth--;
                return false;
            }
            // Place increment code here (continueTarget)
            continueTarget = compiler->chunk->count;
            compiler->loopStack[compiler->loopDepth - 1].continueTarget = continueTarget;
            
            // Patch all continue jumps to point to increment code
            LoopContext* continueLoop = &compiler->loopStack[compiler->loopDepth - 1];
            for (int i = 0; i < continueLoop->continueJumps.offsets.count; i++) {
                patchJump(compiler, continueLoop->continueJumps.offsets.data[i]);
            }
            
            if (node->forRange.step) {
                // Use custom step
                int stepReg = compileExpr(node->forRange.step, compiler);
                emitByte(compiler, OP_ADD_I32_R);
                emitByte(compiler, loopVarReg);
                emitByte(compiler, loopVarReg);
                emitByte(compiler, (uint8_t)stepReg);
                freeRegister(compiler, stepReg);
            } else {
                // Default step of 1
                int oneReg = allocateRegister(compiler);
                emitConstant(compiler, oneReg, I32_VAL(1));
                emitByte(compiler, OP_ADD_I32_R);
                emitByte(compiler, loopVarReg);
                emitByte(compiler, loopVarReg);
                emitByte(compiler, oneReg);
                freeRegister(compiler, oneReg);
            }
            // Jump back to condition check
            emitLoop(compiler, loopStart);
            
            // Patch exit jump
            patchJump(compiler, exitJump);
            
            // Patch all break jumps BEFORE freeing jumptable and popping loop context
            LoopContext* patchLoop = &compiler->loopStack[compiler->loopDepth - 1];
            for (int i = 0; i < patchLoop->breakJumps.offsets.count; i++) {
                patchJump(compiler, patchLoop->breakJumps.offsets.data[i]);
            }
            
            // Cleanup scope and remove loop variables  
            symbol_table_end_scope(&compiler->symbols, compiler->scopeDepth);
            compiler->scopeDepth--;
            
            // Free the loop variable register and end register
            freeRegister(compiler, loopVarReg);
            freeRegister(compiler, endReg);
            
            // ✨ LICM: Deactivate expression replacements when exiting FOR_RANGE loop
            disableLICMReplacements();
            
            // Cleanup
            jumptable_free(&patchLoop->breakJumps);
            jumptable_free(&patchLoop->continueJumps);
            compiler->loopDepth--;
            
            return true;
        }

        case NODE_BLOCK: {
            // Compile all statements in the block
            for (int i = 0; i < node->block.count; i++) {
                if (!compileNode(node->block.statements[i], compiler)) {
                    return false;
                }
            }
            return true;
        }

        case NODE_BREAK: {
            // Find the target loop context
            LoopContext* loop = NULL;
            if (node->breakStmt.label) {
                // Find labeled loop
                for (int i = compiler->loopDepth - 1; i >= 0; i--) {
                    if (compiler->loopStack[i].label && 
                        strcmp(compiler->loopStack[i].label, node->breakStmt.label) == 0) {
                        loop = &compiler->loopStack[i];
                        break;
                    }
                }
                if (!loop) {
                    // TODO: Use structured error reporting
                    fprintf(stderr, "Error: Undefined label '%s' in break statement\n", node->breakStmt.label);
                    return false;
                }
            } else {
                // Find nearest loop
                if (compiler->loopDepth == 0) {
                    // TODO: Use structured error reporting
                    fprintf(stderr, "Error: break statement outside of loop\n");
                    return false;
                }
                loop = &compiler->loopStack[compiler->loopDepth - 1];
            }
            
            // Add jump to break table for later patching
            emitByte(compiler, OP_JUMP);
            int breakJump = emitJump(compiler);
            jumptable_add(&loop->breakJumps, breakJump);
            
            return true;
        }

        case NODE_CONTINUE: {
            // Find the target loop context
            LoopContext* loop = NULL;
            if (node->continueStmt.label) {
                // Find labeled loop
                for (int i = compiler->loopDepth - 1; i >= 0; i--) {
                    if (compiler->loopStack[i].label && 
                        strcmp(compiler->loopStack[i].label, node->continueStmt.label) == 0) {
                        loop = &compiler->loopStack[i];
                        break;
                    }
                }
                if (!loop) {
                    // TODO: Use structured error reporting
                    fprintf(stderr, "Error: Undefined label '%s' in continue statement\n", node->continueStmt.label);
                    return false;
                }
            } else {
                // Find nearest loop
                if (compiler->loopDepth == 0) {
                    // TODO: Use structured error reporting
                    fprintf(stderr, "Error: continue statement outside of loop\n");
                    return false;
                }
                loop = &compiler->loopStack[compiler->loopDepth - 1];
            }
            
            // Check if continue target is already set (for while loops)
            if (loop->continueTarget != -1) {
                // Jump to continue target (loop start)
                emitLoop(compiler, loop->continueTarget);
            } else {
                // For loops where continue target is not yet set, add jump to continue table
                emitByte(compiler, OP_JUMP);
                int continueJump = emitJump(compiler);
                jumptable_add(&loop->continueJumps, continueJump);
            }
            
            return true;
        }

        case NODE_FOR_ITER: {
            // Enter loop context
            LoopContext loopCtx;
            loopCtx.breakJumps = jumptable_new();
            loopCtx.continueJumps = jumptable_new();
            loopCtx.scopeDepth = compiler->scopeDepth;
            loopCtx.label = node->forIter.label;
            loopCtx.loopVarIndex = -1;
            loopCtx.loopVarStartInstr = compiler->chunk->count;
            
            // Push loop context
            if (compiler->loopDepth >= 16) {
                // TODO: Use structured error reporting
                fprintf(stderr, "Error: Maximum loop nesting depth exceeded\n");
                return false;
            }
            compiler->loopStack[compiler->loopDepth++] = loopCtx;
            
            // Begin new scope for loop variables
            compiler->scopeDepth++;
            symbol_table_begin_scope(&compiler->symbols, compiler->scopeDepth);
            
            // Compile iterable expression
            int iterableReg = compileExpr(node->forIter.iterable, compiler);
            
            // Create iterator from iterable
            int iteratorReg = allocateRegister(compiler);
            emitByte(compiler, OP_GET_ITER_R);
            emitByte(compiler, iteratorReg);
            emitByte(compiler, iterableReg);
            freeRegister(compiler, iterableReg);
            
            // Allocate loop variable register
            int loopVarReg = allocateRegister(compiler);
            
            // TODO: Add loop variable to symbol table for proper scoping
            // For now, store in global variable (needs proper scoping)
            int varIdx = vm.variableCount++;
            ObjString* varName = allocateString(node->forIter.varName, 
                                              (int)strlen(node->forIter.varName));
            vm.variableNames[varIdx].name = varName;
            vm.variableNames[varIdx].length = varName->length;
            vm.globals[varIdx] = NIL_VAL;
            
            // Add to symbol table so it can be found during compilation
            symbol_table_set(&compiler->symbols, node->forIter.varName, varIdx, compiler->scopeDepth);
            
            // Set type for loop variable (iterator type depends on iterable)
            vm.globalTypes[varIdx] = getPrimitiveType(TYPE_ANY);
            
            // Loop start (continue target)
            loopCtx.continueTarget = compiler->chunk->count;
            
            // Update the stack with the continue target
            compiler->loopStack[compiler->loopDepth - 1].continueTarget = loopCtx.continueTarget;
            
            // Patch all continue jumps to point to loop start
            LoopContext* continueLoop2 = &compiler->loopStack[compiler->loopDepth - 1];
            for (int i = 0; i < continueLoop2->continueJumps.offsets.count; i++) {
                patchJump(compiler, continueLoop2->continueJumps.offsets.data[i]);
            }
            
            // Get next value from iterator
            int hasNextReg = allocateRegister(compiler);
            emitByte(compiler, OP_ITER_NEXT_R);
            emitByte(compiler, loopVarReg);
            emitByte(compiler, iteratorReg);
            emitByte(compiler, hasNextReg);
            
            // Store loop variable in global for access in body
            emitByte(compiler, OP_STORE_GLOBAL);
            emitByte(compiler, (uint8_t)varIdx);
            emitByte(compiler, loopVarReg);
            
            // Exit loop if no more values
            emitByte(compiler, OP_JUMP_IF_NOT_R);
            emitByte(compiler, hasNextReg);
            int exitJump = emitJump(compiler);
            
            freeRegister(compiler, hasNextReg);
            
            // Compile loop body
            if (!compileNode(node->forIter.body, compiler)) {
                compiler->loopDepth--;
                return false;
            }
            
            // Jump back to loop start
            emitLoop(compiler, loopCtx.continueTarget);
            
            // Patch exit jump
            patchJump(compiler, exitJump);
            
            // Patch all break jumps BEFORE freeing jumptable and popping loop context
            LoopContext* patchLoop = &compiler->loopStack[compiler->loopDepth - 1];
            for (int i = 0; i < patchLoop->breakJumps.offsets.count; i++) {
                patchJump(compiler, patchLoop->breakJumps.offsets.data[i]);
            }
            
            // Cleanup scope and remove loop variables  
            symbol_table_end_scope(&compiler->symbols, compiler->scopeDepth);
            compiler->scopeDepth--;
            
            // Cleanup
            jumptable_free(&patchLoop->breakJumps);
            jumptable_free(&patchLoop->continueJumps);
            compiler->loopDepth--;
            
            return true;
        }

        default:
            compileExpr(node, compiler);
            return !compiler->hadError;
    }
}

bool compile(ASTNode* ast, Compiler* compiler, bool isModule) {
    (void)isModule;
    compiler->hadError = false;
    return compileNode(ast, compiler) && !compiler->hadError;
}
