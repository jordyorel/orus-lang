#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "vm/vm.h"
#include "compiler/compiler.h"
#include "compiler/backend_selection.h"
#include "compiler/profile_guided_optimization.h"

#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))

// Forward declaration for helper function
static void analyzeNodeComplexity(ASTNode* node, CodeComplexity* complexity, int depth);

// Global profile data for hot path detection (legacy - now using PGO system)
__attribute__((unused)) static ProfileData* g_profileData = NULL;
__attribute__((unused)) static int g_profileCount = 0;
__attribute__((unused)) static int g_profileCapacity = 0;

// Initialize compilation context
void initCompilationContext(CompilationContext* ctx, bool debugMode) {
    ctx->isDebugMode = debugMode;
    ctx->isHotPath = false;
    ctx->functionCallDepth = 0;
    ctx->loopNestingDepth = 0;
    ctx->expressionComplexity = 0;
    ctx->hasBreakContinue = false;
    ctx->hasComplexTypes = false;
    ctx->codeSize = 0;
}

// Unified complexity analysis (merging both previous implementations)
CodeComplexity analyzeCodeComplexity(ASTNode* node) {
    CodeComplexity result = {0};
    if (!node) return result;
    
    analyzeNodeComplexity(node, &result, 0);
    
    // Calculate unified complexity score
    result.complexityScore = 
        result.functionCount * 3.0f + 
        result.loopCount * 2.0f +
        result.nestedLoopDepth * 4.0f + 
        result.callCount * 1.0f + 
        result.complexExpressionCount * 1.5f +
        (result.hasBreakContinue ? 3.0f : 0.0f) +
        (result.hasComplexArithmetic ? 2.0f : 0.0f);
    
    return result;
}

// Recursive helper function for unified complexity analysis
static void analyzeNodeComplexity(ASTNode* node, CodeComplexity* complexity, int depth) {
    if (!node) return;

    switch (node->type) {
        case NODE_PROGRAM:
            for (int i = 0; i < node->program.count; i++) {
                analyzeNodeComplexity(node->program.declarations[i], complexity, depth);
            }
            break;

        case NODE_FUNCTION:
            complexity->functionCount++;
            analyzeNodeComplexity(node->function.body, complexity, depth + 1);
            break;

        case NODE_FOR_RANGE:
        case NODE_WHILE:
            complexity->loopCount++;
            if (depth > complexity->nestedLoopDepth) {
                complexity->nestedLoopDepth = depth;
            }

            ASTNode* loopBody = (node->type == NODE_FOR_RANGE)
                                    ? node->forRange.body
                                    : node->whileStmt.body;
            analyzeNodeComplexity(loopBody, complexity, depth + 1);
            break;

        case NODE_BREAK:
        case NODE_CONTINUE:
            complexity->hasBreakContinue = true;
            break;

        case NODE_CALL:
            complexity->callCount++;
            analyzeNodeComplexity(node->call.callee, complexity, depth);
            for (int i = 0; i < node->call.argCount; i++) {
                analyzeNodeComplexity(node->call.args[i], complexity, depth);
            }
            break;

        case NODE_BINARY:
            // Check for complex arithmetic operations
            if (node->binary.op && 
                (strcmp(node->binary.op, "*") == 0 || 
                 strcmp(node->binary.op, "/") == 0 ||
                 strcmp(node->binary.op, "%") == 0)) {
                complexity->hasComplexArithmetic = true;
                complexity->complexExpressionCount++;
            }
            analyzeNodeComplexity(node->binary.left, complexity, depth);
            analyzeNodeComplexity(node->binary.right, complexity, depth);
            break;

        case NODE_CAST:
            complexity->complexExpressionCount++;
            analyzeNodeComplexity(node->cast.expression, complexity, depth);
            break;

        case NODE_TERNARY:
            complexity->complexExpressionCount++;
            analyzeNodeComplexity(node->ternary.condition, complexity, depth);
            analyzeNodeComplexity(node->ternary.trueExpr, complexity, depth);
            analyzeNodeComplexity(node->ternary.falseExpr, complexity, depth);
            break;

        case NODE_BLOCK:
            for (int i = 0; i < node->block.count; i++) {
                analyzeNodeComplexity(node->block.statements[i], complexity, depth);
            }
            break;

        case NODE_IF:
            analyzeNodeComplexity(node->ifStmt.condition, complexity, depth);
            analyzeNodeComplexity(node->ifStmt.thenBranch, complexity, depth);
            if (node->ifStmt.elseBranch) {
                analyzeNodeComplexity(node->ifStmt.elseBranch, complexity, depth);
            }
            break;

        case NODE_VAR_DECL:
            if (node->varDecl.initializer) {
                analyzeNodeComplexity(node->varDecl.initializer, complexity, depth);
            }
            break;

        case NODE_ASSIGN:
            analyzeNodeComplexity(node->assign.value, complexity, depth);
            break;

        case NODE_RETURN:
            if (node->returnStmt.value) {
                analyzeNodeComplexity(node->returnStmt.value, complexity, depth);
            }
            break;

        default:
            // For simple nodes like NODE_LITERAL, NODE_IDENTIFIER, no additional complexity
            break;
    }
}

