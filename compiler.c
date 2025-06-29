// register_compiler.c - Complete compiler for register-based VM
#include "vm.h"

// Register allocation state
typedef struct {
    bool inUse[REGISTER_COUNT];
    int allocCount;
} RegisterAllocator;

// Extended compiler state
typedef struct {
    Compiler base;
    RegisterAllocator allocator;

    // Local variable mapping
    struct {
        const char* name;
        uint8_t reg;
        int depth;
        bool initialized;
    } locals[REGISTER_COUNT];
    int localCount;
    int scopeDepth;

    // Loop context for break/continue
    struct {
        int breakJumps[16];
        int breakCount;
        int continueJumps[16];
        int continueCount;
        int start;
    } loops[8];
    int loopDepth;
} ExtendedCompiler;

// Forward declarations
static uint8_t compileExpression(ExtendedCompiler* compiler, ASTNode* node);
static void compileStatement(ExtendedCompiler* compiler, ASTNode* node);

// Register allocation functions
static void initRegisterAllocator(RegisterAllocator* allocator) {
    for (int i = 0; i < REGISTER_COUNT; i++) {
        allocator->inUse[i] = false;
    }
    allocator->allocCount = 0;
}

static uint8_t allocateRegisterEx(ExtendedCompiler* compiler) {
    RegisterAllocator* allocator = &compiler->allocator;

    for (int i = 0; i < REGISTER_COUNT; i++) {
        if (!allocator->inUse[i]) {
            allocator->inUse[i] = true;
            allocator->allocCount++;

            if (i > compiler->base.maxRegisters) {
                compiler->base.maxRegisters = i;
            }

            return i;
        }
    }

    // Register allocation failed
    compiler->base.hadError = true;
    fprintf(stderr, "Error: Out of registers\n");
    return 0;
}

static void freeRegisterEx(ExtendedCompiler* compiler, uint8_t reg) {
    if (reg < REGISTER_COUNT) {
        compiler->allocator.inUse[reg] = false;
        compiler->allocator.allocCount--;
    }
}

// Check if a register contains a local variable
static bool isLocalRegister(ExtendedCompiler* compiler, uint8_t reg) {
    for (int i = 0; i < compiler->localCount; i++) {
        if (compiler->locals[i].reg == reg) {
            return true;
        }
    }
    return false;
}

// Emit functions
static void emitByte(ExtendedCompiler* compiler, uint8_t byte) {
    writeChunk(compiler->base.chunk, byte, 1, 1);
}

static void emitBytes(ExtendedCompiler* compiler, uint8_t byte1,
                      uint8_t byte2) {
    emitByte(compiler, byte1);
    emitByte(compiler, byte2);
}

static void emitBytes3(ExtendedCompiler* compiler, uint8_t byte1, uint8_t byte2,
                       uint8_t byte3) {
    emitByte(compiler, byte1);
    emitByte(compiler, byte2);
    emitByte(compiler, byte3);
}

static void emitBytes4(ExtendedCompiler* compiler, uint8_t byte1, uint8_t byte2,
                       uint8_t byte3, uint8_t byte4) {
    emitByte(compiler, byte1);
    emitByte(compiler, byte2);
    emitByte(compiler, byte3);
    emitByte(compiler, byte4);
}

static void emitConstant(ExtendedCompiler* compiler, uint8_t reg, Value value) {
    int constant = addConstant(compiler->base.chunk, value);
    if (constant > UINT8_MAX) {
        compiler->base.hadError = true;
        fprintf(stderr, "Error: Too many constants\n");
        return;
    }
    emitBytes3(compiler, OP_LOAD_CONST, reg, (uint8_t)constant);
}

static int emitJump(ExtendedCompiler* compiler, uint8_t instruction) {
    emitByte(compiler, instruction);
    emitByte(compiler, 0xFF);
    emitByte(compiler, 0xFF);
    return compiler->base.chunk->count - 2;
}

static void patchJump(ExtendedCompiler* compiler, int offset) {
    int jump = compiler->base.chunk->count - offset - 2;

    if (jump > UINT16_MAX) {
        compiler->base.hadError = true;
        fprintf(stderr, "Error: Jump too large\n");
    }

    compiler->base.chunk->code[offset] = (jump >> 8) & 0xFF;
    compiler->base.chunk->code[offset + 1] = jump & 0xFF;
}

