// hybrid_compiler.h
// Author: Hierat
// Date: 2023-10-01
// Description: Hybrid compiler controller - decides between single-pass and multi-pass

#ifndef HYBRID_COMPILER_H
#define HYBRID_COMPILER_H

#include "compiler/compiler.h"
#include "ast.h"

// Compilation strategy
typedef enum {
    COMPILE_SINGLE_PASS,    // Fast compilation for simple code
    COMPILE_MULTI_PASS,     // Advanced compilation with optimizations
    COMPILE_AUTO            // Automatically choose based on code complexity
} CompilationStrategy;

// Complexity analysis result
typedef struct {
    int functionCount;
    int loopCount;
    int nestedLoopDepth;
    int upvalueCount;
    int callCount;
    bool hasBreakContinue;
    bool hasComplexExpressions;
    int complexityScore;
} CodeComplexity;

// Hybrid compiler interface
bool compileHybrid(ASTNode* ast, Compiler* compiler, bool isModule, CompilationStrategy strategy);
CodeComplexity analyzeComplexity(ASTNode* ast);
CompilationStrategy chooseStrategy(CodeComplexity complexity);

// Single-pass compiler interface
void initSinglePassCompiler(Compiler* compiler, Chunk* chunk, const char* fileName, const char* source);
void freeSinglePassCompiler(Compiler* compiler);
bool compileSinglePass(ASTNode* ast, Compiler* compiler, bool isModule);

// Multi-pass compiler interface  
void initMultiPassCompiler(Compiler* compiler, Chunk* chunk, const char* fileName, const char* source);
void freeMultiPassCompiler(Compiler* compiler);
bool compileMultiPass(ASTNode* ast, Compiler* compiler, bool isModule);

#endif // HYBRID_COMPILER_H

