#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "compiler/node_registry.h"
#include "internal/logging.h"

// Registry implementation
#define MAX_NODE_HANDLERS 64
static NodeHandler g_nodeHandlers[MAX_NODE_HANDLERS];
static int g_handlerCount = 0;
static bool g_registryInitialized = false;

// Initialize the node registry
void initNodeRegistry(void) {
    if (g_registryInitialized) return;
    
    memset(g_nodeHandlers, 0, sizeof(g_nodeHandlers));
    g_handlerCount = 0;
    g_registryInitialized = true;
    
    LOG_DEBUG("Node registry initialized");
}

// Free the node registry
void freeNodeRegistry(void) {
    g_handlerCount = 0;
    g_registryInitialized = false;
    LOG_DEBUG("Node registry freed");
}

// Register a node handler
void registerNodeHandler(NodeHandler handler) {
    if (!g_registryInitialized) {
        initNodeRegistry();
    }
    
    if (g_handlerCount >= MAX_NODE_HANDLERS) {
        LOG_DEBUG("Node registry full, cannot register handler for type %d", handler.type);
        return;
    }
    
    // Check if handler already exists
    for (int i = 0; i < g_handlerCount; i++) {
        if (g_nodeHandlers[i].type == handler.type) {
            LOG_DEBUG("Replacing existing handler for node type %d", handler.type);
            g_nodeHandlers[i] = handler;
            return;
        }
    }
    
    // Add new handler
    g_nodeHandlers[g_handlerCount++] = handler;
    LOG_DEBUG("Registered handler for node type %d", handler.type);
}

// Get a node handler by type
NodeHandler* getNodeHandler(NodeType type) {
    if (!g_registryInitialized) {
        return NULL;
    }
    
    for (int i = 0; i < g_handlerCount; i++) {
        if (g_nodeHandlers[i].type == type) {
            return &g_nodeHandlers[i];
        }
    }
    
    return NULL;
}

// Default implementations
int defaultCompileExpr(ASTNode* node, Compiler* compiler, CompilerContext* ctx) {
    // Default expression compilation - fallback to legacy compilation
    (void)ctx; // Suppress unused parameter warning
    
    // Simple fallback - would be replaced with specific implementations
    if (node->type == NODE_LITERAL) {
        return -1; // Placeholder - actual implementation would be in specific handlers
    }
    return -1; // Not implemented
}

bool defaultCompileNode(ASTNode* node, Compiler* compiler, CompilerContext* ctx) {
    // Default node compilation - fallback implementation
    (void)ctx; // Suppress unused parameter warning
    (void)node; // Suppress unused parameter warning
    (void)compiler; // Suppress unused parameter warning
    
    // Simple fallback - would be replaced with specific implementations
    return false; // Not implemented
}

void defaultAnalyzeComplexity(ASTNode* node, CodeComplexity* complexity) {
    if (!node || !complexity) return;
    
    // Default complexity analysis - simple scoring
    switch (node->type) {
        case NODE_LITERAL:
        case NODE_IDENTIFIER:
            complexity->complexityScore += 1.0f;
            break;
            
        case NODE_BINARY:
            complexity->complexExpressionCount++;
            complexity->complexityScore += 2.0f;
            break;
            
        case NODE_IF:
            complexity->complexityScore += 3.0f;
            break;
            
        case NODE_WHILE:
        case NODE_FOR_RANGE:
            complexity->loopCount++;
            complexity->complexityScore += 5.0f;
            break;
            
        case NODE_CALL:
            complexity->callCount++;
            complexity->complexityScore += 4.0f;
            break;
            
        case NODE_BREAK:
        case NODE_CONTINUE:
            complexity->hasBreakContinue = true;
            complexity->complexityScore += 2.0f;
            break;
            
        default:
            complexity->complexityScore += 1.0f;
            break;
    }
}

void defaultOptimize(ASTNode* node, VMOptimizationContext* vmCtx, RegisterState* regState) {
    if (!node || !vmCtx || !regState) return;
    
    // Default optimization - mark hot paths for simple nodes
    switch (node->type) {
        case NODE_BINARY:
            if (vmCtx->optimizeForSpeed) {
                markHotPath(node, regState);
            }
            break;
            
        case NODE_FOR_RANGE:
        case NODE_WHILE:
            if (vmCtx->optimizeForSpeed) {
                optimizeHotPath(node, vmCtx, NULL);
            }
            break;
            
        default:
            // No specific optimizations
            break;
    }
}

