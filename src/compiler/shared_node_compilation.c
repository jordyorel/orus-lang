// shared_node_compilation.c
// Author: Hierat
// Date: 2023-10-01
// Description: Shared node compilation logic to reduce duplication between single-pass and multi-pass compilers

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler/shared_node_compilation.h"
#include "compiler/symbol_table.h"
#include "compiler/lexer.h"
#include "errors/features/type_errors.h"
#include "errors/features/variable_errors.h"
#include "internal/error_reporting.h"
#include "type/type.h"
#include "vm/register_file.h"
#include "vm/vm_constants.h"
#include "vm/vm_config.h"

// Helper function declarations (these are implemented in hybride_compiler.c)
static int findLocal(Compiler* compiler, const char* name);
static int addLocal(Compiler* compiler, const char* name, bool isAssigned);

// Missing function declarations
int addConstant(Chunk* chunk, Value value);
void writeChunk(Chunk* chunk, uint8_t byte, int line, int column);

// Context creation functions
CompilerContext createSinglePassContext(void) {
    CompilerContext ctx = {0};
    ctx.supportsBreakContinue = false;
    ctx.supportsFunctions = false;
    ctx.enableOptimizations = false;
    ctx.vmOptCtx = NULL;
    return ctx;
}

CompilerContext createMultiPassContext(VMOptimizationContext* vmOptCtx) {
    CompilerContext ctx = {0};
    ctx.supportsBreakContinue = true;
    ctx.supportsFunctions = true;
    ctx.enableOptimizations = true;
    ctx.vmOptCtx = vmOptCtx;
    return ctx;
}

// Shared literal compilation
int compileSharedLiteral(ASTNode* node, Compiler* compiler, CompilerContext* ctx) {
    if (!node || node->type != NODE_LITERAL) {
        return -1;
    }

    uint8_t reg;
    if (ctx->enableOptimizations && ctx->vmOptCtx) {
        // Use optimized register allocation
        extern RegisterState g_regState;
        reg = allocateOptimalRegister(&g_regState, ctx->vmOptCtx, false, 10);
        if (reg == (uint8_t)-1) {
            reg = allocateRegister(compiler);
        }
    } else {
        reg = allocateRegister(compiler);
    }

    emitConstant(compiler, reg, node->literal.value);
    return reg;
}

// Shared binary operation compilation
int compileSharedBinaryOp(ASTNode* node, Compiler* compiler, CompilerContext* ctx) {
    if (!node || node->type != NODE_BINARY) {
        return -1;
    }

    // Compile operands
    int leftReg = compileSharedNode(node->binary.left, compiler, ctx) ? 
                  compiler->nextRegister - 1 : -1;
    if (leftReg == -1) return -1;

    int rightReg = compileSharedNode(node->binary.right, compiler, ctx) ? 
                   compiler->nextRegister - 1 : -1;
    if (rightReg == -1) {
        freeRegister(compiler, leftReg);
        return -1;
    }

    // Allocate result register
    uint8_t resultReg;
    if (ctx->enableOptimizations && ctx->vmOptCtx) {
        extern RegisterState g_regState;
        resultReg = allocateOptimalRegister(&g_regState, ctx->vmOptCtx, false, 20);
        if (resultReg == (uint8_t)-1) {
            resultReg = allocateRegister(compiler);
        }
    } else {
        resultReg = allocateRegister(compiler);
    }

    // Emit operation based on operator using centralized opcodes
    const char* op = node->binary.op;
    if (strcmp(op, "+") == 0) {
        emitByte(compiler, OPCODE_ADD_I32_R);
    } else if (strcmp(op, "-") == 0) {
        emitByte(compiler, OPCODE_SUB_I32_R);
    } else if (strcmp(op, "*") == 0) {
        emitByte(compiler, OPCODE_MUL_I32_R);
    } else if (strcmp(op, "/") == 0) {
        emitByte(compiler, OPCODE_DIV_I32_R);
    } else if (strcmp(op, "==") == 0) {
        emitByte(compiler, OPCODE_EQ_I32_R);
    } else if (strcmp(op, "!=") == 0) {
        emitByte(compiler, OPCODE_NE_I32_R);
    } else if (strcmp(op, ">") == 0) {
        emitByte(compiler, OPCODE_GT_I32_R);
    } else if (strcmp(op, ">=") == 0) {
        emitByte(compiler, OPCODE_GE_I32_R);
    } else if (strcmp(op, "<") == 0) {
        emitByte(compiler, OPCODE_LT_I32_R);
    } else if (strcmp(op, "<=") == 0) {
        emitByte(compiler, OPCODE_LE_I32_R);
    } else {
        freeRegister(compiler, leftReg);
        freeRegister(compiler, rightReg);
        freeRegister(compiler, resultReg);
        return -1;
    }

    emitByte(compiler, resultReg);
    emitByte(compiler, leftReg);
    emitByte(compiler, rightReg);

    // Free operand registers
    freeRegister(compiler, leftReg);
    freeRegister(compiler, rightReg);

    return resultReg;
}

