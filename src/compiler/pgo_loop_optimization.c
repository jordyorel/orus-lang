/* Author: Jordy Orel KONDA
 *
 * PGO-Enhanced Loop Optimization for Orus
 * 
 * Integrates Profile-Guided Optimization data with loop optimization
 * to make smarter decisions based on runtime behavior.
 */

#include "compiler/loop_optimization.h"
#include "compiler/compiler.h"
#include "compiler/profile_guided_optimization.h"
#include "vm/vm_profiling.h"
#include <stdio.h>
#include <stdlib.h>

// Simplified PGO loop context
typedef struct {
    int64_t iterationCount;
    bool isConstantRange;
    bool canUnroll;
    bool isPGOHot;
    float hotness;
    HotPathAnalysis* pgoAnalysis;
} SimplePGOLoopContext;

// Helper function to check if a node is a simple constant
static bool isSimpleConstant(ASTNode* node) {
    return node && node->type == NODE_LITERAL;
}

// Enhanced loop analysis with PGO data
static void analyzePGOLoop(ASTNode* node, Compiler* compiler, SimplePGOLoopContext* context) {
    if (!node || !compiler || !context) return;

    context->iterationCount = 0;
    context->isConstantRange = false;
    context->canUnroll = false;
    context->isPGOHot = false;
    context->hotness = 0.0f;

    // Get PGO analysis if available
    if (g_pgoContext.isEnabled) {
        void* codeAddress = (void*)node;
        context->pgoAnalysis = analyzeHotPath(node, codeAddress);
        if (context->pgoAnalysis) {
            context->isPGOHot = isPGOHotPath(context->pgoAnalysis);
            context->hotness = context->pgoAnalysis->hotness;
        }
    }

    // Analyze constant range for for-loops
    if (node->type == NODE_FOR_RANGE) {
        ASTNode* start = node->forRange.start;
        ASTNode* end = node->forRange.end;
        ASTNode* step = node->forRange.step;

        if (isSimpleConstant(start) && isSimpleConstant(end) && 
            (!step || isSimpleConstant(step))) {
            context->isConstantRange = true;
            
            int64_t startVal = AS_I32(start->literal.value);
            int64_t endVal = AS_I32(end->literal.value);
            int64_t stepVal = step ? AS_I32(step->literal.value) : 1;
            
            if (stepVal > 0 && endVal > startVal) {
                context->iterationCount = (endVal - startVal + stepVal - 1) / stepVal;
            }

            // PGO-enhanced unrolling decision
            bool basicUnroll = context->iterationCount > 0 && context->iterationCount <= 16;
            
            if (context->isPGOHot && context->pgoAnalysis) {
                // Use PGO data for smarter unrolling
                bool shouldUnroll = shouldUnrollLoop(node, context->pgoAnalysis);
                context->canUnroll = basicUnroll && shouldUnroll;
                
                if (context->canUnroll) {
                    printf("PGO: Hot loop unrolling enabled (hotness: %.1f%%, iterations: %lld)\\n", 
                           context->hotness * 100.0f, (long long)context->iterationCount);
                }
            } else {
                context->canUnroll = basicUnroll;
            }
        }
    }
}

// Apply PGO-enhanced optimizations
static bool applyPGOOptimizations(ASTNode* node, SimplePGOLoopContext* context) {
    if (!context->isPGOHot) {
        return false; // Only optimize hot paths with PGO
    }

    // Apply PGO-specific optimizations
    PGODecisionFlags decisions = makePGODecisions(node, context->pgoAnalysis, BACKEND_AUTO);
    
    if (decisions & PGO_DECISION_OPTIMIZE_BACKEND) {
        printf("PGO: Backend optimization applied (hotness: %.1f%%)\\n", 
               context->hotness * 100.0f);
    }
    
    if (decisions & PGO_DECISION_UNROLL && context->canUnroll) {
        int unrollFactor = calculateUnrollFactor(node, context->pgoAnalysis);
        printf("PGO: Loop unroll factor: %d\\n", unrollFactor);
        g_pgoContext.loopsOptimized++;
    }

    return true;
}

// Main PGO-enhanced loop optimization function
bool optimizePGOLoop(ASTNode* node, Compiler* compiler) {
    if (!node || !compiler) {
        return false;
    }

    if (node->type != NODE_FOR_RANGE && node->type != NODE_WHILE) {
        return false;
    }

    // Create PGO loop context
    SimplePGOLoopContext context = {0};
    
    // Analyze loop with PGO enhancements
    analyzePGOLoop(node, compiler, &context);

    // Apply PGO optimizations if this is a hot path
    bool pgoApplied = applyPGOOptimizations(node, &context);
    
    // Fallback to standard loop optimization if no PGO optimizations applied
    if (!pgoApplied) {
        return optimizeLoop(node, compiler);
    }

    // For hot paths, we still need to compile the loop body
    ASTNode* loopBody = (node->type == NODE_FOR_RANGE) ? 
                        node->forRange.body : node->whileStmt.body;
    
    if (loopBody) {
        return compileNode(loopBody, compiler);
    }

    return true;
}

// Integration function to apply PGO to compilation
void applyPGOToLoopCompilation(ASTNode* node, Compiler* compiler) {
    if (!g_pgoContext.isEnabled || !node || !compiler) return;
    
    // Update profiling data
    updateHotPathFromProfiling();
    
    // Apply PGO compilation optimizations
    applyPGOToCompilation(node, compiler);
}

// Get PGO loop optimization statistics
void printPGOLoopStats(void) {
    if (!g_pgoContext.isEnabled) {
        printf("PGO loop optimization is disabled\\n");
        return;
    }

    printf("\\n=== PGO Loop Optimization Statistics ===\\n");
    printf("Loops optimized: %u\\n", g_pgoContext.loopsOptimized);
    printf("Functions optimized: %u\\n", g_pgoContext.functionsOptimized);
    printf("Backend switches: %u\\n", g_pgoContext.backendSwitches);
    printf("Hot paths detected: %u\\n", g_pgoContext.hotPathCount);
}