static void emitLoop(ExtendedCompiler* compiler, int loopStart) {
    emitByte(compiler, OP_LOOP);

    int offset = compiler->base.chunk->count - loopStart + 2;
    if (offset > UINT16_MAX) {
        compiler->base.hadError = true;
        fprintf(stderr, "Error: Loop body too large\n");
    }

    emitByte(compiler, (offset >> 8) & 0xFF);
    emitByte(compiler, offset & 0xFF);
}

// Variable management
static int resolveLocal(ExtendedCompiler* compiler, const char* name) {
    for (int i = compiler->localCount - 1; i >= 0; i--) {
        if (strcmp(compiler->locals[i].name, name) == 0) {
            if (!compiler->locals[i].initialized) {
                compiler->base.hadError = true;
                fprintf(stderr,
                        "Error: Variable '%s' used before initialization\n",
                        name);
            }
            return i;
        }
    }
    return -1;
}

static uint8_t addLocal(ExtendedCompiler* compiler, const char* name) {
    if (compiler->localCount >= REGISTER_COUNT) {
        compiler->base.hadError = true;
        fprintf(stderr, "Error: Too many local variables\n");
        return 0;
    }

    uint8_t reg = allocateRegisterEx(compiler);
    compiler->locals[compiler->localCount].name = name;
    compiler->locals[compiler->localCount].reg = reg;
    compiler->locals[compiler->localCount].depth = compiler->scopeDepth;
    compiler->locals[compiler->localCount].initialized = false;
    compiler->localCount++;

    return reg;
}

// Expression compilation
static uint8_t compileIdentifier(ExtendedCompiler* compiler, ASTNode* node) {
    const char* name = node->identifier.name;

    // Check if it's a local variable
    int local = resolveLocal(compiler, name);
    if (local != -1) {
        return compiler->locals[local].reg;
    }

    // Check if it's a global variable
    for (int i = 0; i < vm.variableCount; i++) {
        if (vm.variableNames[i].name &&
            strcmp(vm.variableNames[i].name->chars, name) == 0) {
            uint8_t reg = allocateRegisterEx(compiler);
            emitBytes3(compiler, OP_LOAD_GLOBAL, reg, i);
            return reg;
        }
    }

    compiler->base.hadError = true;
    fprintf(stderr, "Error: Undefined variable '%s'\n", name);
    return 0;
}

static uint8_t compileLiteral(ExtendedCompiler* compiler, ASTNode* node) {
    uint8_t reg = allocateRegisterEx(compiler);
    Value value = node->literal.value;

    switch (value.type) {
        case VAL_NIL:
            emitBytes(compiler, OP_LOAD_NIL, reg);
            break;
        case VAL_BOOL:
            emitBytes(compiler, AS_BOOL(value) ? OP_LOAD_TRUE : OP_LOAD_FALSE,
                      reg);
            break;
        default:
            emitConstant(compiler, reg, value);
            break;
    }

    return reg;
}