// Shared variable declaration compilation
int compileSharedVarDecl(ASTNode* node, Compiler* compiler, CompilerContext* ctx) {
    if (!node || node->type != NODE_VAR_DECL) {
        return -1;
    }

    // Check if variable already exists
    int existing = findLocal(compiler, node->varDecl.name);
    if (existing != -1) {
        SrcLocation loc = {.file = compiler->fileName, .line = node->location.line, .column = node->location.column};
        report_variable_redefinition(loc, node->varDecl.name, node->location.line);
        return -1;
    }

    // Compile initializer if present
    uint8_t valueReg = 0;
    if (node->varDecl.initializer) {
        if (!compileSharedNode(node->varDecl.initializer, compiler, ctx)) {
            return -1;
        }
        valueReg = compiler->nextRegister - 1;
    } else {
        // Default initialize based on type
        valueReg = allocateRegister(compiler);
        Value defaultVal = {.type = VAL_I32, .as.i32 = 0};
        emitConstant(compiler, valueReg, defaultVal);
    }

    // Add to local variables
    int localIndex = addLocal(compiler, node->varDecl.name, true);
    if (localIndex < 0) {
        SrcLocation loc = {.file = compiler->fileName, .line = node->location.line, .column = node->location.column};
        report_compile_error(E1009_EXPRESSION_TOO_COMPLEX, loc, "Too many local variables");
        freeRegister(compiler, valueReg);
        return -1;
    }

    compiler->locals[localIndex].reg = valueReg;
    return valueReg;
}

// Shared assignment compilation
int compileSharedAssignment(ASTNode* node, Compiler* compiler, CompilerContext* ctx) {
    if (!node || node->type != NODE_ASSIGN) {
        return -1;
    }

    // Find the variable
    int localIndex = findLocal(compiler, node->assign.name);
    if (localIndex == -1) {
        SrcLocation loc = {.file = compiler->fileName, .line = node->location.line, .column = node->location.column};
        report_undefined_variable(loc, node->assign.name);
        return -1;
    }

    // Compile the value expression
    if (!compileSharedNode(node->assign.value, compiler, ctx)) {
        return -1;
    }

    uint8_t valueReg = compiler->nextRegister - 1;
    uint8_t targetReg = compiler->locals[localIndex].reg;

    // Move value to target register if different
    if (valueReg != targetReg) {
        emitByte(compiler, OPCODE_MOVE_R);
        emitByte(compiler, targetReg);
        emitByte(compiler, valueReg);
        freeRegister(compiler, valueReg);
    }

    return targetReg;
}

