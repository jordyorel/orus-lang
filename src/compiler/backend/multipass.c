// Project: Orus Language Compiler
// Author: KONDA JORDY OREL
// Date: 2025-07-01

/*
  * A multi-pass compiler for the Orus language, designed to optimize
  * code generation through advanced techniques like loop optimization,
  * register allocation, and closure handling.
  * 
*/ 



#include <stdio.h>
#include <string.h>

#include "compiler/compiler.h"
#include "compiler/loop_optimization.h"
#include "compiler/symbol_table.h"
#include "compiler/vm_optimization.h"
#include "compiler/node_registry.h"
#include "errors/features/type_errors.h"
#include "errors/features/variable_errors.h"
#include "internal/error_reporting.h"
#include "internal/logging.h"
#include "runtime/jumptable.h"
#include "runtime/memory.h"
#include "tools/scope_analysis.h"
#include "type/type.h"
#include "vm/register_file.h"
#include "vm/vm_constants.h"

// Forward declarations
void initMultiPassCompiler(Compiler* compiler, Chunk* chunk, const char* fileName, const char* source);
bool compileMultiPass(ASTNode* ast, Compiler* compiler, bool isModule);
bool compileMultiPassNode(ASTNode* node, Compiler* compiler);

// Core compiler functions (previously in hybrid_compiler.c)
uint16_t allocateRegister(Compiler* compiler) {
    if (!compiler) {
        return 65535; // Invalid register
    }
    
    // PRIORITY 1: Global registers (0-255) - bytecode compatible
    bool register_used[GLOBAL_REGISTERS] = {true}; // Reserve register 0
    
    // Mark registers used by active local variables in global space
    for (int i = 0; i < compiler->localCount; i++) {
        if (compiler->locals[i].isActive && compiler->locals[i].reg < GLOBAL_REGISTERS) {
            register_used[compiler->locals[i].reg] = true;
        }
    }
    
    // Try to find a free register in global space (0-255) - preferred for bytecode compatibility
    for (uint16_t reg = 1; reg < GLOBAL_REGISTERS; reg++) {
        if (!register_used[reg]) {
            // Update nextRegister only if we're allocating beyond current high water mark
            if (reg >= compiler->nextRegister && reg < GLOBAL_REGISTERS) {
                compiler->nextRegister = reg + 1;
            }
            return reg;
        }
    }
    
    // PRIORITY 2: Use extended registers (VM register file integration)
    printf("[INFO] Using extended register space (>255) - activating Phase 2 extended opcodes\n");
    
    // Check frame registers (256-319) 
    for (uint16_t reg = FRAME_REG_START; reg < FRAME_REG_START + FRAME_REGISTERS; reg++) {
        bool frame_reg_used = false;
        for (int i = 0; i < compiler->localCount; i++) {
            if (compiler->locals[i].isActive && compiler->locals[i].reg == reg) {
                frame_reg_used = true;
                break;
            }
        }
        if (!frame_reg_used) {
            return reg;
        }
    }
    
    // Check temp registers (320-351)
    for (uint16_t reg = TEMP_REG_START; reg < TEMP_REG_START + TEMP_REGISTERS; reg++) {
        bool temp_reg_used = false;
        for (int i = 0; i < compiler->localCount; i++) {
            if (compiler->locals[i].isActive && compiler->locals[i].reg == reg) {
                temp_reg_used = true;
                break;
            }
        }
        if (!temp_reg_used) {
            return reg;
        }
    }
    
    // PRIORITY 3: Module registers (352-479) - now activated! (Phase 2.2)
    printf("[INFO] Using module register space (352-479) - large program mode activated\n");
    
    for (uint16_t reg = MODULE_REG_START; reg < MODULE_REG_START + MODULE_REGISTERS; reg++) {
        bool module_reg_used = false;
        for (int i = 0; i < compiler->localCount; i++) {
            if (compiler->locals[i].isActive && compiler->locals[i].reg == reg) {
                module_reg_used = true;
                break;
            }
        }
        if (!module_reg_used) {
            return reg;
        }
    }
    
    return 65535; // No available registers (480 registers exhausted!)
}

void freeRegister(Compiler* compiler, uint16_t reg) {
    if (!compiler || reg >= 65535) {
        return;
    }
    
    // With the larger register space, we can implement proper freeing
    // Mark the register as available for reuse in future allocations
    // TODO: Implement register lifecycle tracking for optimization
}

void emitByte(Compiler* compiler, uint8_t byte) {
    if (!compiler || !compiler->chunk) {
        return;
    }
    writeChunk(compiler->chunk, byte, compiler->currentLine, compiler->currentColumn);
}

void emitShort(Compiler* compiler, uint16_t value) {
    if (!compiler || !compiler->chunk) {
        return;
    }
    // Emit 16-bit value in big-endian order
    emitByte(compiler, (uint8_t)(value >> 8));   // High byte first
    emitByte(compiler, (uint8_t)(value & 0xFF)); // Low byte second
}

// Safe register emission - handles register > 255 with warnings
void emitRegister(Compiler* compiler, uint16_t reg) {
    if (reg > 255) {
        printf("[ERROR] Cannot emit register %d in bytecode (>255). Need extended opcodes.\n", reg);
        // For now, fallback to register within bytecode range
        // TODO: Implement extended opcodes for registers > 255
        reg = reg % 256;
        printf("[FALLBACK] Using register %d instead.\n", reg);
    }
    emitByte(compiler, (uint8_t)reg);
}

