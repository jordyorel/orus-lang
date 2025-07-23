#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "vm/vm.h"
#include "compiler/compiler.h"
#include "compiler/backend_selection.h"
#include "compiler/profile_guided_optimization.h"

#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))

// Global profile data for hot path detection
static ProfileData* g_profileData = NULL;
static int g_profileCount = 0;
static int g_profileCapacity = 0;

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

// Analyze code complexity recursively
CodeAnalysisResult analyzeCodeComplexity(ASTNode* node) {
    CodeAnalysisResult result = {0};
    if (!node) return result;
    
    switch (node->type) {
        case NODE_FOR_RANGE:
        case NODE_WHILE:
            result.loopCount++;
            result.controlFlowCount++;
            result.maxNestingDepth = 1; // Simplified for now
            break;
            
        case NODE_CALL:
            result.functionCallCount++;
            break;
            
        case NODE_BINARY:
            if (node->binary.op && 
                (strcmp(node->binary.op, "*") == 0 || 
                 strcmp(node->binary.op, "/") == 0 ||
                 strcmp(node->binary.op, "%") == 0)) {
                result.hasComplexArithmetic = true;
                result.complexExpressionCount++;
            }
            break;
            
        case NODE_CAST:
            result.complexExpressionCount++;
            break;
            
        case NODE_BREAK:
        case NODE_CONTINUE:
            result.controlFlowCount++;
            break;
            
        case NODE_BLOCK:
            // Analyze all statements in block
            for (int i = 0; i < node->block.count; i++) {
                CodeAnalysisResult nested = analyzeCodeComplexity(node->block.statements[i]);
                result.loopCount += nested.loopCount;
                result.maxNestingDepth = max(result.maxNestingDepth, nested.maxNestingDepth);
                result.functionCallCount += nested.functionCallCount;
                result.complexExpressionCount += nested.complexExpressionCount;
                result.controlFlowCount += nested.controlFlowCount;
                result.hasComplexArithmetic = result.hasComplexArithmetic || nested.hasComplexArithmetic;
            }
            break;
            
        default:
            // For other node types, recursively analyze children
            break;
    }
    
    // Calculate optimization potential (0.0 - 1.0)
    float complexity = (float)(result.loopCount * 3 + 
                              result.functionCallCount * 2 + 
                              result.complexExpressionCount +
                              result.controlFlowCount);
    result.optimizationPotential = min(1.0f, complexity / 20.0f);
    
    return result;
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
            CodeAnalysisResult analysis = analyzeCodeComplexity(node);
            return analysis.maxNestingDepth > 1 || 
                   analysis.controlFlowCount > 2 ||
                   analysis.functionCallCount > 0 ||
                   analysis.complexExpressionCount > 3;
        }
        default:
            return false;
    }
}

// Check for optimization opportunities
bool hasOptimizationOpportunities(ASTNode* node) {
    CodeAnalysisResult analysis = analyzeCodeComplexity(node);
    return analysis.optimizationPotential > 0.3f;
}

// Calculate optimization benefit score
float calculateOptimizationBenefit(ASTNode* node) {
    CodeAnalysisResult analysis = analyzeCodeComplexity(node);
    
    float benefit = 0.0f;
    
    // Loops benefit greatly from optimization
    benefit += analysis.loopCount * 0.4f;
    
    // Nested structures benefit from optimization
    benefit += analysis.maxNestingDepth * 0.2f;
    
    // Complex expressions benefit moderately
    benefit += analysis.complexExpressionCount * 0.1f;
    
    // Function calls benefit from optimization
    benefit += analysis.functionCallCount * 0.15f;
    
    // Complex arithmetic benefits from optimization
    if (analysis.hasComplexArithmetic) {
        benefit += 0.3f;
    }
    
    return min(1.0f, benefit);
}

// Determine if optimized backend should be used
bool shouldUseOptimizedBackend(CodeAnalysisResult* analysis, CompilationContext* ctx) {
    // Always use fast backend in debug mode
    if (ctx->isDebugMode) return false;
    
    // Use optimized for hot paths
    if (ctx->isHotPath) return true;
    
    // Use optimized for complex loops
    if (analysis->loopCount >= 2 || analysis->maxNestingDepth >= 2) return true;
    
    // Use optimized for high function call density
    if (analysis->functionCallCount >= 3) return true;
    
    // Use optimized if optimization potential is high
    if (analysis->optimizationPotential >= 0.5f) return true;
    
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
    
    CodeAnalysisResult analysis = analyzeCodeComplexity(node);
    
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
bool isCompilationHotPath(ASTNode* node, ProfileData* profile) {
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