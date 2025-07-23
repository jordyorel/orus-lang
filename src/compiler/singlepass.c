// single_pass_compiler.c
// Author: Hierat
// Date: 2023-10-01
// Description: Single-pass compiler for the Orus language - fast compilation

#include <string.h>

#include "compiler/compiler.h"
#include "compiler/symbol_table.h"
#include "errors/features/type_errors.h"
#include "errors/features/variable_errors.h"
#include "internal/error_reporting.h"
#include "runtime/jumptable.h"
#include "runtime/memory.h"
#include "type/type.h"
#include "vm/register_file.h"
#include "vm/vm_constants.h"

// Single-pass specific structures - SIMPLIFIED (no break/continue support)
typedef struct {
    int loopStart;  // Only track loop start for simple loops
} SinglePassLoopContext;

typedef struct {
    Compiler* base;
    SinglePassLoopContext* loops;
    int loopCount;
    int loopCapacity;
} SinglePassCompiler;

// Global single-pass compiler instance
static SinglePassCompiler* g_singlePassCompiler = NULL;

// Forward declarations
static bool compileSinglePassNode(ASTNode* node, Compiler* compiler);
static int compileSinglePassExpr(ASTNode* node, Compiler* compiler);

// Missing function declarations
int addConstant(Chunk* chunk, Value value);
void writeChunk(Chunk* chunk, uint8_t byte, int line, int column);

void initSinglePassCompiler(Compiler* compiler, Chunk* chunk,
                            const char* fileName, const char* source) {
    // Initialize base compiler
    compiler->chunk = chunk;
    compiler->fileName = fileName;
    compiler->source = source;
    compiler->nextRegister = 0;
    compiler->maxRegisters = 0;
    compiler->localCount = 0;
    compiler->scopeDepth = 0;
    compiler->loopDepth = 0;
    compiler->hadError = false;
    compiler->currentLine = 1;
    compiler->currentColumn = 1;
    compiler->currentFunctionParameterCount = 0;
    symbol_table_init(&compiler->symbols);

    // Initialize locals array
    for (int i = 0; i < REGISTER_COUNT; i++) {
        compiler->locals[i].name = NULL;
        compiler->locals[i].reg = 0;
        compiler->locals[i].isActive = false;
        compiler->locals[i].depth = -1;
        compiler->locals[i].isMutable = false;
        compiler->locals[i].type = VAL_NIL;
        compiler->locals[i].liveRangeIndex = -1;
        compiler->locals[i].isSpilled = false;
        compiler->locals[i].hasKnownType = false;
        compiler->locals[i].knownType = VAL_NIL;
    }

    // Initialize single-pass compiler
    g_singlePassCompiler = malloc(sizeof(SinglePassCompiler));
    g_singlePassCompiler->base = compiler;
    g_singlePassCompiler->loops = malloc(sizeof(SinglePassLoopContext) * 8);
    g_singlePassCompiler->loopCount = 0;
    g_singlePassCompiler->loopCapacity = 8;
}

void freeSinglePassCompiler(Compiler* compiler) {
    symbol_table_free(&compiler->symbols);

    if (g_singlePassCompiler) {
        // Simple cleanup - no jump tables to free
        free(g_singlePassCompiler->loops);
        free(g_singlePassCompiler);
        g_singlePassCompiler = NULL;
    }
}

// Using shared allocateRegister and freeRegister functions from hybrid_compiler.c

static void beginScope(Compiler* compiler) {
    compiler->scopeDepth++;
    symbol_table_begin_scope(&compiler->symbols, compiler->scopeDepth);
}

static void endScope(Compiler* compiler) {
    // Free local variables in this scope
    for (int i = 0; i < compiler->localCount; i++) {
        if (compiler->locals[i].isActive &&
            compiler->locals[i].depth == compiler->scopeDepth) {
            if (compiler->locals[i].name) {
                free(compiler->locals[i].name);
                compiler->locals[i].name = NULL;
            }
            compiler->locals[i].isActive = false;
        }
    }

    symbol_table_end_scope(&compiler->symbols, compiler->scopeDepth);
    compiler->scopeDepth--;
}

static int addLocal(Compiler* compiler, const char* name, bool isMutable) {
    if (compiler->localCount >= REGISTER_COUNT) {
        return -1;
    }

    int index = compiler->localCount++;
    uint8_t reg = allocateRegister(compiler);

    compiler->locals[index].name = strdup(name);
    compiler->locals[index].reg = reg;
    compiler->locals[index].isActive = true;
    compiler->locals[index].depth = compiler->scopeDepth;
    compiler->locals[index].isMutable = isMutable;
    compiler->locals[index].type = VAL_I32;
    compiler->locals[index].liveRangeIndex = -1;
    compiler->locals[index].isSpilled = false;
    compiler->locals[index].hasKnownType = false;
    compiler->locals[index].knownType = VAL_NIL;

    symbol_table_set(&compiler->symbols, name, index, compiler->scopeDepth);
    return index;
}

