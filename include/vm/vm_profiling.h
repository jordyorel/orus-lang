// Orus Language Project
// ---------------------------------------------------------------------------
// File: include/vm/vm_profiling.h
// Author: Jordy Orel KONDA
// Copyright (c) 2025 Jordy Orel KONDA
// License: MIT License (see LICENSE file in the project root)
// Description: Declares profiling and instrumentation helpers for VM execution.

#ifndef VM_PROFILING_H
#define VM_PROFILING_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

// Profiling configuration flags
typedef enum {
    PROFILE_NONE = 0,
    PROFILE_INSTRUCTIONS = 1 << 0,     // Profile instruction execution counts
    PROFILE_HOT_PATHS = 1 << 1,        // Detect hot paths/loops
    PROFILE_REGISTER_USAGE = 1 << 2,   // Profile register allocation patterns
    PROFILE_MEMORY_ACCESS = 1 << 3,    // Profile memory access patterns
    PROFILE_BRANCH_PREDICTION = 1 << 4, // Profile branch prediction accuracy
    PROFILE_FUNCTION_CALLS = 1 << 5,   // Profile function invocation frequency
    PROFILE_ALL = 0x3F
} ProfilingFlags;

// Hot path detection thresholds
#define HOT_PATH_THRESHOLD 1000        // Executions to consider hot
#define HOT_LOOP_THRESHOLD 10000       // Loop iterations to consider hot
#define PROFILING_SAMPLE_RATE 100      // Sample every N instructions when enabled
#define LOOP_HIT_SAMPLE_RATE 64        // Sample loop hit counts every 64 iterations
#define FUNCTION_HIT_SAMPLE_RATE 32    // Sample function hits every 32 calls

#define LOOP_PROFILE_SLOTS 1024
#define FUNCTION_PROFILE_SLOTS 512

// Instruction profiling data
typedef struct {
    uint64_t executionCount;
    uint64_t totalCycles;
    double averageCycles;
    bool isHotPath;
} InstructionProfile;

// Hot path detection data
typedef struct {
    uint64_t entryCount;
    uint64_t totalIterations;
    double averageIterations;
    uint64_t lastAccessed;
    bool isCurrentlyHot;
} HotPathData;

// Register usage profiling
typedef struct {
    uint64_t allocations;
    uint64_t spills;
    uint64_t reuses;
    double averageLifetime;
} RegisterProfile;

// Loop hit sampling data (hashed by code address)
typedef struct {
    uintptr_t address;
    uint64_t hitCount;
    uint64_t pendingIterations;
    uint64_t lastHitInstruction;
} LoopProfile;

// Function invocation sampling data (hashed by function pointer)
typedef struct {
    uintptr_t address;
    uint64_t hitCount;
    uint64_t pendingCalls;
    uint64_t lastHitInstruction;
    bool isNative;
} FunctionProfile;

// Main profiling context
typedef struct {
    // Configuration
    ProfilingFlags enabledFlags;
    bool isActive;
    uint64_t sampleCounter;
    
    // Timing infrastructure
    struct timespec startTime;
    uint64_t totalInstructions;
    uint64_t totalCycles;
    
    // Instruction profiling (indexed by opcode)
    InstructionProfile instructionStats[256];
    
    // Hot path detection (hash table for code addresses)
    HotPathData hotPaths[1024];
    uint32_t hotPathCount;
    
    // Register profiling
    RegisterProfile registerStats[256];

    // Memory access patterns
    uint64_t memoryReads;
    uint64_t memoryWrites;
    uint64_t cacheHits;
    uint64_t cacheMisses;

    // Branch prediction stats
    uint64_t branchesTotal;
    uint64_t branchesCorrect;
    double branchAccuracy;

    // Loop and function sampling
    uint64_t loopSampleCounter;
    uint64_t functionSampleCounter;
    LoopProfile loopStats[LOOP_PROFILE_SLOTS];
    FunctionProfile functionStats[FUNCTION_PROFILE_SLOTS];
} VMProfilingContext;

// Global profiling instance
extern VMProfilingContext g_profiling;

// Timestamp function for VM dispatch
uint64_t getTimestamp(void);

// Core profiling API
void initVMProfiling(void);
void enableProfiling(ProfilingFlags flags);
void disableProfiling(ProfilingFlags flags);
void resetProfiling(void);
void shutdownVMProfiling(void);

// Runtime profiling hooks (inline for performance)
static inline void profileInstruction(uint8_t opcode, uint64_t cycles) {
    if (!(g_profiling.enabledFlags & PROFILE_INSTRUCTIONS) || !g_profiling.isActive) return;
    
    // Sample-based profiling to reduce overhead
    if (++g_profiling.sampleCounter % PROFILING_SAMPLE_RATE != 0) return;
    
    InstructionProfile* profile = &g_profiling.instructionStats[opcode];
    profile->executionCount++;
    profile->totalCycles += cycles;
    profile->averageCycles = (double)profile->totalCycles / profile->executionCount;
    
    // Mark as hot path if execution count exceeds threshold
    if (profile->executionCount > HOT_PATH_THRESHOLD) {
        profile->isHotPath = true;
    }
}

