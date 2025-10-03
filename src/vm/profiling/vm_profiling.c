// Orus Language Project
// ---------------------------------------------------------------------------
// File: src/vm/profiling/vm_profiling.c
// Author: Jordy Orel KONDA
// Copyright (c) 2025 Jordy Orel KONDA
// License: MIT License (see LICENSE file in the project root)
// Description: Implements profiling hooks and metrics collection for the Orus VM.


#define _POSIX_C_SOURCE 199309L
#include "vm/vm_profiling.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Global profiling context
VMProfilingContext g_profiling = {0};

// Get high-resolution timestamp (exported for VM dispatch)
uint64_t getTimestamp(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

void initVMProfiling(void) {
    memset(&g_profiling, 0, sizeof(VMProfilingContext));
    g_profiling.enabledFlags = PROFILE_NONE;
    g_profiling.isActive = false;
    g_profiling.sampleCounter = 0;
    
    clock_gettime(CLOCK_MONOTONIC, &g_profiling.startTime);
    
    // printf("VM Profiling system initialized\n");
}

void enableProfiling(ProfilingFlags flags) {
    g_profiling.enabledFlags |= flags;
    g_profiling.isActive = (g_profiling.enabledFlags != PROFILE_NONE);
    
    if (g_profiling.isActive) {
        clock_gettime(CLOCK_MONOTONIC, &g_profiling.startTime);
        printf("VM Profiling enabled with flags: 0x%X\n", flags);
    }
}

void disableProfiling(ProfilingFlags flags) {
    g_profiling.enabledFlags &= ~flags;
    g_profiling.isActive = (g_profiling.enabledFlags != PROFILE_NONE);
    
    printf("VM Profiling disabled for flags: 0x%X\n", flags);
    if (!g_profiling.isActive) {
        printf("VM Profiling completely disabled\n");
    }
}

void resetProfiling(void) {
    ProfilingFlags savedFlags = g_profiling.enabledFlags;
    bool wasActive = g_profiling.isActive;
    
    memset(&g_profiling, 0, sizeof(VMProfilingContext));
    g_profiling.enabledFlags = savedFlags;
    g_profiling.isActive = wasActive;
    
    if (wasActive) {
        clock_gettime(CLOCK_MONOTONIC, &g_profiling.startTime);
    }
    
    printf("VM Profiling data reset\n");
}

void shutdownVMProfiling(void) {
    if (g_profiling.isActive) {
        printf("\n=== Final Profiling Report ===\n");
        dumpProfilingStats();
    }
    
    memset(&g_profiling, 0, sizeof(VMProfilingContext));
    printf("VM Profiling system shutdown\n");
}

// Query functions
bool isHotPath(void* codeAddress) {
    if (!g_profiling.isActive || !(g_profiling.enabledFlags & PROFILE_HOT_PATHS)) {
        return false;
    }
    
    uint32_t hash = ((uintptr_t)codeAddress >> 3) % 1024;
    return g_profiling.hotPaths[hash].isCurrentlyHot;
}

bool isHotInstruction(uint8_t opcode) {
    if (!g_profiling.isActive || !(g_profiling.enabledFlags & PROFILE_INSTRUCTIONS)) {
        return false;
    }
    
    return g_profiling.instructionStats[opcode].isHotPath;
}

uint64_t getHotPathIterations(void* codeAddress) {
    if (!g_profiling.isActive || !(g_profiling.enabledFlags & PROFILE_HOT_PATHS)) {
        return 0;
    }
    
    uint32_t hash = ((uintptr_t)codeAddress >> 3) % 1024;
    return g_profiling.hotPaths[hash].totalIterations;
}

double getInstructionHotness(uint8_t opcode) {
    if (!g_profiling.isActive || !(g_profiling.enabledFlags & PROFILE_INSTRUCTIONS)) {
        return 0.0;
    }
    
    InstructionProfile* profile = &g_profiling.instructionStats[opcode];
    if (g_profiling.totalInstructions == 0) return 0.0;
    
    return (double)profile->executionCount / g_profiling.totalInstructions;
}

// Profiling data output functions
void dumpProfilingStats(void) {
    if (!g_profiling.isActive) {
        printf("Profiling is not active\n");
        return;
    }
    
    printf("\n=== VM Profiling Statistics ===\n");
    printf("Total Instructions: %llu\n", (unsigned long long)g_profiling.totalInstructions);
    printf("Total Cycles: %llu\n", (unsigned long long)g_profiling.totalCycles);
    
    if (g_profiling.enabledFlags & PROFILE_INSTRUCTIONS) {
        printInstructionProfile();
    }
    
    if (g_profiling.enabledFlags & PROFILE_HOT_PATHS) {
        printHotPaths();
    }
    
    if (g_profiling.enabledFlags & PROFILE_REGISTER_USAGE) {
        printRegisterProfile();
    }
    
    if (g_profiling.enabledFlags & PROFILE_MEMORY_ACCESS) {
        printf("\n--- Memory Access Profile ---\n");
        printf("Memory Reads: %llu\n", (unsigned long long)g_profiling.memoryReads);
        printf("Memory Writes: %llu\n", (unsigned long long)g_profiling.memoryWrites);
        printf("Cache Hits: %llu\n", (unsigned long long)g_profiling.cacheHits);
        printf("Cache Misses: %llu\n", (unsigned long long)g_profiling.cacheMisses);
        
        if (g_profiling.cacheHits + g_profiling.cacheMisses > 0) {
            double hitRate = (double)g_profiling.cacheHits / 
                           (g_profiling.cacheHits + g_profiling.cacheMisses);
            printf("Cache Hit Rate: %.2f%%\n", hitRate * 100.0);
        }
    }
    
    if (g_profiling.enabledFlags & PROFILE_BRANCH_PREDICTION) {
        printf("\n--- Branch Prediction Profile ---\n");
        printf("Total Branches: %llu\n", (unsigned long long)g_profiling.branchesTotal);
        printf("Correct Predictions: %llu\n", (unsigned long long)g_profiling.branchesCorrect);
        printf("Branch Accuracy: %.2f%%\n", g_profiling.branchAccuracy * 100.0);
    }
}

void printInstructionProfile(void) {
    printf("\n--- Instruction Execution Profile ---\n");
    printf("%-20s %12s %12s %8s %8s\n", "Instruction", "Count", "Cycles", "Avg", "Hot");
    printf("--------------------------------------------------------\n");
    
    const char* opcodeNames[256] = {
        [0] = "OP_CONSTANT", [1] = "OP_NIL", [2] = "OP_TRUE", [3] = "OP_FALSE",
        [4] = "OP_NEGATE", [5] = "OP_ADD", [6] = "OP_SUBTRACT", [7] = "OP_MULTIPLY",
        [8] = "OP_DIVIDE", [9] = "OP_NOT", [10] = "OP_EQUAL", [11] = "OP_GREATER",
        [12] = "OP_LESS", [13] = "OP_PRINT", [14] = "OP_POP", [15] = "OP_DEFINE_GLOBAL",
        [16] = "OP_GET_GLOBAL", [17] = "OP_SET_GLOBAL", [18] = "OP_GET_LOCAL",
        [19] = "OP_SET_LOCAL", [20] = "OP_JUMP_IF_FALSE", [21] = "OP_JUMP", [22] = "OP_LOOP",
        [23] = "OP_CALL", [24] = "OP_RETURN", [25] = "OP_HALT"
    };
    
    for (int i = 0; i < 256; i++) {
        InstructionProfile* profile = &g_profiling.instructionStats[i];
        if (profile->executionCount > 0) {
            const char* name = opcodeNames[i] ? opcodeNames[i] : "UNKNOWN";
            printf("%-20s %12llu %12llu %8.1f %8s\n",
                   name,
                   (unsigned long long)profile->executionCount,
                   (unsigned long long)profile->totalCycles,
                   profile->averageCycles,
                   profile->isHotPath ? "YES" : "NO");
        }
    }
}

void printHotPaths(void) {
    printf("\n--- Hot Path Analysis ---\n");
    printf("%-16s %12s %12s %12s %8s\n", "Address", "Entries", "Iterations", "Avg Iter", "Hot");
    printf("------------------------------------------------------------\n");
    
    for (int i = 0; i < 1024; i++) {
        HotPathData* hotPath = &g_profiling.hotPaths[i];
        if (hotPath->entryCount > 0) {
            printf("0x%014lX %12llu %12llu %12.1f %8s\n",
                   (unsigned long)(i * 8), // Approximate address
                   (unsigned long long)hotPath->entryCount,
                   (unsigned long long)hotPath->totalIterations,
                   hotPath->averageIterations,
                   hotPath->isCurrentlyHot ? "YES" : "NO");
        }
    }
}

void printRegisterProfile(void) {
    printf("\n--- Register Usage Profile ---\n");
    printf("%-8s %12s %12s %12s %12s\n", "Reg", "Allocations", "Spills", "Reuses", "Avg Life");
    printf("------------------------------------------------------------\n");
    
    for (int i = 0; i < 256; i++) {
        RegisterProfile* profile = &g_profiling.registerStats[i];
        if (profile->allocations > 0) {
            printf("R%-7d %12llu %12llu %12llu %12.1f\n",
                   i,
                   (unsigned long long)profile->allocations,
                   (unsigned long long)profile->spills,
                   (unsigned long long)profile->reuses,
                   profile->averageLifetime);
        }
    }
}

void exportProfilingData(const char* filename) {
    if (!g_profiling.isActive) {
        printf("Profiling is not active - cannot export data\n");
        return;
    }
    
    FILE* file = fopen(filename, "w");
    if (!file) {
        printf("Failed to open file for profiling export: %s\n", filename);
        return;
    }
    
    // Export in JSON format for easy parsing
    fprintf(file, "{\n");
    fprintf(file, "  \"totalInstructions\": %llu,\n", (unsigned long long)g_profiling.totalInstructions);
    fprintf(file, "  \"totalCycles\": %llu,\n", (unsigned long long)g_profiling.totalCycles);
    fprintf(file, "  \"enabledFlags\": %d,\n", g_profiling.enabledFlags);
    
    // Export instruction statistics
    fprintf(file, "  \"instructions\": [\n");
    bool firstInst = true;
    for (int i = 0; i < 256; i++) {
        InstructionProfile* profile = &g_profiling.instructionStats[i];
        if (profile->executionCount > 0) {
            if (!firstInst) fprintf(file, ",\n");
            fprintf(file, "    {\"opcode\": %d, \"count\": %llu, \"cycles\": %llu, \"isHot\": %s}",
                    i, (unsigned long long)profile->executionCount,
                    (unsigned long long)profile->totalCycles,
                    profile->isHotPath ? "true" : "false");
            firstInst = false;
        }
    }
    fprintf(file, "\n  ],\n");
    
    // Export hot paths
    fprintf(file, "  \"hotPaths\": [\n");
    bool firstPath = true;
    for (int i = 0; i < 1024; i++) {
        HotPathData* hotPath = &g_profiling.hotPaths[i];
        if (hotPath->entryCount > 0) {
            if (!firstPath) fprintf(file, ",\n");
            fprintf(file, "    {\"hash\": %d, \"entries\": %llu, \"iterations\": %llu, \"isHot\": %s}",
                    i, (unsigned long long)hotPath->entryCount,
                    (unsigned long long)hotPath->totalIterations,
                    hotPath->isCurrentlyHot ? "true" : "false");
            firstPath = false;
        }
    }
    fprintf(file, "\n  ]\n");
    
    fprintf(file, "}\n");
    fclose(file);
    
    printf("Profiling data exported to: %s\n", filename);
}

// Integration with VM optimization
void updateOptimizationHints(struct VMOptimizationContext* vmCtx) {
    if (!vmCtx || !g_profiling.isActive) return;
    
    // Enable aggressive optimizations if we detect many hot paths
    uint32_t hotPathCount = 0;
    for (int i = 0; i < 1024; i++) {
        if (g_profiling.hotPaths[i].isCurrentlyHot) {
            hotPathCount++;
        }
    }
    
    // Note: The actual VMOptimizationContext fields would be accessed here
    // For now, we provide a simplified implementation that doesn't 
    // require the full vm_optimization.h header to avoid circular dependencies
    
    printf("Debug: Hot path count detected: %u\n", hotPathCount);
    if (hotPathCount > 10) {
        printf("Debug: Enabling aggressive optimizations\n");
    }
}

bool shouldOptimizeForHotPath(void* codeAddress) {
    return isHotPath(codeAddress) && g_profiling.isActive;
}