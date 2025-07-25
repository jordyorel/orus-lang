/*
 * Loop Optimization Framework for Orus
 * 
 * High-performance loop optimizations for the single-pass compiler.
 * All optimizations are applied during the single forward compilation pass.
 */

#ifndef LOOP_OPTIMIZATION_H
#define LOOP_OPTIMIZATION_H

#include <stdbool.h>
#include <stdint.h>
#include "vm/vm.h"

// Forward declarations
typedef struct ASTNode ASTNode;

// Loop optimization statistics
typedef struct {
    int unrollCount;
    int strengthReductionCount;
    int boundsEliminationCount;
    int licmCount;
    int totalOptimizations;
} LoopOptimizationStats;

// Loop optimizer state
typedef struct {
    bool enabled;
    int unrollCount;
    int strengthReductionCount;
    int boundsEliminationCount;
    int licmCount;
    int totalOptimizations;
} LoopOptimizer;

// Initialize loop optimization system
void initLoopOptimization(LoopOptimizer* optimizer);

// Main entry point for loop optimization
// Returns true if any optimization was applied
bool optimizeLoop(ASTNode* node, Compiler* compiler);

// PGO-enhanced loop optimization entry point
// Integrates profile-guided optimization data for smarter optimization decisions
bool optimizePGOLoop(ASTNode* node, Compiler* compiler);
void applyPGOToLoopCompilation(ASTNode* node, Compiler* compiler);
void printPGOLoopStats(void);

// Get optimization statistics
LoopOptimizationStats getLoopOptimizationStats(LoopOptimizer* optimizer);

// Print optimization statistics
void printLoopOptimizationStats(LoopOptimizer* optimizer);

// Enable or disable loop optimizations
void setLoopOptimizationEnabled(LoopOptimizer* optimizer, bool enabled);

// Reset optimization counters
void resetLoopOptimizationStats(LoopOptimizer* optimizer);

// Global optimization statistics
void updateGlobalOptimizationStats(LoopOptimizer* optimizer);
void updateGlobalOptimizationStatsFromCompiler(Compiler* compiler);
void printGlobalOptimizationStats(void);

// LICM Expression Replacement Interface (single-pass compatible)
bool tryReplaceInvariantExpression(ASTNode* expr, int* outTempVarIdx);
void enableLICMReplacements(void);
void disableLICMReplacements(void);

#endif // LOOP_OPTIMIZATION_H