static int findLocal(Compiler* compiler, const char* name) {
    for (int i = compiler->localCount - 1; i >= 0; i--) {
        if (compiler->locals[i].isActive &&
            strcmp(compiler->locals[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

// Using shared emitByte and emitBytes functions from hybrid_compiler.c

static int emitJump(Compiler* compiler) {
    emitByte(compiler, 0xff);
    emitByte(compiler, 0xff);
    return compiler->chunk->count - 2;
}

static void emitLoop(Compiler* compiler, int loopStart) {
    emitByte(compiler, OP_LOOP);

    int offset = compiler->chunk->count - loopStart + 2;
    if (offset > UINT16_MAX) {
        compiler->hadError = true;
        return;
    }

    emitByte(compiler, (offset >> 8) & 0xff);
    emitByte(compiler, offset & 0xff);
}

static void patchJump(Compiler* compiler, int offset) {
    int jump = compiler->chunk->count - offset - 2;
    if (jump > UINT16_MAX) {
        SrcLocation loc = {.file = compiler->fileName,
                           .line = compiler->currentLine,
                           .column = compiler->currentColumn};
        report_compile_error(E1009_EXPRESSION_TOO_COMPLEX, loc,
                             "Too much code to jump over.");
        return;
    }
    compiler->chunk->code[offset] = (jump >> 8) & 0xff;
    compiler->chunk->code[offset + 1] = jump & 0xff;
}

// Using shared emitConstant function from hybrid_compiler.c

// Single-pass loop management
static void enterLoop(Compiler* compiler) {
    SinglePassCompiler* spCompiler = g_singlePassCompiler;

    if (spCompiler->loopCount >= spCompiler->loopCapacity) {
        spCompiler->loopCapacity *= 2;
        spCompiler->loops =
            realloc(spCompiler->loops,
                    spCompiler->loopCapacity * sizeof(SinglePassLoopContext));
    }

    SinglePassLoopContext* context =
        &spCompiler->loops[spCompiler->loopCount++];
    context->loopStart = compiler->chunk->count;
}

static void exitLoop(Compiler* compiler) {
    SinglePassCompiler* spCompiler = g_singlePassCompiler;
    if (spCompiler->loopCount == 0) return;
    
    // Simple exit - no break/continue jump patching needed
    spCompiler->loopCount--;
}

// patchContinueJumps removed - single-pass doesn't handle break/continue

// Expression compilation
static int compileSinglePassLiteral(ASTNode* node, Compiler* compiler) {
    uint8_t reg = allocateRegister(compiler);
    emitConstant(compiler, reg, node->literal.value);
    return reg;
}

static int compileSinglePassIdentifier(ASTNode* node, Compiler* compiler) {
    int localIndex = findLocal(compiler, node->identifier.name);
    if (localIndex >= 0) {
        return compiler->locals[localIndex].reg;
    }

    printf("[DEBUG] singlepass: About to report undefined variable '%s'\n", node->identifier.name);
    fflush(stdout);
    report_undefined_variable(node->location, node->identifier.name);
    return -1;
}

static int compileSinglePassBinaryOp(ASTNode* node, Compiler* compiler) {
    int leftReg = compileSinglePassExpr(node->binary.left, compiler);
    int rightReg = compileSinglePassExpr(node->binary.right, compiler);
    uint8_t resultReg = allocateRegister(compiler);

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
    } else if (strcmp(node->binary.op, ">") == 0) {
        emitByte(compiler, OP_GT_I32_R);
    } else if (strcmp(node->binary.op, "<") == 0) {
        emitByte(compiler, OP_LT_I32_R);
    } else if (strcmp(node->binary.op, ">=") == 0) {
        emitByte(compiler, OP_GE_I32_R);
    } else if (strcmp(node->binary.op, "<=") == 0) {
        emitByte(compiler, OP_LE_I32_R);
    } else if (strcmp(node->binary.op, "==") == 0) {
        emitByte(compiler, OP_EQ_R);
    } else if (strcmp(node->binary.op, "!=") == 0) {
        emitByte(compiler, OP_NE_R);
    } else {
        SrcLocation loc = {.file = compiler->fileName,
                           .line = node->location.line,
                           .column = node->location.column};
        report_compile_error(E1006_INVALID_SYNTAX, loc,
                             "Unknown binary operator");
        freeRegister(compiler, leftReg);
        freeRegister(compiler, rightReg);
        freeRegister(compiler, resultReg);
        return -1;
    }

    emitByte(compiler, resultReg);
    emitByte(compiler, leftReg);
    emitByte(compiler, rightReg);

    freeRegister(compiler, leftReg);
    freeRegister(compiler, rightReg);

    return resultReg;
}

static int compileSinglePassExpr(ASTNode* node, Compiler* compiler) {
    if (!node) return -1;

    switch (node->type) {
        case NODE_LITERAL:
            return compileSinglePassLiteral(node, compiler);
        case NODE_IDENTIFIER:
            return compileSinglePassIdentifier(node, compiler);
        case NODE_BINARY:
            return compileSinglePassBinaryOp(node, compiler);
        case NODE_TIME_STAMP: {
            uint8_t resultReg = allocateRegister(compiler);
            emitByte(compiler, OP_TIME_STAMP);
            emitByte(compiler, resultReg);
            return resultReg;
        }
        default:
            SrcLocation loc = {.file = compiler->fileName,
                               .line = node->location.line,
                               .column = node->location.column};
            report_compile_error(E1006_INVALID_SYNTAX, loc,
                                 "Unsupported expression type in single-pass");
            return -1;
    }
}

static bool compileSinglePassNode(ASTNode* node, Compiler* compiler) {
    if (!node) return true;

    compiler->currentLine = node->location.line;
    compiler->currentColumn = node->location.column;

    switch (node->type) {
        case NODE_PROGRAM:
            for (int i = 0; i < node->program.count; i++) {
                if (!compileSinglePassNode(node->program.declarations[i],
                                           compiler)) {
                    return false;
                }
            }
            return true;

        case NODE_FOR_RANGE: {
            // Simple single-pass for loop compilation
            int startReg =
                compileSinglePassExpr(node->forRange.start, compiler);
            int endReg = compileSinglePassExpr(node->forRange.end, compiler);
            
            // Protect end register from being reused during loop body compilation
            // by marking it as permanently allocated during the loop

            beginScope(compiler);

            // Add loop variable
            int loopVarIndex =
                addLocal(compiler, node->forRange.varName, false);
            if (loopVarIndex < 0) {
                SrcLocation loc = {.file = compiler->fileName,
                                   .line = node->location.line,
                                   .column = node->location.column};
                report_compile_error(E1009_EXPRESSION_TOO_COMPLEX, loc,
                                     "Too many local variables");
                return false;
            }

            uint8_t iterReg = compiler->locals[loopVarIndex].reg;

            // Initialize iterator
            emitByte(compiler, OP_MOVE);
            emitByte(compiler, iterReg);
            emitByte(compiler, startReg);

            // Enter loop context
            enterLoop(compiler);
            int loopStart = compiler->chunk->count;

            // Check condition
            uint8_t condReg = allocateRegister(compiler);
            emitByte(compiler, OP_LE_I32_R);
            emitByte(compiler, condReg);
            emitByte(compiler, iterReg);
            emitByte(compiler, endReg);

            emitByte(compiler, OP_JUMP_IF_NOT_R);
            emitByte(compiler, condReg);
            int exitJump = emitJump(compiler);

            // Don't free condReg yet - keep it reserved during loop body compilation
            // to prevent nested loops from corrupting our condition evaluation

            // Compile body
            bool success = compileSinglePassNode(node->forRange.body, compiler);

            if (!success) {
                endScope(compiler);
                exitLoop(compiler);
                return false;
            }

            // No continue jump patching needed - single-pass doesn't support break/continue

            // Increment
            emitByte(compiler, OP_INC_I32_R);
            emitByte(compiler, iterReg);

            // Jump back
            emitLoop(compiler, loopStart);

            // Patch exit
            patchJump(compiler, exitJump);

            // Exit loop and clean up
            exitLoop(compiler);
            endScope(compiler);

            // Free all registers at the very end to avoid nested loop conflicts
            freeRegister(compiler, condReg);  // Free the condition register we kept reserved
            freeRegister(compiler, startReg);
            freeRegister(compiler, endReg);

            return true;
        }

        case NODE_WHILE: {
            beginScope(compiler);
            enterLoop(compiler);

            int loopStart = compiler->chunk->count;
            int conditionReg =
                compileSinglePassExpr(node->whileStmt.condition, compiler);

            if (conditionReg < 0) {
                endScope(compiler);
                exitLoop(compiler);
                return false;
            }

            emitByte(compiler, OP_JUMP_IF_NOT_R);
            emitByte(compiler, conditionReg);
            int exitJump = emitJump(compiler);

            freeRegister(compiler, conditionReg);

            bool success =
                compileSinglePassNode(node->whileStmt.body, compiler);

            if (!success) {
                endScope(compiler);
                exitLoop(compiler);
                return false;
            }

            // No continue jump patching needed - single-pass doesn't support break/continue

            emitLoop(compiler, loopStart);
            patchJump(compiler, exitJump);

            exitLoop(compiler);
            endScope(compiler);

            return true;
        }

        case NODE_BREAK:
        case NODE_CONTINUE: {
            // Break/continue are now handled ONLY by multi-pass compiler
            // Single-pass should never encounter these due to routing logic
            SrcLocation loc = {.file = compiler->fileName,
                               .line = node->location.line,
                               .column = node->location.column};
            report_compile_error(E1006_INVALID_SYNTAX, loc,
                                 "break/continue statements require multi-pass compilation");
            return false;
        }

        case NODE_ASSIGN: {
            int valueReg = compileSinglePassExpr(node->assign.value, compiler);
            if (valueReg < 0) return false;

            int localIndex = findLocal(compiler, node->assign.name);
            if (localIndex >= 0) {
                if (!compiler->locals[localIndex].isMutable) {
                    report_immutable_variable_assignment(node->location,
                                                         node->assign.name);
                    freeRegister(compiler, valueReg);
                    return false;
                }

                emitByte(compiler, OP_MOVE);
                emitByte(compiler, compiler->locals[localIndex].reg);
                emitByte(compiler, valueReg);
                freeRegister(compiler, valueReg);
                return true;
            }

            // Create new variable
            int newLocalIndex = addLocal(compiler, node->assign.name, true);
            if (newLocalIndex < 0) {
                SrcLocation loc = {.file = compiler->fileName,
                                   .line = node->location.line,
                                   .column = node->location.column};
                report_compile_error(E1009_EXPRESSION_TOO_COMPLEX, loc,
                                     "Too many local variables");
                freeRegister(compiler, valueReg);
                return false;
            }

            emitByte(compiler, OP_MOVE);
            emitByte(compiler, compiler->locals[newLocalIndex].reg);
            emitByte(compiler, valueReg);
            freeRegister(compiler, valueReg);
            return true;
        }

        case NODE_VAR_DECL: {
            int localIndex =
                addLocal(compiler, node->varDecl.name, node->varDecl.isMutable);
            if (localIndex < 0) {
                SrcLocation loc = {.file = compiler->fileName,
                                   .line = node->location.line,
                                   .column = node->location.column};
                report_compile_error(E1009_EXPRESSION_TOO_COMPLEX, loc,
                                     "Too many local variables");
                return false;
            }

            uint8_t reg = compiler->locals[localIndex].reg;

            if (node->varDecl.initializer) {
                int valueReg =
                    compileSinglePassExpr(node->varDecl.initializer, compiler);
                if (valueReg < 0) return false;

                emitByte(compiler, OP_MOVE);
                emitByte(compiler, reg);
                emitByte(compiler, valueReg);
                freeRegister(compiler, valueReg);
            } else {
                emitByte(compiler, OP_LOAD_NIL);
                emitByte(compiler, reg);
            }

            return true;
        }

        case NODE_BLOCK:
            for (int i = 0; i < node->block.count; i++) {
                if (!compileSinglePassNode(node->block.statements[i],
                                           compiler)) {
                    return false;
                }
            }
            return true;

        case NODE_PRINT: {
            if (node->print.count == 1) {
                int valueReg =
                    compileSinglePassExpr(node->print.values[0], compiler);
                if (valueReg < 0) return false;

                emitByte(compiler, OP_PRINT_R);
                emitByte(compiler, valueReg);
                freeRegister(compiler, valueReg);
            }
            return true;
        }

        case NODE_IF: {
            int conditionReg =
                compileSinglePassExpr(node->ifStmt.condition, compiler);
            if (conditionReg < 0) return false;

            emitByte(compiler, OP_JUMP_IF_NOT_R);
            emitByte(compiler, conditionReg);
            int thenJump = emitJump(compiler);

            freeRegister(compiler, conditionReg);

            beginScope(compiler);
            bool success =
                compileSinglePassNode(node->ifStmt.thenBranch, compiler);
            endScope(compiler);

            if (!success) return false;

            if (node->ifStmt.elseBranch) {
                emitByte(compiler, OP_JUMP);
                int elseJump = emitJump(compiler);

                patchJump(compiler, thenJump);

                beginScope(compiler);
                success =
                    compileSinglePassNode(node->ifStmt.elseBranch, compiler);
                endScope(compiler);

                if (!success) return false;
                patchJump(compiler, elseJump);
            } else {
                patchJump(compiler, thenJump);
            }

            return true;
        }

        default: {
            int reg = compileSinglePassExpr(node, compiler);
            if (reg >= 0) {
                freeRegister(compiler, reg);
                return true;
            }
            return false;
        }
    }
}

bool compileSinglePass(ASTNode* ast, Compiler* compiler, bool isModule) {
    if (!ast) return false;

    bool success = compileSinglePassNode(ast, compiler);

    if (success && !isModule) {
        emitByte(compiler, OP_RETURN_VOID);
    }

    return success && !compiler->hadError;
}