#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "vm/vm.h"
#include "compiler/compiler.h"
#include "compiler/vm_optimization.h"
#include "vm/vm_profiling.h"

#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))

// VM Constants
#define VM_REGISTER_COUNT 256
#define VM_CACHE_LINE_SIZE 64
#define VM_PREFERRED_WORKING_SET 128  // Preferred number of active registers

// Create VM optimization context based on backend selection
VMOptimizationContext createVMOptimizationContext(CompilerBackend backend) {
    VMOptimizationContext ctx = {0};
    
    switch (backend) {
        case BACKEND_FAST:
            // Fast compilation - minimal optimizations
            ctx.targetRegisterCount = 32;
            ctx.enableRegisterReuse = false;
            ctx.optimizeForSpeed = false;
            ctx.enableComputedGoto = false;
            ctx.registerPressure = 0.0f;
            ctx.spillThreshold = 24;
            break;
            
        case BACKEND_OPTIMIZED:
            // Optimized compilation - aggressive optimizations
            ctx.targetRegisterCount = VM_PREFERRED_WORKING_SET;
            ctx.enableRegisterReuse = true;
            ctx.optimizeForSpeed = true;
            ctx.enableComputedGoto = true;
            ctx.registerPressure = 0.0f;
            ctx.spillThreshold = 200;
            break;
            
        case BACKEND_HYBRID:
        case BACKEND_AUTO:
            // Balanced approach
            ctx.targetRegisterCount = 64;
            ctx.enableRegisterReuse = true;
            ctx.optimizeForSpeed = true;
            ctx.enableComputedGoto = true;
            ctx.registerPressure = 0.0f;
            ctx.spillThreshold = 50;
            break;
    }
    
    return ctx;
}

// Initialize register state for 256-register VM
void initRegisterState(RegisterState* regState) {
    memset(regState, 0, sizeof(RegisterState));
    regState->availableRegisters = VM_REGISTER_COUNT;
    regState->highWaterMark = 0;
    
    // Mark first few registers as reserved for special purposes
    for (int i = 0; i < 4; i++) {
        regState->isPinned[i] = true;
        regState->availableRegisters--;
    }
}

// Calculate current register pressure (0.0 = no pressure, 1.0 = full)
float calculateRegisterPressure(RegisterState* regState) {
    int activeRegisters = 0;
    for (int i = 0; i < VM_REGISTER_COUNT; i++) {
        if (regState->liveRegisters[i] > 0) {
            activeRegisters++;
        }
    }
    return (float)activeRegisters / (float)VM_REGISTER_COUNT;
}

// Allocate optimal register based on VM characteristics
int allocateOptimalRegister(RegisterState* regState, VMOptimizationContext* vmCtx, 
                           bool isLoopVar, int estimatedLifetime) {
    // Find best register based on optimization context
    int bestReg = -1;
    int bestScore = -1;
    
    for (int i = 4; i < VM_REGISTER_COUNT; i++) { // Skip reserved registers
        if (regState->isPinned[i] || regState->liveRegisters[i] > 0) {
            continue;
        }
        
        int score = 100; // Base score
        
        // Prefer lower-numbered registers for better cache locality
        score -= i / 4;
        
        // For loop variables, prefer registers that were previously loop vars
        if (isLoopVar && regState->isLoopVariable[i]) {
            score += 20;
        }
        
        // Prefer registers with low spill cost
        score += (10 - regState->spillCost[i]);
        
        // For reuse optimization, prefer recently freed registers
        if (vmCtx->enableRegisterReuse && regState->lastUse[i] > 0) {
            score += 15;
        }
        
        if (score > bestScore) {
            bestScore = score;
            bestReg = i;
        }
    }
    
    if (bestReg >= 0) {
        regState->liveRegisters[bestReg] = estimatedLifetime;
        regState->isLoopVariable[bestReg] = isLoopVar;
        regState->useCount[bestReg]++;
        regState->highWaterMark = max(regState->highWaterMark, bestReg + 1);
        regState->availableRegisters--;
    }
    
    return bestReg;
}

