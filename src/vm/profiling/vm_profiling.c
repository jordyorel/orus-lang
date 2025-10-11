// Orus Language Project
// ---------------------------------------------------------------------------
// File: src/vm/profiling/vm_profiling.c
// Author: Jordy Orel KONDA
// Copyright (c) 2025 Jordy Orel KONDA
// License: MIT License (see LICENSE file in the project root)
// Description: Implements profiling hooks and metrics collection for the Orus VM.


#define _POSIX_C_SOURCE 199309L
#if !defined(_POSIX_C_SOURCE) || _POSIX_C_SOURCE < 200112L
#undef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif
#include "vm/vm_profiling.h"
#include "vm/vm.h"
#include "vm/vm_tiering.h"
#include "vm/jit_ir.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef FUNCTION_SPECIALIZATION_THRESHOLD
#define FUNCTION_SPECIALIZATION_THRESHOLD 512ULL
#endif

// Global profiling context
VMProfilingContext g_profiling = {0};

extern VM vm;

static const char* function_display_name(Function* function, int index, char* buffer, size_t size) {
    if (function && function->debug_name && function->debug_name[0] != '\0') {
        return function->debug_name;
    }

    if (!buffer || size == 0) {
        return "";
    }

    snprintf(buffer, size, "<fn_%d>", index);
    buffer[size - 1] = '\0';
    return buffer;
}

static void write_json_string(FILE* file, const char* value) {
    fputc('"', file);
    if (!value) {
        fputc('"', file);
        return;
    }

    const unsigned char* cursor = (const unsigned char*)value;
    while (*cursor) {
        unsigned char ch = *cursor++;
        switch (ch) {
            case '\\':
                fputs("\\\\", file);
                break;
            case '"':
                fputs("\\\"", file);
                break;
            case '\b':
                fputs("\\b", file);
                break;
            case '\f':
                fputs("\\f", file);
                break;
            case '\n':
                fputs("\\n", file);
                break;
            case '\r':
                fputs("\\r", file);
                break;
            case '\t':
                fputs("\\t", file);
                break;
            default:
                if (ch < 0x20u || ch == 0x7Fu) {
                    fprintf(file, "\\u%04X", ch);
                } else {
                    fputc((char)ch, file);
                }
                break;
        }
    }

    fputc('"', file);
}

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

uint64_t getLoopHitCount(void* codeAddress) {
    if (!g_profiling.isActive || !(g_profiling.enabledFlags & PROFILE_HOT_PATHS)) {
        return 0;
    }

    uint32_t hash = ((uintptr_t)codeAddress >> 3) % LOOP_PROFILE_SLOTS;
    LoopProfile* loopProfile = &g_profiling.loopStats[hash];
    if (loopProfile->address != (uintptr_t)codeAddress) {
        return 0;
    }

    return loopProfile->hitCount + loopProfile->pendingIterations;
}

