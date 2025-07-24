#ifndef NODE_REGISTRY_H
#define NODE_REGISTRY_H

#include "compiler.h"
#include "backend_selection.h"
#include "vm_optimization.h"
#include "shared_node_compilation.h"

// Node handler function types
typedef struct {
    NodeType type;
    int (*compileExpr)(ASTNode* node, Compiler* compiler, CompilerContext* ctx);
    bool (*compileNode)(ASTNode* node, Compiler* compiler, CompilerContext* ctx);
    void (*analyzeComplexity)(ASTNode* node, CodeComplexity* complexity);
    void (*optimize)(ASTNode* node, VMOptimizationContext* vmCtx, RegisterState* regState);
} NodeHandler;

// Node handler registry functions
void initNodeRegistry(void);
void freeNodeRegistry(void);
void registerNodeHandler(NodeHandler handler);
NodeHandler* getNodeHandler(NodeType type);

// Default handler implementations
int defaultCompileExpr(ASTNode* node, Compiler* compiler, CompilerContext* ctx);
bool defaultCompileNode(ASTNode* node, Compiler* compiler, CompilerContext* ctx);
void defaultAnalyzeComplexity(ASTNode* node, CodeComplexity* complexity);
void defaultOptimize(ASTNode* node, VMOptimizationContext* vmCtx, RegisterState* regState);

// Register all standard node handlers
void registerAllNodeHandlers(void);

#endif // NODE_REGISTRY_H