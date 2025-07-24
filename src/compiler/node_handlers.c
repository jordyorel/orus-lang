#include <string.h>
#include <stdio.h>

#include "compiler/node_registry.h"
#include "compiler/compiler.h"
#include "compiler/shared_node_compilation.h"
#include "compiler/vm_optimization.h"
#include "internal/logging.h"

// Specific node handler implementations

// Literal node handler
static int compileLiteralExpr(ASTNode* node, Compiler* compiler, CompilerContext* ctx) {
    if (!node || !compiler || !ctx) return -1;
    
    LOG_DEBUG("Compiling literal node via registry");
    
    // Use shared literal compilation
    return compileSharedLiteral(node, compiler, ctx);
}

// Binary operation node handler
static int compileBinaryExpr(ASTNode* node, Compiler* compiler, CompilerContext* ctx) {
    if (!node || !compiler || !ctx) return -1;
    
    LOG_DEBUG("Compiling binary operation via registry");
    
    // Use shared binary operation compilation
    return compileSharedBinaryOp(node, compiler, ctx);
}

// Variable declaration node handler
static bool compileVarDeclNode(ASTNode* node, Compiler* compiler, CompilerContext* ctx) {
    if (!node || !compiler || !ctx) return false;
    
    LOG_DEBUG("Compiling variable declaration via registry");
    
    // Delegate to shared implementation
    return compileSharedNode(node, compiler, ctx);
}

// If statement node handler  
static bool compileIfNode(ASTNode* node, Compiler* compiler, CompilerContext* ctx) {
    if (!node || !compiler || !ctx) return false;
    
    LOG_DEBUG("Compiling if statement via registry");
    
    // Delegate to shared implementation
    return compileSharedNode(node, compiler, ctx);
}

// Loop-specific complexity analysis
static void analyzeLoopComplexity(ASTNode* node, CodeComplexity* complexity) {
    if (!node || !complexity) return;
    
    complexity->loopCount++;
    complexity->complexityScore += 8.0f; // Loops are expensive
    
    // Analyze nested complexity
    if (node->type == NODE_FOR_RANGE && node->forRange.body) {
        // Check for nested loops
        if (node->forRange.body->type == NODE_BLOCK) {
            for (int i = 0; i < node->forRange.body->block.count; i++) {
                ASTNode* stmt = node->forRange.body->block.statements[i];
                if (stmt->type == NODE_FOR_RANGE || stmt->type == NODE_WHILE) {
                    complexity->nestedLoopDepth++;
                    complexity->complexityScore += 5.0f;
                }
            }
        }
    } else if (node->type == NODE_WHILE && node->whileStmt.body) {
        // Similar analysis for while loops
        if (node->whileStmt.body->type == NODE_BLOCK) {
            for (int i = 0; i < node->whileStmt.body->block.count; i++) {
                ASTNode* stmt = node->whileStmt.body->block.statements[i];
                if (stmt->type == NODE_FOR_RANGE || stmt->type == NODE_WHILE) {
                    complexity->nestedLoopDepth++;
                    complexity->complexityScore += 5.0f;
                }
            }
        }
    }
}

// Function call complexity analysis
static void analyzeCallComplexity(ASTNode* node, CodeComplexity* complexity) {
    if (!node || !complexity) return;
    
    complexity->callCount++;
    complexity->complexityScore += 6.0f; // Function calls are moderately expensive
    
    // Add complexity for arguments
    if (node->call.argCount > 0) {
        complexity->complexityScore += node->call.argCount * 1.5f;
    }
}

// Binary operation optimization
static void optimizeBinaryOp(ASTNode* node, VMOptimizationContext* vmCtx, RegisterState* regState) {
    if (!node || !vmCtx || !regState) return;
    
    if (vmCtx->optimizeForSpeed) {
        // Mark as hot path for common operations
        if (strcmp(node->binary.op, "+") == 0 || 
            strcmp(node->binary.op, "-") == 0 ||
            strcmp(node->binary.op, "*") == 0) {
            markHotPath(node, regState);
        }
    }
}

// Loop optimization
static void optimizeLoop(ASTNode* node, VMOptimizationContext* vmCtx, RegisterState* regState) {
    if (!node || !vmCtx || !regState) return;
    
    if (vmCtx->optimizeForSpeed) {
        // Apply loop-specific optimizations
        optimizeHotPath(node, vmCtx, NULL);
        
        // Mark loop variables as pinned for better allocation
        if (node->type == NODE_FOR_RANGE) {
            // Mark iterator variable as long-lived
            // This would be implemented in the actual compilation phase
        }
    }
}

// Register optimized node handlers
void registerOptimizedNodeHandlers(void) {
    initNodeRegistry();
    
    // Override default handlers with optimized versions
    
    // Optimized literal handler
    NodeHandler optimizedLiteralHandler = {
        .type = NODE_LITERAL,
        .compileExpr = compileLiteralExpr,
        .compileNode = NULL,
        .analyzeComplexity = defaultAnalyzeComplexity,
        .optimize = NULL
    };
    registerNodeHandler(optimizedLiteralHandler);
    
    // Optimized binary operation handler
    NodeHandler optimizedBinaryHandler = {
        .type = NODE_BINARY,
        .compileExpr = compileBinaryExpr,
        .compileNode = NULL,
        .analyzeComplexity = defaultAnalyzeComplexity,
        .optimize = optimizeBinaryOp
    };
    registerNodeHandler(optimizedBinaryHandler);
    
    // Optimized variable declaration handler
    NodeHandler optimizedVarDeclHandler = {
        .type = NODE_VAR_DECL,
        .compileExpr = NULL,
        .compileNode = compileVarDeclNode,
        .analyzeComplexity = defaultAnalyzeComplexity,
        .optimize = NULL
    };
    registerNodeHandler(optimizedVarDeclHandler);
    
    // Optimized if statement handler
    NodeHandler optimizedIfHandler = {
        .type = NODE_IF,
        .compileExpr = NULL,
        .compileNode = compileIfNode,
        .analyzeComplexity = defaultAnalyzeComplexity,
        .optimize = defaultOptimize
    };
    registerNodeHandler(optimizedIfHandler);
    
    // Optimized for-range handler
    NodeHandler optimizedForRangeHandler = {
        .type = NODE_FOR_RANGE,
        .compileExpr = NULL,
        .compileNode = defaultCompileNode,
        .analyzeComplexity = analyzeLoopComplexity,
        .optimize = optimizeLoop
    };
    registerNodeHandler(optimizedForRangeHandler);
    
    // Optimized while handler
    NodeHandler optimizedWhileHandler = {
        .type = NODE_WHILE,
        .compileExpr = NULL,
        .compileNode = defaultCompileNode,
        .analyzeComplexity = analyzeLoopComplexity,
        .optimize = optimizeLoop
    };
    registerNodeHandler(optimizedWhileHandler);
    
    // Optimized function call handler
    NodeHandler optimizedCallHandler = {
        .type = NODE_CALL,
        .compileExpr = defaultCompileExpr,
        .compileNode = defaultCompileNode,
        .analyzeComplexity = analyzeCallComplexity,
        .optimize = defaultOptimize
    };
    registerNodeHandler(optimizedCallHandler);
    
    LOG_DEBUG("Optimized node handlers registered");
}