static uint8_t compileBinary(ExtendedCompiler* compiler, ASTNode* node) {
    const char* op = node->binary.op;

    // Compile operands
    uint8_t left = compileExpression(compiler, node->binary.left);
    uint8_t right = compileExpression(compiler, node->binary.right);
    uint8_t result = allocateRegisterEx(compiler);

    // Determine the operation based on types
    Type* leftType = node->binary.left->dataType;
    Type* rightType = node->binary.right->dataType;

    if (strcmp(op, "+") == 0) {
        if (leftType && leftType->kind == TYPE_I32) {
            emitBytes4(compiler, OP_ADD_I32_R, result, left, right);
        } else if (leftType && leftType->kind == TYPE_I64) {
            emitBytes4(compiler, OP_ADD_I64_R, result, left, right);
        } else if (leftType && leftType->kind == TYPE_F64) {
            emitBytes4(compiler, OP_ADD_F64_R, result, left, right);
        } else {
            // Generic add
            emitBytes4(compiler, OP_ADD_I32_R, result, left, right);
        }
    } else if (strcmp(op, "-") == 0) {
        if (leftType && leftType->kind == TYPE_I32) {
            emitBytes4(compiler, OP_SUB_I32_R, result, left, right);
        } else if (leftType && leftType->kind == TYPE_I64) {
            emitBytes4(compiler, OP_SUB_I64_R, result, left, right);
        } else if (leftType && leftType->kind == TYPE_F64) {
            emitBytes4(compiler, OP_SUB_F64_R, result, left, right);
        } else {
            emitBytes4(compiler, OP_SUB_I32_R, result, left, right);
        }
    } else if (strcmp(op, "*") == 0) {
        if (leftType && leftType->kind == TYPE_I32) {
            emitBytes4(compiler, OP_MUL_I32_R, result, left, right);
        } else if (leftType && leftType->kind == TYPE_I64) {
            emitBytes4(compiler, OP_MUL_I64_R, result, left, right);
        } else if (leftType && leftType->kind == TYPE_F64) {
            emitBytes4(compiler, OP_MUL_F64_R, result, left, right);
        } else {
            emitBytes4(compiler, OP_MUL_I32_R, result, left, right);
        }
    } else if (strcmp(op, "/") == 0) {
        if (leftType && leftType->kind == TYPE_I32) {
            emitBytes4(compiler, OP_DIV_I32_R, result, left, right);
        } else if (leftType && leftType->kind == TYPE_I64) {
            emitBytes4(compiler, OP_DIV_I64_R, result, left, right);
        } else if (leftType && leftType->kind == TYPE_F64) {
            emitBytes4(compiler, OP_DIV_F64_R, result, left, right);
        } else {
            emitBytes4(compiler, OP_DIV_I32_R, result, left, right);
        }
    } else if (strcmp(op, "<") == 0) {
        if (leftType && leftType->kind == TYPE_I32) {
            emitBytes4(compiler, OP_LT_I32_R, result, left, right);
        } else if (leftType && leftType->kind == TYPE_I64) {
            emitBytes4(compiler, OP_LT_I64_R, result, left, right);
        } else if (leftType && leftType->kind == TYPE_F64) {
            emitBytes4(compiler, OP_LT_F64_R, result, left, right);
        } else {
            emitBytes4(compiler, OP_LT_I32_R, result, left, right);
        }
    } else if (strcmp(op, "<=") == 0) {
        if (leftType && leftType->kind == TYPE_I32) {
            emitBytes4(compiler, OP_LE_I32_R, result, left, right);
        } else if (leftType && leftType->kind == TYPE_I64) {
            emitBytes4(compiler, OP_LE_I64_R, result, left, right);
        } else if (leftType && leftType->kind == TYPE_F64) {
            emitBytes4(compiler, OP_LE_F64_R, result, left, right);
        } else {
            emitBytes4(compiler, OP_LE_I32_R, result, left, right);
        }
    } else if (strcmp(op, ">") == 0) {
        if (leftType && leftType->kind == TYPE_I32) {
            emitBytes4(compiler, OP_GT_I32_R, result, left, right);
        } else if (leftType && leftType->kind == TYPE_I64) {
            emitBytes4(compiler, OP_GT_I64_R, result, left, right);
        } else if (leftType && leftType->kind == TYPE_F64) {
            emitBytes4(compiler, OP_GT_F64_R, result, left, right);
        } else {
            emitBytes4(compiler, OP_GT_I32_R, result, left, right);
        }
    } else if (strcmp(op, ">=") == 0) {
        if (leftType && leftType->kind == TYPE_I32) {
            emitBytes4(compiler, OP_GE_I32_R, result, left, right);
        } else if (leftType && leftType->kind == TYPE_I64) {
            emitBytes4(compiler, OP_GE_I64_R, result, left, right);
        } else if (leftType && leftType->kind == TYPE_F64) {
            emitBytes4(compiler, OP_GE_F64_R, result, left, right);
        } else {
            emitBytes4(compiler, OP_GE_I32_R, result, left, right);
        }
    } else if (strcmp(op, "==") == 0) {
        emitBytes4(compiler, OP_EQ_R, result, left, right);
    } else if (strcmp(op, "!=") == 0) {
        emitBytes4(compiler, OP_NE_R, result, left, right);
    } else if (strcmp(op, "&&") == 0) {
        emitBytes4(compiler, OP_AND_BOOL_R, result, left, right);
    } else if (strcmp(op, "||") == 0) {
        emitBytes4(compiler, OP_OR_BOOL_R, result, left, right);
    } else if (strcmp(op, "&") == 0) {
        emitBytes4(compiler, OP_AND_I32_R, result, left, right);
    } else if (strcmp(op, "|") == 0) {
        emitBytes4(compiler, OP_OR_I32_R, result, left, right);
    } else if (strcmp(op, "^") == 0) {
        emitBytes4(compiler, OP_XOR_I32_R, result, left, right);
    } else if (strcmp(op, "<<") == 0) {
        emitBytes4(compiler, OP_SHL_I32_R, result, left, right);
    } else if (strcmp(op, ">>") == 0) {
        emitBytes4(compiler, OP_SHR_I32_R, result, left, right);
    } else if (strcmp(op, "%") == 0) {
        emitBytes4(compiler, OP_MOD_I32_R, result, left, right);
    } else {
        compiler->base.hadError = true;
        fprintf(stderr, "Error: Unknown binary operator '%s'\n", op);
    }

    // Free temporary registers
    if (!isLocalRegister(compiler, left)) {
        freeRegisterEx(compiler, left);
    }
    if (!isLocalRegister(compiler, right)) {
        freeRegisterEx(compiler, right);
    }

    return result;
}