static inline void profileHotPath(void* codeAddress, uint64_t iterations) {
    if (!(g_profiling.enabledFlags & PROFILE_HOT_PATHS) || !g_profiling.isActive) return;
    
    // Hash the code address to find hot path slot
    uint32_t hash = ((uintptr_t)codeAddress >> 3) % 1024;
    HotPathData* hotPath = &g_profiling.hotPaths[hash];
    
    hotPath->entryCount++;
    hotPath->totalIterations += iterations;
    hotPath->averageIterations = (double)hotPath->totalIterations / hotPath->entryCount;
    hotPath->lastAccessed = g_profiling.totalInstructions;

    // Mark as currently hot once cumulative iterations cross the threshold
    if (hotPath->totalIterations > HOT_LOOP_THRESHOLD) {
        hotPath->isCurrentlyHot = true;
    }
}

static inline uint64_t profileLoopHit(void* codeAddress) {
    if (!(g_profiling.enabledFlags & PROFILE_HOT_PATHS) || !g_profiling.isActive) return 0;

    g_profiling.loopSampleCounter++;

    uint32_t hash = ((uintptr_t)codeAddress >> 3) % LOOP_PROFILE_SLOTS;
    LoopProfile* loopProfile = &g_profiling.loopStats[hash];

    if (loopProfile->address != (uintptr_t)codeAddress) {
        loopProfile->address = (uintptr_t)codeAddress;
        loopProfile->hitCount = 0;
        loopProfile->pendingIterations = 0;
        loopProfile->lastHitInstruction = 0;
    }

    loopProfile->pendingIterations++;

    if (g_profiling.loopSampleCounter % LOOP_HIT_SAMPLE_RATE != 0) {
        return 0;
    }

    uint64_t recordedIterations = loopProfile->pendingIterations;
    loopProfile->pendingIterations = 0;
    loopProfile->hitCount += recordedIterations;
    loopProfile->lastHitInstruction = g_profiling.totalInstructions;
    return recordedIterations;
}

static inline void profileFunctionHit(void* functionPtr, bool isNative) {
    if (!(g_profiling.enabledFlags & PROFILE_FUNCTION_CALLS) || !g_profiling.isActive) return;

    g_profiling.functionSampleCounter++;

    uint32_t hash = ((uintptr_t)functionPtr >> 3) % FUNCTION_PROFILE_SLOTS;
    FunctionProfile* functionProfile = &g_profiling.functionStats[hash];

    if (functionProfile->address != (uintptr_t)functionPtr || functionProfile->isNative != isNative) {
        functionProfile->address = (uintptr_t)functionPtr;
        functionProfile->hitCount = 0;
        functionProfile->pendingCalls = 0;
        functionProfile->lastHitInstruction = 0;
        functionProfile->isNative = isNative;
    }

    functionProfile->pendingCalls++;

    if (g_profiling.functionSampleCounter % FUNCTION_HIT_SAMPLE_RATE != 0) {
        return;
    }

    functionProfile->hitCount += functionProfile->pendingCalls;
    functionProfile->lastHitInstruction = g_profiling.totalInstructions;
    functionProfile->pendingCalls = 0;
}

static inline void profileRegisterAllocation(uint8_t regNum, bool isSpill, bool isReuse) {
    if (!(g_profiling.enabledFlags & PROFILE_REGISTER_USAGE) || !g_profiling.isActive) return;
    
    RegisterProfile* profile = &g_profiling.registerStats[regNum];
    profile->allocations++;
    if (isSpill) profile->spills++;
    if (isReuse) profile->reuses++;
}

static inline void profileMemoryAccess(bool isRead, bool cacheHit) {
    if (!(g_profiling.enabledFlags & PROFILE_MEMORY_ACCESS) || !g_profiling.isActive) return;
    
    if (isRead) {
        g_profiling.memoryReads++;
    } else {
        g_profiling.memoryWrites++;
    }
    
    if (cacheHit) {
        g_profiling.cacheHits++;
    } else {
        g_profiling.cacheMisses++;
    }
}

static inline void profileBranch(bool wasTaken, bool predicted) {
    if (!(g_profiling.enabledFlags & PROFILE_BRANCH_PREDICTION) || !g_profiling.isActive) return;
    
    g_profiling.branchesTotal++;
    if (wasTaken == predicted) {
        g_profiling.branchesCorrect++;
    }
    g_profiling.branchAccuracy = (double)g_profiling.branchesCorrect / g_profiling.branchesTotal;
}

// Query API for hot path detection
bool isHotPath(void* codeAddress);
bool isHotInstruction(uint8_t opcode);
uint64_t getHotPathIterations(void* codeAddress);
double getInstructionHotness(uint8_t opcode);
uint64_t getLoopHitCount(void* codeAddress);
uint64_t getFunctionHitCount(void* functionPtr, bool isNative);

// Profiling data export
void dumpProfilingStats(void);
void exportProfilingData(const char* filename);
void printHotPaths(void);
void printInstructionProfile(void);
void printRegisterProfile(void);
void printLoopProfile(void);
void printFunctionProfile(void);

// Forward declaration to avoid circular dependency
struct VMOptimizationContext;

// Integration with VM optimization
void updateOptimizationHints(struct VMOptimizationContext* vmCtx);
bool shouldOptimizeForHotPath(void* codeAddress);

#endif // VM_PROFILING_H