void emitConstant(Compiler* compiler, uint16_t reg, Value value) {
    if (!compiler || !compiler->chunk) {
        return;
    }
    
    // Smart opcode selection based on register ID
    if (reg > 255) {
        // Use extended opcode for registers > 255
        emitConstantExt(compiler, reg, value);
        return;
    }
    
    // Use standard opcode for registers 0-255
    int constant = addConstant(compiler->chunk, value);
    if (constant < 65536) {  // 16-bit constant index
        emitByte(compiler, OP_LOAD_CONST);
        emitByte(compiler, (uint8_t)reg);  // 8-bit register (bytecode compatible)
        // Emit 16-bit constant index in big-endian order
        emitByte(compiler, (uint8_t)(constant >> 8));   // High byte
        emitByte(compiler, (uint8_t)(constant & 0xFF)); // Low byte
    }
}

// Extended constant loading for registers > 255
void emitConstantExt(Compiler* compiler, uint16_t reg, Value value) {
    if (!compiler || !compiler->chunk) {
        return;
    }
    int constant = addConstant(compiler->chunk, value);
    if (constant < 65536) {  // 16-bit constant index
        emitByte(compiler, OP_LOAD_CONST_EXT);
        emitShort(compiler, reg);        // 16-bit register ID
        emitShort(compiler, constant);   // 16-bit constant index
    }
}

// Extended register move for registers > 255 (Phase 2.2)
void emitMoveExt(Compiler* compiler, uint16_t dst_reg, uint16_t src_reg) {
    if (!compiler || !compiler->chunk) {
        return;
    }
    emitByte(compiler, OP_MOVE_EXT);
    emitShort(compiler, dst_reg);    // 16-bit destination register
    emitShort(compiler, src_reg);    // 16-bit source register
}

// Smart move operation - chooses standard or extended opcode
void emitMove(Compiler* compiler, uint16_t dst_reg, uint16_t src_reg) {
    if (!compiler || !compiler->chunk) {
        return;
    }
    
    // Use extended opcode if either register > 255
    if (dst_reg > 255 || src_reg > 255) {
        emitMoveExt(compiler, dst_reg, src_reg);
        return;
    }
    
    // Use standard opcode for registers 0-255
    emitByte(compiler, OP_MOVE);
    emitByte(compiler, (uint8_t)dst_reg);
    emitByte(compiler, (uint8_t)src_reg);
}

void initCompiler(Compiler* compiler, Chunk* chunk, const char* fileName, const char* source) {
    if (!compiler) return;
    
    // Initialize basic fields
    compiler->chunk = chunk;
    compiler->fileName = fileName;
    compiler->source = source;
    compiler->nextRegister = 0;
    compiler->currentLine = 1;
    compiler->currentColumn = 1;
    compiler->localCount = 0;
    compiler->scopeDepth = 0;
    compiler->loopDepth = 0;
    compiler->hadError = false;
    
    // Initialize symbol table
    symbol_table_init(&compiler->symbols);
    
    // Initialize jump table
    compiler->pendingJumps = jumptable_new();
}

void freeCompiler(Compiler* compiler) {
    if (!compiler) return;
    
    // Free symbol table
    symbol_table_free(&compiler->symbols);
    
    // Free jump table
    jumptable_free(&compiler->pendingJumps);
    
    // Reset fields
    compiler->chunk = NULL;
    compiler->fileName = NULL;
    compiler->source = NULL;
    compiler->nextRegister = 0;
    compiler->hadError = false;
}

bool compileProgram(ASTNode* ast, Compiler* compiler, bool isModule) {
    if (!ast || !compiler) {
        return false;
    }
    
    // Initialize multi-pass compiler state (basic initCompiler already called by VM)
    initMultiPassCompiler(compiler, compiler->chunk, compiler->fileName, compiler->source);
    
    // Route to multi-pass compilation
    return compileMultiPass(ast, compiler, isModule);
}

bool compileNode(ASTNode* node, Compiler* compiler) {
    if (!node || !compiler) return false;
    
    // Route to multi-pass node compilation
    return compileMultiPassNode(node, compiler);
}

// Simplified register allocation - replace optimized allocation with basic allocation
#define allocateOptimizedRegister(compiler, isLoopVar, lifetime) allocateRegister(compiler)

// MultiPassCompiler will be defined later in the file

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

// Forward declarations (moved to top)
static int compileMultiPassExpr(ASTNode* node, Compiler* compiler);
static void collectUpvalues(ASTNode* node, MultiPassCompiler* mpCompiler);
void addUpvalue(UpvalueSet* upvalues, const char* name, int idx, bool isLocal,
                int scope);
static void analyzeLoopInvariants(ASTNode* loopBody,
                                  MultiPassCompiler* mpCompiler,
                                  LoopInvariants* invariants);
static bool dependsOnModified(ASTNode* expr, ModifiedSet* modified);

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

// void resetMultiPassCompiler() {
//     if (g_multiPassCompiler) {
//         g_multiPassCompiler->typeAnalysisComplete = false;
//         g_multiPassCompiler->scopeAnalysisComplete = false;
//         g_multiPassCompiler->optimizationComplete = false;
//         g_multiPassCompiler->inFunction = false;