static uint8_t compileUnary(ExtendedCompiler* compiler, ASTNode* node) {
    const char* op = node->unary.op;
    uint8_t operand = compileExpression(compiler, node->unary.operand);
    uint8_t result = allocateRegisterEx(compiler);

    if (strcmp(op, "-") == 0) {
        // Negate - compile as 0 - operand
        uint8_t zero = allocateRegisterEx(compiler);
        emitConstant(compiler, zero, I32_VAL(0));
        emitBytes4(compiler, OP_SUB_I32_R, result, zero, operand);
        freeRegisterEx(compiler, zero);
    } else if (strcmp(op, "!") == 0) {
        emitBytes3(compiler, OP_NOT_BOOL_R, result, operand);
    } else if (strcmp(op, "~") == 0) {
        emitBytes3(compiler, OP_NOT_I32_R, result, operand);
    } else {
        compiler->base.hadError = true;
        fprintf(stderr, "Error: Unknown unary operator '%s'\n", op);
    }

    if (!isLocalRegister(compiler, operand)) {
        freeRegisterEx(compiler, operand);
    }

    return result;
}

static uint8_t compileArray(ExtendedCompiler* compiler, ASTNode* node) {
    int count = node->array.count;
    uint8_t firstReg = 0;

    // Compile all elements into consecutive registers
    if (count > 0) {
        firstReg = compileExpression(compiler, node->array.elements[0]);

        for (int i = 1; i < count; i++) {
            uint8_t reg = compileExpression(compiler, node->array.elements[i]);
            // Ensure consecutive registers
            if (reg != firstReg + i) {
                emitBytes3(compiler, OP_MOVE, firstReg + i, reg);
                if (!isLocalRegister(compiler, reg)) {
                    freeRegisterEx(compiler, reg);
                }
            }
        }
    }

    // Create array
    uint8_t result = allocateRegisterEx(compiler);
    emitBytes4(compiler, OP_MAKE_ARRAY_R, result, firstReg, count);

    // Free element registers
    for (int i = 0; i < count; i++) {
        if (!isLocalRegister(compiler, firstReg + i)) {
            freeRegisterEx(compiler, firstReg + i);
        }
    }

    return result;
}

static uint8_t compileExpression(ExtendedCompiler* compiler, ASTNode* node) {
    switch (node->type) {
        case NODE_LITERAL:
            return compileLiteral(compiler, node);

        case NODE_IDENTIFIER:
            return compileIdentifier(compiler, node);

        case NODE_BINARY:
            return compileBinary(compiler, node);

        case NODE_UNARY:
            return compileUnary(compiler, node);

        case NODE_ARRAY_LITERAL:
            return compileArray(compiler, node);

        default:
            compiler->base.hadError = true;
            fprintf(stderr, "Error: Unknown expression type %d\n", node->type);
            return 0;
    }
}

// Statement compilation
static void compileVarDecl(ExtendedCompiler* compiler, ASTNode* node) {
    const char* name = node->varDecl.name;

    // Check for redeclaration in current scope
    for (int i = compiler->localCount - 1; i >= 0; i--) {
        if (compiler->locals[i].depth < compiler->scopeDepth) {
            break;
        }
        if (strcmp(compiler->locals[i].name, name) == 0) {
            compiler->base.hadError = true;
            fprintf(stderr, "Error: Variable '%s' already declared\n", name);
            return;
        }
    }

    uint8_t reg = addLocal(compiler, name);

    if (node->varDecl.initializer) {
        uint8_t valueReg =
            compileExpression(compiler, node->varDecl.initializer);
        if (valueReg != reg) {
            emitBytes3(compiler, OP_MOVE, reg, valueReg);
            if (!isLocalRegister(compiler, valueReg)) {
                freeRegisterEx(compiler, valueReg);
            }
        }
    } else {
        emitBytes(compiler, OP_LOAD_NIL, reg);
    }

    // Mark as initialized
    compiler->locals[compiler->localCount - 1].initialized = true;
}