// Shared if statement compilation
int compileSharedIfStatement(ASTNode* node, Compiler* compiler, CompilerContext* ctx) {
    if (!node || node->type != NODE_IF) {
        return -1;
    }

    // Compile condition
    if (!compileSharedNode(node->ifStmt.condition, compiler, ctx)) {
        return -1;
    }

    uint8_t condReg = compiler->nextRegister - 1;

    // Jump if false
    emitByte(compiler, OPCODE_JUMP_IF_FALSE_R);
    emitByte(compiler, condReg);
    int jumpIfFalse = compiler->chunk->count;
    emitByte(compiler, 0xFF); // Placeholder for jump offset
    emitByte(compiler, 0xFF);

    freeRegister(compiler, condReg);

    // Compile then branch
    if (!compileSharedNode(node->ifStmt.thenBranch, compiler, ctx)) {
        return -1;
    }

    // Jump over else branch
    int jumpOverElse = -1;
    if (node->ifStmt.elseBranch) {
        emitByte(compiler, OPCODE_JUMP);
        jumpOverElse = compiler->chunk->count;
        emitByte(compiler, 0xFF); // Placeholder
        emitByte(compiler, 0xFF);
    }

    // Patch jump if false
    int elseStart = compiler->chunk->count;
    int jumpIfFalseOffset = elseStart - jumpIfFalse - 2;
    compiler->chunk->code[jumpIfFalse] = (jumpIfFalseOffset >> 8) & 0xFF;
    compiler->chunk->code[jumpIfFalse + 1] = jumpIfFalseOffset & 0xFF;

    // Compile else branch
    if (node->ifStmt.elseBranch) {
        if (!compileSharedNode(node->ifStmt.elseBranch, compiler, ctx)) {
            return -1;
        }

        // Patch jump over else
        int afterElse = compiler->chunk->count;
        int jumpOverElseOffset = afterElse - jumpOverElse - 2;
        compiler->chunk->code[jumpOverElse] = (jumpOverElseOffset >> 8) & 0xFF;
        compiler->chunk->code[jumpOverElse + 1] = jumpOverElseOffset & 0xFF;
    }

    return 0; // Statements don't return registers
}

// Shared while loop compilation
int compileSharedWhileLoop(ASTNode* node, Compiler* compiler, CompilerContext* ctx) {
    if (!node || node->type != NODE_WHILE) {
        return -1;
    }

    int loopStart = compiler->chunk->count;

    // Compile condition
    if (!compileSharedNode(node->whileStmt.condition, compiler, ctx)) {
        return -1;
    }

    uint8_t condReg = compiler->nextRegister - 1;

    // Jump if false (exit loop)
    emitByte(compiler, OPCODE_JUMP_IF_FALSE_R);
    emitByte(compiler, condReg);
    int exitJump = compiler->chunk->count;
    emitByte(compiler, 0xFF); // Placeholder
    emitByte(compiler, 0xFF);

    freeRegister(compiler, condReg);

    // Compile loop body
    if (!compileSharedNode(node->whileStmt.body, compiler, ctx)) {
        return -1;
    }

    // Jump back to loop start
    emitByte(compiler, OPCODE_JUMP);
    int backJumpOffset = loopStart - compiler->chunk->count - 3;
    emitByte(compiler, (backJumpOffset >> 8) & 0xFF);
    emitByte(compiler, backJumpOffset & 0xFF);

    // Patch exit jump
    int afterLoop = compiler->chunk->count;
    int exitJumpOffset = afterLoop - exitJump - 2;
    compiler->chunk->code[exitJump] = (exitJumpOffset >> 8) & 0xFF;
    compiler->chunk->code[exitJump + 1] = exitJumpOffset & 0xFF;

    return 0;
}

