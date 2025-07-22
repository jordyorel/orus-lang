// multi_pass_compiler.c
// Author: Hierat
// Date: 2023-10-01
// Description: Multi-pass compiler for the Orus language - advanced
// optimizations

#include <string.h>

#include "compiler/compiler.h"
#include "compiler/loop_optimization.h"
#include "compiler/symbol_table.h"
#include "errors/features/type_errors.h"
#include "errors/features/variable_errors.h"
#include "internal/error_reporting.h"
#include "runtime/jumptable.h"
#include "runtime/memory.h"
#include "type/type.h"
#include "vm/register_file.h"
#include "vm/vm_constants.h"

// Upvalue entry for closure handling
typedef struct {
    char* name;
    int index;
    bool isLocal;
    int scope;
} UpvalueEntry;

typedef struct {
    UpvalueEntry* entries;
    int count;
    int capacity;
} UpvalueSet;

// Loop invariant entry for LICM
typedef struct {
    ASTNode* expr;
    uint8_t reg;
} InvariantEntry;

typedef struct {
    InvariantEntry* entries;
    int count;
    int capacity;
} LoopInvariants;

// Modified variables set
typedef struct {
    char** names;
    int count;
    int capacity;
} ModifiedSet;

// Multi-pass loop context with advanced features
typedef struct {
    LoopInvariants invariants;
    ModifiedSet modifiedVars;
    JumpTable breakJumps;
    JumpTable continueJumps;
    int startInstr;
    int scopeDepth;
    const char* label;
    bool isOptimized;  // Track if loop was optimized
} MultiPassLoopContext;

// Multi-pass compiler state
typedef struct {
    Compiler* base;
    UpvalueSet upvalues;
    MultiPassLoopContext* loops;
    int loopCount;
    int loopCapacity;
    LoopInvariants* currentInvariants;
    bool inFunction;

    // Analysis passes
    bool typeAnalysisComplete;
    bool scopeAnalysisComplete;
    bool optimizationComplete;
} MultiPassCompiler;

// Global multi-pass compiler instance
static MultiPassCompiler* g_multiPassCompiler = NULL;

// Forward declarations
static bool compileMultiPassNode(ASTNode* node, Compiler* compiler);
static int compileMultiPassExpr(ASTNode* node, Compiler* compiler);
static void collectUpvalues(ASTNode* node, MultiPassCompiler* mpCompiler);
void addUpvalue(UpvalueSet* upvalues, const char* name, int idx, bool isLocal, int scope);
static void analyzeLoopInvariants(ASTNode* loopBody,
                                  MultiPassCompiler* mpCompiler,
                                  LoopInvariants* invariants);

void initMultiPassCompiler(Compiler* compiler, Chunk* chunk,
                           const char* fileName, const char* source) {
    // Initialize base compiler (same as single-pass)
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

    // Initialize loop optimizer
    compiler->optimizer.enabled = true;
    compiler->optimizer.unrollCount = 0;
    compiler->optimizer.strengthReductionCount = 0;
    compiler->optimizer.boundsEliminationCount = 0;
    compiler->optimizer.totalOptimizations = 0;

    // Initialize multi-pass compiler
    g_multiPassCompiler = malloc(sizeof(MultiPassCompiler));
    g_multiPassCompiler->base = compiler;

    // Initialize upvalues
    g_multiPassCompiler->upvalues.entries = malloc(sizeof(UpvalueEntry) * 8);
    g_multiPassCompiler->upvalues.count = 0;
    g_multiPassCompiler->upvalues.capacity = 8;

    // Initialize loop contexts
    g_multiPassCompiler->loops = malloc(sizeof(MultiPassLoopContext) * 8);
    g_multiPassCompiler->loopCount = 0;
    g_multiPassCompiler->loopCapacity = 8;

    g_multiPassCompiler->currentInvariants = NULL;
    g_multiPassCompiler->inFunction = false;

    // Initialize analysis state
    g_multiPassCompiler->typeAnalysisComplete = false;
    g_multiPassCompiler->scopeAnalysisComplete = false;
    g_multiPassCompiler->optimizationComplete = false;
}

void freeMultiPassCompiler(Compiler* compiler) {
    symbol_table_free(&compiler->symbols);

    if (g_multiPassCompiler) {
        // Free upvalues
        for (int i = 0; i < g_multiPassCompiler->upvalues.count; i++) {
            free(g_multiPassCompiler->upvalues.entries[i].name);
        }
        free(g_multiPassCompiler->upvalues.entries);

        // Free loop contexts
        for (int i = 0; i < g_multiPassCompiler->loopCount; i++) {
            free(g_multiPassCompiler->loops[i].invariants.entries);
            for (int j = 0;
                 j < g_multiPassCompiler->loops[i].modifiedVars.count; j++) {
                free(g_multiPassCompiler->loops[i].modifiedVars.names[j]);
            }
            free(g_multiPassCompiler->loops[i].modifiedVars.names);
            jumptable_free(&g_multiPassCompiler->loops[i].breakJumps);
            jumptable_free(&g_multiPassCompiler->loops[i].continueJumps);
        }
        free(g_multiPassCompiler->loops);

        free(g_multiPassCompiler);
        g_multiPassCompiler = NULL;
    }
}