// Check if expression is simple
bool isSimpleExpression(ASTNode* node) {
    if (!node) return true;
    
    switch (node->type) {
        case NODE_LITERAL:
        case NODE_IDENTIFIER:
            return true;
        case NODE_BINARY:
            // Simple arithmetic
            if (node->binary.op && 
                (strcmp(node->binary.op, "+") == 0 || 
                 strcmp(node->binary.op, "-") == 0)) {
                return isSimpleExpression(node->binary.left) && 
                       isSimpleExpression(node->binary.right);
            }
            return false;
        default:
            return false;
    }
}

// Check if loop is complex
bool isComplexLoop(ASTNode* node) {
    if (!node) return false;
    
    switch (node->type) {
        case NODE_FOR_RANGE:
        case NODE_WHILE: {
            CodeComplexity analysis = analyzeCodeComplexity(node);
            return analysis.nestedLoopDepth > 1 || 
                   analysis.loopCount > 2 ||
                   analysis.callCount > 0 ||
                   analysis.complexExpressionCount > 3;
        }
        default:
            return false;
    }
}

// Check for optimization opportunities
bool hasOptimizationOpportunities(ASTNode* node) {
    CodeComplexity analysis = analyzeCodeComplexity(node);
    return analysis.complexityScore > 10.0f;
}

// Calculate optimization benefit score
float calculateOptimizationBenefit(ASTNode* node) {
    CodeComplexity analysis = analyzeCodeComplexity(node);
    
    float benefit = 0.0f;
    
    // Loops benefit greatly from optimization
    benefit += analysis.loopCount * 0.4f;
    
    // Nested structures benefit from optimization
    benefit += analysis.nestedLoopDepth * 0.2f;
    
    // Complex expressions benefit moderately
    benefit += analysis.complexExpressionCount * 0.1f;
    
    // Function calls benefit from optimization
    benefit += analysis.callCount * 0.15f;
    
    // Complex arithmetic benefits from optimization
    if (analysis.hasComplexArithmetic) {
        benefit += 0.3f;
    }
    
    return min(1.0f, benefit);
}

// Determine if optimized backend should be used
bool shouldUseOptimizedBackend(CodeComplexity* analysis, CompilationContext* ctx) {
    // Always use fast backend in debug mode
    if (ctx->isDebugMode) return false;
    
    // Use optimized for hot paths
    if (ctx->isHotPath) return true;
    
    // Use optimized for complex loops
    if (analysis->loopCount >= 2 || analysis->nestedLoopDepth >= 2) return true;
    
    // Use optimized for high function call density
    if (analysis->callCount >= 3) return true;
    
    // Use optimized for complex expressions
    if (analysis->complexExpressionCount >= 3) return true;
    
    // Use optimized if overall complexity score is high (threshold: 15.0)
    if (analysis->complexityScore > 15.0f) return true;
    
    return false;
}

// Main backend selection function with PGO integration
CompilerBackend chooseOptimalBackend(ASTNode* node, CompilationContext* ctx) {
    if (!node || !ctx) return BACKEND_FAST;
    
    // Always use fast backend in debug mode for quick compilation
    if (ctx->isDebugMode) return BACKEND_FAST;
    
    // Check PGO data first if available
    if (g_pgoContext.isEnabled) {
        void* codeAddress = (void*)node;
        HotPathAnalysis* pgoAnalysis = analyzeHotPath(node, codeAddress);
        if (pgoAnalysis) {
            // Use PGO-guided backend selection
            CompilerBackend pgoBackend = choosePGOBackend(node, pgoAnalysis, BACKEND_FAST);
            if (pgoBackend != BACKEND_FAST) {
                return pgoBackend;
            }
        }
    }
    
    CodeComplexity analysis = analyzeCodeComplexity(node);
    
    // Simple expressions always use fast backend
    if (isSimpleExpression(node)) {
        return BACKEND_FAST;
    }
    
    // Complex loops benefit from optimization
    if (isComplexLoop(node)) {
        return BACKEND_OPTIMIZED;
    }
    
    // Hot paths use optimized backend
    if (ctx->isHotPath) {
        return BACKEND_OPTIMIZED;
    }
    
    // Use heuristics to decide
    if (shouldUseOptimizedBackend(&analysis, ctx)) {
        return BACKEND_OPTIMIZED;
    }
    
    // Default to fast for everything else
    return BACKEND_FAST;
}

