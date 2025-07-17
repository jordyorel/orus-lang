// Author: Hierat
// Date: 2023-10-01
// Description: A single pass compiler for the Orus language, handling AST compilation to bytecode.



#include "compiler/compiler.h"
#include "runtime/memory.h"
#include "compiler/symbol_table.h"
#include "type/type.h"
#include "internal/error_reporting.h"
#include "errors/features/type_errors.h"
#include <string.h>

static bool compileNode(ASTNode* node, Compiler* compiler);
static int compileExpr(ASTNode* node, Compiler* compiler);
static Type* getExprType(ASTNode* node, Compiler* compiler);

// Expression compilation handlers
static int compileLiteral(ASTNode* node, Compiler* compiler);
static int compileIdentifier(ASTNode* node, Compiler* compiler);
static int compileBinaryOp(ASTNode* node, Compiler* compiler);
static int compileCast(ASTNode* node, Compiler* compiler);
static int compileUnary(ASTNode* node, Compiler* compiler);

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
    compiler->hadError = false;
    compiler->currentLine = 1;     // Initialize line tracking
    compiler->currentColumn = 1;   // Initialize column tracking
    symbol_table_init(&compiler->symbols);
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

// Helper function to get the type of an expression
static Type* getExprType(ASTNode* node, Compiler* compiler) {
    if (!node) return getPrimitiveType(TYPE_UNKNOWN);
    
    switch (node->type) {
        case NODE_LITERAL:
            return infer_literal_type_extended(&node->literal.value);
        case NODE_IDENTIFIER: {
            int index;
            if (symbol_table_get(&compiler->symbols, node->identifier.name, &index)) {
                return vm.globalTypes[index];
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
    if (!symbol_table_get(&compiler->symbols, node->identifier.name, &index)) {
        compiler->hadError = true;
        return allocateRegister(compiler);
    }
    uint8_t r = allocateRegister(compiler);
    emitByte(compiler, OP_LOAD_GLOBAL);
    emitByte(compiler, r);
    emitByte(compiler, (uint8_t)index);
    return r;
}

static int compileBinaryOp(ASTNode* node, Compiler* compiler) {
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
    int operand = compileExpr(node->unary.operand, compiler);
    uint8_t dst = allocateRegister(compiler);
    
    if (strcmp(node->unary.op, "not") == 0) {
        emitByte(compiler, OP_NOT_BOOL_R);
        emitByte(compiler, dst);
        emitByte(compiler, (uint8_t)operand);
    } else if (strcmp(node->unary.op, "-") == 0) {
        // Use proper single-pass negation opcode (in-place operation)
        emitByte(compiler, OP_MOVE);  // First copy operand to destination
        emitByte(compiler, dst);
        emitByte(compiler, (uint8_t)operand);
        emitByte(compiler, OP_NEG_I32_R);  // Then negate in-place (single register)
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

static int compileExpr(ASTNode* node, Compiler* compiler) {
    // Update current line/column for error reporting
    if (node) {
        compiler->currentLine = node->location.line;
        compiler->currentColumn = node->location.column;
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
        default:
            compiler->hadError = true;
            return allocateRegister(compiler);
    }
}

static bool compileNode(ASTNode* node, Compiler* compiler) {
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
            
            symbol_table_set(&compiler->symbols, node->varDecl.name, idx);
            emitByte(compiler, OP_STORE_GLOBAL);
            emitByte(compiler, (uint8_t)idx);
            emitByte(compiler, (uint8_t)reg);
            freeRegister(compiler, reg);
            return true;
        }

        case NODE_ASSIGN: {
            int reg = compileExpr(node->assign.value, compiler);
            int idx;
            if (!symbol_table_get(&compiler->symbols, node->assign.name, &idx)) {
                // Variable doesn't exist - create new one (immutable by default for plain assignments)
                idx = vm.variableCount++;
                ObjString* name = allocateString(node->assign.name,
                                                 (int)strlen(node->assign.name));
                vm.variableNames[idx].name = name;
                vm.variableNames[idx].length = name->length;
                vm.globals[idx] = NIL_VAL;
                // For assignments without declaration, infer type from assigned expression
                Type* inferredType = getExprType(node->assign.value, compiler);
                vm.globalTypes[idx] = inferredType ? inferredType : getPrimitiveType(TYPE_ANY);
                vm.mutableGlobals[idx] = false; // Plain assignments create immutable variables
                symbol_table_set(&compiler->symbols, node->assign.name, idx);
            } else {
                // Variable exists - check if it's mutable
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
            // Compile condition
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
            // Mark loop start for continue statements
            int loopStart = compiler->chunk->count;
            
            // Compile condition
            int conditionReg = compileExpr(node->whileStmt.condition, compiler);
            
            // Emit conditional jump to exit loop if condition is false
            emitByte(compiler, OP_JUMP_IF_NOT_R);
            emitByte(compiler, (uint8_t)conditionReg);
            int exitJump = emitJump(compiler);
            
            freeRegister(compiler, conditionReg);
            
            // Compile body
            if (!compileNode(node->whileStmt.body, compiler)) return false;
            
            // Jump back to loop start
            emitLoop(compiler, loopStart);
            
            // Patch exit jump to point here
            patchJump(compiler, exitJump);
            
            return true;
        }

        case NODE_FOR_RANGE: {
            // For now, implement basic for loop as while loop equivalent
            // TODO: Implement proper range iterator support
            
            // Allocate loop variable
            int loopVarReg = allocateRegister(compiler);
            
            // Initialize loop variable with start value
            int startReg = compileExpr(node->forRange.start, compiler);
            emitByte(compiler, OP_MOVE);
            emitByte(compiler, loopVarReg);
            emitByte(compiler, (uint8_t)startReg);
            freeRegister(compiler, startReg);
            
            // Get end value
            int endReg = compileExpr(node->forRange.end, compiler);
            
            // Mark loop start
            int loopStart = compiler->chunk->count;
            
            // Check condition: loopVar < end
            int conditionReg = allocateRegister(compiler);
            emitByte(compiler, OP_LT_I32_R);
            emitByte(compiler, conditionReg);
            emitByte(compiler, loopVarReg);
            emitByte(compiler, (uint8_t)endReg);
            
            // Exit if condition is false
            emitByte(compiler, OP_JUMP_IF_NOT_R);
            emitByte(compiler, conditionReg);
            int exitJump = emitJump(compiler);
            
            freeRegister(compiler, conditionReg);
            
            // Store loop variable in global for access in body
            // TODO: Implement proper scoped variables
            int varIdx = vm.variableCount++;
            ObjString* varName = allocateString(node->forRange.varName, 
                                              (int)strlen(node->forRange.varName));
            vm.variableNames[varIdx].name = varName;
            vm.variableNames[varIdx].length = varName->length;
            vm.globals[varIdx] = NIL_VAL;
            
            emitByte(compiler, OP_STORE_GLOBAL);
            emitByte(compiler, (uint8_t)varIdx);
            emitByte(compiler, loopVarReg);
            
            // Compile body
            if (!compileNode(node->forRange.body, compiler)) return false;
            
            // Increment loop variable
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
            
            freeRegister(compiler, endReg);
            freeRegister(compiler, loopVarReg);
            
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
