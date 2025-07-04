#ifndef COMPILER_H
#define COMPILER_H

#include "common.h"
#include "vm.h"
#include "ast.h"

void initCompiler(Compiler* compiler, Chunk* chunk, const char* fileName, const char* source);
void freeCompiler(Compiler* compiler);
uint8_t allocateRegister(Compiler* compiler);
void freeRegister(Compiler* compiler, uint8_t reg);
bool compile(ASTNode* ast, Compiler* compiler, bool isModule);

// Enhanced register allocation with lifetime tracking
void initRegisterAllocator(RegisterAllocator* allocator);
void freeRegisterAllocator(RegisterAllocator* allocator);
uint8_t allocateRegisterWithLifetime(Compiler* compiler, const char* name, ValueType type, bool isLoopVar);
void markVariableLastUse(Compiler* compiler, int localIndex, int instruction);
void endVariableLifetime(Compiler* compiler, int localIndex, int instruction);
uint8_t reuseOrAllocateRegister(Compiler* compiler, const char* name, ValueType type);
void optimizeLoopVariableLifetimes(Compiler* compiler, int loopStart, int loopEnd);


// Loop Invariant Code Motion (LICM) optimization
typedef struct {
    ASTNode** invariantNodes;       // Array of loop-invariant expressions
    int count;                      // Number of invariant expressions
    int capacity;                   // Capacity of invariantNodes array
    uint8_t* hoistedRegs;          // Registers for hoisted values
    int* originalInstructions;      // Original instruction positions
    bool* canHoist;                // Whether each expression can be safely hoisted
} LICMAnalysis;

void initLICMAnalysis(LICMAnalysis* analysis);
void freeLICMAnalysis(LICMAnalysis* analysis);
bool performLICM(Compiler* compiler, int loopStart, int loopEnd, LoopContext* loopCtx);
bool isLoopInvariant(ASTNode* expr, LoopContext* loopCtx, Compiler* compiler);
bool canSafelyHoist(ASTNode* expr, LoopContext* loopCtx);
void hoistInvariantCode(Compiler* compiler, LICMAnalysis* analysis, int preHeaderPos);
bool hasSideEffects(ASTNode* expr);
bool dependsOnLoopVariable(ASTNode* expr, LoopContext* loopCtx);
void collectLoopInvariantExpressions(ASTNode* node, LICMAnalysis* analysis, LoopContext* loopCtx, Compiler* compiler);

void emitByte(Compiler* compiler, uint8_t byte);
void emitBytes(Compiler* compiler, uint8_t byte1, uint8_t byte2);
void emitConstant(Compiler* compiler, uint8_t reg, Value value);

// Compilation helpers
bool compileExpression(ASTNode* node, Compiler* compiler);
int compileExpressionToRegister(ASTNode* node, Compiler* compiler);

#endif // COMPILER_H