// Update compilation context based on current node
void updateCompilationContext(CompilationContext* ctx, ASTNode* node) {
    if (!ctx || !node) return;
    
    // Check if this is a hot path using PGO data
    if (g_pgoContext.isEnabled) {
        ctx->isHotPath = isCompilationHotPath(node, NULL);
    }
    
    switch (node->type) {
        case NODE_FOR_RANGE:
        case NODE_WHILE:
            ctx->loopNestingDepth++;
            break;
            
        case NODE_CALL:
            ctx->functionCallDepth++;
            break;
            
        case NODE_BREAK:
        case NODE_CONTINUE:
            ctx->hasBreakContinue = true;
            break;
            
        case NODE_CAST:
            ctx->hasComplexTypes = true;
            ctx->expressionComplexity++;
            break;
            
        case NODE_BINARY:
            if (node->binary.op && 
                (strcmp(node->binary.op, "*") == 0 || 
                 strcmp(node->binary.op, "/") == 0)) {
                ctx->expressionComplexity++;
            }
            break;
            
        default:
            break;
    }
}

// New function: Apply PGO decisions to compilation context
void applyPGOToCompilationContext(CompilationContext* ctx, ASTNode* node) {
    if (!ctx || !node || !g_pgoContext.isEnabled) return;
    
    void* codeAddress = (void*)node;
    HotPathAnalysis* analysis = analyzeHotPath(node, codeAddress);
    
    if (analysis && shouldOptimizeNode(node, analysis)) {
        // Mark as hot path to influence backend selection
        ctx->isHotPath = true;
        
        // Apply PGO-specific compilation decisions
        PGODecisionFlags decisions = makePGODecisions(node, analysis, BACKEND_AUTO);
        
        if (decisions & PGO_DECISION_OPTIMIZE_BACKEND) {
            // Force use of optimized backend
            ctx->isHotPath = true;
        }
        
        // Integrate with VM optimization system
        integrateWithBackendSelection(ctx, analysis);
    }
}

// Hot path detection using PGO data
bool isCompilationHotPath(ASTNode* node, ProfileData* profile __attribute__((unused))) {
    // If PGO is enabled, use actual profiling data
    if (g_pgoContext.isEnabled) {
        void* codeAddress = (void*)node;
        HotPathAnalysis* analysis = analyzeHotPath(node, codeAddress);
        if (analysis) {
            return isPGOHotPath(analysis);
        }
    }
    
    // Fallback to heuristic - complex loops are considered hot
    return isComplexLoop(node);
}

// Update profile data (placeholder for Phase 4)
void updateProfileData(const char* functionName, double executionTime) {
    // Placeholder - would maintain execution statistics
    (void)functionName;
    (void)executionTime;
}

// Get VM optimization hints based on backend
VMOptimizationHints getVMOptimizationHints(CompilerBackend backend) {
    VMOptimizationHints hints = {0};
    
    switch (backend) {
        case BACKEND_FAST:
            hints.preferRegisterReuse = false;
            hints.minimizeSpilling = false;
            hints.optimizeForSpeed = false;
            hints.targetRegisterCount = 32; // Use fewer registers for simplicity
            break;
            
        case BACKEND_OPTIMIZED:
            hints.preferRegisterReuse = true;
            hints.minimizeSpilling = true;
            hints.optimizeForSpeed = true;
            hints.targetRegisterCount = 128; // Use more registers for optimization
            break;
            
        case BACKEND_HYBRID:
        case BACKEND_AUTO:
            hints.preferRegisterReuse = true;
            hints.minimizeSpilling = true;
            hints.optimizeForSpeed = true;
            hints.targetRegisterCount = 64; // Balanced approach
            break;
    }
    
    return hints;
}