// Shared for loop compilation
int compileSharedForLoop(ASTNode* node, Compiler* compiler, CompilerContext* ctx) {
    if (!node || node->type != NODE_FOR_RANGE) {
        return -1;
    }

    // Compile start value
    if (!compileSharedNode(node->forRange.start, compiler, ctx)) {
        return -1;
    }
    uint8_t startReg = compiler->nextRegister - 1;

    // Compile end value
    if (!compileSharedNode(node->forRange.end, compiler, ctx)) {
        freeRegister(compiler, startReg);
        return -1;
    }
    uint8_t endReg = compiler->nextRegister - 1;

    // Create loop variable
    int loopVarIndex = addLocal(compiler, node->forRange.varName, false);
    if (loopVarIndex < 0) {
        SrcLocation loc = {.file = compiler->fileName, .line = node->location.line, .column = node->location.column};
        report_compile_error(E1009_EXPRESSION_TOO_COMPLEX, loc, "Too many local variables");
        freeRegister(compiler, startReg);
        freeRegister(compiler, endReg);
        return -1;
    }

    uint8_t iterReg;
    if (ctx->enableOptimizations && ctx->vmOptCtx) {
        extern RegisterState g_regState;
        iterReg = allocateOptimalRegister(&g_regState, ctx->vmOptCtx, true, 100); // Loop variable, long lifetime
        if (iterReg == (uint8_t)-1) {
            iterReg = allocateRegister(compiler);
        }
    } else {
        iterReg = allocateRegister(compiler);
    }

    compiler->locals[loopVarIndex].reg = iterReg;

    // Initialize iterator
    emitByte(compiler, OPCODE_MOVE_R);
    emitByte(compiler, iterReg);
    emitByte(compiler, startReg);

    int loopStart = compiler->chunk->count;

    // Check loop condition (iter < end)
    emitByte(compiler, OPCODE_LT_I32_R);
    uint8_t condReg = allocateRegister(compiler);
    emitByte(compiler, condReg);
    emitByte(compiler, iterReg);
    emitByte(compiler, endReg);

    // Jump if false (exit loop)
    emitByte(compiler, OPCODE_JUMP_IF_FALSE_R);
    emitByte(compiler, condReg);
    int exitJump = compiler->chunk->count;
    emitByte(compiler, 0xFF); // Placeholder
    emitByte(compiler, 0xFF);

    freeRegister(compiler, condReg);

    // Compile loop body
    if (!compileSharedNode(node->forRange.body, compiler, ctx)) {
        freeRegister(compiler, startReg);
        freeRegister(compiler, endReg);
        freeRegister(compiler, iterReg);
        return -1;
    }

    // Increment iterator
    emitByte(compiler, OPCODE_ADD_I32_R);
    emitByte(compiler, iterReg);
    emitByte(compiler, iterReg);
    
    // Load constant 1 for increment
    uint8_t oneReg = allocateRegister(compiler);
    Value oneVal = {.type = VAL_I32, .as.i32 = 1};
    emitConstant(compiler, oneReg, oneVal);
    emitByte(compiler, oneReg);
    freeRegister(compiler, oneReg);

    // Jump back to loop start
    emitByte(compiler, OPCODE_JUMP);
    int backJumpOffset = loopStart - compiler->chunk->count - 3;
    emitByte(compiler, (backJumpOffset >> 8) & 0xFF);
    emitByte(compiler, backJumpOffset & 0xFF);

    // Patch exit jump
    int afterLoop = compiler->chunk->count;
    int exitJumpOffset = afterLoop - exitJump - 2;
    compiler->chunk->code[exitJump] = (exitJumpOffset >> 8) & 0xFF;
    compiler->chunk->code[exitJump + 1] = exitJumpOffset & 0xFF;

    // Clean up registers
    freeRegister(compiler, startReg);
    freeRegister(compiler, endReg);
    freeRegister(compiler, iterReg);

    // Remove loop variable from scope
    compiler->localCount--;

    return 0;
}

// Shared block compilation
int compileSharedBlock(ASTNode* node, Compiler* compiler, CompilerContext* ctx) {
    if (!node || node->type != NODE_BLOCK) {
        return -1;
    }

    int savedLocalCount = compiler->localCount;

    for (int i = 0; i < node->block.count; i++) {
        if (!compileSharedNode(node->block.statements[i], compiler, ctx)) {
            // Restore local count on error
            compiler->localCount = savedLocalCount;
            return -1;
        }
    }

    // Pop locals from this block
    while (compiler->localCount > savedLocalCount) {
        compiler->localCount--;
        if (compiler->locals[compiler->localCount].reg != 0) {
            freeRegister(compiler, compiler->locals[compiler->localCount].reg);
        }
    }

    return 0;
}

// Shared cast compilation
int compileSharedCast(ASTNode* node, Compiler* compiler, CompilerContext* ctx) {
    if (!node || node->type != NODE_CAST) {
        return -1;
    }

    // Compile the expression to be cast
    if (!compileSharedNode(node->cast.expression, compiler, ctx)) {
        return -1;
    }

    uint8_t sourceReg = compiler->nextRegister - 1;
    uint8_t targetReg;

    if (ctx->enableOptimizations && ctx->vmOptCtx) {
        extern RegisterState g_regState;
        targetReg = allocateOptimalRegister(&g_regState, ctx->vmOptCtx, false, 15);
        if (targetReg == (uint8_t)-1) {
            targetReg = allocateRegister(compiler);
        }
    } else {
        targetReg = allocateRegister(compiler);
    }

    // Emit cast instruction based on target type using centralized opcodes  
    ASTNode* targetTypeNode = node->cast.targetType;
    if (targetTypeNode && targetTypeNode->type == NODE_TYPE) {
        const char* typeName = targetTypeNode->typeAnnotation.name;
        if (strcmp(typeName, "string") == 0) {
            emitByte(compiler, OPCODE_TO_STRING_R);
        } else if (strcmp(typeName, "i32") == 0) {
            emitByte(compiler, OPCODE_TO_I32_R);
        } else if (strcmp(typeName, "i64") == 0) {
            emitByte(compiler, OPCODE_TO_I64_R);
        } else if (strcmp(typeName, "f64") == 0) {
            emitByte(compiler, OPCODE_TO_F64_R);
        } else if (strcmp(typeName, "bool") == 0) {
            emitByte(compiler, OPCODE_TO_BOOL_R);
        } else {
            freeRegister(compiler, sourceReg);
            freeRegister(compiler, targetReg);
            return -1;
        }
    } else {
        freeRegister(compiler, sourceReg);
        freeRegister(compiler, targetReg);
        return -1;
    }

    emitByte(compiler, targetReg);
    emitByte(compiler, sourceReg);

    freeRegister(compiler, sourceReg);
    return targetReg;
}