// Multi-pass specific utility functions
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

// PASS 1: Upvalue collection and analysis
static void collectUpvalues(ASTNode* node, MultiPassCompiler* mpCompiler) {
    if (!node) return;

    switch (node->type) {
        case NODE_IDENTIFIER: {
            bool found = false;
            // Look for local variable in current scope
            for (int i = 0; i < mpCompiler->base->localCount; i++) {
                if (mpCompiler->base->locals[i].isActive &&
                    strcmp(mpCompiler->base->locals[i].name,
                           node->identifier.name) == 0) {
                    found = true;
                    break;
                }
            }

            if (!found) {
                // Add as upvalue if not found locally
                addUpvalue(&mpCompiler->upvalues, node->identifier.name, 0,
                           true, 0);
            }
            break;
        }
        case NODE_BINARY:
            collectUpvalues(node->binary.left, mpCompiler);
            collectUpvalues(node->binary.right, mpCompiler);
            break;
        case NODE_CALL:
            collectUpvalues(node->call.callee, mpCompiler);
            for (int i = 0; i < node->call.argCount; i++) {
                collectUpvalues(node->call.args[i], mpCompiler);
            }
            break;
        case NODE_BLOCK:
            for (int i = 0; i < node->block.count; i++) {
                collectUpvalues(node->block.statements[i], mpCompiler);
            }
            break;
        default:
            break;
    }
}

void addUpvalue(UpvalueSet* upvalues, const char* name, int idx,
                       bool isLocal, int scope) {
    // Check if already added
    for (int i = 0; i < upvalues->count; i++) {
        if (strcmp(upvalues->entries[i].name, name) == 0) return;
    }

    // Resize if needed
    if (upvalues->count >= upvalues->capacity) {
        upvalues->capacity *= 2;
        upvalues->entries = realloc(upvalues->entries,
                                    upvalues->capacity * sizeof(UpvalueEntry));
    }

    UpvalueEntry* entry = &upvalues->entries[upvalues->count++];
    entry->name = strdup(name);
    entry->index = idx;
    entry->isLocal = isLocal;
    entry->scope = scope;
}

// PASS 2: Modified variable analysis
static void addModified(ModifiedSet* set, const char* name) {
    // Check if already added
    for (int i = 0; i < set->count; i++) {
        if (strcmp(set->names[i], name) == 0) return;
    }

    // Resize if needed
    if (set->count >= set->capacity) {
        set->capacity = set->capacity ? set->capacity * 2 : 8;
        set->names = realloc(set->names, set->capacity * sizeof(char*));
    }

    set->names[set->count++] = strdup(name);
}

static void collectModifiedVariables(ASTNode* node, ModifiedSet* modified) {
    if (!node) return;

    switch (node->type) {
        case NODE_ASSIGN:
            addModified(modified, node->assign.name);
            collectModifiedVariables(node->assign.value, modified);
            break;
        case NODE_VAR_DECL:
            if (node->varDecl.name) {
                addModified(modified, node->varDecl.name);
            }
            if (node->varDecl.initializer) {
                collectModifiedVariables(node->varDecl.initializer, modified);
            }
            break;
        case NODE_BINARY:
            collectModifiedVariables(node->binary.left, modified);
            collectModifiedVariables(node->binary.right, modified);
            break;
        case NODE_CALL:
            collectModifiedVariables(node->call.callee, modified);
            for (int i = 0; i < node->call.argCount; i++) {
                collectModifiedVariables(node->call.args[i], modified);
            }
            break;
        case NODE_BLOCK:
            for (int i = 0; i < node->block.count; i++) {
                collectModifiedVariables(node->block.statements[i], modified);
            }
            break;
        default:
            break;
    }
}

// PASS 3: Loop invariant analysis (LICM)
static bool dependsOnModified(ASTNode* node, ModifiedSet* modified) {
    if (!node) return false;

    switch (node->type) {
        case NODE_IDENTIFIER:
            for (int i = 0; i < modified->count; i++) {
                if (strcmp(node->identifier.name, modified->names[i]) == 0) {
                    return true;
                }
            }
            return false;
        case NODE_BINARY:
            return dependsOnModified(node->binary.left, modified) ||
                   dependsOnModified(node->binary.right, modified);
        case NODE_CALL:
            if (dependsOnModified(node->call.callee, modified)) return true;
            for (int i = 0; i < node->call.argCount; i++) {
                if (dependsOnModified(node->call.args[i], modified))
                    return true;
            }
            return false;
        default:
            return false;
    }
}

bool hasSideEffects(ASTNode* node) {
    if (!node) return false;

    switch (node->type) {
        case NODE_CALL:
            return true;  // Function calls may have side effects
        case NODE_ASSIGN:
        case NODE_VAR_DECL:
            return true;  // Assignments modify state
        case NODE_BINARY:
            return hasSideEffects(node->binary.left) ||
                   hasSideEffects(node->binary.right);
        case NODE_IDENTIFIER:
        case NODE_LITERAL:
            return false;
        default:
            return false;
    }
}