// Free optimized register
void freeOptimizedRegister(RegisterState* regState, int reg) {
    if (reg >= 0 && reg < VM_REGISTER_COUNT && !regState->isPinned[reg]) {
        regState->liveRegisters[reg] = 0;
        regState->lastUse[reg] = 1; // Mark as recently freed
        regState->availableRegisters++;
    }
}

// Get instruction optimization hints
InstructionOptHints getInstructionOptimizations(ASTNode* node, VMOptimizationContext* vmCtx) {
    InstructionOptHints hints = {0};
    
    if (!node || !vmCtx) return hints;
    
    hints.enableConstFolding = vmCtx->optimizeForSpeed;
    hints.enableDeadCodeElim = vmCtx->optimizeForSpeed;
    hints.enableBranchPrediction = vmCtx->enableComputedGoto;
    
    switch (node->type) {
        case NODE_BINARY:
            // Simple operations can be inlined
            hints.preferInlineOp = true;
            break;
            
        case NODE_FOR_RANGE:
        case NODE_WHILE:
            // Small loops can be unrolled
            if (vmCtx->optimizeForSpeed) {
                hints.loopUnrollFactor = 2;
            }
            break;
            
        case NODE_CALL:
            // Function calls benefit from optimization
            hints.enableBranchPrediction = true;
            break;
            
        default:
            break;
    }
    
    return hints;
}

// Get loop-specific optimizations
LoopOptimizationHints getLoopOptimizations(ASTNode* loopNode __attribute__((unused)), VMOptimizationContext* vmCtx) {
    LoopOptimizationHints hints = {0};
    
    if (!vmCtx->optimizeForSpeed) {
        return hints; // No optimizations for fast compilation
    }
    
    hints.enableLoopUnrolling = true;
    hints.enableInvariantHoisting = true;
    hints.optimizeInductionVars = true;
    hints.maxUnrollIterations = vmCtx->enableComputedGoto ? 8 : 4;
    hints.enableVectorization = vmCtx->targetRegisterCount >= 64;
    
    return hints;
}

// Get memory layout optimizations
MemoryLayoutHints getMemoryLayoutOptimizations(ASTNode* node __attribute__((unused)), VMOptimizationContext* vmCtx) {
    MemoryLayoutHints hints = {0};
    
    hints.enableConstantPooling = vmCtx->optimizeForSpeed;
    hints.alignForCache = vmCtx->targetRegisterCount >= 64;
    hints.constantPoolSize = vmCtx->optimizeForSpeed ? 256 : 64;
    hints.localVarFrameSize = vmCtx->targetRegisterCount;
    
    return hints;
}

// Main VM optimization function
void optimizeForRegisterVM(ASTNode* node, VMOptimizationContext* vmCtx, 
                          RegisterState* regState, Compiler* compiler) {
    if (!node || !vmCtx || !regState || !compiler) return;
    
    // Update register pressure
    vmCtx->registerPressure = calculateRegisterPressure(regState);
    
    // Integrate profiling data into optimization decisions
    if (g_profiling.isActive) {
        updateOptimizationHints((struct VMOptimizationContext*)vmCtx);
    }
    
    // Apply register allocation optimizations
    optimizeRegisterUsage(node, regState);
    
    // Get instruction optimizations
    InstructionOptHints instHints __attribute__((unused)) = getInstructionOptimizations(node, vmCtx);
    
    // Apply loop optimizations if this is a loop
    if (node->type == NODE_FOR_RANGE || node->type == NODE_WHILE) {
        LoopOptimizationHints loopHints = getLoopOptimizations(node, vmCtx);
        applyLoopOptimizations(node, &loopHints, compiler);
    }
    
    // Mark hot paths for special optimization
    if (vmCtx->optimizeForSpeed && vmCtx->registerPressure < 0.7f) {
        markHotPath(node, regState);
    }
}

// Optimize register usage for a node
void optimizeRegisterUsage(ASTNode* node, RegisterState* regState) {
    if (!node) return;
    
    // This is a placeholder for register usage optimization
    // In a full implementation, this would analyze the node and optimize
    // register allocation patterns
    
    switch (node->type) {
        case NODE_FOR_RANGE:
        case NODE_WHILE:
            // Loop variables get special treatment
            for (int i = 0; i < VM_REGISTER_COUNT; i++) {
                if (regState->liveRegisters[i] > 0) {
                    regState->isLoopVariable[i] = true;
                }
            }
            break;
        default:
            break;
    }
}