// Register all standard node handlers
void registerAllNodeHandlers(void) {
    initNodeRegistry();
    
    // Register handlers for each node type
    
    // Literal handler
    NodeHandler literalHandler = {
        .type = NODE_LITERAL,
        .compileExpr = defaultCompileExpr,
        .compileNode = NULL, // Literals don't compile as statements
        .analyzeComplexity = defaultAnalyzeComplexity,
        .optimize = NULL // No specific optimizations
    };
    registerNodeHandler(literalHandler);
    
    // Identifier handler
    NodeHandler identifierHandler = {
        .type = NODE_IDENTIFIER,
        .compileExpr = defaultCompileExpr,
        .compileNode = NULL,
        .analyzeComplexity = defaultAnalyzeComplexity,
        .optimize = NULL
    };
    registerNodeHandler(identifierHandler);
    
    // Binary operation handler
    NodeHandler binaryHandler = {
        .type = NODE_BINARY,
        .compileExpr = defaultCompileExpr,
        .compileNode = NULL,
        .analyzeComplexity = defaultAnalyzeComplexity,
        .optimize = defaultOptimize
    };
    registerNodeHandler(binaryHandler);
    
    // Variable declaration handler
    NodeHandler varDeclHandler = {
        .type = NODE_VAR_DECL,
        .compileExpr = NULL,
        .compileNode = defaultCompileNode,
        .analyzeComplexity = defaultAnalyzeComplexity,
        .optimize = NULL
    };
    registerNodeHandler(varDeclHandler);
    
    // If statement handler
    NodeHandler ifHandler = {
        .type = NODE_IF,
        .compileExpr = NULL,
        .compileNode = defaultCompileNode,
        .analyzeComplexity = defaultAnalyzeComplexity,
        .optimize = defaultOptimize
    };
    registerNodeHandler(ifHandler);
    
    // While loop handler
    NodeHandler whileHandler = {
        .type = NODE_WHILE,
        .compileExpr = NULL,
        .compileNode = defaultCompileNode,
        .analyzeComplexity = defaultAnalyzeComplexity,
        .optimize = defaultOptimize
    };
    registerNodeHandler(whileHandler);
    
    // For-range loop handler
    NodeHandler forRangeHandler = {
        .type = NODE_FOR_RANGE,
        .compileExpr = NULL,
        .compileNode = defaultCompileNode,
        .analyzeComplexity = defaultAnalyzeComplexity,
        .optimize = defaultOptimize
    };
    registerNodeHandler(forRangeHandler);
    
    // Function call handler
    NodeHandler callHandler = {
        .type = NODE_CALL,
        .compileExpr = defaultCompileExpr,
        .compileNode = defaultCompileNode,
        .analyzeComplexity = defaultAnalyzeComplexity,  
        .optimize = defaultOptimize
    };
    registerNodeHandler(callHandler);
    
    // Print statement handler
    NodeHandler printHandler = {
        .type = NODE_PRINT,
        .compileExpr = NULL,
        .compileNode = defaultCompileNode,
        .analyzeComplexity = defaultAnalyzeComplexity,
        .optimize = NULL
    };
    registerNodeHandler(printHandler);
    
    // Type cast handler
    NodeHandler castHandler = {
        .type = NODE_CAST,
        .compileExpr = defaultCompileExpr,
        .compileNode = NULL,
        .analyzeComplexity = defaultAnalyzeComplexity,
        .optimize = NULL
    };
    registerNodeHandler(castHandler);
    
    // Break/Continue handlers (multi-pass only)
    NodeHandler breakHandler = {
        .type = NODE_BREAK,
        .compileExpr = NULL,
        .compileNode = defaultCompileNode,
        .analyzeComplexity = defaultAnalyzeComplexity,
        .optimize = NULL
    };
    registerNodeHandler(breakHandler);
    
    NodeHandler continueHandler = {
        .type = NODE_CONTINUE,
        .compileExpr = NULL,
        .compileNode = defaultCompileNode,
        .analyzeComplexity = defaultAnalyzeComplexity,
        .optimize = NULL
    };
    registerNodeHandler(continueHandler);
    
    // Time stamp handler
    NodeHandler timeStampHandler = {
        .type = NODE_TIME_STAMP,
        .compileExpr = defaultCompileExpr,
        .compileNode = NULL,
        .analyzeComplexity = defaultAnalyzeComplexity,
        .optimize = NULL
    };
    registerNodeHandler(timeStampHandler);
    
    LOG_DEBUG("All standard node handlers registered");
}