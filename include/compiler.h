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

// Loop safety and infinite loop detection
bool analyzeLoopSafety(Compiler* compiler, ASTNode* loopNode, LoopSafetyInfo* safety);
bool detectInfiniteLoop(ASTNode* condition, ASTNode* increment, LoopSafetyInfo* safety);
bool validateRangeDirection(ASTNode* start, ASTNode* end, ASTNode* step);
int computeStaticIterationCount(ASTNode* start, ASTNode* end, ASTNode* step, bool inclusive);
bool hasBreakOrReturnInASTNode(ASTNode* node);

void emitByte(Compiler* compiler, uint8_t byte);
void emitBytes(Compiler* compiler, uint8_t byte1, uint8_t byte2);
void emitConstant(Compiler* compiler, uint8_t reg, Value value);

// Compilation helpers
bool compileExpression(ASTNode* node, Compiler* compiler);
int compileExpressionToRegister(ASTNode* node, Compiler* compiler);

#endif // COMPILER_H