//         // Reset loop contexts
//         g_multiPassCompiler->loopCount = 0;
//         for (int i = 0; i < g_multiPassCompiler->loopCapacity; i++) {
//             g_multiPassCompiler->loops[i].startInstr = -1;
//             g_multiPassCompiler->loops[i].scopeDepth = -1;
//             g_multiPassCompiler->loops[i].label = NULL;
//             g_multiPassCompiler->loops[i].isOptimized = false;
//         }
//     }
// }


static void beginScope(Compiler* compiler) {
    compiler->scopeDepth++;
    symbol_table_begin_scope(&compiler->symbols, compiler->scopeDepth);
    // Use scope analysis system for comprehensive tracking
    compilerEnterScope(compiler, false);  // false = not a loop scope initially
}

static void beginLoopScope(Compiler* compiler) {
    compiler->scopeDepth++;
    LOG_COMPILER_DEBUG("multipass", "beginLoopScope: entered loop scope, depth now %d", 
                      compiler->scopeDepth);
    symbol_table_begin_scope(&compiler->symbols, compiler->scopeDepth);
    // Use scope analysis system for comprehensive tracking
    compilerEnterScope(compiler, true);  // true = this is a loop scope
}

static void endScope(Compiler* compiler) {
    LOG_COMPILER_DEBUG("multipass", "endScope: exiting scope at depth %d", 
                      compiler->scopeDepth);

    // Use scope analysis system first
    compilerExitScope(compiler);

    // Keep existing local variable cleanup for now
    for (int i = 0; i < compiler->localCount; i++) {
        if (compiler->locals[i].isActive &&
            compiler->locals[i].depth == compiler->scopeDepth) {
            printf("[DEBUG] endScope: deactivating variable '%s' at depth %d\n",
                   compiler->locals[i].name ? compiler->locals[i].name : "NULL",
                   compiler->locals[i].depth);
            if (compiler->locals[i].name) {
                free(compiler->locals[i].name);
                compiler->locals[i].name = NULL;
            }
            compiler->locals[i].isActive = false;
        }
    }

    symbol_table_end_scope(&compiler->symbols, compiler->scopeDepth);
    compiler->scopeDepth--;
    printf("[DEBUG] endScope: depth now %d\n", compiler->scopeDepth);
}

static int addLocal(Compiler* compiler, const char* name, bool isMutable) {
    if (compiler->localCount >= REGISTER_COUNT) {
        return -1;
    }

    int index = compiler->localCount++;
    
    // Proper nested scope register allocation
    uint8_t reg = 1; // Start from register 1 (0 is reserved)
    
    // Create a register usage map for active variables
    bool register_used[256] = {true}; // Reserve register 0
    
    // Mark registers used by currently active locals
    for (int i = 0; i < compiler->localCount - 1; i++) {
        if (compiler->locals[i].isActive) {
            register_used[compiler->locals[i].reg] = true;
        }
    }
    
    // Find the first available register
    for (uint8_t r = 1; r < 255; r++) {
        if (!register_used[r]) {
            reg = r;
            break;
        }
    }
    
    // If no register found, use fallback (this shouldn't happen with 256 registers)
    if (register_used[reg]) {
        reg = (index % 254) + 1; // Ensure we stay in valid range
    }

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

    printf("[DEBUG] addLocal: added '%s' at index %d, depth %d, reg %d\n", name,
           index, compiler->scopeDepth, reg);

    symbol_table_set(&compiler->symbols, name, index, compiler->scopeDepth);
    return index;
}

