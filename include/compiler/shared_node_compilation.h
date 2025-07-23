#ifndef SHARED_NODE_COMPILATION_H
#define SHARED_NODE_COMPILATION_H

#include "compiler/compiler.h"
#include "compiler/vm_optimization.h"
#include "ast/ast.h"

// Compiler context for backend-specific settings
typedef struct {
    bool supportsBreakContinue;
    bool supportsFunctions;
    bool enableOptimizations;
    VMOptimizationContext* vmOptCtx; // For VM-specific optimizations
    // Add other backend-specific flags as needed
} CompilerContext;

// Shared node compilation functions
int compileSharedLiteral(ASTNode* node, Compiler* compiler, CompilerContext* ctx);
int compileSharedBinaryOp(ASTNode* node, Compiler* compiler, CompilerContext* ctx);
int compileSharedVarDecl(ASTNode* node, Compiler* compiler, CompilerContext* ctx);
int compileSharedAssignment(ASTNode* node, Compiler* compiler, CompilerContext* ctx);
int compileSharedIfStatement(ASTNode* node, Compiler* compiler, CompilerContext* ctx);
int compileSharedWhileLoop(ASTNode* node, Compiler* compiler, CompilerContext* ctx);
int compileSharedForLoop(ASTNode* node, Compiler* compiler, CompilerContext* ctx);
int compileSharedBlock(ASTNode* node, Compiler* compiler, CompilerContext* ctx);
int compileSharedCast(ASTNode* node, Compiler* compiler, CompilerContext* ctx);

// Main shared node compilation dispatch function
bool compileSharedNode(ASTNode* node, Compiler* compiler, CompilerContext* ctx);

// Helper functions for context management
CompilerContext createSinglePassContext(void);
CompilerContext createMultiPassContext(VMOptimizationContext* vmOptCtx);

#endif // SHARED_NODE_COMPILATION_H