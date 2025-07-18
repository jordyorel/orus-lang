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
    int totalOptimizations;
} LoopOptimizationStats;

// Loop optimizer state
typedef struct {
    bool enabled;
    int unrollCount;
    int strengthReductionCount;
    int boundsEliminationCount;
    int totalOptimizations;
} LoopOptimizer;

// Initialize loop optimization system
void initLoopOptimization(LoopOptimizer* optimizer);

// Main entry point for loop optimization
// Returns true if any optimization was applied
bool optimizeLoop(ASTNode* node, Compiler* compiler);

// Get optimization statistics
LoopOptimizationStats getLoopOptimizationStats(LoopOptimizer* optimizer);

// Print optimization statistics
void printLoopOptimizationStats(LoopOptimizer* optimizer);

// Enable or disable loop optimizations
void setLoopOptimizationEnabled(LoopOptimizer* optimizer, bool enabled);

// Reset optimization counters
void resetLoopOptimizationStats(LoopOptimizer* optimizer);

#endif // LOOP_OPTIMIZATION_H