static int findLocal(Compiler* compiler, const char* name) {
    printf("[DEBUG] findLocal: searching for '%s' among %d locals\n", name,
           compiler->localCount);
    for (int i = compiler->localCount - 1; i >= 0; i--) {
        printf("[DEBUG]   local[%d]: name='%s', active=%d, depth=%d\n", i,
               compiler->locals[i].name ? compiler->locals[i].name : "NULL",
               compiler->locals[i].isActive, compiler->locals[i].depth);
        if (compiler->locals[i].isActive && compiler->locals[i].name != NULL &&
            strcmp(compiler->locals[i].name, name) == 0) {
            printf("[DEBUG] findLocal: FOUND '%s' at index %d\n", name, i);
            return i;
        }
    }
    printf("[DEBUG] findLocal: NOT FOUND '%s'\n", name);
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

void addUpvalue(UpvalueSet* upvalues, const char* name, int idx, bool isLocal,
                int scope) {
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

// Modified variable analysis
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

// Loop invariant analysis (LICM)
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

    invariants->entries = malloc(sizeof(InvariantEntry) * 8);
    invariants->capacity = 8;
    invariants->count = 0;

    // Traverse loop body to find invariant expressions
    if (loopBody->type == NODE_BLOCK) {
        for (int i = 0; i < loopBody->block.count; i++) {
            ASTNode* node = loopBody->block.statements[i];
            
            // Only consider expression nodes for invariant analysis
            // Statement nodes (NODE_PRINT, NODE_IF, NODE_WHILE, etc.) should never be hoisted
            if (node->type == NODE_LITERAL || 
                node->type == NODE_IDENTIFIER || 
                node->type == NODE_BINARY ||
                node->type == NODE_CALL ||
                node->type == NODE_CAST ||
                node->type == NODE_UNARY ||
                node->type == NODE_TERNARY) {
                
                if (!hasSideEffects(node) && !dependsOnModified(node, &modified)) {
                    if (invariants->count >= invariants->capacity) {
                        invariants->capacity *= 2;
                        invariants->entries = realloc(invariants->entries, invariants->capacity * sizeof(InvariantEntry));
                    }
                    InvariantEntry* entry = &invariants->entries[invariants->count++];
                    entry->expr = node;
                    entry->reg = allocateOptimizedRegister(mpCompiler->base, false, 80);
                }
            }
            // Statement nodes are ignored - they cannot be hoisted as invariants
        }
    }

    // Free modified set
    for (int i = 0; i < modified.count; i++) {
        free(modified.names[i]);
    }
    free(modified.names);
}

// Remove duplicate - using existing function above

// Jump table management
static void patchBreakJumps(JumpTable* table, Compiler* compiler) {
    printf("[DEBUG] patchBreakJumps: Patching %d break jumps to position %d\n",
           table->offsets.count, compiler->chunk->count);
    for (int i = 0; i < table->offsets.count; i++) {
        int offset = table->offsets.data[i];
        int jump = compiler->chunk->count - offset - 2;
        printf("[DEBUG] patchBreakJumps: offset=%d, current=%d, jump=%d\n",
               offset, compiler->chunk->count, jump);
        
        // Validate jump bounds before patching
        if (jump < 0) {
            printf("[DEBUG] patchBreakJumps: Invalid negative jump %d\n", jump);
            continue;
        }
        if (jump > UINT16_MAX) {
            printf("[DEBUG] patchBreakJumps: Jump %d exceeds UINT16_MAX\n", jump);
            continue;
        }
        
        // Additional bounds check: ensure the target is within chunk bounds
        int targetPos = offset + 2 + jump;
        if (targetPos > compiler->chunk->count) {
            printf("[DEBUG] patchBreakJumps: Target position %d exceeds chunk size %d\n", 
                   targetPos, compiler->chunk->count);
            continue;
        }
        
        compiler->chunk->code[offset] = (jump >> 8) & 0xff;
        compiler->chunk->code[offset + 1] = jump & 0xff;
    }
}

static void patchContinueJumps(JumpTable* table, Compiler* compiler,
                               int continueTarget) {
    // Only patch if there are continue jumps to patch
    if (table->offsets.count == 0) {
        printf("[DEBUG] patchContinueJumps: No continue jumps to patch\n");
        return;
    }

    printf("[DEBUG] patchContinueJumps: Patching %d continue jumps to target %d\n",
           table->offsets.count, continueTarget);

    for (int i = 0; i < table->offsets.count; i++) {
        int offset = table->offsets.data[i];

        // Determine if this is a forward or backward jump
        bool isForwardJump = continueTarget > offset;
        int jump;
        
        if (isForwardJump) {
            // Forward jump: like patchJump does
            jump = continueTarget - offset - 2;
            printf("[DEBUG] patchContinueJumps: offset=%d, continueTarget=%d, forward_jump=%d\n",
                   offset, continueTarget, jump);
        } else {
            // Backward jump: like emitLoop does  
            jump = offset - continueTarget + 2;
            printf("[DEBUG] patchContinueJumps: offset=%d, continueTarget=%d, backward_jump=%d\n",
                   offset, continueTarget, jump);
        }
            
        // Validate jump bounds before patching
        if (jump < 0) {
            printf("[DEBUG] patchContinueJumps: Invalid negative jump %d\n", jump);
            continue;
        }
        if (jump > UINT16_MAX) {
            printf("[DEBUG] patchContinueJumps: Jump %d exceeds UINT16_MAX\n", jump);
            continue;
        }
        
        // Additional bounds check: ensure the target is within chunk bounds
        if (continueTarget < 0 || continueTarget > compiler->chunk->count) {
            printf("[DEBUG] patchContinueJumps: Continue target %d out of chunk bounds [0, %d]\n", 
                   continueTarget, compiler->chunk->count);
            continue;
        }
        
        // For forward jumps, verify that jumping forward lands at continueTarget
        if (isForwardJump) {
            int landingPos = offset + 2 + jump;
            if (landingPos != continueTarget) {
                printf("[DEBUG] patchContinueJumps: Jump calculation error: expected %d, got %d\n", 
                       continueTarget, landingPos);
                continue;
            }
        }
        
        // Patch the jump offset (keep OP_JUMP instruction unchanged for both directions)
        compiler->chunk->code[offset] = (jump >> 8) & 0xff;
        compiler->chunk->code[offset + 1] = jump & 0xff;
    }
}

// Expression compilation with multi-pass features
static int compileMultiPassLiteral(ASTNode* node, Compiler* compiler) {
    // Use optimized register allocation for literals - short lifetime
    uint8_t reg = allocateOptimizedRegister(compiler, false, 5);
    emitConstant(compiler, reg, node->literal.value);
    return reg;
}

static int compileMultiPassIdentifier(ASTNode* node, Compiler* compiler) {
    MultiPassCompiler* mpCompiler = g_multiPassCompiler;

    // DEBUG: Variable resolution for identifier
    printf(
        "[DEBUG] compileIdentifier: Looking for variable '%s' at scope depth "
        "%d\n",
        node->identifier.name, compiler->scopeDepth);
    fflush(stdout);

    // Try locals first (handles loop variables and current scope)
    int localIndex = findLocal(compiler, node->identifier.name);
    printf("[DEBUG] findLocal returned index: %d\n", localIndex);

    if (localIndex >= 0 && compiler->locals[localIndex].name != NULL) {
        printf("[DEBUG] Found '%s' in locals at index %d, depth %d, reg %d\n",
               node->identifier.name, localIndex,
               compiler->locals[localIndex].depth,
               compiler->locals[localIndex].reg);
        // Record variable usage in scope analysis
        compilerUseVariable(compiler, node->identifier.name);
        return compiler->locals[localIndex].reg;
    }

    // Try scope analysis for cross-scope variable access
    ScopeVariable* scopeVar = findVariableInScopeChain(
        compiler->scopeAnalyzer.currentScope, node->identifier.name);
    if (scopeVar) {
        // Found in scope chain - record usage and return register
        compilerUseVariable(compiler, node->identifier.name);
        return scopeVar->reg;
    }

    // Check upvalues if in function
    if (mpCompiler && mpCompiler->inFunction) {
        for (int i = 0; i < mpCompiler->upvalues.count; i++) {
            if (strcmp(mpCompiler->upvalues.entries[i].name,
                       node->identifier.name) == 0) {
                // Record variable usage in scope analysis
                compilerUseVariable(compiler, node->identifier.name);
                uint8_t reg = allocateOptimizedRegister(compiler, false, 20);
                emitByte(compiler, OP_GET_UPVALUE_R);
                emitByte(compiler, reg);
                emitByte(compiler, (uint8_t)i);
                return reg;
            }
        }
    }

    printf(
        "[DEBUG] compileIdentifier: About to report undefined variable '%s'\n",
        node->identifier.name);
    fflush(stdout);
    report_undefined_variable(node->location, node->identifier.name);
    return -1;
}

static int compileMultiPassBinaryOp(ASTNode* node, Compiler* compiler) {
    int leftReg = compileMultiPassExpr(node->binary.left, compiler);
    int rightReg = compileMultiPassExpr(node->binary.right, compiler);
    // Use optimized register allocation for binary operations - moderate lifetime
    uint8_t resultReg = allocateOptimizedRegister(compiler, false, 15);

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
    } else if (strcmp(node->binary.op, "and") == 0) {
        emitByte(compiler, OP_AND_BOOL_R);
    } else if (strcmp(node->binary.op, "or") == 0) {
        emitByte(compiler, OP_OR_BOOL_R);
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
            return compileMultiPassLiteral(node, compiler);
        case NODE_IDENTIFIER:
            return compileMultiPassIdentifier(node, compiler);
        case NODE_BINARY:
            return compileMultiPassBinaryOp(node, compiler);
        case NODE_TIME_STAMP: {
            uint8_t resultReg = allocateOptimizedRegister(compiler, false, 10);
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
                    uint8_t resultReg = allocateOptimizedRegister(compiler, false, 10);
                    emitByte(compiler, OP_TIME_STAMP);
                    emitByte(compiler, resultReg);
                    return resultReg;
                }
            }

            // Regular function call
            int funcReg = compileMultiPassExpr(node->call.callee, compiler);
            int resultReg = allocateOptimizedRegister(compiler, false, 25);

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
        case NODE_CAST: {
            // Type casting support
            int sourceReg = compileMultiPassExpr(node->cast.expression, compiler);
            if (sourceReg < 0) return -1;
            
            uint8_t resultReg = allocateOptimizedRegister(compiler, false, 15);
            
            // Get target type from AST node
            // For now, assume string casting (most common case)
            // TODO: Implement proper type parsing from targetType AST node
            emitByte(compiler, OP_TO_STRING_R);
            emitByte(compiler, resultReg);
            emitByte(compiler, sourceReg);
            
            freeRegister(compiler, sourceReg);
            return resultReg;
        }
        case NODE_UNARY: {
            // Add support for unary expressions (e.g., -123, !true)
            int operandReg = compileMultiPassExpr(node->unary.operand, compiler);
            if (operandReg < 0) return -1;
            
            uint8_t resultReg = allocateRegister(compiler);
            
            if (strcmp(node->unary.op, "-") == 0) {
                // Negation
                emitByte(compiler, OP_NEG_I32_R);
                emitByte(compiler, resultReg);
                emitByte(compiler, operandReg);
            } else if (strcmp(node->unary.op, "!") == 0) {
                // Logical NOT
                emitByte(compiler, OP_NOT_BOOL_R);
                emitByte(compiler, resultReg);
                emitByte(compiler, operandReg);
            } else {
                SrcLocation loc = {.file = compiler->fileName,
                                   .line = node->location.line,
                                   .column = node->location.column};
                report_compile_error(E1006_INVALID_SYNTAX, loc,
                                     "Unknown unary operator");
                freeRegister(compiler, operandReg);
                freeRegister(compiler, resultReg);
                return -1;
            }
            
            freeRegister(compiler, operandReg);
            return resultReg;
        }
        case NODE_TERNARY: {
            // Add support for ternary expressions (condition ? true_val : false_val)
            int conditionReg = compileMultiPassExpr(node->ternary.condition, compiler);
            if (conditionReg < 0) return -1;
            
            // Jump if condition is false (jump to else branch)
            emitByte(compiler, OP_JUMP_IF_NOT_R);
            emitByte(compiler, conditionReg);
            int elseJump = emitJump(compiler);
            
            freeRegister(compiler, conditionReg);
            
            // Compile true branch and use its register as the result
            int trueReg = compileMultiPassExpr(node->ternary.trueExpr, compiler);
            if (trueReg < 0) return -1;
            
            // Jump over false branch (unconditional jump to end)
            emitByte(compiler, OP_JUMP);
            int endJump = emitJump(compiler);
            
            // Patch else jump to point here (start of false branch)
            patchJump(compiler, elseJump);
            
            // For the false branch, we need to compile it and ensure it uses the same
            // register as the true branch for consistency. This is tricky with the current
            // register allocation system. Let's try a simpler approach: just return
            // whichever register the false branch gives us, and hope they match.
            int falseReg = compileMultiPassExpr(node->ternary.falseExpr, compiler);
            if (falseReg < 0) {
                freeRegister(compiler, trueReg);
                return -1;
            }
            
            // Patch end jump to point here
            patchJump(compiler, endJump);
            
            // For now, just return trueReg and see what happens
            // This is not correct but will help us debug the issue
            return trueReg;
        }
        case NODE_PRINT:
        case NODE_IF:
        case NODE_FOR_RANGE:
        case NODE_WHILE:
        case NODE_BLOCK: {
            // These are statement nodes that should never be treated as expressions
            // If we reach here, there's a bug in the calling code
            SrcLocation loc = {.file = compiler->fileName,
                               .line = node->location.line,
                               .column = node->location.column};
            report_compile_error(E1006_INVALID_SYNTAX, loc,
                                 "Statement node cannot be used as expression");
            return -1;
        }
        default: {
            SrcLocation loc = {.file = compiler->fileName,
                               .line = node->location.line,
                               .column = node->location.column};
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), 
                    "Unsupported expression type in multi-pass: %d", node->type);
            report_compile_error(E1006_INVALID_SYNTAX, loc, error_msg);
            return -1;
        }
    }
}

