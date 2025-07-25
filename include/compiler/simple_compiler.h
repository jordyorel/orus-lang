#ifndef SIMPLE_COMPILER_H
#define SIMPLE_COMPILER_H

#include "compiler/compiler.h"

// Single entry point for all Orus compilation
// Routes everything through multi-pass compiler for stability
bool compileProgram(ASTNode* ast, Compiler* compiler, bool isModule);

// Compatibility wrapper for old compileNode API
bool compileNode(ASTNode* node, Compiler* compiler);

// PHASE 2: Stub for disabled optimization statistics
void printGlobalOptimizationStats(void);

#endif // SIMPLE_COMPILER_H