static void analyzeLoopInvariants(ASTNode* loopBody,
                                  MultiPassCompiler* mpCompiler,
                                  LoopInvariants* invariants) {
    ModifiedSet modified = {0};
    collectModifiedVariables(loopBody, &modified);

    // Initialize invariants
    invariants->entries = NULL;
    invariants->count = 0;
    invariants->capacity = 0;

    // TODO: Traverse AST to find candidate expressions that:
    // 1. Don't depend on modified variables
    // 2. Don't have side effects
    // 3. Are worth hoisting

    // For now, just initialize the structure

    // Free modified set
    for (int i = 0; i < modified.count; i++) {
        free(modified.names[i]);
    }
    free(modified.names);
}

// Jump table management
static void patchBreakJumps(JumpTable* table, Compiler* compiler) {
    for (int i = 0; i < table->offsets.count; i++) {
        int offset = table->offsets.data[i];
        int jump = compiler->chunk->count - offset - 2;
        if (jump <= UINT16_MAX) {
            compiler->chunk->code[offset] = (jump >> 8) & 0xff;
            compiler->chunk->code[offset + 1] = jump & 0xff;
        }
    }
}

static void patchContinueJumps(JumpTable* table, Compiler* compiler,
                               int continueTarget) {
    for (int i = 0; i < table->offsets.count; i++) {
        int offset = table->offsets.data[i];

        // Change OP_JUMP to OP_LOOP
        compiler->chunk->code[offset - 1] = OP_LOOP;

        int loopOffset = (offset + 2) - continueTarget;
        printf("DEBUG: Continue patch - offset=%d, continueTarget=%d, loopOffset=%d\n", 
               offset, continueTarget, loopOffset);
        if (loopOffset > 0 && loopOffset <= UINT16_MAX) {
            compiler->chunk->code[offset] = (loopOffset >> 8) & 0xff;
            compiler->chunk->code[offset + 1] = loopOffset & 0xff;
            printf("DEBUG: Continue patch SUCCESS\n");
        } else {
            printf("DEBUG: Continue patch FAILED - using original bytes\n");
        }
    }
}

// Expression compilation with multi-pass features
static int compileLiteral(ASTNode* node, Compiler* compiler) {
    uint8_t reg = allocateRegister(compiler);
    emitConstant(compiler, reg, node->literal.value);
    return reg;
}

static int compileIdentifier(ASTNode* node, Compiler* compiler) {
    MultiPassCompiler* mpCompiler = g_multiPassCompiler;

    // Try locals first
    int localIndex = findLocal(compiler, node->identifier.name);
    if (localIndex >= 0) {
        return compiler->locals[localIndex].reg;
    }

    // Check upvalues if in function
    if (mpCompiler && mpCompiler->inFunction) {
        for (int i = 0; i < mpCompiler->upvalues.count; i++) {
            if (strcmp(mpCompiler->upvalues.entries[i].name,
                       node->identifier.name) == 0) {
                uint8_t reg = allocateRegister(compiler);
                emitByte(compiler, OP_GET_UPVALUE_R);
                emitByte(compiler, reg);
                emitByte(compiler, (uint8_t)i);
                return reg;
            }
        }
    }

    report_undefined_variable(node->location, node->identifier.name);
    return -1;
}