bool compileMultiPassNode(ASTNode* node, Compiler* compiler) {
    if (!node) return true;

    MultiPassCompiler* mpCompiler = g_multiPassCompiler;
    compiler->currentLine = node->location.line;
    compiler->currentColumn = node->location.column;

    printf("[DEBUG] compileMultiPassNode: handling node type %d at line %d\n", 
           node->type, node->location.line);
    fflush(stdout);

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

            // Upvalue analysis
            mpCompiler->inFunction = true;
            UpvalueSet oldUpvalues = mpCompiler->upvalues;
            mpCompiler->upvalues.entries = malloc(sizeof(UpvalueEntry) * 8);
            mpCompiler->upvalues.count = 0;
            mpCompiler->upvalues.capacity = 8;

            collectUpvalues(node->function.body, mpCompiler);

            // Create function object
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

            // Create function compiler
            Compiler functionCompiler;
            initMultiPassCompiler(&functionCompiler, objFunction->chunk,
                                  compiler->fileName, compiler->source);
            functionCompiler.scopeDepth = compiler->scopeDepth + 1;
            functionCompiler.currentFunctionParameterCount =
                node->function.paramCount;

            // Setup upvalues
            for (int i = 0; i < mpCompiler->upvalues.count; i++) {
                UpvalueEntry* upvalue = &mpCompiler->upvalues.entries[i];
                int closureIndex = -(2000 + i);
                symbol_table_set(&functionCompiler.symbols, upvalue->name,
                                 closureIndex, 0);
            }

            // Add parameters
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

            // Compile function body
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
            printf("[DEBUG] Matched NODE_FOR_RANGE case at line %d\n", node->location.line);
            fflush(stdout);
            // Simple, robust for-loop compilation - no complex optimizations
            beginLoopScope(compiler);

            // Push loop context onto stack
            if (mpCompiler->loopCount >= mpCompiler->loopCapacity) {
                mpCompiler->loopCapacity *= 2;
                mpCompiler->loops = realloc(
                    mpCompiler->loops,
                    mpCompiler->loopCapacity * sizeof(MultiPassLoopContext));
            }

            MultiPassLoopContext* context = &mpCompiler->loops[mpCompiler->loopCount++];
            context->breakJumps = jumptable_new();
            context->continueJumps = jumptable_new();
            context->label = NULL;
            context->isOptimized = false;
            context->scopeDepth = compiler->scopeDepth;
            
            // Initialize empty invariants - no complex optimization
            context->invariants.entries = NULL;
            context->invariants.count = 0;
            context->invariants.capacity = 0;
            context->modifiedVars.names = NULL;
            context->modifiedVars.count = 0;

            // Evaluate start and end expressions
            int startReg = compileMultiPassExpr(node->forRange.start, compiler);
            int endReg = compileMultiPassExpr(node->forRange.end, compiler);
            if (startReg < 0 || endReg < 0) {
                endScope(compiler);
                mpCompiler->loopCount--;
                if (startReg >= 0) freeRegister(compiler, startReg);
                if (endReg >= 0) freeRegister(compiler, endReg);
                return false;
            }

            // Create loop variable
            int loopVarIndex = addLocal(compiler, node->forRange.varName, false);
            if (loopVarIndex < 0) {
                SrcLocation loc = {.file = compiler->fileName,
                                   .line = node->location.line,
                                   .column = node->location.column};
                report_compile_error(E1009_EXPRESSION_TOO_COMPLEX, loc,
                                     "Too many local variables");
                endScope(compiler);
                mpCompiler->loopCount--;
                freeRegister(compiler, startReg);
                freeRegister(compiler, endReg);
                return false;
            }

            compilerDeclareVariable(compiler, node->forRange.varName, VAL_I32,
                                    compiler->locals[loopVarIndex].reg);
            uint8_t iterReg = compiler->locals[loopVarIndex].reg;

            // Initialize: iter = start
            emitByte(compiler, OP_MOVE);
            emitByte(compiler, iterReg);
            emitByte(compiler, startReg);
            freeRegister(compiler, startReg);

            // Store end value in a local to preserve it across iterations
            char hiddenEndName[32];
            snprintf(hiddenEndName, sizeof(hiddenEndName), "__end_%d_%d",
                     compiler->scopeDepth, mpCompiler->loopCount);
            int hiddenEndIndex = addLocal(compiler, hiddenEndName, false);
            if (hiddenEndIndex < 0) {
                SrcLocation loc = {.file = compiler->fileName,
                                   .line = node->location.line,
                                   .column = node->location.column};
                report_compile_error(E1009_EXPRESSION_TOO_COMPLEX, loc,
                                     "Too many local variables");
                endScope(compiler);
                mpCompiler->loopCount--;
                freeRegister(compiler, endReg);
                return false;
            }
            uint8_t hiddenEndReg = compiler->locals[hiddenEndIndex].reg;
            emitByte(compiler, OP_MOVE);
            emitByte(compiler, hiddenEndReg);
            emitByte(compiler, endReg);
            freeRegister(compiler, endReg);

            // Loop condition check: iter <= hiddenEnd
            int loopStart = compiler->chunk->count;
            context->startInstr = loopStart;

            // Use optimized register allocation for loop condition - long lifetime
            uint8_t condReg = allocateOptimizedRegister(compiler, true, 100);
            emitByte(compiler, OP_LE_I32_R);
            emitByte(compiler, condReg);
            emitByte(compiler, iterReg);
            emitByte(compiler, hiddenEndReg);

            emitByte(compiler, OP_JUMP_IF_NOT_R);
            emitByte(compiler, condReg);
            int exitJump = emitJump(compiler);
            freeRegister(compiler, condReg);

            // Compile loop body
            bool success = compileMultiPassNode(node->forRange.body, compiler);
            if (!success) {
                endScope(compiler);
                mpCompiler->loopCount--;
                return false;
            }

            // Continue target: increment iterator (capture position before emitting)
            int continueTarget = compiler->chunk->count;
            emitByte(compiler, OP_INC_I32_R);
            emitByte(compiler, iterReg);
            
            // Patch continue jumps to point to increment instruction we just emitted
            patchContinueJumps(&context->continueJumps, compiler, continueTarget);

            // Jump back to condition
            emitLoop(compiler, loopStart);

            // Patch exit and break jumps
            patchJump(compiler, exitJump);
            patchBreakJumps(&context->breakJumps, compiler);
            endScope(compiler);
            jumptable_free(&context->breakJumps);
            jumptable_free(&context->continueJumps);
            mpCompiler->loopCount--;
            
            return true;
        }

        case NODE_WHILE: {
            // MULTI-PASS WHILE LOOP COMPILATION
            beginLoopScope(compiler);

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
            patchContinueJumps(&context->continueJumps, compiler,
                               context->startInstr);

            emitLoop(compiler, context->startInstr);
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

            // Find the loop at the current scope depth
            MultiPassLoopContext* targetLoop = NULL;
            for (int i = mpCompiler->loopCount - 1; i >= 0; i--) {
                if (mpCompiler->loops[i].scopeDepth <= compiler->scopeDepth) {
                    targetLoop = &mpCompiler->loops[i];
                    break;
                }
            }
            
            if (!targetLoop) {
                targetLoop = &mpCompiler->loops[mpCompiler->loopCount - 1];
            }
            if (node->breakStmt.label) {
                // Find labeled loop
                targetLoop = NULL; // Reset for search
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
                // For continue statements, target the innermost loop (most
                // common case) Unlike break, continue statements typically
                // target the immediate enclosing loop
                targetLoop = &mpCompiler->loops[mpCompiler->loopCount - 1];
            }

            // Continue should be a forward jump to the increment section
            emitByte(compiler, OP_JUMP);
            int continueJump = emitJump(compiler);
            jumptable_add(&targetLoop->continueJumps, continueJump);

            return true;
        }

        case NODE_ASSIGN: {
            int valueReg = compileMultiPassExpr(node->assign.value, compiler);
            if (valueReg < 0) return false;

            // Try scope analysis first for cross-scope variable assignment
            ScopeVariable* scopeVar = findVariableInScopeChain(
                compiler->scopeAnalyzer.currentScope, node->assign.name);
            if (scopeVar) {
                // Found in scope chain - record usage and assign
                compilerUseVariable(compiler, node->assign.name);
                emitByte(compiler, OP_MOVE);
                emitByte(compiler, scopeVar->reg);
                emitByte(compiler, valueReg);
                freeRegister(compiler, valueReg);
                return true;
            }

            // Fallback: Try locals for backward compatibility
            int localIndex = findLocal(compiler, node->assign.name);
            if (localIndex >= 0) {
                if (!compiler->locals[localIndex].isMutable) {
                    report_immutable_variable_assignment(node->location,
                                                         node->assign.name);
                    freeRegister(compiler, valueReg);
                    return false;
                }

                // Record variable usage in scope analysis
                compilerUseVariable(compiler, node->assign.name);
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
                        // Record variable usage in scope analysis
                        compilerUseVariable(compiler, node->assign.name);
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
            printf("[DEBUG] Matched NODE_PRINT case at line %d\n", node->location.line);
            fflush(stdout);
            if (node->print.count == 0) {
                uint8_t r = allocateOptimizedRegister(compiler, false, 5);
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
                    argRegs[i] = allocateOptimizedRegister(compiler, false, 5);
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

            bool success =
                compileMultiPassNode(node->ifStmt.thenBranch, compiler);

            if (!success) return false;

            if (node->ifStmt.elseBranch) {
                emitByte(compiler, OP_JUMP);
                int elseJump = emitJump(compiler);

                patchJump(compiler, thenJump);

                success =
                    compileMultiPassNode(node->ifStmt.elseBranch, compiler);

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
            printf("[DEBUG] Hit default case for node type %d at line %d - treating as expression\n", 
                   node->type, node->location.line);
            fflush(stdout);
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

    printf("[DEBUG] compileMultiPass: Starting multi-pass compilation\n");
    fflush(stdout);

    MultiPassCompiler* mpCompiler = g_multiPassCompiler;

    // Initialize scope analyzer
    initCompilerScopeAnalysis(compiler);

    // PASS 1: Type analysis (placeholder for now)
    mpCompiler->typeAnalysisComplete = true;

    // PASS 2: Scope analysis - now using comprehensive system
    // The scope analysis happens incrementally during compilation
    mpCompiler->scopeAnalysisComplete = true;

    // PASS 3: Main compilation with optimizations
    bool success = compileMultiPassNode(ast, compiler);

    // PASS 4: Post-compilation optimizations and finalization
    if (success) {
        finalizeCompilerScopeAnalysis(compiler);
    }
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
    if (!expr) return NULL;
    
    switch (expr->type) {
        case NODE_LITERAL:
            return getPrimitiveType(valueTypeToTypeKind(expr->literal.value.type));
            
        case NODE_BINARY: {
            // Infer based on operator and operand types
            Type* leftType = inferExpressionType(compiler, expr->binary.left);
            Type* rightType = inferExpressionType(compiler, expr->binary.right);
            
            if (leftType && rightType && leftType->kind == rightType->kind) {
                return leftType; // Simplified: assumes same type for now
            }
            
            // For mixed types, apply promotion rules
            if (leftType && rightType) {
                if (leftType->kind == TYPE_F64 || rightType->kind == TYPE_F64) {
                    return getPrimitiveType(TYPE_F64);
                }
                if (leftType->kind == TYPE_I64 || rightType->kind == TYPE_I64) {
                    return getPrimitiveType(TYPE_I64);
                }
                if (leftType->kind == TYPE_I32 || rightType->kind == TYPE_I32) {
                    return getPrimitiveType(TYPE_I32);
                }
            }
            return NULL;
        }
        
        case NODE_CAST:
            // Use specified target type from AST
            if (expr->cast.targetType && expr->cast.targetType->type == NODE_TYPE) {
                const char* typeName = expr->cast.targetType->typeAnnotation.name;
                if (strcmp(typeName, "i32") == 0) return getPrimitiveType(TYPE_I32);
                if (strcmp(typeName, "i64") == 0) return getPrimitiveType(TYPE_I64);
                if (strcmp(typeName, "f64") == 0) return getPrimitiveType(TYPE_F64);
                if (strcmp(typeName, "bool") == 0) return getPrimitiveType(TYPE_BOOL);
                if (strcmp(typeName, "string") == 0) return getPrimitiveType(TYPE_STRING);
            }
            return NULL;
            
        case NODE_IDENTIFIER: {
            // Look up variable type from locals
            int localIndex = findLocal(compiler, expr->identifier.name);
            if (localIndex >= 0 && compiler->locals[localIndex].hasKnownType) {
                return getPrimitiveType(valueTypeToTypeKind(compiler->locals[localIndex].knownType));
            }
            return NULL;
        }
        
        case NODE_UNARY: {
            Type* operandType = inferExpressionType(compiler, expr->unary.operand);
            const char* op = expr->unary.op;
            
            if (strcmp(op, "-") == 0 || strcmp(op, "+") == 0) {
                return operandType; // Unary arithmetic preserves type
            }
            if (strcmp(op, "!") == 0) {
                return getPrimitiveType(TYPE_BOOL); // Logical not always returns bool
            }
            return NULL;
        }
        
        default:
            return NULL;
    }
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