// Apply loop optimizations
void applyLoopOptimizations(ASTNode* loopNode, LoopOptimizationHints* hints, 
                           Compiler* compiler) {
    if (!loopNode || !hints || !compiler) return;
    
    // This is a placeholder for loop optimization implementation
    // In a full implementation, this would apply the specified optimizations
    
    if (hints->enableLoopUnrolling) {
        // Mark this loop for unrolling during code generation
    }
    
    if (hints->enableInvariantHoisting) {
        // Identify and hoist loop-invariant expressions
    }
    
    if (hints->optimizeInductionVars) {
        // Optimize induction variable operations
    }
}

// Mark node as hot path
void markHotPath(ASTNode* node, RegisterState* regState) {
    if (!node) return;
    
    // Integration with profiling system: Check if this code address is hot
    #ifdef VM_PROFILING_H
    if (shouldOptimizeForHotPath((void*)node)) {
        // Hot paths get priority register allocation
        for (int i = 0; i < min(32, VM_REGISTER_COUNT); i++) {
            if (regState->liveRegisters[i] > 0) {
                regState->spillCost[i] = 0; // Don't spill hot path registers
            }
        }
    }
    #else
    // Fallback: Assume all nodes might be hot paths
    for (int i = 0; i < min(32, VM_REGISTER_COUNT); i++) {
        if (regState->liveRegisters[i] > 0) {
            regState->spillCost[i] = 0; // Don't spill hot path registers
        }
    }
    #endif
}

// Optimize hot path specifically
void optimizeHotPath(ASTNode* node, VMOptimizationContext* vmCtx, Compiler* compiler) {
    if (!node || !vmCtx || !compiler) return;
    
    // Hot paths get maximum optimization
    vmCtx->enableRegisterReuse = true;
    vmCtx->optimizeForSpeed = true;
    vmCtx->enableComputedGoto = true;
    vmCtx->targetRegisterCount = min(VM_PREFERRED_WORKING_SET, VM_REGISTER_COUNT);
}

// Debug helpers
void dumpRegisterState(RegisterState* regState) {
    if (!regState) return;
    
    printf("=== Register State Dump ===\n");
    printf("Available registers: %d\n", regState->availableRegisters);
    printf("High water mark: %d\n", regState->highWaterMark);
    
    int liveCount = 0;
    for (int i = 0; i < VM_REGISTER_COUNT; i++) {
        if (regState->liveRegisters[i] > 0) {
            liveCount++;
        }
    }
    printf("Live registers: %d\n", liveCount);
    printf("Register pressure: %.2f\n", calculateRegisterPressure(regState));
}

void dumpVMOptimizationContext(VMOptimizationContext* vmCtx) {
    if (!vmCtx) return;
    
    printf("=== VM Optimization Context ===\n");
    printf("Target register count: %d\n", vmCtx->targetRegisterCount);
    printf("Enable register reuse: %s\n", vmCtx->enableRegisterReuse ? "yes" : "no");
    printf("Optimize for speed: %s\n", vmCtx->optimizeForSpeed ? "yes" : "no");
    printf("Enable computed goto: %s\n", vmCtx->enableComputedGoto ? "yes" : "no");
    printf("Register pressure: %.2f\n", vmCtx->registerPressure);
    printf("Spill threshold: %d\n", vmCtx->spillThreshold);
}

void validateRegisterAllocation(RegisterState* regState) {
    if (!regState) return;
    
    int liveCount = 0;
    for (int i = 0; i < VM_REGISTER_COUNT; i++) {
        if (regState->liveRegisters[i] > 0) {
            liveCount++;
        }
    }
    
    int expectedAvailable = VM_REGISTER_COUNT - liveCount - 4; // 4 reserved
    if (regState->availableRegisters != expectedAvailable) {
        printf("WARNING: Register accounting mismatch. Expected %d available, got %d\n",
               expectedAvailable, regState->availableRegisters);
    }
}