// Main shared node compilation dispatch function
bool compileSharedNode(ASTNode* node, Compiler* compiler, CompilerContext* ctx) {
    if (!node || !compiler || !ctx) {
        return false;
    }

    switch (node->type) {
        case NODE_LITERAL:
            return compileSharedLiteral(node, compiler, ctx) >= 0;
        case NODE_BINARY:
            return compileSharedBinaryOp(node, compiler, ctx) >= 0;
        case NODE_VAR_DECL:
            return compileSharedVarDecl(node, compiler, ctx) >= 0;
        case NODE_ASSIGN:
            return compileSharedAssignment(node, compiler, ctx) >= 0;
        case NODE_IF:
            return compileSharedIfStatement(node, compiler, ctx) >= 0;
        case NODE_WHILE:
            return compileSharedWhileLoop(node, compiler, ctx) >= 0;
        case NODE_FOR_RANGE:
            return compileSharedForLoop(node, compiler, ctx) >= 0;
        case NODE_BLOCK:
            return compileSharedBlock(node, compiler, ctx) >= 0;
        case NODE_CAST:
            return compileSharedCast(node, compiler, ctx) >= 0;
        case NODE_IDENTIFIER: {
            // Variable access
            int localIndex = findLocal(compiler, node->identifier.name);
            if (localIndex == -1) {
                SrcLocation loc = {.file = compiler->fileName, .line = node->location.line, .column = node->location.column};
                report_undefined_variable(loc, node->identifier.name);
                return false;
            }
            // The register is already allocated, just update nextRegister
            compiler->nextRegister = compiler->locals[localIndex].reg + 1;
            return true;
        }
        // Backend-specific nodes should be handled by the individual compilers
        case NODE_BREAK:
        case NODE_CONTINUE:
            if (!ctx->supportsBreakContinue) {
                SrcLocation loc = {.file = compiler->fileName, .line = node->location.line, .column = node->location.column};
                report_compile_error(E1009_EXPRESSION_TOO_COMPLEX, loc, "Break/continue not supported in single-pass compilation");
                return false;
            }
            // Let the backend handle this
            return false;
        case NODE_FUNCTION:
        case NODE_CALL:
            if (!ctx->supportsFunctions) {
                SrcLocation loc = {.file = compiler->fileName, .line = node->location.line, .column = node->location.column};
                report_compile_error(E1009_EXPRESSION_TOO_COMPLEX, loc, "Functions not supported in single-pass compilation");
                return false;
            }
            // Let the backend handle this
            return false;
        default:
            // Unknown node type, let backend handle
            return false;
    }
}

// Helper function implementations use the ones from hybride_compiler.c

static int findLocal(Compiler* compiler, const char* name) {
    for (int i = compiler->localCount - 1; i >= 0; i--) {
        if (strcmp(compiler->locals[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static int addLocal(Compiler* compiler, const char* name, bool isAssigned) {
    if (compiler->localCount >= REGISTER_COUNT) {
        return -1;
    }
    
    int index = compiler->localCount++;
    compiler->locals[index].name = strdup(name);
    compiler->locals[index].isActive = true;
    compiler->locals[index].depth = compiler->scopeDepth;
    compiler->locals[index].isMutable = isAssigned;
    compiler->locals[index].reg = 0; // Will be set by caller
    
    return index;
}