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
    COMPILE_HYBRID,         // Granular compilation with per-node strategy selection
    COMPILE_AUTO            // Automatically choose based on code complexity
} CompilationStrategy;

// Use unified complexity analysis from backend_selection.h
#include "compiler/backend_selection.h"

// Hybrid compiler interface
bool compileHybrid(ASTNode* ast, Compiler* compiler, bool isModule, CompilationStrategy strategy);
// Remove duplicate - use analyzeCodeComplexity from backend_selection.h
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