static void compilePrint(ExtendedCompiler* compiler, ASTNode* node) {
    for (int i = 0; i < node->print.count; i++) {
        uint8_t reg = compileExpression(compiler, node->print.expressions[i]);

        if (node->print.newline || i == node->print.count - 1) {
            emitBytes(compiler, OP_PRINT_R, reg);
        } else {
            emitBytes(compiler, OP_PRINT_NO_NL_R, reg);
        }

        if (!isLocalRegister(compiler, reg)) {
            freeRegisterEx(compiler, reg);
        }
    }
}

static void compileIf(ExtendedCompiler* compiler, ASTNode* node) {
    // Compile condition
    uint8_t condReg = compileExpression(compiler, node->ifStmt.condition);

    // Jump if false
    emitBytes(compiler, OP_JUMP_IF_NOT_R, condReg);
    int thenJump = compiler->base.chunk->count;
    emitBytes(compiler, 0xFF, 0xFF);

    if (!isLocalRegister(compiler, condReg)) {
        freeRegisterEx(compiler, condReg);
    }

    // Compile then branch
    compileStatement(compiler, node->ifStmt.thenBranch);

    // Jump over else
    int elseJump = -1;
    if (node->ifStmt.elseBranch) {
        elseJump = emitJump(compiler, OP_JUMP);
    }

    // Patch then jump
    patchJump(compiler, thenJump);

    // Compile else branch
    if (node->ifStmt.elseBranch) {
        compileStatement(compiler, node->ifStmt.elseBranch);
        patchJump(compiler, elseJump);
    }
}

static void compileWhile(ExtendedCompiler* compiler, ASTNode* node) {
    // Enter loop context
    compiler->loops[compiler->loopDepth].breakCount = 0;
    compiler->loops[compiler->loopDepth].continueCount = 0;
    compiler->loops[compiler->loopDepth].start = compiler->base.chunk->count;
    compiler->loopDepth++;

    int loopStart = compiler->base.chunk->count;

    // Compile condition
    uint8_t condReg = compileExpression(compiler, node->whileStmt.condition);

    // Exit if false
    emitBytes(compiler, OP_JUMP_IF_NOT_R, condReg);
    int exitJump = compiler->base.chunk->count;
    emitBytes(compiler, 0xFF, 0xFF);

    if (!isLocalRegister(compiler, condReg)) {
        freeRegisterEx(compiler, condReg);
    }

    // Compile body
    compileStatement(compiler, node->whileStmt.body);

    // Loop back
    emitLoop(compiler, loopStart);

    // Patch exit jump
    patchJump(compiler, exitJump);

    // Patch break jumps
    compiler->loopDepth--;
    for (int i = 0; i < compiler->loops[compiler->loopDepth].breakCount; i++) {
        patchJump(compiler, compiler->loops[compiler->loopDepth].breakJumps[i]);
    }
}

static void compileFor(ExtendedCompiler* compiler, ASTNode* node) {
    // Begin scope for loop variable
    compiler->scopeDepth++;

    // Compile initialization
    if (node->forStmt.init) {
        compileStatement(compiler, node->forStmt.init);
    }

    // Enter loop context
    compiler->loops[compiler->loopDepth].breakCount = 0;
    compiler->loops[compiler->loopDepth].continueCount = 0;
    compiler->loopDepth++;

    int loopStart = compiler->base.chunk->count;

    // Compile condition
    int exitJump = -1;
    if (node->forStmt.condition) {
        uint8_t condReg = compileExpression(compiler, node->forStmt.condition);
        emitBytes(compiler, OP_JUMP_IF_NOT_R, condReg);
        exitJump = compiler->base.chunk->count;
        emitBytes(compiler, 0xFF, 0xFF);

        if (!isLocalRegister(compiler, condReg)) {
            freeRegisterEx(compiler, condReg);
        }
    }

    // Compile body
    compileStatement(compiler, node->forStmt.body);

    // Continue target
    compiler->loops[compiler->loopDepth - 1].start =
        compiler->base.chunk->count;

    // Compile update
    if (node->forStmt.update) {
        compileStatement(compiler, node->forStmt.update);
    }

    // Loop back
    emitLoop(compiler, loopStart);

    // Patch exit jump
    if (exitJump != -1) {
        patchJump(compiler, exitJump);
    }

    // Patch break jumps
    compiler->loopDepth--;
    for (int i = 0; i < compiler->loops[compiler->loopDepth].breakCount; i++) {
        patchJump(compiler, compiler->loops[compiler->loopDepth].breakJumps[i]);
    }

    // End scope
    compiler->scopeDepth--;
    while (compiler->localCount > 0 &&
           compiler->locals[compiler->localCount - 1].depth >
               compiler->scopeDepth) {
        freeRegisterEx(compiler, compiler->locals[--compiler->localCount].reg);
    }
}