uint64_t getFunctionHitCount(void* functionPtr, bool isNative) {
    if (!g_profiling.isActive || !(g_profiling.enabledFlags & PROFILE_FUNCTION_CALLS)) {
        return 0;
    }

    uint32_t hash = ((uintptr_t)functionPtr >> 3) % FUNCTION_PROFILE_SLOTS;
    FunctionProfile* functionProfile = &g_profiling.functionStats[hash];
    if (functionProfile->address != (uintptr_t)functionPtr || functionProfile->isNative != isNative) {
        return 0;
    }

    return functionProfile->hitCount + functionProfile->pendingCalls;
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
        printLoopProfile();
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

    if (g_profiling.enabledFlags & PROFILE_FUNCTION_CALLS) {
        printFunctionProfile();
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

void printLoopProfile(void) {
    printf("\n--- Loop Hit Profile ---\n");
    printf("%-16s %12s %12s %12s\n", "Address", "Hits", "Pending", "LastInstr");
    printf("------------------------------------------------------------\n");

    for (int i = 0; i < LOOP_PROFILE_SLOTS; i++) {
        LoopProfile* loopProfile = &g_profiling.loopStats[i];
        if (loopProfile->hitCount == 0 && loopProfile->pendingIterations == 0) {
            continue;
        }

        printf("0x%014lX %12llu %12llu %12llu\n",
               (unsigned long)loopProfile->address,
               (unsigned long long)loopProfile->hitCount,
               (unsigned long long)loopProfile->pendingIterations,
               (unsigned long long)loopProfile->lastHitInstruction);
    }
}

void printFunctionProfile(void) {
    printf("\n--- Function Call Profile ---\n");
    printf("%-16s %12s %12s %8s %12s\n", "Address", "Hits", "Pending", "Native", "LastInstr");
    printf("---------------------------------------------------------------------\n");

    for (int i = 0; i < FUNCTION_PROFILE_SLOTS; i++) {
        FunctionProfile* functionProfile = &g_profiling.functionStats[i];
        if (functionProfile->hitCount == 0 && functionProfile->pendingCalls == 0) {
            continue;
        }

        printf("0x%014lX %12llu %12llu %8s %12llu\n",
               (unsigned long)functionProfile->address,
               (unsigned long long)functionProfile->hitCount,
               (unsigned long long)functionProfile->pendingCalls,
               functionProfile->isNative ? "YES" : "NO",
               (unsigned long long)functionProfile->lastHitInstruction);
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
    fprintf(file, "\n  ],\n");

    // Export loop hit sampling data
    fprintf(file, "  \"loopHits\": [\n");
    bool firstLoop = true;
    for (int i = 0; i < LOOP_PROFILE_SLOTS; i++) {
        LoopProfile* loopProfile = &g_profiling.loopStats[i];
        if (loopProfile->hitCount > 0 || loopProfile->pendingIterations > 0) {
            if (!firstLoop) fprintf(file, ",\n");
            fprintf(file, "    {\"address\": %llu, \"hits\": %llu, \"pending\": %llu, \"lastInstr\": %llu}",
                    (unsigned long long)loopProfile->address,
                    (unsigned long long)loopProfile->hitCount,
                    (unsigned long long)loopProfile->pendingIterations,
                    (unsigned long long)loopProfile->lastHitInstruction);
            firstLoop = false;
        }
    }
    fprintf(file, "\n  ],\n");

    // Export function hit sampling data
    fprintf(file, "  \"functionHits\": [\n");
    bool firstFunction = true;
    for (int i = 0; i < FUNCTION_PROFILE_SLOTS; i++) {
        FunctionProfile* functionProfile = &g_profiling.functionStats[i];
        if (functionProfile->hitCount > 0 || functionProfile->pendingCalls > 0) {
            if (!firstFunction) fprintf(file, ",\n");
            fprintf(file, "    {\"address\": %llu, \"hits\": %llu, \"pending\": %llu, \"native\": %s, \"lastInstr\": %llu}",
                    (unsigned long long)functionProfile->address,
                    (unsigned long long)functionProfile->hitCount,
                    (unsigned long long)functionProfile->pendingCalls,
                    functionProfile->isNative ? "true" : "false",
                    (unsigned long long)functionProfile->lastHitInstruction);
            firstFunction = false;
        }
    }
    fprintf(file, "\n  ],\n");

    fprintf(file, "  \"specializations\": [\n");
    bool firstSpecialization = true;
    if (vm.functionCount > 0) {
        for (int i = 0; i < vm.functionCount; ++i) {
            Function* function = &vm.functions[i];
            if (!function) {
                continue;
            }

            uint64_t currentHits = getFunctionHitCount(function, false);
            uint64_t recordedHits = function->specialization_hits;
            bool specializedTier = function->tier == FUNCTION_TIER_SPECIALIZED;
            bool active = specializedTier && function->specialized_chunk != NULL;
            bool eligible = currentHits >= FUNCTION_SPECIALIZATION_THRESHOLD;
            char fallback[32];
            const char* name = function_display_name(function, i, fallback, sizeof(fallback));
            const char* tier = specializedTier ? "specialized" : "baseline";

            if (!firstSpecialization) {
                fprintf(file, ",\n");
            }

            fprintf(file, "    {\"index\": %d, \"name\": ", i);
            write_json_string(file, name);
            fprintf(file,
                    ", \"tier\": \"%s\", \"currentHits\": %llu, \"specializationHits\": %llu, \"threshold\": %llu, \"eligible\": %s, \"active\": %s}",
                    tier,
                    (unsigned long long)currentHits,
                    (unsigned long long)recordedHits,
                    (unsigned long long)FUNCTION_SPECIALIZATION_THRESHOLD,
                    eligible ? "true" : "false",
                    active ? "true" : "false");

            firstSpecialization = false;
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

static void
vm_jit_enter_entry(VMState* vm_state, const JITEntry* entry) {
    if (!vm_state || !entry || !entry->entry_point) {
        return;
    }

    const JITBackendVTable* vtable = orus_jit_backend_vtable();
    if (!vtable || !vtable->enter) {
        return;
    }

    vtable->enter((struct VM*)vm_state, entry);
    vm_state->jit_invocation_count++;

    if (vm_state->jit_pending_invalidate) {
        vm_jit_invalidate_entry(&vm_state->jit_pending_trigger);
        vm_state->jit_pending_invalidate = false;
        memset(&vm_state->jit_pending_trigger, 0,
               sizeof(vm_state->jit_pending_trigger));
    }
}

static uint16_t
read_be_u16(const uint8_t* bytes) {
    return (uint16_t)((bytes[0] << 8) | bytes[1]);
}

static bool
map_const_opcode(uint8_t opcode,
                 OrusJitIROpcode* ir_opcode,
                 OrusJitValueKind* kind) {
    switch (opcode) {
        case OP_LOAD_I32_CONST:
            *ir_opcode = ORUS_JIT_IR_OP_LOAD_I32_CONST;
            *kind = ORUS_JIT_VALUE_I32;
            return true;
        case OP_LOAD_I64_CONST:
            *ir_opcode = ORUS_JIT_IR_OP_LOAD_I64_CONST;
            *kind = ORUS_JIT_VALUE_I64;
            return true;
        case OP_LOAD_F64_CONST:
            *ir_opcode = ORUS_JIT_IR_OP_LOAD_F64_CONST;
            *kind = ORUS_JIT_VALUE_F64;
            return true;
        default:
            break;
    }
    return false;
}

static bool
map_move_opcode(uint8_t opcode,
                OrusJitIROpcode* ir_opcode,
                OrusJitValueKind* kind) {
    switch (opcode) {
        case OP_MOVE_I32:
            *ir_opcode = ORUS_JIT_IR_OP_MOVE_I32;
            *kind = ORUS_JIT_VALUE_I32;
            return true;
        case OP_MOVE_I64:
            *ir_opcode = ORUS_JIT_IR_OP_MOVE_I64;
            *kind = ORUS_JIT_VALUE_I64;
            return true;
        case OP_MOVE_F64:
            *ir_opcode = ORUS_JIT_IR_OP_MOVE_F64;
            *kind = ORUS_JIT_VALUE_F64;
            return true;
        default:
            break;
    }
    return false;
}

static bool
map_arithmetic_opcode(uint8_t opcode,
                      OrusJitIROpcode* ir_opcode,
                      OrusJitValueKind* kind) {
    switch (opcode) {
        case OP_ADD_I32_TYPED:
            *kind = ORUS_JIT_VALUE_I32;
            *ir_opcode = ORUS_JIT_IR_OP_ADD_I32;
            return true;
        case OP_SUB_I32_TYPED:
            *kind = ORUS_JIT_VALUE_I32;
            *ir_opcode = ORUS_JIT_IR_OP_SUB_I32;
            return true;
        case OP_MUL_I32_TYPED:
            *kind = ORUS_JIT_VALUE_I32;
            *ir_opcode = ORUS_JIT_IR_OP_MUL_I32;
            return true;
        case OP_DIV_I32_TYPED:
            *kind = ORUS_JIT_VALUE_I32;
            *ir_opcode = ORUS_JIT_IR_OP_DIV_I32;
            return true;
        case OP_MOD_I32_TYPED:
            *kind = ORUS_JIT_VALUE_I32;
            *ir_opcode = ORUS_JIT_IR_OP_MOD_I32;
            return true;
        case OP_ADD_I64_TYPED:
            *kind = ORUS_JIT_VALUE_I64;
            *ir_opcode = ORUS_JIT_IR_OP_ADD_I64;
            return true;
        case OP_SUB_I64_TYPED:
            *kind = ORUS_JIT_VALUE_I64;
            *ir_opcode = ORUS_JIT_IR_OP_SUB_I64;
            return true;
        case OP_MUL_I64_TYPED:
            *kind = ORUS_JIT_VALUE_I64;
            *ir_opcode = ORUS_JIT_IR_OP_MUL_I64;
            return true;
        case OP_DIV_I64_TYPED:
            *kind = ORUS_JIT_VALUE_I64;
            *ir_opcode = ORUS_JIT_IR_OP_DIV_I64;
            return true;
        case OP_MOD_I64_TYPED:
            *kind = ORUS_JIT_VALUE_I64;
            *ir_opcode = ORUS_JIT_IR_OP_MOD_I64;
            return true;
        case OP_ADD_U32_TYPED:
            *kind = ORUS_JIT_VALUE_U32;
            *ir_opcode = ORUS_JIT_IR_OP_ADD_U32;
            return true;
        case OP_SUB_U32_TYPED:
            *kind = ORUS_JIT_VALUE_U32;
            *ir_opcode = ORUS_JIT_IR_OP_SUB_U32;
            return true;
        case OP_MUL_U32_TYPED:
            *kind = ORUS_JIT_VALUE_U32;
            *ir_opcode = ORUS_JIT_IR_OP_MUL_U32;
            return true;
        case OP_DIV_U32_TYPED:
            *kind = ORUS_JIT_VALUE_U32;
            *ir_opcode = ORUS_JIT_IR_OP_DIV_U32;
            return true;
        case OP_MOD_U32_TYPED:
            *kind = ORUS_JIT_VALUE_U32;
            *ir_opcode = ORUS_JIT_IR_OP_MOD_U32;
            return true;
        case OP_ADD_U64_TYPED:
            *kind = ORUS_JIT_VALUE_U64;
            *ir_opcode = ORUS_JIT_IR_OP_ADD_U64;
            return true;
        case OP_SUB_U64_TYPED:
            *kind = ORUS_JIT_VALUE_U64;
            *ir_opcode = ORUS_JIT_IR_OP_SUB_U64;
            return true;
        case OP_MUL_U64_TYPED:
            *kind = ORUS_JIT_VALUE_U64;
            *ir_opcode = ORUS_JIT_IR_OP_MUL_U64;
            return true;
        case OP_DIV_U64_TYPED:
            *kind = ORUS_JIT_VALUE_U64;
            *ir_opcode = ORUS_JIT_IR_OP_DIV_U64;
            return true;
        case OP_MOD_U64_TYPED:
            *kind = ORUS_JIT_VALUE_U64;
            *ir_opcode = ORUS_JIT_IR_OP_MOD_U64;
            return true;
        case OP_ADD_F64_TYPED:
            *kind = ORUS_JIT_VALUE_F64;
            *ir_opcode = ORUS_JIT_IR_OP_ADD_F64;
            return true;
        case OP_SUB_F64_TYPED:
            *kind = ORUS_JIT_VALUE_F64;
            *ir_opcode = ORUS_JIT_IR_OP_SUB_F64;
            return true;
        case OP_MUL_F64_TYPED:
            *kind = ORUS_JIT_VALUE_F64;
            *ir_opcode = ORUS_JIT_IR_OP_MUL_F64;
            return true;
        case OP_DIV_F64_TYPED:
            *kind = ORUS_JIT_VALUE_F64;
            *ir_opcode = ORUS_JIT_IR_OP_DIV_F64;
            return true;
        case OP_MOD_F64_TYPED:
            *kind = ORUS_JIT_VALUE_F64;
            *ir_opcode = ORUS_JIT_IR_OP_MOD_F64;
            return true;
        default:
            break;
    }
    return false;
}

static bool
orus_jit_translate_linear_block(VMState* vm_state,
                                Function* function,
                                const HotPathSample* sample,
                                OrusJitIRProgram* program,
                                bool* out_unsupported) {
    if (!function || !program || !sample) {
        return false;
    }
    if (!function->chunk || !function->chunk->code || function->chunk->count <= 0) {
        return false;
    }

    const Chunk* chunk = function->chunk;
    size_t start_offset = (size_t)function->start;
    if ((size_t)sample->loop < (size_t)chunk->count) {
        start_offset = (size_t)sample->loop;
    }
    if (start_offset >= (size_t)chunk->count) {
        return false;
    }

    if (out_unsupported) {
        *out_unsupported = false;
    }

    program->source_chunk = (const struct Chunk*)chunk;
    program->function_index = sample->func;
    program->loop_index = sample->loop;
    program->loop_start_offset = (uint32_t)start_offset;

    size_t offset = start_offset;
    bool saw_terminal = false;
    size_t instructions_since_safepoint = 0u;
    const size_t safepoint_interval = 12u;

#define INSERT_SAFEPOINT(byte_offset)                                                   \
    do {                                                                               \
        OrusJitIRInstruction* safepoint__ = orus_jit_ir_program_append(program);       \
        if (!safepoint__) {                                                            \
            return false;                                                              \
        }                                                                              \
        safepoint__->opcode = ORUS_JIT_IR_OP_SAFEPOINT;                                \
        safepoint__->bytecode_offset = (byte_offset);                                  \
        instructions_since_safepoint = 0u;                                             \
    } while (0)

    while (offset < (size_t)chunk->count) {
        GC_SAFEPOINT(vm_state);
        uint8_t opcode = chunk->code[offset];
        switch (opcode) {
            case OP_RETURN_VOID: {
                OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
                if (!inst) {
                    return false;
                }
                inst->opcode = ORUS_JIT_IR_OP_RETURN;
                inst->bytecode_offset = (uint32_t)offset;
                saw_terminal = true;
                offset += 1u;
                goto translation_done;
            }
            case OP_RETURN_R: {
                if (offset + 1u >= (size_t)chunk->count) {
                    return false;
                }
                OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
                if (!inst) {
                    return false;
                }
                inst->opcode = ORUS_JIT_IR_OP_RETURN;
                inst->bytecode_offset = (uint32_t)offset;
                saw_terminal = true;
                offset += 2u;
                goto translation_done;
            }
            case OP_LOOP_SHORT: {
                if (offset + 1u >= (size_t)chunk->count) {
                    return false;
                }
                uint8_t back = chunk->code[offset + 1u];
                size_t target = (offset >= (size_t)back) ? (offset - (size_t)back) : 0u;
                if (target != start_offset) {
                    if (out_unsupported) {
                        *out_unsupported = true;
                    }
                    return false;
                }
                if (instructions_since_safepoint > 0u) {
                    INSERT_SAFEPOINT((uint32_t)offset);
                }
                OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
                if (!inst) {
                    return false;
                }
                inst->opcode = ORUS_JIT_IR_OP_LOOP_BACK;
                inst->bytecode_offset = (uint32_t)offset;
                inst->operands.loop_back.back_offset = back;
                saw_terminal = true;
                offset += 2u;
                goto translation_done;
            }
            default:
                break;
        }

        OrusJitValueKind kind;
        OrusJitIROpcode ir_opcode;
        if (map_const_opcode(opcode, &ir_opcode, &kind)) {
            if (kind != ORUS_JIT_VALUE_I32) {
                if (out_unsupported) {
                    *out_unsupported = true;
                }
                return false;
            }
            if (offset + 3u >= (size_t)chunk->count) {
                return false;
            }
            uint16_t dst = chunk->code[offset + 1u];
            uint16_t constant_index = read_be_u16(&chunk->code[offset + 2u]);
            OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
            if (!inst) {
                return false;
            }
            inst->opcode = ir_opcode;
            inst->value_kind = kind;
            inst->bytecode_offset = (uint32_t)offset;
            inst->operands.load_const.dst_reg = dst;
            inst->operands.load_const.constant_index = constant_index;
            if (constant_index >= (uint16_t)chunk->constants.count) {
                return false;
            }
            Value constant = chunk->constants.values[constant_index];
            if (!IS_I32(constant)) {
                if (out_unsupported) {
                    *out_unsupported = true;
                }
                return false;
            }
            inst->operands.load_const.immediate_bits =
                (uint64_t)(uint32_t)AS_I32(constant);
            offset += 4u;
            if (++instructions_since_safepoint >= safepoint_interval) {
                INSERT_SAFEPOINT((uint32_t)offset);
            }
            continue;
        }

        if (map_move_opcode(opcode, &ir_opcode, &kind)) {
            if (kind != ORUS_JIT_VALUE_I32) {
                if (out_unsupported) {
                    *out_unsupported = true;
                }
                return false;
            }
            if (offset + 2u >= (size_t)chunk->count) {
                return false;
            }
            uint16_t dst = chunk->code[offset + 1u];
            uint16_t src = chunk->code[offset + 2u];
            OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
            if (!inst) {
                return false;
            }
            inst->opcode = ir_opcode;
            inst->value_kind = kind;
            inst->bytecode_offset = (uint32_t)offset;
            inst->operands.move.dst_reg = dst;
            inst->operands.move.src_reg = src;
            offset += 3u;
            if (++instructions_since_safepoint >= safepoint_interval) {
                INSERT_SAFEPOINT((uint32_t)offset);
            }
            continue;
        }

        if (map_arithmetic_opcode(opcode, &ir_opcode, &kind)) {
            if (kind != ORUS_JIT_VALUE_I32) {
                if (out_unsupported) {
                    *out_unsupported = true;
                }
                return false;
            }
            if (ir_opcode != ORUS_JIT_IR_OP_ADD_I32 &&
                ir_opcode != ORUS_JIT_IR_OP_SUB_I32 &&
                ir_opcode != ORUS_JIT_IR_OP_MUL_I32) {
                if (out_unsupported) {
                    *out_unsupported = true;
                }
                return false;
            }
            if (offset + 3u >= (size_t)chunk->count) {
                return false;
            }
            uint16_t dst = chunk->code[offset + 1u];
            uint16_t lhs = chunk->code[offset + 2u];
            uint16_t rhs = chunk->code[offset + 3u];
            OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
            if (!inst) {
                return false;
            }
            inst->opcode = ir_opcode;
            inst->value_kind = kind;
            inst->bytecode_offset = (uint32_t)offset;
            inst->operands.arithmetic.dst_reg = dst;
            inst->operands.arithmetic.lhs_reg = lhs;
            inst->operands.arithmetic.rhs_reg = rhs;
            offset += 4u;
            if (++instructions_since_safepoint >= safepoint_interval) {
                INSERT_SAFEPOINT((uint32_t)offset);
            }
            continue;
        }

        // Unsupported opcode - abort translation.
        if (out_unsupported) {
            *out_unsupported = true;
        }
        return false;
    }

translation_done:
    program->loop_end_offset = (uint32_t)offset;
#undef INSERT_SAFEPOINT
    if (!saw_terminal) {
        OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
        if (!inst) {
            return false;
        }
        inst->opcode = ORUS_JIT_IR_OP_RETURN;
        inst->bytecode_offset = (uint32_t)offset;
        saw_terminal = true;
    }

    return program->count > 0 && saw_terminal;
}

void queue_tier_up(VMState* vm_state, const HotPathSample* sample) {
    if (!vm_state || !sample) {
        return;
    }

    if (!vm_state->jit_enabled || !vm_state->jit_backend) {
        return;
    }

    if (sample->loop >= VM_MAX_PROFILED_LOOPS) {
        return;
    }

    if (vm_state->jit_loop_blocklist[sample->loop]) {
        return;
    }

    if (sample->func == UINT16_MAX || sample->func >= (FunctionId)vm_state->functionCount) {
        return;
    }

    HotPathSample* record = &vm_state->profile[sample->loop];
    record->hit_count = 0;

    Function* function = &vm_state->functions[sample->func];

    JITEntry* cached = vm_jit_lookup_entry(sample->func, sample->loop);
    if (cached && cached->entry_point) {
        vm_state->jit_cache_hit_count++;
        vm_jit_enter_entry(vm_state, cached);
        return;
    }

    vm_state->jit_cache_miss_count++;

    OrusJitIRProgram program;
    orus_jit_ir_program_init(&program);

    bool translated = false;
    bool unsupported = false;
    bool owns_program_buffer = false;
    if (function) {
        translated = orus_jit_translate_linear_block(vm_state, function, sample,
                                                     &program, &unsupported);
    }

    if (!translated) {
        vm_state->jit_translation_failure_count++;
        vm_state->jit_loop_blocklist[sample->loop] = true;
        if (unsupported) {
            JITDeoptTrigger trigger = {
                .function_index = sample->func,
                .loop_index = sample->loop,
                .generation = 0,
            };
            vm_jit_invalidate_entry(&trigger);
        }
        OrusJitIRInstruction fallback[] = {
            {.opcode = ORUS_JIT_IR_OP_RETURN},
        };
        if (!orus_jit_ir_program_reserve(&program, sizeof(fallback) / sizeof(fallback[0]))) {
            return;
        }
        memcpy(program.instructions, fallback, sizeof(fallback));
        program.count = sizeof(fallback) / sizeof(fallback[0]);
        program.source_chunk = function ? (const struct Chunk*)function->chunk : NULL;
        program.function_index = sample->func;
        program.loop_index = sample->loop;
        program.loop_start_offset = 0;
        program.loop_end_offset = 0;
        owns_program_buffer = true;
    } else {
        vm_state->jit_translation_success_count++;
        owns_program_buffer = true;
    }

    JITEntry entry;
    memset(&entry, 0, sizeof(entry));

    JITBackendStatus status =
        orus_jit_backend_compile_ir(vm_state->jit_backend, &program, &entry);
    if (owns_program_buffer) {
        orus_jit_ir_program_reset(&program);
    }
    if (status != JIT_BACKEND_OK) {
        vm_jit_enter_entry(vm_state, &vm_state->jit_entry_stub);
        return;
    }

    uint64_t generation =
        vm_jit_install_entry(sample->func, sample->loop, &entry);
    if (generation == 0) {
        vm_jit_enter_entry(vm_state, &vm_state->jit_entry_stub);
        return;
    }

    vm_state->jit_compilation_count++;

    cached = vm_jit_lookup_entry(sample->func, sample->loop);
    if (cached && cached->entry_point) {
        vm_jit_enter_entry(vm_state, cached);
        return;
    }

    vm_jit_enter_entry(vm_state, &vm_state->jit_entry_stub);
}
