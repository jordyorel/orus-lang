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
    int leftReg = compileSharedNodeExpr(node->binary.left, compiler, ctx);
    if (leftReg == -1) return -1;

    int rightReg = compileSharedNodeExpr(node->binary.right, compiler, ctx);
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

    // Emit operation based on operator using correct opcodes (matching single-pass)
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
    } else if (strcmp(op, "!=") == 0) {
        emitByte(compiler, OP_NE_R);
    } else if (strcmp(op, ">") == 0) {
        emitByte(compiler, OP_GT_I32_R);
    } else if (strcmp(op, ">=") == 0) {
        emitByte(compiler, OP_GE_I32_R);
    } else if (strcmp(op, "<") == 0) {
        emitByte(compiler, OP_LT_I32_R);
    } else if (strcmp(op, "<=") == 0) {
        emitByte(compiler, OP_LE_I32_R);
    } else if (strcmp(op, "and") == 0 || strcmp(op, "&&") == 0) {
        emitByte(compiler, OP_AND_BOOL_R);
    } else if (strcmp(op, "or") == 0 || strcmp(op, "||") == 0) {
        emitByte(compiler, OP_OR_BOOL_R);
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
        int initReg = compileSharedNodeExpr(node->varDecl.initializer, compiler, ctx);
        if (initReg < 0) {
            return -1;
        }
        valueReg = initReg;
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

    // Compile the value expression first and get the register it's in
    int valueRegResult = compileSharedNodeExpr(node->assign.value, compiler, ctx);
    if (valueRegResult < 0) {
        return -1;
    }

    uint8_t valueReg = (uint8_t)valueRegResult;

    // Find the variable
    int localIndex = findLocal(compiler, node->assign.name);
    if (localIndex >= 0) {
        // Variable exists - check if it's mutable for assignment
        if (!compiler->locals[localIndex].isMutable) {
            report_immutable_variable_assignment(node->location, node->assign.name);
            freeRegister(compiler, valueReg);
            return -1;
        }
        
        uint8_t targetReg = compiler->locals[localIndex].reg;
        
        // Move value to target register if different
        if (valueReg != targetReg) {
            emitByte(compiler, OP_MOVE);
            emitByte(compiler, targetReg);
            emitByte(compiler, valueReg);
            freeRegister(compiler, valueReg);
        }
        
        return targetReg;
    } else {
        // Variable doesn't exist - create new mutable variable (matching single-pass behavior)
        int newLocalIndex = addLocal(compiler, node->assign.name, true);
        if (newLocalIndex < 0) {
            SrcLocation loc = {.file = compiler->fileName, .line = node->location.line, .column = node->location.column};
            report_compile_error(E1009_EXPRESSION_TOO_COMPLEX, loc, "Too many local variables");
            freeRegister(compiler, valueReg);
            return -1;
        }

        uint8_t targetReg = compiler->locals[newLocalIndex].reg;
        
        // Move value to target register if different
        if (valueReg != targetReg) {
            emitByte(compiler, OP_MOVE);
            emitByte(compiler, targetReg);
            emitByte(compiler, valueReg);
            freeRegister(compiler, valueReg);
        }
        
        return targetReg;
    }
}

// Shared if statement compilation
int compileSharedIfStatement(ASTNode* node, Compiler* compiler, CompilerContext* ctx) {
    if (!node || node->type != NODE_IF) {
        return -1;
    }

    // Compile condition
    int condRegResult = compileSharedNodeExpr(node->ifStmt.condition, compiler, ctx);
    if (condRegResult < 0) {
        return -1;
    }

    uint8_t condReg = condRegResult;

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
    int condRegResult = compileSharedNodeExpr(node->whileStmt.condition, compiler, ctx);
    if (condRegResult < 0) {
        return -1;
    }

    uint8_t condReg = condRegResult;

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
    int startRegResult = compileSharedNodeExpr(node->forRange.start, compiler, ctx);
    if (startRegResult < 0) {
        return -1;
    }
    uint8_t startReg = startRegResult;

    // Compile end value
    int endRegResult = compileSharedNodeExpr(node->forRange.end, compiler, ctx);
    if (endRegResult < 0) {
        freeRegister(compiler, startReg);
        return -1;
    }
    uint8_t endReg = endRegResult;

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
    emitByte(compiler, OP_MOVE);
    emitByte(compiler, iterReg);
    emitByte(compiler, startReg);

    int loopStart = compiler->chunk->count;

    // Check loop condition (iter < end)
    emitByte(compiler, OP_LT_I32_R);
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
    emitByte(compiler, OP_ADD_I32_R);
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
    int sourceRegResult = compileSharedNodeExpr(node->cast.expression, compiler, ctx);
    if (sourceRegResult < 0) {
        return -1;
    }

    uint8_t sourceReg = sourceRegResult;
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
            emitByte(compiler, OP_TO_STRING_R);
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

// Expression compilation that returns register number
int compileSharedNodeExpr(ASTNode* node, Compiler* compiler, CompilerContext* ctx) {
    if (!node || !compiler || !ctx) {
        return -1;
    }

    switch (node->type) {
        case NODE_LITERAL:
            return compileSharedLiteral(node, compiler, ctx);
        case NODE_BINARY:
            return compileSharedBinaryOp(node, compiler, ctx);
        case NODE_CAST:
            return compileSharedCast(node, compiler, ctx);
        case NODE_IDENTIFIER: {
            // Variable access
            int localIndex = findLocal(compiler, node->identifier.name);
            if (localIndex == -1) {
                SrcLocation loc = {.file = compiler->fileName, .line = node->location.line, .column = node->location.column};
                report_undefined_variable(loc, node->identifier.name);
                return -1;
            }
            // For variable access, we need to make the variable's register available as a temporary
            // This requires allocating a temporary register and copying the value
            uint8_t tempReg = allocateRegister(compiler);
            uint8_t varReg = compiler->locals[localIndex].reg;
            
            // Copy variable value to temp register
            emitByte(compiler, OP_MOVE);
            emitByte(compiler, tempReg);
            emitByte(compiler, varReg);
            
            return tempReg;
        }
        default:
            // Unknown expression type
            return -1;
    }
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
            // For variable access, we need to make the variable's register available as nextRegister - 1
            // This requires allocating a temporary register and copying the value
            uint8_t tempReg = allocateRegister(compiler);
            uint8_t varReg = compiler->locals[localIndex].reg;
            
            // Copy variable value to temp register
            emitByte(compiler, OP_MOVE);
            emitByte(compiler, tempReg);
            emitByte(compiler, varReg);
            
            // Now nextRegister - 1 points to the temp register with the variable's value
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
    
    // Variables get persistent registers starting from 0, never freed
    // This ensures variables don't interfere with temporary expression registers
    uint8_t reg = index; // Use the local index as the register number
    compiler->locals[index].reg = reg;
    
    // Update nextRegister to ensure temporaries don't conflict with variables
    if (reg >= compiler->nextRegister) {
        compiler->nextRegister = reg + 1;
        if (compiler->nextRegister > compiler->maxRegisters) {
            compiler->maxRegisters = compiler->nextRegister;
        }
    }
    
    return index;
}