static void compileBlock(ExtendedCompiler* compiler, ASTNode* node) {
    compiler->scopeDepth++;

    for (int i = 0; i < node->block.count; i++) {
        compileStatement(compiler, node->block.statements[i]);
    }

    compiler->scopeDepth--;

    // Free locals from this scope
    while (compiler->localCount > 0 &&
           compiler->locals[compiler->localCount - 1].depth >
               compiler->scopeDepth) {
        freeRegisterEx(compiler, compiler->locals[--compiler->localCount].reg);
    }
}

static void compileStatement(ExtendedCompiler* compiler, ASTNode* node) {
    switch (node->type) {
        case NODE_VAR_DECL:
            compileVarDecl(compiler, node);
            break;

        case NODE_EXPRESSION_STMT: {
            uint8_t reg =
                compileExpression(compiler, node->exprStmt.expression);
            if (!isLocalRegister(compiler, reg)) {
                freeRegisterEx(compiler, reg);
            }
            break;
        }

        case NODE_PRINT:
            compilePrint(compiler, node);
            break;

        case NODE_IF:
            compileIf(compiler, node);
            break;

        case NODE_WHILE:
            compileWhile(compiler, node);
            break;

        case NODE_FOR:
            compileFor(compiler, node);
            break;

        case NODE_BLOCK:
            compileBlock(compiler, node);
            break;

        case NODE_BREAK:
            if (compiler->loopDepth == 0) {
                compiler->base.hadError = true;
                fprintf(stderr, "Error: 'break' outside of loop\n");
                break;
            }
            compiler->loops[compiler->loopDepth - 1]
                .breakJumps[compiler->loops[compiler->loopDepth - 1]
                                .breakCount++] = emitJump(compiler, OP_JUMP);
            break;

        case NODE_CONTINUE:
            if (compiler->loopDepth == 0) {
                compiler->base.hadError = true;
                fprintf(stderr, "Error: 'continue' outside of loop\n");
                break;
            }
            emitLoop(compiler, compiler->loops[compiler->loopDepth - 1].start);
            break;

        case NODE_RETURN:
            if (node->returnStmt.value) {
                uint8_t reg =
                    compileExpression(compiler, node->returnStmt.value);
                emitBytes(compiler, OP_RETURN_R, reg);
                if (!isLocalRegister(compiler, reg)) {
                    freeRegisterEx(compiler, reg);
                }
            } else {
                emitByte(compiler, OP_RETURN_VOID);
            }
            break;

        default:
            compiler->base.hadError = true;
            fprintf(stderr, "Error: Unknown statement type %d\n", node->type);
            break;
    }
}

// Main compilation function
bool compileASTToRegisterCode(ASTNode* ast, Chunk* chunk, const char* fileName,
                              const char* source) {
    ExtendedCompiler compiler;

    // Initialize base compiler
    initCompiler(&compiler.base, chunk, fileName, source);

    // Initialize extended state
    initRegisterAllocator(&compiler.allocator);
    compiler.localCount = 0;
    compiler.scopeDepth = 0;
    compiler.loopDepth = 0;

    // Compile based on node type
    if (ast->type == NODE_PROGRAM) {
        for (int i = 0; i < ast->program.count; i++) {
            compileStatement(&compiler, ast->program.declarations[i]);
        }
    } else {
        // Single statement/expression
        compileStatement(&compiler, ast);
    }

    // Emit halt instruction
    emitByte(&compiler, OP_HALT);

    // Report register usage
    if (!compiler.base.hadError) {
        printf("Compilation successful. Max registers used: %d\n",
               compiler.base.maxRegisters + 1);
    }

    return !compiler.base.hadError;
}