static int compileBinaryOp(ASTNode* node, Compiler* compiler) {
    int leftReg = compileMultiPassExpr(node->binary.left, compiler);
    int rightReg = compileMultiPassExpr(node->binary.right, compiler);
    uint8_t resultReg = allocateRegister(compiler);

    // Enhanced binary operations with type awareness
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

static int compileMultiPassExpr(ASTNode* node, Compiler* compiler) {
    if (!node) return -1;

    MultiPassCompiler* mpCompiler = g_multiPassCompiler;

    // Check if this is a hoisted invariant
    if (mpCompiler && mpCompiler->currentInvariants) {
        for (int i = 0; i < mpCompiler->currentInvariants->count; i++) {
            if (mpCompiler->currentInvariants->entries[i].expr == node) {
                return mpCompiler->currentInvariants->entries[i].reg;
            }
        }
    }

    switch (node->type) {
        case NODE_LITERAL:
            return compileLiteral(node, compiler);
        case NODE_IDENTIFIER:
            return compileIdentifier(node, compiler);
        case NODE_BINARY:
            return compileBinaryOp(node, compiler);
        case NODE_TIME_STAMP: {
            uint8_t resultReg = allocateRegister(compiler);
            emitByte(compiler, OP_TIME_STAMP);
            emitByte(compiler, resultReg);
            return resultReg;
        }
        case NODE_CALL: {
            // Enhanced function call handling with upvalue support
            if (node->call.callee &&
                node->call.callee->type == NODE_IDENTIFIER) {
                const char* funcName = node->call.callee->identifier.name;

                if (strcmp(funcName, "time_stamp") == 0) {
                    if (node->call.argCount != 0) {
                        SrcLocation loc = {.file = compiler->fileName,
                                           .line = node->location.line,
                                           .column = node->location.column};
                        report_compile_error(E1006_INVALID_SYNTAX, loc,
                                             "time_stamp() takes no arguments");
                        return -1;
                    }
                    uint8_t resultReg = allocateRegister(compiler);
                    emitByte(compiler, OP_TIME_STAMP);
                    emitByte(compiler, resultReg);
                    return resultReg;
                }
            }

            // Regular function call
            int funcReg = compileMultiPassExpr(node->call.callee, compiler);
            int resultReg = allocateRegister(compiler);

            int firstArgReg = 0;
            if (node->call.argCount > 0) {
                firstArgReg = compiler->nextRegister;
                for (int i = 0; i < node->call.argCount; i++) {
                    int targetReg = firstArgReg + i;
                    int argReg =
                        compileMultiPassExpr(node->call.args[i], compiler);

                    if (argReg != targetReg) {
                        emitByte(compiler, OP_MOVE);
                        emitByte(compiler, targetReg);
                        emitByte(compiler, argReg);
                        freeRegister(compiler, argReg);
                    }

                    if (targetReg >= compiler->nextRegister) {
                        compiler->nextRegister = targetReg + 1;
                        if (compiler->nextRegister > compiler->maxRegisters)
                            compiler->maxRegisters = compiler->nextRegister;
                    }
                }
            }

            emitByte(compiler, OP_CALL_R);
            emitByte(compiler, funcReg);
            emitByte(compiler, firstArgReg);
            emitByte(compiler, node->call.argCount);
            emitByte(compiler, resultReg);

            freeRegister(compiler, funcReg);
            return resultReg;
        }
        default:
            SrcLocation loc = {.file = compiler->fileName,
                               .line = node->location.line,
                               .column = node->location.column};
            report_compile_error(E1006_INVALID_SYNTAX, loc,
                                 "Unsupported expression type in multi-pass");
            return -1;
    }
}

static bool compileMultiPassNode(ASTNode* node, Compiler* compiler) {
    if (!node) return true;

    MultiPassCompiler* mpCompiler = g_multiPassCompiler;
    compiler->currentLine = node->location.line;
    compiler->currentColumn = node->location.column;

    switch (node->type) {
        case NODE_PROGRAM:
            for (int i = 0; i < node->program.count; i++) {
                if (!compileMultiPassNode(node->program.declarations[i],
                                          compiler)) {
                    return false;
                }
            }
            return true;

        case NODE_FUNCTION: {
            // MULTI-PASS FUNCTION COMPILATION
            printf("DEBUG: Multi-pass function compilation for '%s'\n",
                   node->function.name);

            // PASS 1: Upvalue analysis
            mpCompiler->inFunction = true;
            UpvalueSet oldUpvalues = mpCompiler->upvalues;
            mpCompiler->upvalues.entries = malloc(sizeof(UpvalueEntry) * 8);
            mpCompiler->upvalues.count = 0;
            mpCompiler->upvalues.capacity = 8;

            collectUpvalues(node->function.body, mpCompiler);

            // PASS 2: Create function object
            extern VM vm;
            int functionIdx = vm.functionCount++;
            ObjFunction* objFunction = allocateFunction();
            objFunction->name = allocateString(node->function.name,
                                               strlen(node->function.name));
            objFunction->arity = node->function.paramCount;
            objFunction->chunk = malloc(sizeof(Chunk));
            initChunk(objFunction->chunk);
            objFunction->upvalueCount = mpCompiler->upvalues.count;

            Function function;
            function.start = 0;
            function.arity = node->function.paramCount;
            function.chunk = objFunction->chunk;
            vm.functions[functionIdx] = function;

            // PASS 3: Create function compiler
            Compiler functionCompiler;
            initMultiPassCompiler(&functionCompiler, objFunction->chunk,
                                  compiler->fileName, compiler->source);
            functionCompiler.scopeDepth = compiler->scopeDepth + 1;
            functionCompiler.currentFunctionParameterCount =
                node->function.paramCount;

            // PASS 4: Setup upvalues
            for (int i = 0; i < mpCompiler->upvalues.count; i++) {
                UpvalueEntry* upvalue = &mpCompiler->upvalues.entries[i];
                int closureIndex = -(2000 + i);
                symbol_table_set(&functionCompiler.symbols, upvalue->name,
                                 closureIndex, 0);
            }

            // PASS 5: Add parameters
            for (int i = 0; i < node->function.paramCount; i++) {
                FunctionParam* param = &node->function.params[i];

                functionCompiler.locals[functionCompiler.localCount].name =
                    param->name;
                functionCompiler.locals[functionCompiler.localCount].reg = i;
                functionCompiler.locals[functionCompiler.localCount].isActive =
                    true;
                functionCompiler.locals[functionCompiler.localCount].depth =
                    functionCompiler.scopeDepth;
                functionCompiler.locals[functionCompiler.localCount].isMutable =
                    true;
                functionCompiler.locals[functionCompiler.localCount].type =
                    VAL_NIL;
                functionCompiler.locals[functionCompiler.localCount]
                    .liveRangeIndex = -1;
                functionCompiler.locals[functionCompiler.localCount].isSpilled =
                    false;
                functionCompiler.locals[functionCompiler.localCount]
                    .hasKnownType = false;

                symbol_table_set(&functionCompiler.symbols, param->name,
                                 functionCompiler.localCount,
                                 functionCompiler.scopeDepth);
                functionCompiler.localCount++;
            }

            // PASS 6: Compile function body
            bool success =
                compileMultiPassNode(node->function.body, &functionCompiler);

            if (success) {
                if (node->function.returnType == NULL) {
                    emitByte(&functionCompiler, OP_RETURN_VOID);
                }

                // Store as global
                int globalIdx = vm.variableCount++;
                vm.variableNames[globalIdx].name = objFunction->name;
                vm.variableNames[globalIdx].length = objFunction->name->length;
                vm.globals[globalIdx] = FUNCTION_VAL(objFunction);
                vm.globalTypes[globalIdx] = getPrimitiveType(TYPE_FUNCTION);
                vm.mutableGlobals[globalIdx] = false;

                symbol_table_set(&compiler->symbols, objFunction->name->chars,
                                 globalIdx, compiler->scopeDepth);
            }

            // Cleanup
            for (int i = 0; i < mpCompiler->upvalues.count; i++) {
                free(mpCompiler->upvalues.entries[i].name);
            }
            free(mpCompiler->upvalues.entries);
            mpCompiler->upvalues = oldUpvalues;
            mpCompiler->inFunction = false;

            return success && !functionCompiler.hadError;
        }

        case NODE_FOR_RANGE: {
            // MULTI-PASS LOOP COMPILATION WITH OPTIMIZATIONS

            // PASS 1: Try loop optimization first
            if (optimizeLoop(node, compiler)) {
                return true;
            }

            // PASS 2: Enhanced loop analysis
            beginScope(compiler);

            if (mpCompiler->loopCount >= mpCompiler->loopCapacity) {
                mpCompiler->loopCapacity *= 2;
                mpCompiler->loops = realloc(
                    mpCompiler->loops,
                    mpCompiler->loopCapacity * sizeof(MultiPassLoopContext));
            }

            MultiPassLoopContext* context =
                &mpCompiler->loops[mpCompiler->loopCount++];
            context->breakJumps = jumptable_new();
            context->continueJumps = jumptable_new();
            context->label = NULL;
            context->isOptimized = false;
            context->scopeDepth = compiler->scopeDepth;

            // PASS 3: Analyze loop for invariants and modifications
            analyzeLoopInvariants(node->forRange.body, mpCompiler,
                                  &context->invariants);
            collectModifiedVariables(node->forRange.body,
                                     &context->modifiedVars);

            // PASS 4: Hoist invariants (LICM)
            for (int i = 0; i < context->invariants.count; i++) {
                uint8_t reg = context->invariants.entries[i].reg;
                int tempReg = compileMultiPassExpr(
                    context->invariants.entries[i].expr, compiler);
                emitByte(compiler, OP_MOVE);
                emitByte(compiler, reg);
                emitByte(compiler, tempReg);
                freeRegister(compiler, tempReg);
            }

            // PASS 5: Compile loop with enhanced features
            int startReg = compileMultiPassExpr(node->forRange.start, compiler);
            int endReg = compileMultiPassExpr(node->forRange.end, compiler);

            int loopVarIndex =
                addLocal(compiler, node->forRange.varName, false);
            if (loopVarIndex < 0) {
                SrcLocation loc = {.file = compiler->fileName,
                                   .line = node->location.line,
                                   .column = node->location.column};
                report_compile_error(E1009_EXPRESSION_TOO_COMPLEX, loc,
                                     "Too many local variables");
                endScope(compiler);
                mpCompiler->loopCount--;
                return false;
            }

            uint8_t iterReg = compiler->locals[loopVarIndex].reg;

            // Initialize iterator
            emitByte(compiler, OP_MOVE);
            emitByte(compiler, iterReg);
            emitByte(compiler, startReg);

            int loopStart = compiler->chunk->count;
            context->startInstr = loopStart;

            // Check condition
            uint8_t condReg = allocateRegister(compiler);
            emitByte(compiler, OP_LE_I32_R);
            emitByte(compiler, condReg);
            emitByte(compiler, iterReg);
            emitByte(compiler, endReg);

            emitByte(compiler, OP_JUMP_IF_NOT_R);
            emitByte(compiler, condReg);
            int exitJump = emitJump(compiler);

            freeRegister(compiler, condReg);

            // PASS 6: Compile body with invariants available
            mpCompiler->currentInvariants = &context->invariants;
            bool success = compileMultiPassNode(node->forRange.body, compiler);
            mpCompiler->currentInvariants = NULL;

            if (!success) {
                endScope(compiler);
                mpCompiler->loopCount--;
                return false;
            }

            // PASS 7: Handle continue statements - patch to condition check
            int continueTarget = loopStart;
            patchContinueJumps(&context->continueJumps, compiler,
                               continueTarget);

            // Increment iterator
            emitByte(compiler, OP_INC_I32_R);
            emitByte(compiler, iterReg);

            // Jump back to condition
            emitLoop(compiler, loopStart);

            // PASS 8: Patch exit and break jumps
            patchJump(compiler, exitJump);
            patchBreakJumps(&context->breakJumps, compiler);

            // Cleanup
            endScope(compiler);
            free(context->invariants.entries);
            for (int j = 0; j < context->modifiedVars.count; j++) {
                free(context->modifiedVars.names[j]);
            }
            free(context->modifiedVars.names);
            jumptable_free(&context->breakJumps);
            jumptable_free(&context->continueJumps);

            mpCompiler->loopCount--;
            freeRegister(compiler, startReg);
            freeRegister(compiler, endReg);

            return true;
        }

        case NODE_WHILE: {
            // MULTI-PASS WHILE LOOP COMPILATION
            beginScope(compiler);

            if (mpCompiler->loopCount >= mpCompiler->loopCapacity) {
                mpCompiler->loopCapacity *= 2;
                mpCompiler->loops = realloc(
                    mpCompiler->loops,
                    mpCompiler->loopCapacity * sizeof(MultiPassLoopContext));
            }

            MultiPassLoopContext* context =
                &mpCompiler->loops[mpCompiler->loopCount++];
            context->breakJumps = jumptable_new();
            context->continueJumps = jumptable_new();
            context->label = NULL;
            context->isOptimized = false;
            context->scopeDepth = compiler->scopeDepth;

            // Analyze loop
            analyzeLoopInvariants(node->whileStmt.body, mpCompiler,
                                  &context->invariants);
            collectModifiedVariables(node->whileStmt.body,
                                     &context->modifiedVars);

            // Hoist invariants
            for (int i = 0; i < context->invariants.count; i++) {
                uint8_t reg = context->invariants.entries[i].reg;
                int tempReg = compileMultiPassExpr(
                    context->invariants.entries[i].expr, compiler);
                emitByte(compiler, OP_MOVE);
                emitByte(compiler, reg);
                emitByte(compiler, tempReg);
                freeRegister(compiler, tempReg);
            }

            int loopStart = compiler->chunk->count;
            context->startInstr = loopStart;

            int conditionReg =
                compileMultiPassExpr(node->whileStmt.condition, compiler);
            if (conditionReg < 0) {
                endScope(compiler);
                mpCompiler->loopCount--;
                return false;
            }

            emitByte(compiler, OP_JUMP_IF_NOT_R);
            emitByte(compiler, conditionReg);
            int exitJump = emitJump(compiler);

            freeRegister(compiler, conditionReg);

            // Compile body with invariants available
            mpCompiler->currentInvariants = &context->invariants;
            bool success = compileMultiPassNode(node->whileStmt.body, compiler);
            mpCompiler->currentInvariants = NULL;

            if (!success) {
                endScope(compiler);
                mpCompiler->loopCount--;
                return false;
            }

            // Patch continue jumps to loop start
            patchContinueJumps(&context->continueJumps, compiler, loopStart);

            emitLoop(compiler, loopStart);
            patchJump(compiler, exitJump);
            patchBreakJumps(&context->breakJumps, compiler);

            // Cleanup
            endScope(compiler);
            free(context->invariants.entries);
            for (int j = 0; j < context->modifiedVars.count; j++) {
                free(context->modifiedVars.names[j]);
            }
            free(context->modifiedVars.names);
            jumptable_free(&context->breakJumps);
            jumptable_free(&context->continueJumps);

            mpCompiler->loopCount--;

            return true;
        }

        case NODE_BREAK: {
            // MULTI-PASS BREAK HANDLING
            if (mpCompiler->loopCount == 0) {
                SrcLocation loc = {.file = compiler->fileName,
                                   .line = node->location.line,
                                   .column = node->location.column};
                report_compile_error(E1006_INVALID_SYNTAX, loc,
                                     "break statement outside of loop");
                return false;
            }

            // Find target loop (default to innermost unless labeled)
            MultiPassLoopContext* targetLoop = NULL;
            if (node->breakStmt.label) {
                // Find labeled loop
                for (int i = mpCompiler->loopCount - 1; i >= 0; i--) {
                    if (mpCompiler->loops[i].label &&
                        strcmp(mpCompiler->loops[i].label,
                               node->breakStmt.label) == 0) {
                        targetLoop = &mpCompiler->loops[i];
                        break;
                    }
                }
                if (!targetLoop) {
                    SrcLocation loc = {.file = compiler->fileName,
                                       .line = node->location.line,
                                       .column = node->location.column};
                    report_compile_error(
                        E1006_INVALID_SYNTAX, loc,
                        "Undefined loop label in break statement");
                    return false;
                }
            } else {
                // Find the loop that matches the current syntactic scope depth
                // The break should target the loop whose scopeDepth matches current scope
                targetLoop = NULL;
                for (int i = mpCompiler->loopCount - 1; i >= 0; i--) {
                    if (mpCompiler->loops[i].scopeDepth == compiler->scopeDepth) {
                        targetLoop = &mpCompiler->loops[i];
                        break;
                    }
                }
                
                // If no exact scope match found, fall back to innermost loop
                if (!targetLoop) {
                    targetLoop = &mpCompiler->loops[mpCompiler->loopCount - 1];
                }
            }

            emitByte(compiler, OP_JUMP);
            int breakJump = emitJump(compiler);
            jumptable_add(&targetLoop->breakJumps, breakJump);

            return true;
        }

        case NODE_CONTINUE: {
            // MULTI-PASS CONTINUE HANDLING
            if (mpCompiler->loopCount == 0) {
                SrcLocation loc = {.file = compiler->fileName,
                                   .line = node->location.line,
                                   .column = node->location.column};
                report_compile_error(E1006_INVALID_SYNTAX, loc,
                                     "continue statement outside of loop");
                return false;
            }

            // Find target loop (default to innermost unless labeled)
            MultiPassLoopContext* targetLoop = NULL;
            if (node->continueStmt.label) {
                // Find labeled loop
                for (int i = mpCompiler->loopCount - 1; i >= 0; i--) {
                    if (mpCompiler->loops[i].label &&
                        strcmp(mpCompiler->loops[i].label,
                               node->continueStmt.label) == 0) {
                        targetLoop = &mpCompiler->loops[i];
                        break;
                    }
                }
                if (!targetLoop) {
                    SrcLocation loc = {.file = compiler->fileName,
                                       .line = node->location.line,
                                       .column = node->location.column};
                    report_compile_error(
                        E1006_INVALID_SYNTAX, loc,
                        "Undefined loop label in continue statement");
                    return false;
                }
            } else {
                // Find the loop that matches the current syntactic scope depth
                // The continue should target the loop whose scopeDepth matches current scope
                targetLoop = NULL;
                for (int i = mpCompiler->loopCount - 1; i >= 0; i--) {
                    if (mpCompiler->loops[i].scopeDepth == compiler->scopeDepth) {
                        targetLoop = &mpCompiler->loops[i];
                        break;
                    }
                }
                
                // If no exact scope match found, fall back to innermost loop
                if (!targetLoop) {
                    targetLoop = &mpCompiler->loops[mpCompiler->loopCount - 1];
                }
            }

            emitByte(compiler, OP_JUMP);
            int continueJump = emitJump(compiler);
            jumptable_add(&targetLoop->continueJumps, continueJump);

            return true;
        }

        case NODE_ASSIGN: {
            int valueReg = compileMultiPassExpr(node->assign.value, compiler);
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

            // Check upvalues
            if (mpCompiler && mpCompiler->inFunction) {
                for (int i = 0; i < mpCompiler->upvalues.count; i++) {
                    if (strcmp(mpCompiler->upvalues.entries[i].name,
                               node->assign.name) == 0) {
                        emitByte(compiler, OP_SET_UPVALUE_R);
                        emitByte(compiler, (uint8_t)i);
                        emitByte(compiler, valueReg);
                        freeRegister(compiler, valueReg);
                        return true;
                    }
                }
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
                    compileMultiPassExpr(node->varDecl.initializer, compiler);
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
                if (!compileMultiPassNode(node->block.statements[i],
                                          compiler)) {
                    return false;
                }
            }
            return true;

        case NODE_PRINT: {
            if (node->print.count == 0) {
                uint8_t r = allocateRegister(compiler);
                emitByte(compiler, OP_LOAD_NIL);
                emitByte(compiler, r);
                emitByte(compiler, OP_PRINT_R);
                emitByte(compiler, r);
                freeRegister(compiler, r);
            } else if (node->print.count == 1) {
                int valueReg =
                    compileMultiPassExpr(node->print.values[0], compiler);
                if (valueReg < 0) return false;

                emitByte(compiler, OP_PRINT_R);
                emitByte(compiler, valueReg);
                freeRegister(compiler, valueReg);
            } else {
                // Multi-argument print
                uint8_t firstReg = compiler->nextRegister;
                uint8_t* argRegs = malloc(node->print.count * sizeof(uint8_t));

                for (int i = 0; i < node->print.count; i++) {
                    argRegs[i] = allocateRegister(compiler);
                }

                for (int i = 0; i < node->print.count; i++) {
                    int valueReg =
                        compileMultiPassExpr(node->print.values[i], compiler);
                    if (valueReg < 0) {
                        for (int j = 0; j < node->print.count; j++) {
                            freeRegister(compiler, argRegs[j]);
                        }
                        free(argRegs);
                        return false;
                    }

                    if (valueReg != argRegs[i]) {
                        emitByte(compiler, OP_MOVE);
                        emitByte(compiler, argRegs[i]);
                        emitByte(compiler, valueReg);
                        freeRegister(compiler, valueReg);
                    }
                }

                emitByte(compiler, OP_PRINT_MULTI_R);
                emitByte(compiler, firstReg);
                emitByte(compiler, (uint8_t)node->print.count);
                emitByte(compiler, node->print.newline ? 1 : 0);

                for (int i = 0; i < node->print.count; i++) {
                    freeRegister(compiler, argRegs[i]);
                }
                free(argRegs);
            }
            return true;
        }

        case NODE_IF: {
            int conditionReg =
                compileMultiPassExpr(node->ifStmt.condition, compiler);
            if (conditionReg < 0) return false;

            emitByte(compiler, OP_JUMP_IF_NOT_R);
            emitByte(compiler, conditionReg);
            int thenJump = emitJump(compiler);

            freeRegister(compiler, conditionReg);

            beginScope(compiler);
            bool success =
                compileMultiPassNode(node->ifStmt.thenBranch, compiler);
            endScope(compiler);

            if (!success) return false;

            if (node->ifStmt.elseBranch) {
                emitByte(compiler, OP_JUMP);
                int elseJump = emitJump(compiler);

                patchJump(compiler, thenJump);

                beginScope(compiler);
                success =
                    compileMultiPassNode(node->ifStmt.elseBranch, compiler);
                endScope(compiler);

                if (!success) return false;
                patchJump(compiler, elseJump);
            } else {
                patchJump(compiler, thenJump);
            }

            return true;
        }

        case NODE_RETURN: {
            if (node->returnStmt.value) {
                int valueReg =
                    compileMultiPassExpr(node->returnStmt.value, compiler);
                if (valueReg < 0) return false;
                emitByte(compiler, OP_RETURN_R);
                emitByte(compiler, valueReg);
                freeRegister(compiler, valueReg);
            } else {
                emitByte(compiler, OP_RETURN_VOID);
            }
            return true;
        }

        default: {
            int reg = compileMultiPassExpr(node, compiler);
            if (reg >= 0) {
                freeRegister(compiler, reg);
                return true;
            }
            return false;
        }
    }
}

bool compileMultiPass(ASTNode* ast, Compiler* compiler, bool isModule) {
    if (!ast) return false;

    MultiPassCompiler* mpCompiler = g_multiPassCompiler;

    // PASS 1: Type analysis (placeholder for now)
    mpCompiler->typeAnalysisComplete = true;

    // PASS 2: Scope analysis (placeholder for now)
    mpCompiler->scopeAnalysisComplete = true;

    // PASS 3: Main compilation with optimizations
    bool success = compileMultiPassNode(ast, compiler);

    // PASS 4: Post-compilation optimizations (placeholder for now)
    mpCompiler->optimizationComplete = true;

    if (success && !isModule) {
        emitByte(compiler, OP_RETURN_VOID);
    }

    return success && !compiler->hadError;
}

// Interface functions for compatibility
bool compileExpression(ASTNode* node, Compiler* compiler) {
    int reg = compileMultiPassExpr(node, compiler);
    if (reg >= 0) {
        freeRegister(compiler, reg);
        return true;
    }
    return false;
}

int compileExpressionToRegister(ASTNode* node, Compiler* compiler) {
    return compileMultiPassExpr(node, compiler);
}

int compile_typed_expression_to_register(ASTNode* node, Compiler* compiler) {
    return compileMultiPassExpr(node, compiler);
}

int compileExpressionToRegister_new(ASTNode* node, Compiler* compiler) {
    return compileMultiPassExpr(node, compiler);
}

// Placeholder implementations for advanced features
void initCompilerTypeInference(Compiler* compiler) { (void)compiler; }

void freeCompilerTypeInference(Compiler* compiler) { (void)compiler; }

Type* inferExpressionType(Compiler* compiler, ASTNode* expr) {
    (void)compiler;
    (void)expr;
    return NULL;
}

bool resolveVariableType(Compiler* compiler, const char* name,
                         Type* inferredType) {
    (void)compiler;
    (void)name;
    (void)inferredType;
    return true;
}

ValueType typeKindToValueType(TypeKind kind) {
    switch (kind) {
        case TYPE_I32:
            return VAL_I32;
        case TYPE_I64:
            return VAL_I64;
        case TYPE_U32:
            return VAL_U32;
        case TYPE_U64:
            return VAL_U64;
        case TYPE_F64:
            return VAL_F64;
        case TYPE_BOOL:
            return VAL_BOOL;
        case TYPE_STRING:
            return VAL_STRING;
        default:
            return VAL_I32;
    }
}

TypeKind valueTypeToTypeKind(ValueType vtype) {
    switch (vtype) {
        case VAL_I32:
            return TYPE_I32;
        case VAL_I64:
            return TYPE_I64;
        case VAL_U32:
            return TYPE_U32;
        case VAL_U64:
            return TYPE_U64;
        case VAL_F64:
            return TYPE_F64;
        case VAL_BOOL:
            return TYPE_BOOL;
        case VAL_STRING:
            return TYPE_STRING;
        default:
            return TYPE_I32;
    }
}

bool canEmitTypedInstruction(Compiler* compiler, ASTNode* left, ASTNode* right,
                             ValueType* outType) {
    (void)compiler;
    (void)left;
    (void)right;
    if (outType) *outType = VAL_I32;
    return false;
}

void emitTypedBinaryOp(Compiler* compiler, const char* op, ValueType type,
                       uint8_t dst, uint8_t left, uint8_t right) {
    (void)type;
    if (strcmp(op, "+") == 0) {
        emitByte(compiler, OP_ADD_I32_R);
    } else if (strcmp(op, "-") == 0) {
        emitByte(compiler, OP_SUB_I32_R);
    } else if (strcmp(op, "*") == 0) {
        emitByte(compiler, OP_MUL_I32_R);
    } else if (strcmp(op, "/") == 0) {
        emitByte(compiler, OP_DIV_I32_R);
    } else {
        emitByte(compiler, OP_ADD_I32_R);
    }
    emitByte(compiler, dst);
    emitByte(compiler, left);
    emitByte(compiler, right);
}