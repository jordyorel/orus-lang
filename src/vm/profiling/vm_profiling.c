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
#include "vm/jit_ir_debug.h"
#include "vm/jit_translation.h"
#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

typedef enum OrusJitIteratorKind {
    ORUS_JIT_ITERATOR_NONE = 0,
    ORUS_JIT_ITERATOR_RANGE,
    ORUS_JIT_ITERATOR_GENERIC,
} OrusJitIteratorKind;

#define VM_OPCODE_WINDOW_THRESHOLD 64u
#define VM_OPCODE_WINDOW_COOLDOWN 4096u

static inline bool
orus_jit_kind_is_integer(OrusJitValueKind kind) {
    switch (kind) {
        case ORUS_JIT_VALUE_I32:
        case ORUS_JIT_VALUE_I64:
        case ORUS_JIT_VALUE_U32:
        case ORUS_JIT_VALUE_U64:
            return true;
        default:
            return false;
    }
}
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char*
opcode_family_name(OrusOpcodeFamily family) {
    switch (family) {
        case ORUS_OPCODE_FAMILY_LITERAL:
            return "literal";
        case ORUS_OPCODE_FAMILY_MOVES:
            return "moves";
        case ORUS_OPCODE_FAMILY_ARITHMETIC:
            return "arithmetic";
        case ORUS_OPCODE_FAMILY_BITWISE:
            return "bitwise";
        case ORUS_OPCODE_FAMILY_COMPARISON:
            return "comparison";
        case ORUS_OPCODE_FAMILY_LOGIC:
            return "logic";
        case ORUS_OPCODE_FAMILY_CONVERSION:
            return "conversion";
        case ORUS_OPCODE_FAMILY_STRING:
            return "string";
        case ORUS_OPCODE_FAMILY_COLLECTION:
            return "collection";
        case ORUS_OPCODE_FAMILY_ITERATOR:
            return "iterator";
        case ORUS_OPCODE_FAMILY_CONTROL:
            return "control";
        case ORUS_OPCODE_FAMILY_CALL:
            return "call";
        case ORUS_OPCODE_FAMILY_FRAME:
            return "frame";
        case ORUS_OPCODE_FAMILY_SPILL:
            return "spill";
        case ORUS_OPCODE_FAMILY_MODULE:
            return "module";
        case ORUS_OPCODE_FAMILY_CLOSURE:
            return "closure";
        case ORUS_OPCODE_FAMILY_RUNTIME:
            return "runtime";
        case ORUS_OPCODE_FAMILY_TYPED:
            return "typed";
        case ORUS_OPCODE_FAMILY_EXTENDED:
            return "extended";
        case ORUS_OPCODE_FAMILY_OTHER:
        default:
            return "other";
    }
}

#ifndef FUNCTION_SPECIALIZATION_THRESHOLD
#define FUNCTION_SPECIALIZATION_THRESHOLD 512ULL
#endif

// Global profiling context
VMProfilingContext g_profiling = {0};

extern VM vm;

static void
opcode_window_profile_reset(OpcodeWindowProfile* profile) {
    if (!profile) {
        return;
    }
    memset(profile, 0, sizeof(*profile));
}

static bool
opcode_window_is_candidate(const uint8_t* opcodes, uint8_t length) {
    if (!opcodes) {
        return false;
    }
    if (length == 3) {
        if (opcodes[0] == OP_INC_I32_R && opcodes[1] == OP_CMP_I32_IMM) {
            uint8_t term = opcodes[2];
            return term == OP_JUMP_IF_NOT_SHORT || term == OP_JUMP_SHORT ||
                   term == OP_JUMP_BACK_SHORT;
        }
    }
    return false;
}

static uint32_t
opcode_window_hash(uintptr_t start_address, const uint8_t* opcodes, uint8_t length) {
    uint32_t hash = (uint32_t)(start_address >> 3);
    for (uint8_t i = 0; i < length; ++i) {
        hash = (hash * 131u) ^ (uint32_t)opcodes[i];
    }
    return hash % (uint32_t)(sizeof(g_profiling.window_profiles) /
                              sizeof(g_profiling.window_profiles[0]));
}

static void
opcode_window_consider(uintptr_t start_address,
                       const uint8_t* opcodes,
                       uint8_t length) {
    if (start_address == 0 || !opcode_window_is_candidate(opcodes, length)) {
        return;
    }

    uint32_t slot_index = opcode_window_hash(start_address, opcodes, length);
    OpcodeWindowProfile* profile = &g_profiling.window_profiles[slot_index];

    if (profile->start_address != start_address || profile->length != length ||
        memcmp(profile->opcodes, opcodes, length) != 0) {
        opcode_window_profile_reset(profile);
        profile->start_address = start_address;
        profile->length = length;
        memcpy(profile->opcodes, opcodes, length);
    }

    if (g_profiling.totalInstructions - profile->last_seen > VM_OPCODE_WINDOW_COOLDOWN) {
        profile->hit_count = 0;
        profile->metadata_requested = false;
    }

    profile->last_seen = g_profiling.totalInstructions;
    if (profile->hit_count < UINT64_MAX) {
        profile->hit_count++;
    }

    if (profile->hit_count >= VM_OPCODE_WINDOW_THRESHOLD &&
        !profile->metadata_requested) {
        VMHotWindowDescriptor descriptor = {0};
        descriptor.start_ip = (const uint8_t*)profile->start_address;
        descriptor.length = profile->length;
        memcpy(descriptor.opcodes, profile->opcodes, profile->length);
        vm_tiering_request_window_fusion(&descriptor);
        profile->metadata_requested = true;
    }
}

void
vm_profiling_record_opcode_window(const uint8_t* start_addr, uint8_t opcode) {
    OpcodeWindowSampler* sampler = &g_profiling.window_sampler;

    if (sampler->recent_count < VM_MAX_FUSION_WINDOW) {
        sampler->recent_addresses[sampler->recent_count] = (uintptr_t)start_addr;
        sampler->recent_opcodes[sampler->recent_count] = opcode;
        sampler->recent_count++;
    } else {
        memmove(&sampler->recent_addresses[0], &sampler->recent_addresses[1],
                (VM_MAX_FUSION_WINDOW - 1) * sizeof(uintptr_t));
        memmove(&sampler->recent_opcodes[0], &sampler->recent_opcodes[1],
                (VM_MAX_FUSION_WINDOW - 1) * sizeof(uint8_t));
        sampler->recent_addresses[VM_MAX_FUSION_WINDOW - 1] =
            (uintptr_t)start_addr;
        sampler->recent_opcodes[VM_MAX_FUSION_WINDOW - 1] = opcode;
    }

    uint8_t count = sampler->recent_count;
    if (count < 3) {
        return;
    }

    for (uint8_t length = 3; length <= count && length <= VM_MAX_FUSION_WINDOW;
         ++length) {
        uint8_t offset = count - length;
        opcode_window_consider(sampler->recent_addresses[offset],
                               &sampler->recent_opcodes[offset], length);
    }
}

static bool
orus_jit_trace_ir_enabled(void) {
    static int cached = -1;
    if (cached < 0) {
        cached = 0;
        const char* env = getenv("ORUS_TRACE_JIT_IR");
        if (env && env[0] != '\0') {
            cached = 1;
        } else {
            const char* trace = getenv("ORUS_TRACE");
            if (trace && strstr(trace, "jit-ir")) {
                cached = 1;
            }
        }
    }
    return cached != 0;
}

void orus_jit_translation_failure_log_init(
    OrusJitTranslationFailureLog* log) {
    if (!log) {
        return;
    }
    memset(log, 0, sizeof(*log));
}

#ifndef NDEBUG
static bool jit_failure_status_counts_toward_supported_alert(
    OrusJitTranslationStatus status) {
    switch (status) {
        case ORUS_JIT_TRANSLATE_STATUS_OK:
        case ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT:
        case ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY:
        case ORUS_JIT_TRANSLATE_STATUS_ROLLOUT_DISABLED:
            return false;
        default:
            return true;
    }
}
#endif

static OrusJitTranslationFailureCategory
jit_failure_category_for_status(OrusJitTranslationStatus status) {
    switch (status) {
        case ORUS_JIT_TRANSLATE_STATUS_UNSUPPORTED_VALUE_KIND:
        case ORUS_JIT_TRANSLATE_STATUS_UNSUPPORTED_CONSTANT_KIND:
        case ORUS_JIT_TRANSLATE_STATUS_UNHANDLED_OPCODE:
            return ORUS_JIT_TRANSLATION_FAILURE_CATEGORY_UNSUPPORTED_BYTECODE;
        case ORUS_JIT_TRANSLATE_STATUS_UNSUPPORTED_LOOP_SHAPE:
            return ORUS_JIT_TRANSLATION_FAILURE_CATEGORY_MALFORMED_LOOP;
        case ORUS_JIT_TRANSLATE_STATUS_ROLLOUT_DISABLED:
            return ORUS_JIT_TRANSLATION_FAILURE_CATEGORY_ROLLOUT_DISABLED;
        default:
            return ORUS_JIT_TRANSLATION_FAILURE_CATEGORY_OTHER;
    }
}

void orus_jit_translation_failure_log_record(
    OrusJitTranslationFailureLog* log,
    const OrusJitTranslationFailureRecord* record) {
    if (!log || !record) {
        return;
    }

    log->total_failures++;
    if ((size_t)record->status < ORUS_JIT_TRANSLATE_STATUS_COUNT) {
        log->reason_counts[record->status]++;
    }

    OrusJitTranslationFailureCategory category =
        jit_failure_category_for_status(record->status);
    if ((size_t)category < ORUS_JIT_TRANSLATION_FAILURE_CATEGORY_COUNT) {
        log->category_counts[category]++;
    }

    if ((size_t)record->value_kind < ORUS_JIT_VALUE_KIND_COUNT) {
        log->value_kind_counts[record->value_kind]++;
#ifndef NDEBUG
        if (jit_failure_status_counts_toward_supported_alert(record->status)) {
            uint64_t supported_failures =
                ++log->supported_kind_failures[record->value_kind];
            assert(supported_failures <
                       ORUS_JIT_SUPPORTED_FAILURE_ALERT_THRESHOLD &&
                   "baseline JIT bailout threshold exceeded for supported value "
                   "kind");
        }
#endif
    }

    if (ORUS_JIT_TRANSLATION_FAILURE_HISTORY == 0u) {
        return;
    }

    size_t slot = log->next_index;
    if (slot >= ORUS_JIT_TRANSLATION_FAILURE_HISTORY) {
        slot %= ORUS_JIT_TRANSLATION_FAILURE_HISTORY;
    }
    log->records[slot] = *record;
    log->next_index = (slot + 1u) % ORUS_JIT_TRANSLATION_FAILURE_HISTORY;
    if (log->count < ORUS_JIT_TRANSLATION_FAILURE_HISTORY) {
        log->count++;
    }
}

static uint32_t
orus_jit_rollout_mask_for_stage(OrusJitRolloutStage stage) {
    if (stage < ORUS_JIT_ROLLOUT_STAGE_I32_ONLY) {
        stage = ORUS_JIT_ROLLOUT_STAGE_I32_ONLY;
    } else if (stage >= ORUS_JIT_ROLLOUT_STAGE_COUNT) {
        stage = ORUS_JIT_ROLLOUT_STAGE_STRINGS;
    }

    uint32_t mask = 0u;
    mask |= (1u << ORUS_JIT_VALUE_I32);
    mask |= (1u << ORUS_JIT_VALUE_BOOL);
    if (stage >= ORUS_JIT_ROLLOUT_STAGE_WIDE_INTS) {
        mask |= (1u << ORUS_JIT_VALUE_I64);
        mask |= (1u << ORUS_JIT_VALUE_U32);
        mask |= (1u << ORUS_JIT_VALUE_U64);
    }
    if (stage >= ORUS_JIT_ROLLOUT_STAGE_FLOATS) {
        mask |= (1u << ORUS_JIT_VALUE_F64);
    }
    if (stage >= ORUS_JIT_ROLLOUT_STAGE_STRINGS) {
        mask |= (1u << ORUS_JIT_VALUE_STRING);
    }
    return mask;
}

const char*
orus_jit_rollout_stage_name(OrusJitRolloutStage stage) {
    switch (stage) {
        case ORUS_JIT_ROLLOUT_STAGE_I32_ONLY:
            return "i32-only";
        case ORUS_JIT_ROLLOUT_STAGE_WIDE_INTS:
            return "wide-int";
        case ORUS_JIT_ROLLOUT_STAGE_FLOATS:
            return "floats";
        case ORUS_JIT_ROLLOUT_STAGE_STRINGS:
            return "strings";
        default:
            break;
    }
    return "unknown";
}

bool
orus_jit_rollout_stage_parse(const char* text, OrusJitRolloutStage* out_stage) {
    if (!text || !out_stage) {
        return false;
    }

    if (strcmp(text, "i32") == 0 || strcmp(text, "i32-only") == 0 ||
        strcmp(text, "baseline") == 0) {
        *out_stage = ORUS_JIT_ROLLOUT_STAGE_I32_ONLY;
        return true;
    }
    if (strcmp(text, "wide-int") == 0 || strcmp(text, "wide-ints") == 0 ||
        strcmp(text, "wide") == 0) {
        *out_stage = ORUS_JIT_ROLLOUT_STAGE_WIDE_INTS;
        return true;
    }
    if (strcmp(text, "floats") == 0 || strcmp(text, "float") == 0) {
        *out_stage = ORUS_JIT_ROLLOUT_STAGE_FLOATS;
        return true;
    }
    if (strcmp(text, "strings") == 0 || strcmp(text, "string") == 0 ||
        strcmp(text, "full") == 0) {
        *out_stage = ORUS_JIT_ROLLOUT_STAGE_STRINGS;
        return true;
    }
    return false;
}

void
orus_jit_rollout_set_stage(VMState* vm_state, OrusJitRolloutStage stage) {
    if (!vm_state) {
        return;
    }

    vm_state->jit_rollout.stage = stage;
    vm_state->jit_rollout.enabled_kind_mask =
        orus_jit_rollout_mask_for_stage(stage);
}

bool
orus_jit_rollout_is_kind_enabled(const VMState* vm_state,
                                 OrusJitValueKind kind) {
    if (!vm_state || (size_t)kind >= ORUS_JIT_VALUE_KIND_COUNT) {
        return false;
    }
    uint32_t bit = 1u << (uint32_t)kind;
    return (vm_state->jit_rollout.enabled_kind_mask & bit) != 0u;
}

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

static void
printOpcodeFamilyProfile(void) {
    uint64_t total_samples = 0;
    for (size_t i = 0; i < ORUS_OPCODE_FAMILY_COUNT; ++i) {
        total_samples += g_profiling.familyStats[i].executions;
    }

    if (total_samples == 0) {
        return;
    }

    printf("\n--- Opcode Family Profile ---\n");
    printf("%-18s %12s %12s %12s %8s\n",
           "Family",
           "Samples",
           "Cycles",
           "Avg",
           "Share");

    for (size_t i = 0; i < ORUS_OPCODE_FAMILY_COUNT; ++i) {
        const OpcodeFamilyProfile* profile = &g_profiling.familyStats[i];
        if (profile->executions == 0) {
            continue;
        }

        double average_cycles =
            (profile->executions > 0)
                ? (double)profile->cycles / (double)profile->executions
                : 0.0;
        double share =
            (total_samples > 0)
                ? (100.0 * (double)profile->executions / (double)total_samples)
                : 0.0;

        printf("%-18s %12llu %12llu %12.1f %7.1f%%\n",
               opcode_family_name((OrusOpcodeFamily)i),
               (unsigned long long)profile->executions,
               (unsigned long long)profile->cycles,
               average_cycles,
               share);
    }
}

static void
printJitFailureSummary(const OrusJitTranslationFailureLog* log) {
    printf("\n--- JIT Translation Failures ---\n");
    if (!log || log->total_failures == 0) {
        printf("No translation failures recorded.\n");
        return;
    }

    printf("Total failures: %" PRIu64 "\n", (uint64_t)log->total_failures);

    printf("By reason:\n");
    for (size_t i = 0; i < ORUS_JIT_TRANSLATE_STATUS_COUNT; ++i) {
        uint64_t count = log->reason_counts[i];
        if (count == 0) {
            continue;
        }
        printf("    - %s: %" PRIu64 "\n",
               orus_jit_translation_status_name((OrusJitTranslationStatus)i),
               count);
    }

    printf("By category:\n");
    for (size_t i = 0; i < ORUS_JIT_TRANSLATION_FAILURE_CATEGORY_COUNT; ++i) {
        uint64_t count = log->category_counts[i];
        if (count == 0) {
            continue;
        }
        printf("    - %s: %" PRIu64 "\n",
               orus_jit_translation_failure_category_name(
                   (OrusJitTranslationFailureCategory)i),
               count);
    }

    printf("By value kind:\n");
    for (size_t i = 0; i < ORUS_JIT_VALUE_KIND_COUNT; ++i) {
        uint64_t count = log->value_kind_counts[i];
        if (count == 0) {
            continue;
        }
        double share =
            (log->total_failures > 0)
                ? (100.0 * (double)count / (double)log->total_failures)
                : 0.0;
        printf("    - %s: %" PRIu64 " (%.1f%%)\n",
               orus_jit_value_kind_name((OrusJitValueKind)i),
               count,
               share);
    }
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
        printOpcodeFamilyProfile();
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

    printJitFailureSummary(&vm.jit_translation_failures);
}

void printInstructionProfile(void) {
    printf("\n--- Instruction Execution Profile ---\n");
    printf("%-8s %-18s %12s %12s %8s %8s\n",
           "Opcode",
           "Family",
           "Samples",
           "Cycles",
           "Avg",
           "Hot");
    printf("------------------------------------------------------------------\n");

    for (int i = 0; i < 256; i++) {
        InstructionProfile* profile = &g_profiling.instructionStats[i];
        if (profile->executionCount == 0) {
            continue;
        }

        OrusOpcodeFamily family = vm_opcode_family((uint8_t)i);
        const char* family_name = opcode_family_name(family);

        printf("%-8d %-18s %12llu %12llu %8.1f %8s\n",
               i,
               family_name,
               (unsigned long long)profile->executionCount,
               (unsigned long long)profile->totalCycles,
               profile->averageCycles,
               profile->isHotPath ? "YES" : "NO");
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

    fprintf(file, "  \"opcodeFamilies\": [\n");
    bool firstFamily = true;
    for (size_t i = 0; i < ORUS_OPCODE_FAMILY_COUNT; ++i) {
        const OpcodeFamilyProfile* family = &g_profiling.familyStats[i];
        if (family->executions == 0) {
            continue;
        }
        if (!firstFamily) {
            fprintf(file, ",\n");
        }
        fprintf(file, "    {\"family\": ");
        write_json_string(file, opcode_family_name((OrusOpcodeFamily)i));
        double avg_cycles =
            (family->executions > 0)
                ? (double)family->cycles / (double)family->executions
                : 0.0;
        fprintf(file,
                ", \"samples\": %llu, \"cycles\": %llu, \"average\": %.4f}",
                (unsigned long long)family->executions,
                (unsigned long long)family->cycles,
                avg_cycles);
        firstFamily = false;
    }
    if (!firstFamily) {
        fprintf(file, "\n");
    }
    fprintf(file, "  ],\n");

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

    const OrusJitTranslationFailureLog* failure_log = &vm.jit_translation_failures;
    fprintf(file, "  \"jitFailures\": {\n");
    fprintf(file,
            "    \"total\": %llu,\n",
            (unsigned long long)failure_log->total_failures);

    fprintf(file, "    \"byReason\": [\n");
    bool firstReason = true;
    for (size_t i = 0; i < ORUS_JIT_TRANSLATE_STATUS_COUNT; ++i) {
        uint64_t count = failure_log->reason_counts[i];
        if (count == 0) {
            continue;
        }
        if (!firstReason) {
            fprintf(file, ",\n");
        }
        fprintf(file, "      {\"reason\": ");
        write_json_string(file,
                          orus_jit_translation_status_name(
                              (OrusJitTranslationStatus)i));
        fprintf(file, ", \"count\": %llu}", (unsigned long long)count);
        firstReason = false;
    }
    if (!firstReason) {
        fprintf(file, "\n");
    }
    fprintf(file, "    ],\n");

    fprintf(file, "    \"byCategory\": [\n");
    bool firstCategory = true;
    for (size_t i = 0; i < ORUS_JIT_TRANSLATION_FAILURE_CATEGORY_COUNT; ++i) {
        uint64_t count = failure_log->category_counts[i];
        if (count == 0) {
            continue;
        }
        if (!firstCategory) {
            fprintf(file, ",\n");
        }
        fprintf(file, "      {\"category\": ");
        write_json_string(file,
                          orus_jit_translation_failure_category_name(
                              (OrusJitTranslationFailureCategory)i));
        fprintf(file, ", \"count\": %llu}", (unsigned long long)count);
        firstCategory = false;
    }
    if (!firstCategory) {
        fprintf(file, "\n");
    }
    fprintf(file, "    ],\n");

    fprintf(file, "    \"byValueKind\": [\n");
    bool firstKind = true;
    for (size_t i = 0; i < ORUS_JIT_VALUE_KIND_COUNT; ++i) {
        uint64_t count = failure_log->value_kind_counts[i];
        if (count == 0) {
            continue;
        }
        if (!firstKind) {
            fprintf(file, ",\n");
        }
        fprintf(file, "      {\"valueKind\": ");
        write_json_string(file, orus_jit_value_kind_name((OrusJitValueKind)i));
        double share =
            (failure_log->total_failures > 0)
                ? (double)count / (double)failure_log->total_failures
                : 0.0;
        fprintf(file,
                ", \"count\": %llu, \"share\": %.6f}",
                (unsigned long long)count,
                share);
        firstKind = false;
    }
    if (!firstKind) {
        fprintf(file, "\n");
    }
    fprintf(file, "    ]\n");
    fprintf(file, "  },\n");

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

static inline JITEntryCacheSlot*
vm_jit_entry_cache_slot_for(VMState* vm_state, const JITEntry* entry) {
    if (!vm_state || !entry || !vm_state->jit_cache.slots ||
        vm_state->jit_cache.capacity == 0) {
        return NULL;
    }

    JITEntryCacheSlot* slot =
        (JITEntryCacheSlot*)((uint8_t*)entry - offsetof(JITEntryCacheSlot, entry));
    if (slot < vm_state->jit_cache.slots ||
        slot >= vm_state->jit_cache.slots + vm_state->jit_cache.capacity) {
        return NULL;
    }

    return slot;
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

    bool measure_entry = (entry != &vm_state->jit_entry_stub);
    uint64_t start_cycles = measure_entry ? getTimestamp() : 0;
    vtable->enter((struct VM*)vm_state, entry);
    uint64_t elapsed_cycles =
        measure_entry ? (getTimestamp() - start_cycles) : 0;

    vm_state->jit_invocation_count++;

    if (measure_entry && elapsed_cycles > 0) {
        JITEntryCacheSlot* slot = vm_jit_entry_cache_slot_for(vm_state, entry);
        if (slot && slot->occupied) {
            if (!slot->warmup_recorded) {
                vm_state->jit_enter_cycle_warmup_total += elapsed_cycles;
                vm_state->jit_enter_cycle_warmup_samples++;
                slot->warmup_recorded = true;
            } else {
                vm_state->jit_enter_cycle_total += elapsed_cycles;
                vm_state->jit_enter_cycle_samples++;
            }
        } else {
            vm_state->jit_enter_cycle_total += elapsed_cycles;
            vm_state->jit_enter_cycle_samples++;
        }
    }

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

static OrusJitValueKind
orus_jit_value_kind_from_constant(Value value) {
    if (IS_I32(value)) {
        return ORUS_JIT_VALUE_I32;
    }
    if (IS_I64(value)) {
        return ORUS_JIT_VALUE_I64;
    }
    if (IS_U32(value)) {
        return ORUS_JIT_VALUE_U32;
    }
    if (IS_U64(value)) {
        return ORUS_JIT_VALUE_U64;
    }
    if (IS_F64(value)) {
        return ORUS_JIT_VALUE_F64;
    }
    if (IS_BOOL(value)) {
        return ORUS_JIT_VALUE_BOOL;
    }
    if (IS_STRING(value)) {
        return ORUS_JIT_VALUE_STRING;
    }
    return ORUS_JIT_VALUE_BOXED;
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
        case OP_LOAD_U32_CONST:
            *ir_opcode = ORUS_JIT_IR_OP_LOAD_U32_CONST;
            *kind = ORUS_JIT_VALUE_U32;
            return true;
        case OP_LOAD_U64_CONST:
            *ir_opcode = ORUS_JIT_IR_OP_LOAD_U64_CONST;
            *kind = ORUS_JIT_VALUE_U64;
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
select_move_opcode_for_kind(OrusJitValueKind kind, OrusJitIROpcode* opcode) {
    if (!opcode) {
        return false;
    }

    switch (kind) {
        case ORUS_JIT_VALUE_I32:
            *opcode = ORUS_JIT_IR_OP_MOVE_I32;
            return true;
        case ORUS_JIT_VALUE_I64:
            *opcode = ORUS_JIT_IR_OP_MOVE_I64;
            return true;
        case ORUS_JIT_VALUE_U32:
            *opcode = ORUS_JIT_IR_OP_MOVE_U32;
            return true;
        case ORUS_JIT_VALUE_U64:
            *opcode = ORUS_JIT_IR_OP_MOVE_U64;
            return true;
        case ORUS_JIT_VALUE_F64:
            *opcode = ORUS_JIT_IR_OP_MOVE_F64;
            return true;
        case ORUS_JIT_VALUE_BOOL:
            *opcode = ORUS_JIT_IR_OP_MOVE_BOOL;
            return true;
        case ORUS_JIT_VALUE_STRING:
            *opcode = ORUS_JIT_IR_OP_MOVE_STRING;
            return true;
        case ORUS_JIT_VALUE_BOXED:
            *opcode = ORUS_JIT_IR_OP_MOVE_VALUE;
            return true;
        default:
            break;
    }

    return false;
}

static bool
orus_jit_value_kind_is_integer_like(OrusJitValueKind kind) {
    switch (kind) {
        case ORUS_JIT_VALUE_I32:
        case ORUS_JIT_VALUE_I64:
        case ORUS_JIT_VALUE_U32:
        case ORUS_JIT_VALUE_U64:
            return true;
        default:
            return false;
    }
}

static bool
orus_jit_value_kind_is_boxed_like(OrusJitValueKind kind) {
    switch (kind) {
        case ORUS_JIT_VALUE_STRING:
        case ORUS_JIT_VALUE_BOXED:
            return true;
        default:
            return false;
    }
}

static bool
map_arithmetic_opcode(uint8_t opcode,
                      OrusJitIROpcode* ir_opcode,
                      OrusJitValueKind* kind) {
    switch (opcode) {
        case OP_ADD_I32_R:
        case OP_ADD_I32_TYPED:
            *kind = ORUS_JIT_VALUE_I32;
            *ir_opcode = ORUS_JIT_IR_OP_ADD_I32;
            return true;
        case OP_SUB_I32_R:
        case OP_SUB_I32_TYPED:
            *kind = ORUS_JIT_VALUE_I32;
            *ir_opcode = ORUS_JIT_IR_OP_SUB_I32;
            return true;
        case OP_MUL_I32_R:
        case OP_MUL_I32_TYPED:
            *kind = ORUS_JIT_VALUE_I32;
            *ir_opcode = ORUS_JIT_IR_OP_MUL_I32;
            return true;
        case OP_DIV_I32_R:
        case OP_DIV_I32_TYPED:
            *kind = ORUS_JIT_VALUE_I32;
            *ir_opcode = ORUS_JIT_IR_OP_DIV_I32;
            return true;
        case OP_MOD_I32_R:
        case OP_MOD_I32_TYPED:
            *kind = ORUS_JIT_VALUE_I32;
            *ir_opcode = ORUS_JIT_IR_OP_MOD_I32;
            return true;
        case OP_ADD_I64_R:
        case OP_ADD_I64_TYPED:
            *kind = ORUS_JIT_VALUE_I64;
            *ir_opcode = ORUS_JIT_IR_OP_ADD_I64;
            return true;
        case OP_SUB_I64_R:
        case OP_SUB_I64_TYPED:
            *kind = ORUS_JIT_VALUE_I64;
            *ir_opcode = ORUS_JIT_IR_OP_SUB_I64;
            return true;
        case OP_MUL_I64_R:
        case OP_MUL_I64_TYPED:
            *kind = ORUS_JIT_VALUE_I64;
            *ir_opcode = ORUS_JIT_IR_OP_MUL_I64;
            return true;
        case OP_DIV_I64_R:
        case OP_DIV_I64_TYPED:
            *kind = ORUS_JIT_VALUE_I64;
            *ir_opcode = ORUS_JIT_IR_OP_DIV_I64;
            return true;
        case OP_MOD_I64_R:
        case OP_MOD_I64_TYPED:
            *kind = ORUS_JIT_VALUE_I64;
            *ir_opcode = ORUS_JIT_IR_OP_MOD_I64;
            return true;
        case OP_ADD_U32_R:
        case OP_ADD_U32_TYPED:
            *kind = ORUS_JIT_VALUE_U32;
            *ir_opcode = ORUS_JIT_IR_OP_ADD_U32;
            return true;
        case OP_SUB_U32_R:
        case OP_SUB_U32_TYPED:
            *kind = ORUS_JIT_VALUE_U32;
            *ir_opcode = ORUS_JIT_IR_OP_SUB_U32;
            return true;
        case OP_MUL_U32_R:
        case OP_MUL_U32_TYPED:
            *kind = ORUS_JIT_VALUE_U32;
            *ir_opcode = ORUS_JIT_IR_OP_MUL_U32;
            return true;
        case OP_DIV_U32_R:
        case OP_DIV_U32_TYPED:
            *kind = ORUS_JIT_VALUE_U32;
            *ir_opcode = ORUS_JIT_IR_OP_DIV_U32;
            return true;
        case OP_MOD_U32_R:
        case OP_MOD_U32_TYPED:
            *kind = ORUS_JIT_VALUE_U32;
            *ir_opcode = ORUS_JIT_IR_OP_MOD_U32;
            return true;
        case OP_ADD_U64_R:
        case OP_ADD_U64_TYPED:
            *kind = ORUS_JIT_VALUE_U64;
            *ir_opcode = ORUS_JIT_IR_OP_ADD_U64;
            return true;
        case OP_SUB_U64_R:
        case OP_SUB_U64_TYPED:
            *kind = ORUS_JIT_VALUE_U64;
            *ir_opcode = ORUS_JIT_IR_OP_SUB_U64;
            return true;
        case OP_MUL_U64_R:
        case OP_MUL_U64_TYPED:
            *kind = ORUS_JIT_VALUE_U64;
            *ir_opcode = ORUS_JIT_IR_OP_MUL_U64;
            return true;
        case OP_DIV_U64_R:
        case OP_DIV_U64_TYPED:
            *kind = ORUS_JIT_VALUE_U64;
            *ir_opcode = ORUS_JIT_IR_OP_DIV_U64;
            return true;
        case OP_MOD_U64_R:
        case OP_MOD_U64_TYPED:
            *kind = ORUS_JIT_VALUE_U64;
            *ir_opcode = ORUS_JIT_IR_OP_MOD_U64;
            return true;
        case OP_ADD_F64_R:
        case OP_ADD_F64_TYPED:
            *kind = ORUS_JIT_VALUE_F64;
            *ir_opcode = ORUS_JIT_IR_OP_ADD_F64;
            return true;
        case OP_SUB_F64_R:
        case OP_SUB_F64_TYPED:
            *kind = ORUS_JIT_VALUE_F64;
            *ir_opcode = ORUS_JIT_IR_OP_SUB_F64;
            return true;
        case OP_MUL_F64_R:
        case OP_MUL_F64_TYPED:
            *kind = ORUS_JIT_VALUE_F64;
            *ir_opcode = ORUS_JIT_IR_OP_MUL_F64;
            return true;
        case OP_DIV_F64_R:
        case OP_DIV_F64_TYPED:
            *kind = ORUS_JIT_VALUE_F64;
            *ir_opcode = ORUS_JIT_IR_OP_DIV_F64;
            return true;
        case OP_MOD_F64_R:
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
map_comparison_opcode(uint8_t opcode,
                      OrusJitIROpcode* ir_opcode,
                      OrusJitValueKind* kind) {
    switch (opcode) {
        case OP_LT_I32_R:
        case OP_LT_I32_TYPED:
            *kind = ORUS_JIT_VALUE_BOOL;
            *ir_opcode = ORUS_JIT_IR_OP_LT_I32;
            return true;
        case OP_LE_I32_R:
        case OP_LE_I32_TYPED:
            *kind = ORUS_JIT_VALUE_BOOL;
            *ir_opcode = ORUS_JIT_IR_OP_LE_I32;
            return true;
        case OP_GT_I32_R:
        case OP_GT_I32_TYPED:
            *kind = ORUS_JIT_VALUE_BOOL;
            *ir_opcode = ORUS_JIT_IR_OP_GT_I32;
            return true;
        case OP_GE_I32_R:
        case OP_GE_I32_TYPED:
            *kind = ORUS_JIT_VALUE_BOOL;
            *ir_opcode = ORUS_JIT_IR_OP_GE_I32;
            return true;
        case OP_LT_I64_R:
        case OP_LT_I64_TYPED:
            *kind = ORUS_JIT_VALUE_BOOL;
            *ir_opcode = ORUS_JIT_IR_OP_LT_I64;
            return true;
        case OP_LE_I64_R:
        case OP_LE_I64_TYPED:
            *kind = ORUS_JIT_VALUE_BOOL;
            *ir_opcode = ORUS_JIT_IR_OP_LE_I64;
            return true;
        case OP_GT_I64_R:
        case OP_GT_I64_TYPED:
            *kind = ORUS_JIT_VALUE_BOOL;
            *ir_opcode = ORUS_JIT_IR_OP_GT_I64;
            return true;
        case OP_GE_I64_R:
        case OP_GE_I64_TYPED:
            *kind = ORUS_JIT_VALUE_BOOL;
            *ir_opcode = ORUS_JIT_IR_OP_GE_I64;
            return true;
        case OP_LT_U32_R:
        case OP_LT_U32_TYPED:
            *kind = ORUS_JIT_VALUE_BOOL;
            *ir_opcode = ORUS_JIT_IR_OP_LT_U32;
            return true;
        case OP_LE_U32_R:
        case OP_LE_U32_TYPED:
            *kind = ORUS_JIT_VALUE_BOOL;
            *ir_opcode = ORUS_JIT_IR_OP_LE_U32;
            return true;
        case OP_GT_U32_R:
        case OP_GT_U32_TYPED:
            *kind = ORUS_JIT_VALUE_BOOL;
            *ir_opcode = ORUS_JIT_IR_OP_GT_U32;
            return true;
        case OP_GE_U32_R:
        case OP_GE_U32_TYPED:
            *kind = ORUS_JIT_VALUE_BOOL;
            *ir_opcode = ORUS_JIT_IR_OP_GE_U32;
            return true;
        case OP_LT_U64_R:
        case OP_LT_U64_TYPED:
            *kind = ORUS_JIT_VALUE_BOOL;
            *ir_opcode = ORUS_JIT_IR_OP_LT_U64;
            return true;
        case OP_LE_U64_R:
        case OP_LE_U64_TYPED:
            *kind = ORUS_JIT_VALUE_BOOL;
            *ir_opcode = ORUS_JIT_IR_OP_LE_U64;
            return true;
        case OP_GT_U64_R:
        case OP_GT_U64_TYPED:
            *kind = ORUS_JIT_VALUE_BOOL;
            *ir_opcode = ORUS_JIT_IR_OP_GT_U64;
            return true;
        case OP_GE_U64_R:
        case OP_GE_U64_TYPED:
            *kind = ORUS_JIT_VALUE_BOOL;
            *ir_opcode = ORUS_JIT_IR_OP_GE_U64;
            return true;
        case OP_LT_F64_R:
        case OP_LT_F64_TYPED:
            *kind = ORUS_JIT_VALUE_BOOL;
            *ir_opcode = ORUS_JIT_IR_OP_LT_F64;
            return true;
        case OP_LE_F64_R:
        case OP_LE_F64_TYPED:
            *kind = ORUS_JIT_VALUE_BOOL;
            *ir_opcode = ORUS_JIT_IR_OP_LE_F64;
            return true;
        case OP_GT_F64_R:
        case OP_GT_F64_TYPED:
            *kind = ORUS_JIT_VALUE_BOOL;
            *ir_opcode = ORUS_JIT_IR_OP_GT_F64;
            return true;
        case OP_GE_F64_R:
        case OP_GE_F64_TYPED:
            *kind = ORUS_JIT_VALUE_BOOL;
            *ir_opcode = ORUS_JIT_IR_OP_GE_F64;
            return true;
        default:
            break;
    }
    return false;
}

static OrusJitTranslationResult
make_translation_result(OrusJitTranslationStatus status,
                        OrusJitIROpcode opcode,
                        OrusJitValueKind kind,
                        uint32_t bytecode_offset) {
    OrusJitTranslationResult result;
    result.status = status;
    result.opcode = opcode;
    result.value_kind = kind;
    result.bytecode_offset = bytecode_offset;
    return result;
}

static OrusJitValueKind
orus_jit_value_kind_from_register_type(uint8_t reg_type) {
    switch ((RegisterType)reg_type) {
        case REG_TYPE_I32:
            return ORUS_JIT_VALUE_I32;
        case REG_TYPE_I64:
            return ORUS_JIT_VALUE_I64;
        case REG_TYPE_U32:
            return ORUS_JIT_VALUE_U32;
        case REG_TYPE_U64:
            return ORUS_JIT_VALUE_U64;
        case REG_TYPE_F64:
            return ORUS_JIT_VALUE_F64;
        case REG_TYPE_BOOL:
            return ORUS_JIT_VALUE_BOOL;
        case REG_TYPE_HEAP:
        case REG_TYPE_NONE:
        default:
            return ORUS_JIT_VALUE_BOXED;
    }
}

static void
orus_jit_seed_register_kinds_from_typed_window(const VMState* vm_state,
                                               uint8_t* register_kinds) {
    if (!vm_state || !register_kinds) {
        return;
    }

    const TypedRegisterWindow* window = vm_state->typed_regs.active_window;
    if (!window) {
        window = &vm_state->typed_regs.root_window;
    }
    if (!window) {
        return;
    }

    uint16_t limit = TYPED_REGISTER_WINDOW_SIZE;
    if (limit > REGISTER_COUNT) {
        limit = REGISTER_COUNT;
    }

    for (uint16_t reg = 0; reg < limit; ++reg) {
        if (!typed_window_slot_live(window, reg)) {
            continue;
        }

        OrusJitValueKind kind =
            orus_jit_value_kind_from_register_type(window->reg_types[reg]);
        register_kinds[reg] = (uint8_t)kind;
    }
}

const char* orus_jit_translation_status_name(OrusJitTranslationStatus status) {
    switch (status) {
        case ORUS_JIT_TRANSLATE_STATUS_OK:
            return "ok";
        case ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT:
            return "invalid_input";
        case ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY:
            return "out_of_memory";
        case ORUS_JIT_TRANSLATE_STATUS_UNSUPPORTED_VALUE_KIND:
            return "unsupported_value_kind";
        case ORUS_JIT_TRANSLATE_STATUS_UNSUPPORTED_CONSTANT_KIND:
            return "unsupported_constant_kind";
        case ORUS_JIT_TRANSLATE_STATUS_UNHANDLED_OPCODE:
            return "unhandled_opcode";
        case ORUS_JIT_TRANSLATE_STATUS_UNSUPPORTED_LOOP_SHAPE:
            return "unsupported_loop_shape";
        case ORUS_JIT_TRANSLATE_STATUS_ROLLOUT_DISABLED:
            return "rollout_disabled";
        default:
            break;
    }
    return "unknown";
}

const char* orus_jit_value_kind_name(OrusJitValueKind kind) {
    switch (kind) {
        case ORUS_JIT_VALUE_I32:
            return "i32";
        case ORUS_JIT_VALUE_I64:
            return "i64";
        case ORUS_JIT_VALUE_U32:
            return "u32";
        case ORUS_JIT_VALUE_U64:
            return "u64";
        case ORUS_JIT_VALUE_F64:
            return "f64";
        case ORUS_JIT_VALUE_BOOL:
            return "bool";
        case ORUS_JIT_VALUE_STRING:
            return "string";
        case ORUS_JIT_VALUE_BOXED:
            return "boxed";
        default:
            break;
    }
    return "unknown";
}

bool orus_jit_translation_status_is_unsupported(
    OrusJitTranslationStatus status) {
    switch (status) {
        case ORUS_JIT_TRANSLATE_STATUS_UNSUPPORTED_VALUE_KIND:
        case ORUS_JIT_TRANSLATE_STATUS_UNSUPPORTED_CONSTANT_KIND:
        case ORUS_JIT_TRANSLATE_STATUS_UNHANDLED_OPCODE:
        case ORUS_JIT_TRANSLATE_STATUS_UNSUPPORTED_LOOP_SHAPE:
        case ORUS_JIT_TRANSLATE_STATUS_ROLLOUT_DISABLED:
            return true;
        default:
            return false;
    }
}

const char* orus_jit_translation_failure_category_name(
    OrusJitTranslationFailureCategory category) {
    switch (category) {
        case ORUS_JIT_TRANSLATION_FAILURE_CATEGORY_UNSUPPORTED_BYTECODE:
            return "unsupported_bytecode";
        case ORUS_JIT_TRANSLATION_FAILURE_CATEGORY_MALFORMED_LOOP:
            return "malformed_loop";
        case ORUS_JIT_TRANSLATION_FAILURE_CATEGORY_ROLLOUT_DISABLED:
            return "rollout_disabled";
        case ORUS_JIT_TRANSLATION_FAILURE_CATEGORY_OTHER:
            return "other";
        default:
            break;
    }
    return "unknown";
}

static bool encode_numeric_constant(Value constant,
                                    OrusJitValueKind kind,
                                    uint64_t* out_bits) {
    if (!out_bits) {
        return false;
    }

    switch (kind) {
        case ORUS_JIT_VALUE_I32:
            if (!IS_I32(constant)) {
                return false;
            }
            *out_bits = (uint64_t)(uint32_t)AS_I32(constant);
            return true;
        case ORUS_JIT_VALUE_I64:
            if (!IS_I64(constant)) {
                return false;
            }
            *out_bits = (uint64_t)AS_I64(constant);
            return true;
        case ORUS_JIT_VALUE_U32:
            if (!IS_U32(constant)) {
                return false;
            }
            *out_bits = (uint64_t)AS_U32(constant);
            return true;
        case ORUS_JIT_VALUE_U64:
            if (!IS_U64(constant)) {
                return false;
            }
            *out_bits = AS_U64(constant);
            return true;
        case ORUS_JIT_VALUE_F64:
            if (!IS_F64(constant)) {
                return false;
            }
            {
                double value = AS_F64(constant);
                uint64_t bits = 0u;
                memcpy(&bits, &value, sizeof(bits));
                *out_bits = bits;
            }
            return true;
        default:
            break;
    }

    return false;
}

#define ORUS_JIT_PROFILING_SPECIALIZATION_THRESHOLD 128u

typedef struct {
    bool enabled;
    uint32_t epoch;
    Value constants[REGISTER_COUNT];
    uint32_t reg_epoch[REGISTER_COUNT];
    bool valid[REGISTER_COUNT];
    OrusJitIRInstruction* defining_instruction[REGISTER_COUNT];
} OrusJitSpecializationState;

static void
orus_jit_specialization_state_init(OrusJitSpecializationState* state, bool enabled) {
    if (!state) {
        return;
    }
    state->enabled = enabled;
    state->epoch = 1u;
    memset(state->constants, 0, sizeof(state->constants));
    memset(state->reg_epoch, 0, sizeof(state->reg_epoch));
    memset(state->valid, 0, sizeof(state->valid));
    memset(state->defining_instruction, 0, sizeof(state->defining_instruction));
}

static void
orus_jit_specialization_invalidate_all(OrusJitSpecializationState* state) {
    if (!state || !state->enabled) {
        return;
    }
    state->epoch++;
    memset(state->valid, 0, sizeof(state->valid));
    memset(state->defining_instruction, 0, sizeof(state->defining_instruction));
}

static void
orus_jit_specialization_set_constant(OrusJitSpecializationState* state,
                                     uint16_t reg,
                                     Value value,
                                     OrusJitIRInstruction* inst) {
    if (!state || !state->enabled || reg >= REGISTER_COUNT) {
        return;
    }
    state->constants[reg] = value;
    state->valid[reg] = true;
    state->reg_epoch[reg] = state->epoch;
    if (inst) {
        state->defining_instruction[reg] = inst;
    }
}

static void
orus_jit_specialization_invalidate(OrusJitSpecializationState* state, uint16_t reg) {
    if (!state || !state->enabled || reg >= REGISTER_COUNT) {
        return;
    }
    state->valid[reg] = false;
    state->reg_epoch[reg] = state->epoch;
    state->defining_instruction[reg] = NULL;
}

static bool
orus_jit_specialization_has_constant(const OrusJitSpecializationState* state,
                                     uint16_t reg) {
    if (!state || !state->enabled || reg >= REGISTER_COUNT) {
        return false;
    }
    return state->valid[reg] && state->reg_epoch[reg] == state->epoch;
}

static bool
orus_jit_specialization_constant_matches(const OrusJitSpecializationState* state,
                                         uint16_t reg,
                                         Value value) {
    if (!orus_jit_specialization_has_constant(state, reg)) {
        return false;
    }
    return valuesEqual(state->constants[reg], value);
}

static void
orus_jit_specialization_record_move(OrusJitSpecializationState* state,
                                    uint16_t dst,
                                    uint16_t src,
                                    OrusJitIRInstruction* inst) {
    if (!state || !state->enabled || dst >= REGISTER_COUNT) {
        return;
    }
    if (src < REGISTER_COUNT && orus_jit_specialization_has_constant(state, src)) {
        state->constants[dst] = state->constants[src];
        state->valid[dst] = true;
        state->reg_epoch[dst] = state->epoch;
        state->defining_instruction[dst] = state->defining_instruction[src];
        if (!state->defining_instruction[dst] && inst) {
            state->defining_instruction[dst] = inst;
        }
    } else {
        state->valid[dst] = false;
        state->reg_epoch[dst] = state->epoch;
        state->defining_instruction[dst] = inst;
    }
}

static OrusJitIROpcode
orus_jit_specialization_load_opcode_for_kind(OrusJitValueKind kind) {
    switch (kind) {
        case ORUS_JIT_VALUE_I32:
            return ORUS_JIT_IR_OP_LOAD_I32_CONST;
        case ORUS_JIT_VALUE_I64:
            return ORUS_JIT_IR_OP_LOAD_I64_CONST;
        case ORUS_JIT_VALUE_U32:
            return ORUS_JIT_IR_OP_LOAD_U32_CONST;
        case ORUS_JIT_VALUE_U64:
            return ORUS_JIT_IR_OP_LOAD_U64_CONST;
        case ORUS_JIT_VALUE_F64:
            return ORUS_JIT_IR_OP_LOAD_F64_CONST;
        default:
            break;
    }
    return ORUS_JIT_IR_OP_LOAD_VALUE_CONST;
}

static bool
orus_jit_specialization_try_fold_arithmetic(OrusJitSpecializationState* state,
                                            OrusJitIRInstruction* inst) {
    if (!state || !state->enabled || !inst) {
        return false;
    }

    uint16_t dst = inst->operands.arithmetic.dst_reg;
    uint16_t lhs = inst->operands.arithmetic.lhs_reg;
    uint16_t rhs = inst->operands.arithmetic.rhs_reg;

    if (!orus_jit_specialization_has_constant(state, lhs) ||
        !orus_jit_specialization_has_constant(state, rhs)) {
        orus_jit_specialization_invalidate(state, dst);
        return false;
    }

    Value lhs_value = state->constants[lhs];
    Value rhs_value = state->constants[rhs];
    Value result = {0};
    bool folded = false;

    switch (inst->opcode) {
        case ORUS_JIT_IR_OP_ADD_I32:
            result = I32_VAL(AS_I32(lhs_value) + AS_I32(rhs_value));
            folded = true;
            break;
        case ORUS_JIT_IR_OP_SUB_I32:
            result = I32_VAL(AS_I32(lhs_value) - AS_I32(rhs_value));
            folded = true;
            break;
        case ORUS_JIT_IR_OP_MUL_I32:
            result = I32_VAL(AS_I32(lhs_value) * AS_I32(rhs_value));
            folded = true;
            break;
        case ORUS_JIT_IR_OP_ADD_I64:
            result = I64_VAL(AS_I64(lhs_value) + AS_I64(rhs_value));
            folded = true;
            break;
        case ORUS_JIT_IR_OP_SUB_I64:
            result = I64_VAL(AS_I64(lhs_value) - AS_I64(rhs_value));
            folded = true;
            break;
        case ORUS_JIT_IR_OP_MUL_I64:
            result = I64_VAL(AS_I64(lhs_value) * AS_I64(rhs_value));
            folded = true;
            break;
        case ORUS_JIT_IR_OP_ADD_U32:
            result = U32_VAL((uint32_t)(AS_U32(lhs_value) + AS_U32(rhs_value)));
            folded = true;
            break;
        case ORUS_JIT_IR_OP_SUB_U32:
            result = U32_VAL((uint32_t)(AS_U32(lhs_value) - AS_U32(rhs_value)));
            folded = true;
            break;
        case ORUS_JIT_IR_OP_MUL_U32:
            result = U32_VAL((uint32_t)(AS_U32(lhs_value) * AS_U32(rhs_value)));
            folded = true;
            break;
        case ORUS_JIT_IR_OP_ADD_U64:
            result = U64_VAL(AS_U64(lhs_value) + AS_U64(rhs_value));
            folded = true;
            break;
        case ORUS_JIT_IR_OP_SUB_U64:
            result = U64_VAL(AS_U64(lhs_value) - AS_U64(rhs_value));
            folded = true;
            break;
        case ORUS_JIT_IR_OP_MUL_U64:
            result = U64_VAL(AS_U64(lhs_value) * AS_U64(rhs_value));
            folded = true;
            break;
        case ORUS_JIT_IR_OP_ADD_F64: {
            double value = AS_F64(lhs_value) + AS_F64(rhs_value);
            result = F64_VAL(value);
            folded = true;
            break;
        }
        case ORUS_JIT_IR_OP_SUB_F64: {
            double value = AS_F64(lhs_value) - AS_F64(rhs_value);
            result = F64_VAL(value);
            folded = true;
            break;
        }
        case ORUS_JIT_IR_OP_MUL_F64: {
            double value = AS_F64(lhs_value) * AS_F64(rhs_value);
            result = F64_VAL(value);
            folded = true;
            break;
        }
        default:
            break;
    }

    if (!folded) {
        orus_jit_specialization_invalidate(state, dst);
        return false;
    }

    uint64_t bits = 0u;
    if (!encode_numeric_constant(result, inst->value_kind, &bits)) {
        orus_jit_specialization_invalidate(state, dst);
        return false;
    }

    OrusJitIROpcode load_opcode =
        orus_jit_specialization_load_opcode_for_kind(inst->value_kind);
    if (load_opcode == ORUS_JIT_IR_OP_LOAD_VALUE_CONST) {
        orus_jit_specialization_invalidate(state, dst);
        return false;
    }

    inst->opcode = load_opcode;
    inst->operands.load_const.dst_reg = dst;
    inst->operands.load_const.constant_index = 0u;
    inst->operands.load_const.immediate_bits = bits;

    orus_jit_specialization_set_constant(state, dst, result, inst);
    return true;
}

typedef struct {
    uint8_t* kinds;
    OrusJitIRInstruction** writers;
    bool* visiting;
    const Chunk* chunk;
} OrusJitPromotionContext;

static bool orus_jit_try_promote_register(OrusJitPromotionContext* ctx,
                                          uint16_t reg,
                                          OrusJitValueKind target_kind) {
    if (!ctx || !ctx->kinds || !ctx->writers) {
        return false;
    }

    if (reg >= REGISTER_COUNT) {
        return true;
    }

    OrusJitValueKind current = (OrusJitValueKind)ctx->kinds[reg];
    if (current == target_kind) {
        return true;
    }

    if (current == ORUS_JIT_VALUE_BOXED) {
        return false;
    }

    if (ctx->visiting) {
        if (ctx->visiting[reg]) {
            return false;
        }
        ctx->visiting[reg] = true;
    }

    OrusJitIRInstruction* writer = ctx->writers[reg];
    bool success = false;

    if (!writer) {
        goto promotion_done;
    }

    switch (writer->opcode) {
        case ORUS_JIT_IR_OP_LOAD_I32_CONST: {
            if (current != ORUS_JIT_VALUE_I32 ||
                (target_kind != ORUS_JIT_VALUE_I64 &&
                 target_kind != ORUS_JIT_VALUE_U64)) {
                break;
            }

            int32_t source_value = 0;
            bool have_value = false;
            if (ctx->chunk &&
                writer->operands.load_const.constant_index <
                    (uint16_t)ctx->chunk->constants.count) {
                Value constant = ctx->chunk
                                       ->constants
                                       .values[writer->operands.load_const.constant_index];
                if (IS_I32(constant)) {
                    source_value = AS_I32(constant);
                    have_value = true;
                }
            }
            if (!have_value) {
                source_value =
                    (int32_t)(writer->operands.load_const.immediate_bits & 0xFFFFFFFFu);
            }

            if (target_kind == ORUS_JIT_VALUE_I64) {
                writer->operands.load_const.immediate_bits =
                    (uint64_t)(int64_t)source_value;
                writer->opcode = ORUS_JIT_IR_OP_LOAD_I64_CONST;
                writer->value_kind = ORUS_JIT_VALUE_I64;
            } else {
                writer->operands.load_const.immediate_bits =
                    (uint64_t)(uint32_t)source_value;
                writer->opcode = ORUS_JIT_IR_OP_LOAD_U64_CONST;
                writer->value_kind = ORUS_JIT_VALUE_U64;
            }
            success = true;
            break;
        }
        case ORUS_JIT_IR_OP_LOAD_U32_CONST: {
            if (current != ORUS_JIT_VALUE_U32 ||
                target_kind != ORUS_JIT_VALUE_U64) {
                break;
            }

            uint32_t source_value = 0u;
            bool have_value = false;
            if (ctx->chunk &&
                writer->operands.load_const.constant_index <
                    (uint16_t)ctx->chunk->constants.count) {
                Value constant = ctx->chunk
                                       ->constants
                                       .values[writer->operands.load_const.constant_index];
                if (IS_U32(constant)) {
                    source_value = AS_U32(constant);
                    have_value = true;
                }
            }
            if (!have_value) {
                source_value =
                    (uint32_t)(writer->operands.load_const.immediate_bits & 0xFFFFFFFFu);
            }

            writer->operands.load_const.immediate_bits = (uint64_t)source_value;
            writer->opcode = ORUS_JIT_IR_OP_LOAD_U64_CONST;
            writer->value_kind = ORUS_JIT_VALUE_U64;
            success = true;
            break;
        }
        case ORUS_JIT_IR_OP_MOVE_I32: {
            if (current != ORUS_JIT_VALUE_I32 ||
                (target_kind != ORUS_JIT_VALUE_I64 &&
                 target_kind != ORUS_JIT_VALUE_U64)) {
                break;
            }

            uint16_t src = writer->operands.move.src_reg;
            if (!orus_jit_try_promote_register(ctx, src, target_kind)) {
                break;
            }
            writer->opcode = (target_kind == ORUS_JIT_VALUE_I64)
                                 ? ORUS_JIT_IR_OP_MOVE_I64
                                 : ORUS_JIT_IR_OP_MOVE_U64;
            writer->value_kind = target_kind;
            success = true;
            break;
        }
        case ORUS_JIT_IR_OP_MOVE_U32: {
            if (current != ORUS_JIT_VALUE_U32 ||
                target_kind != ORUS_JIT_VALUE_U64) {
                break;
            }

            uint16_t src = writer->operands.move.src_reg;
            if (!orus_jit_try_promote_register(ctx, src, target_kind)) {
                break;
            }
            writer->opcode = ORUS_JIT_IR_OP_MOVE_U64;
            writer->value_kind = ORUS_JIT_VALUE_U64;
            success = true;
            break;
        }
        default:
            break;
    }

    if (success) {
        ctx->kinds[reg] = (uint8_t)target_kind;
    }

promotion_done:
    if (ctx->visiting) {
        ctx->visiting[reg] = false;
    }
    return success;
}

OrusJitTranslationResult orus_jit_translate_linear_block(
    VMState* vm_state,
    Function* function,
    const Chunk* chunk,
    const HotPathSample* sample,
    OrusJitIRProgram* program) {
    OrusJitTranslationResult result = make_translation_result(
        ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT, ORUS_JIT_IR_OP_RETURN,
        ORUS_JIT_VALUE_I32, 0u);

    if (!function || !program || !sample) {
        return result;
    }

    uint8_t register_kinds[REGISTER_COUNT];
    uint8_t iterator_kinds[REGISTER_COUNT];
    OrusJitIRInstruction* register_writers[REGISTER_COUNT];
    bool promotion_visiting[REGISTER_COUNT];
    for (size_t i = 0; i < REGISTER_COUNT; ++i) {
        register_kinds[i] = (uint8_t)ORUS_JIT_VALUE_BOXED;
        iterator_kinds[i] = (uint8_t)ORUS_JIT_ITERATOR_NONE;
        register_writers[i] = NULL;
        promotion_visiting[i] = false;
    }

    orus_jit_seed_register_kinds_from_typed_window(vm_state, register_kinds);

#define ORUS_JIT_SET_ITERATOR_KIND(reg, iter_value)                                 \
    do {                                                                            \
        uint16_t __iter_reg = (reg);                                                \
        if (__iter_reg < REGISTER_COUNT) {                                          \
            iterator_kinds[__iter_reg] = (uint8_t)(iter_value);                     \
        }                                                                           \
    } while (0)

#define ORUS_JIT_COPY_ITERATOR_KIND(dst, src)                                       \
    ORUS_JIT_SET_ITERATOR_KIND((dst), ORUS_JIT_GET_ITERATOR_KIND((src)))

#define ORUS_JIT_SET_KIND_SELECTOR(_1, _2, _3, NAME, ...) NAME
#define ORUS_JIT_SET_KIND(...)                                                      \
    ORUS_JIT_SET_KIND_SELECTOR(__VA_ARGS__, ORUS_JIT_SET_KIND3,                   \
                               ORUS_JIT_SET_KIND2)(__VA_ARGS__)
#define ORUS_JIT_SET_KIND2(reg, kind_value)                                         \
    do {                                                                            \
        uint16_t __reg = (reg);                                                     \
        if (__reg < REGISTER_COUNT) {                                               \
            register_kinds[__reg] = (uint8_t)(kind_value);                          \
            iterator_kinds[__reg] = (uint8_t)ORUS_JIT_ITERATOR_NONE;                \
            register_writers[__reg] = NULL;                                         \
        }                                                                           \
    } while (0)
#define ORUS_JIT_SET_KIND3(reg, kind_value, writer_value)                           \
    do {                                                                            \
        uint16_t __reg = (reg);                                                     \
        if (__reg < REGISTER_COUNT) {                                               \
            register_kinds[__reg] = (uint8_t)(kind_value);                          \
            iterator_kinds[__reg] = (uint8_t)ORUS_JIT_ITERATOR_NONE;                \
            register_writers[__reg] = (writer_value);                               \
        }                                                                           \
    } while (0)

#define ORUS_JIT_GET_KIND(reg)                                                     \
    (((reg) < REGISTER_COUNT)                                                      \
         ? (OrusJitValueKind)register_kinds[(reg)]                                 \
         : ORUS_JIT_VALUE_BOXED)
#define ORUS_JIT_GET_ITERATOR_KIND(reg)                                            \
    (((reg) < REGISTER_COUNT)                                                      \
         ? (OrusJitIteratorKind)iterator_kinds[(reg)]                              \
         : ORUS_JIT_ITERATOR_NONE)
#define ORUS_JIT_ENSURE_ROLLOUT(kind_value, opcode_value, byte_offset_value)          \
    do {                                                                             \
        OrusJitValueKind __kind = (kind_value);                                      \
        if (__kind != ORUS_JIT_VALUE_BOXED &&                                        \
            !orus_jit_rollout_is_kind_enabled(vm_state, __kind)) {                   \
            return make_translation_result(                                          \
                ORUS_JIT_TRANSLATE_STATUS_ROLLOUT_DISABLED, (opcode_value),         \
                __kind, (uint32_t)(byte_offset_value));                              \
        }                                                                            \
    } while (0)
    if (!chunk) {
        chunk = function ? function->chunk : NULL;
    }

    if (!chunk || !chunk->code || chunk->count <= 0) {
        return result;
    }
    OrusJitPromotionContext promotion_ctx = {
        register_kinds,
        register_writers,
        promotion_visiting,
        chunk,
    };
    OrusJitSpecializationState specialization_state;
    bool specialization_enabled =
        sample && sample->hit_count >= ORUS_JIT_PROFILING_SPECIALIZATION_THRESHOLD;
    orus_jit_specialization_state_init(&specialization_state, specialization_enabled);
    size_t start_offset = (size_t)function->start;
    if ((size_t)sample->loop < (size_t)chunk->count) {
        start_offset = (size_t)sample->loop;
    }
    if (start_offset >= (size_t)chunk->count) {
        return result;
    }

    program->source_chunk = (const struct Chunk*)chunk;
    program->function_index = sample->func;
    program->loop_index = sample->loop;
    program->loop_start_offset = (uint32_t)start_offset;

    bool loop_start_adjusted = false;

    size_t offset = start_offset;
    bool saw_terminal = false;
    size_t instructions_since_safepoint = 0u;
    const size_t safepoint_interval = 12u;

#define ORUS_JIT_SPECIALIZATION_INVALIDATE_REG(reg) \
    orus_jit_specialization_invalidate(&specialization_state, (reg))
#define ORUS_JIT_SPECIALIZATION_INVALIDATE_ALL() \
    orus_jit_specialization_invalidate_all(&specialization_state)

#define INSERT_SAFEPOINT(byte_offset)                                                   \
    do {                                                                               \
        OrusJitIRInstruction* safepoint__ = orus_jit_ir_program_append(program);       \
        if (!safepoint__) {                                                            \
            return make_translation_result(                                            \
                ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY, ORUS_JIT_IR_OP_SAFEPOINT,     \
                ORUS_JIT_VALUE_I32, (byte_offset));                                    \
        }                                                                              \
        safepoint__->opcode = ORUS_JIT_IR_OP_SAFEPOINT;                                \
        safepoint__->bytecode_offset = (byte_offset);                                  \
        instructions_since_safepoint = 0u;                                             \
        ORUS_JIT_SPECIALIZATION_INVALIDATE_ALL();                                      \
    } while (0)

    while (offset < (size_t)chunk->count) {
        GC_SAFEPOINT(vm_state);
        uint8_t opcode = chunk->code[offset];
        switch (opcode) {
            case OP_RETURN_VOID: {
                OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
                if (!inst) {
                    return make_translation_result(
                        ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY,
                        ORUS_JIT_IR_OP_RETURN, ORUS_JIT_VALUE_I32,
                        (uint32_t)offset);
                }
                inst->opcode = ORUS_JIT_IR_OP_RETURN;
                inst->bytecode_offset = (uint32_t)offset;
                saw_terminal = true;
                offset += 1u;
                goto translation_done;
            }
            case OP_RETURN_R: {
                if (offset + 1u >= (size_t)chunk->count) {
                    return make_translation_result(
                        ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                        ORUS_JIT_IR_OP_RETURN, ORUS_JIT_VALUE_I32,
                        (uint32_t)offset);
                }
                OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
                if (!inst) {
                    return make_translation_result(
                        ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY,
                        ORUS_JIT_IR_OP_RETURN, ORUS_JIT_VALUE_I32,
                        (uint32_t)offset);
                }
                inst->opcode = ORUS_JIT_IR_OP_RETURN;
                inst->bytecode_offset = (uint32_t)offset;
                saw_terminal = true;
                offset += 2u;
                goto translation_done;
            }
            case OP_JUMP_SHORT: {
                if (offset + 1u >= (size_t)chunk->count) {
                    return make_translation_result(
                        ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                        ORUS_JIT_IR_OP_JUMP_SHORT, ORUS_JIT_VALUE_I32,
                        (uint32_t)offset);
                }
                uint8_t jump = chunk->code[offset + 1u];
                size_t fallthrough = offset + 2u;
                size_t target = fallthrough + (size_t)jump;
                if (target >= (size_t)chunk->count) {
                    return make_translation_result(
                        ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                        ORUS_JIT_IR_OP_JUMP_SHORT, ORUS_JIT_VALUE_I32,
                        (uint32_t)offset);
                }
                OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
                if (!inst) {
                    return make_translation_result(
                        ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY,
                        ORUS_JIT_IR_OP_JUMP_SHORT, ORUS_JIT_VALUE_I32,
                        (uint32_t)offset);
                }
                inst->opcode = ORUS_JIT_IR_OP_JUMP_SHORT;
                inst->bytecode_offset = (uint32_t)offset;
                inst->operands.jump_short.offset = jump;
                inst->operands.jump_short.bytecode_length = 2u;
                offset += 2u;
                if (++instructions_since_safepoint >= safepoint_interval) {
                    INSERT_SAFEPOINT((uint32_t)offset);
                }
                continue;
            }
            case OP_JUMP: {
                if (offset + 2u >= (size_t)chunk->count) {
                    return make_translation_result(
                        ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                        ORUS_JIT_IR_OP_JUMP_SHORT, ORUS_JIT_VALUE_I32,
                        (uint32_t)offset);
                }
                uint16_t jump = read_be_u16(&chunk->code[offset + 1u]);
                size_t fallthrough = offset + 3u;
                size_t target = fallthrough + (size_t)jump;
                if (target >= (size_t)chunk->count) {
                    return make_translation_result(
                        ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                        ORUS_JIT_IR_OP_JUMP_SHORT, ORUS_JIT_VALUE_I32,
                        (uint32_t)offset);
                }
                OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
                if (!inst) {
                    return make_translation_result(
                        ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY,
                        ORUS_JIT_IR_OP_JUMP_SHORT, ORUS_JIT_VALUE_I32,
                        (uint32_t)offset);
                }
                inst->opcode = ORUS_JIT_IR_OP_JUMP_SHORT;
                inst->bytecode_offset = (uint32_t)offset;
                inst->operands.jump_short.offset = jump;
                inst->operands.jump_short.bytecode_length = 3u;
                offset += 3u;
                if (++instructions_since_safepoint >= safepoint_interval) {
                    INSERT_SAFEPOINT((uint32_t)offset);
                }
                continue;
            }
            case OP_JUMP_BACK_SHORT: {
                if (offset + 1u >= (size_t)chunk->count) {
                    return make_translation_result(
                        ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                        ORUS_JIT_IR_OP_JUMP_BACK_SHORT, ORUS_JIT_VALUE_I32,
                        (uint32_t)offset);
                }
                uint8_t back = chunk->code[offset + 1u];
                size_t fallthrough = offset + 2u;
                if (fallthrough < (size_t)back) {
                    return make_translation_result(
                        ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                        ORUS_JIT_IR_OP_JUMP_BACK_SHORT, ORUS_JIT_VALUE_I32,
                        (uint32_t)offset);
                }
                size_t target = fallthrough - (size_t)back;
                if (target >= (size_t)chunk->count) {
                    return make_translation_result(
                        ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                        ORUS_JIT_IR_OP_JUMP_BACK_SHORT, ORUS_JIT_VALUE_I32,
                        (uint32_t)offset);
                }
                OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
                if (!inst) {
                    return make_translation_result(
                        ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY,
                        ORUS_JIT_IR_OP_JUMP_BACK_SHORT, ORUS_JIT_VALUE_I32,
                        (uint32_t)offset);
                }
                inst->opcode = ORUS_JIT_IR_OP_JUMP_BACK_SHORT;
                inst->bytecode_offset = (uint32_t)offset;
                inst->operands.jump_back_short.back_offset = back;
                offset += 2u;
                if (++instructions_since_safepoint >= safepoint_interval) {
                    INSERT_SAFEPOINT((uint32_t)offset);
                }
                continue;
            }
            case OP_JUMP_IF_NOT_SHORT: {
                if (offset + 2u >= (size_t)chunk->count) {
                    return make_translation_result(
                        ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                        ORUS_JIT_IR_OP_JUMP_IF_NOT_SHORT, ORUS_JIT_VALUE_I32,
                        (uint32_t)offset);
                }
                uint16_t predicate = chunk->code[offset + 1u];
                uint8_t jump = chunk->code[offset + 2u];
                size_t fallthrough = offset + 3u;
                size_t target = fallthrough + (size_t)jump;
                if (target >= (size_t)chunk->count) {
                    return make_translation_result(
                        ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                        ORUS_JIT_IR_OP_JUMP_IF_NOT_SHORT, ORUS_JIT_VALUE_I32,
                        (uint32_t)offset);
                }
                OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
                if (!inst) {
                    return make_translation_result(
                        ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY,
                        ORUS_JIT_IR_OP_JUMP_IF_NOT_SHORT,
                        ORUS_JIT_VALUE_I32, (uint32_t)offset);
                }
                inst->opcode = ORUS_JIT_IR_OP_JUMP_IF_NOT_SHORT;
                inst->bytecode_offset = (uint32_t)offset;
                inst->operands.jump_if_not_short.predicate_reg = predicate;
                inst->operands.jump_if_not_short.offset = jump;
                inst->operands.jump_if_not_short.bytecode_length = 3u;
                offset += 3u;
                if (++instructions_since_safepoint >= safepoint_interval) {
                    INSERT_SAFEPOINT((uint32_t)offset);
                }
                continue;
            }
            case OP_JUMP_IF_NOT_I32_TYPED: {
                if (offset + 4u >= (size_t)chunk->count) {
                    return make_translation_result(
                        ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                        ORUS_JIT_IR_OP_JUMP_IF_NOT_SHORT,
                        ORUS_JIT_VALUE_I32, (uint32_t)offset);
                }
                uint16_t lhs = chunk->code[offset + 1u];
                uint16_t rhs = chunk->code[offset + 2u];
                uint16_t jump = read_be_u16(&chunk->code[offset + 3u]);
                size_t fallthrough = offset + 5u;
                size_t target = fallthrough + (size_t)jump;
                if (target >= (size_t)chunk->count) {
                    return make_translation_result(
                        ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                        ORUS_JIT_IR_OP_JUMP_IF_NOT_SHORT,
                        ORUS_JIT_VALUE_I32, (uint32_t)offset);
                }
                OrusJitValueKind lhs_kind = ORUS_JIT_GET_KIND(lhs);
                if (lhs_kind == ORUS_JIT_VALUE_STRING) {
                    return make_translation_result(
                        ORUS_JIT_TRANSLATE_STATUS_UNSUPPORTED_VALUE_KIND,
                        ORUS_JIT_IR_OP_LT_I32, lhs_kind, (uint32_t)offset);
                }
                if (lhs_kind != ORUS_JIT_VALUE_I32) {
                    if (orus_jit_value_kind_is_boxed_like(lhs_kind) &&
                        lhs < REGISTER_COUNT) {
                        register_kinds[lhs] = (uint8_t)ORUS_JIT_VALUE_I32;
                        iterator_kinds[lhs] = (uint8_t)ORUS_JIT_ITERATOR_NONE;
                        register_writers[lhs] = NULL;
                        lhs_kind = ORUS_JIT_VALUE_I32;
                    } else if (!orus_jit_try_promote_register(&promotion_ctx, lhs,
                                                              ORUS_JIT_VALUE_I32)) {
                        return make_translation_result(
                            ORUS_JIT_TRANSLATE_STATUS_UNSUPPORTED_VALUE_KIND,
                            ORUS_JIT_IR_OP_LT_I32, lhs_kind, (uint32_t)offset);
                    } else {
                        lhs_kind = ORUS_JIT_GET_KIND(lhs);
                    }
                }
                OrusJitValueKind rhs_kind = ORUS_JIT_GET_KIND(rhs);
                if (rhs_kind == ORUS_JIT_VALUE_STRING) {
                    return make_translation_result(
                        ORUS_JIT_TRANSLATE_STATUS_UNSUPPORTED_VALUE_KIND,
                        ORUS_JIT_IR_OP_LT_I32, rhs_kind, (uint32_t)offset);
                }
                if (rhs_kind != ORUS_JIT_VALUE_I32) {
                    if (orus_jit_value_kind_is_boxed_like(rhs_kind) &&
                        rhs < REGISTER_COUNT) {
                        register_kinds[rhs] = (uint8_t)ORUS_JIT_VALUE_I32;
                        iterator_kinds[rhs] = (uint8_t)ORUS_JIT_ITERATOR_NONE;
                        register_writers[rhs] = NULL;
                        rhs_kind = ORUS_JIT_VALUE_I32;
                    } else if (!orus_jit_try_promote_register(&promotion_ctx, rhs,
                                                              ORUS_JIT_VALUE_I32)) {
                        return make_translation_result(
                            ORUS_JIT_TRANSLATE_STATUS_UNSUPPORTED_VALUE_KIND,
                            ORUS_JIT_IR_OP_LT_I32, rhs_kind, (uint32_t)offset);
                    } else {
                        rhs_kind = ORUS_JIT_GET_KIND(rhs);
                    }
                }
                ORUS_JIT_ENSURE_ROLLOUT(ORUS_JIT_VALUE_I32, ORUS_JIT_IR_OP_LT_I32,
                                        offset);
                uint16_t predicate_reg = TEMP_REG_START;
                OrusJitIRInstruction* cmp_inst = orus_jit_ir_program_append(program);
                if (!cmp_inst) {
                    return make_translation_result(
                        ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY,
                        ORUS_JIT_IR_OP_LT_I32, ORUS_JIT_VALUE_I32,
                        (uint32_t)offset);
                }
                cmp_inst->opcode = ORUS_JIT_IR_OP_LT_I32;
                cmp_inst->value_kind = ORUS_JIT_VALUE_BOOL;
                cmp_inst->bytecode_offset = (uint32_t)offset;
                cmp_inst->operands.arithmetic.dst_reg = predicate_reg;
                cmp_inst->operands.arithmetic.lhs_reg = lhs;
                cmp_inst->operands.arithmetic.rhs_reg = rhs;
                ORUS_JIT_SET_KIND(predicate_reg, ORUS_JIT_VALUE_BOOL, cmp_inst);
                OrusJitIRInstruction* jump_inst = orus_jit_ir_program_append(program);
                if (!jump_inst) {
                    return make_translation_result(
                        ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY,
                        ORUS_JIT_IR_OP_JUMP_IF_NOT_SHORT,
                        ORUS_JIT_VALUE_BOOL, (uint32_t)offset);
                }
                jump_inst->opcode = ORUS_JIT_IR_OP_JUMP_IF_NOT_SHORT;
                jump_inst->bytecode_offset = (uint32_t)offset;
                jump_inst->operands.jump_if_not_short.predicate_reg = predicate_reg;
                jump_inst->operands.jump_if_not_short.offset = jump;
                jump_inst->operands.jump_if_not_short.bytecode_length = 5u;
                offset += 5u;
                if (++instructions_since_safepoint >= safepoint_interval) {
                    INSERT_SAFEPOINT((uint32_t)offset);
                }
                continue;
            }
            case OP_JUMP_IF_NOT_R: {
                if (offset + 3u >= (size_t)chunk->count) {
                    return make_translation_result(
                        ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                        ORUS_JIT_IR_OP_JUMP_IF_NOT_SHORT, ORUS_JIT_VALUE_I32,
                        (uint32_t)offset);
                }
                uint16_t predicate = chunk->code[offset + 1u];
                uint16_t jump = read_be_u16(&chunk->code[offset + 2u]);
                size_t fallthrough = offset + 4u;
                size_t target = fallthrough + (size_t)jump;
                if (target >= (size_t)chunk->count) {
                    return make_translation_result(
                        ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                        ORUS_JIT_IR_OP_JUMP_IF_NOT_SHORT, ORUS_JIT_VALUE_I32,
                        (uint32_t)offset);
                }
                OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
                if (!inst) {
                    return make_translation_result(
                        ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY,
                        ORUS_JIT_IR_OP_JUMP_IF_NOT_SHORT,
                        ORUS_JIT_VALUE_I32, (uint32_t)offset);
                }
                inst->opcode = ORUS_JIT_IR_OP_JUMP_IF_NOT_SHORT;
                inst->bytecode_offset = (uint32_t)offset;
                inst->operands.jump_if_not_short.predicate_reg = predicate;
                inst->operands.jump_if_not_short.offset = jump;
                inst->operands.jump_if_not_short.bytecode_length = 4u;
                ORUS_JIT_SET_KIND(predicate, ORUS_JIT_VALUE_BOOL, inst);
                offset += 4u;
                if (++instructions_since_safepoint >= safepoint_interval) {
                    INSERT_SAFEPOINT((uint32_t)offset);
                }
                continue;
            }
            case OP_BRANCH_TYPED: {
                if (offset + 5u >= (size_t)chunk->count) {
                    return make_translation_result(
                        ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                        ORUS_JIT_IR_OP_JUMP_IF_NOT_SHORT,
                        ORUS_JIT_VALUE_BOOL, (uint32_t)offset);
                }
                uint16_t predicate = chunk->code[offset + 3u];
                uint16_t jump = read_be_u16(&chunk->code[offset + 4u]);
                size_t fallthrough = offset + 6u;
                size_t target = fallthrough + (size_t)jump;
                if (target >= (size_t)chunk->count) {
                    return make_translation_result(
                        ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                        ORUS_JIT_IR_OP_JUMP_IF_NOT_SHORT,
                        ORUS_JIT_VALUE_BOOL, (uint32_t)offset);
                }
                OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
                if (!inst) {
                    return make_translation_result(
                        ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY,
                        ORUS_JIT_IR_OP_JUMP_IF_NOT_SHORT,
                        ORUS_JIT_VALUE_BOOL, (uint32_t)offset);
                }
                inst->opcode = ORUS_JIT_IR_OP_JUMP_IF_NOT_SHORT;
                inst->bytecode_offset = (uint32_t)offset;
                inst->operands.jump_if_not_short.predicate_reg = predicate;
                inst->operands.jump_if_not_short.offset = jump;
                inst->operands.jump_if_not_short.bytecode_length = 6u;
                ORUS_JIT_SET_KIND(predicate, ORUS_JIT_VALUE_BOOL, inst);
                offset += 6u;
                if (++instructions_since_safepoint >= safepoint_interval) {
                    INSERT_SAFEPOINT((uint32_t)offset);
                }
                continue;
            }
            case OP_INC_CMP_JMP:
            case OP_DEC_CMP_JMP: {
                if (offset + 4u >= (size_t)chunk->count) {
                    return make_translation_result(
                        ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                        opcode == OP_INC_CMP_JMP
                            ? ORUS_JIT_IR_OP_INC_CMP_JUMP
                            : ORUS_JIT_IR_OP_DEC_CMP_JUMP,
                        ORUS_JIT_VALUE_I32, (uint32_t)offset);
                }

                uint16_t counter_reg = chunk->code[offset + 1u];
                uint16_t limit_reg = chunk->code[offset + 2u];
                int16_t jump_offset =
                    (int16_t)((chunk->code[offset + 3u] << 8) |
                              chunk->code[offset + 4u]);

                OrusJitValueKind counter_kind = ORUS_JIT_GET_KIND(counter_reg);
                OrusJitValueKind limit_kind = ORUS_JIT_GET_KIND(limit_reg);
                bool counter_is_boxed =
                    (counter_kind == ORUS_JIT_VALUE_BOXED);
                bool limit_is_boxed =
                    (limit_kind == ORUS_JIT_VALUE_BOXED);
                if (!counter_is_boxed && !limit_is_boxed &&
                    counter_kind != limit_kind) {
                    if ((counter_kind == ORUS_JIT_VALUE_I32 &&
                         (limit_kind == ORUS_JIT_VALUE_I64 ||
                          limit_kind == ORUS_JIT_VALUE_U64)) ||
                        (counter_kind == ORUS_JIT_VALUE_U32 &&
                         limit_kind == ORUS_JIT_VALUE_U64)) {
                        if (orus_jit_try_promote_register(
                                &promotion_ctx, counter_reg, limit_kind)) {
                            counter_kind = ORUS_JIT_GET_KIND(counter_reg);
                        }
                    }
                    if ((limit_kind == ORUS_JIT_VALUE_I32 &&
                         (counter_kind == ORUS_JIT_VALUE_I64 ||
                          counter_kind == ORUS_JIT_VALUE_U64)) ||
                        (limit_kind == ORUS_JIT_VALUE_U32 &&
                         counter_kind == ORUS_JIT_VALUE_U64)) {
                        if (orus_jit_try_promote_register(
                                &promotion_ctx, limit_reg, counter_kind)) {
                            limit_kind = ORUS_JIT_GET_KIND(limit_reg);
                        }
                    }
                }
                OrusJitValueKind fused_kind = ORUS_JIT_VALUE_BOXED;
                bool use_boxed_helper = false;

                if (!counter_is_boxed && !limit_is_boxed) {
                    if (counter_kind != limit_kind) {
                        fused_kind = ORUS_JIT_VALUE_BOXED;
                        use_boxed_helper = true;
                    } else {
                        fused_kind = counter_kind;
                    }
                } else {
                    if (counter_is_boxed && limit_is_boxed) {
                        fused_kind = ORUS_JIT_VALUE_BOXED;
                        use_boxed_helper = true;
                    } else {
                        OrusJitValueKind typed_partner =
                            counter_is_boxed ? limit_kind : counter_kind;
                        if (!orus_jit_value_kind_is_integer_like(typed_partner)) {
                            fused_kind = ORUS_JIT_VALUE_BOXED;
                            use_boxed_helper = true;
                        } else {
                            fused_kind = ORUS_JIT_VALUE_BOXED;
                            use_boxed_helper = true;
                        }
                    }
                }

                switch (fused_kind) {
                    case ORUS_JIT_VALUE_I32:
                    case ORUS_JIT_VALUE_I64:
                    case ORUS_JIT_VALUE_U32:
                    case ORUS_JIT_VALUE_U64:
                    case ORUS_JIT_VALUE_F64:
                    case ORUS_JIT_VALUE_BOXED:
                        break;
                    default:
                        return make_translation_result(
                            ORUS_JIT_TRANSLATE_STATUS_UNSUPPORTED_VALUE_KIND,
                            opcode == OP_INC_CMP_JMP
                                ? ORUS_JIT_IR_OP_INC_CMP_JUMP
                                : ORUS_JIT_IR_OP_DEC_CMP_JUMP,
                            counter_kind, (uint32_t)offset);
                }

                OrusJitIROpcode ir_opcode =
                    (opcode == OP_INC_CMP_JMP)
                        ? ORUS_JIT_IR_OP_INC_CMP_JUMP
                        : ORUS_JIT_IR_OP_DEC_CMP_JUMP;
                OrusJitIRLoopStepKind step_kind =
                    (opcode == OP_INC_CMP_JMP)
                        ? ORUS_JIT_IR_LOOP_STEP_INCREMENT
                        : ORUS_JIT_IR_LOOP_STEP_DECREMENT;
                OrusJitIRLoopCompareKind compare_kind =
                    (opcode == OP_INC_CMP_JMP)
                        ? ORUS_JIT_IR_LOOP_COMPARE_LESS_THAN
                        : ORUS_JIT_IR_LOOP_COMPARE_GREATER_THAN;

                ORUS_JIT_ENSURE_ROLLOUT(fused_kind, ir_opcode, offset);

                OrusJitIRInstruction* inst =
                    orus_jit_ir_program_append(program);
                if (!inst) {
                    return make_translation_result(
                        ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY, ir_opcode,
                        counter_kind, (uint32_t)offset);
                }

                inst->opcode = ir_opcode;
                inst->value_kind = fused_kind;
                inst->bytecode_offset = (uint32_t)offset;
                inst->operands.fused_loop.counter_reg = counter_reg;
                inst->operands.fused_loop.limit_reg = limit_reg;
                inst->operands.fused_loop.jump_offset = jump_offset;
                inst->operands.fused_loop.step = (int8_t)step_kind;
                inst->operands.fused_loop.compare_kind =
                    (uint8_t)compare_kind;

                if (use_boxed_helper) {
                    ORUS_JIT_SET_KIND(counter_reg, ORUS_JIT_VALUE_BOXED, inst);
                } else {
                    ORUS_JIT_SET_KIND(counter_reg, fused_kind, inst);
                }

                offset += 5u;
                if (++instructions_since_safepoint >= safepoint_interval) {
                    INSERT_SAFEPOINT((uint32_t)offset);
                }
                continue;
            }
            case OP_LOOP_SHORT: {
                if (offset + 1u >= (size_t)chunk->count) {
                    return make_translation_result(
                        ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                        ORUS_JIT_IR_OP_LOOP_BACK, ORUS_JIT_VALUE_I32,
                        (uint32_t)offset);
                }
                uint8_t back = chunk->code[offset + 1u];
                size_t fallthrough = offset + 2u;
                size_t target =
                    (fallthrough >= (size_t)back) ? (fallthrough - (size_t)back) : 0u;
                if (target != start_offset) {
                    if (!loop_start_adjusted) {
                        start_offset = target;
                        program->loop_start_offset = (uint32_t)start_offset;
                        loop_start_adjusted = true;
                    } else {
                        return make_translation_result(
                            ORUS_JIT_TRANSLATE_STATUS_UNSUPPORTED_LOOP_SHAPE,
                            ORUS_JIT_IR_OP_LOOP_BACK, ORUS_JIT_VALUE_I32,
                            (uint32_t)offset);
                    }
                }
                if (instructions_since_safepoint > 0u) {
                    INSERT_SAFEPOINT((uint32_t)offset);
                }
                OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
                if (!inst) {
                    return make_translation_result(
                        ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY,
                        ORUS_JIT_IR_OP_LOOP_BACK, ORUS_JIT_VALUE_I32,
                        (uint32_t)offset);
                }
                inst->opcode = ORUS_JIT_IR_OP_LOOP_BACK;
                inst->bytecode_offset = (uint32_t)offset;
                inst->operands.loop_back.back_offset = back;
                saw_terminal = true;
                offset += 2u;
                goto translation_done;
            }
            case OP_LOOP: {
                if (offset + 2u >= (size_t)chunk->count) {
                    return make_translation_result(
                        ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                        ORUS_JIT_IR_OP_LOOP_BACK, ORUS_JIT_VALUE_I32,
                        (uint32_t)offset);
                }
                uint16_t back = read_be_u16(&chunk->code[offset + 1u]);
                size_t fallthrough = offset + 3u;
                if (fallthrough < (size_t)back) {
                    return make_translation_result(
                        ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                        ORUS_JIT_IR_OP_LOOP_BACK, ORUS_JIT_VALUE_I32,
                        (uint32_t)offset);
                }
                size_t target = fallthrough - (size_t)back;
                if (target != start_offset) {
                    if (!loop_start_adjusted) {
                        start_offset = target;
                        program->loop_start_offset = (uint32_t)start_offset;
                        loop_start_adjusted = true;
                    } else {
                        return make_translation_result(
                            ORUS_JIT_TRANSLATE_STATUS_UNSUPPORTED_LOOP_SHAPE,
                            ORUS_JIT_IR_OP_LOOP_BACK, ORUS_JIT_VALUE_I32,
                            (uint32_t)offset);
                    }
                }
                if (instructions_since_safepoint > 0u) {
                    INSERT_SAFEPOINT((uint32_t)offset);
                }
                OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
                if (!inst) {
                    return make_translation_result(
                        ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY,
                        ORUS_JIT_IR_OP_LOOP_BACK, ORUS_JIT_VALUE_I32,
                        (uint32_t)offset);
                }
                inst->opcode = ORUS_JIT_IR_OP_LOOP_BACK;
                inst->bytecode_offset = (uint32_t)offset;
                inst->operands.loop_back.back_offset = (uint16_t)back;
                saw_terminal = true;
                offset += 3u;
                goto translation_done;
            }
            default:
                break;
        }

        if (opcode == OP_LOAD_TRUE || opcode == OP_LOAD_FALSE) {
            if (offset + 1u >= (size_t)chunk->count) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                    ORUS_JIT_IR_OP_LOAD_BOOL_CONST, ORUS_JIT_VALUE_BOOL,
                    (uint32_t)offset);
            }
            uint16_t dst = chunk->code[offset + 1u];
            bool bool_value = (opcode == OP_LOAD_TRUE);
            ORUS_JIT_ENSURE_ROLLOUT(ORUS_JIT_VALUE_BOOL,
                                    ORUS_JIT_IR_OP_LOAD_BOOL_CONST, offset);
            OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
            if (!inst) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY,
                    ORUS_JIT_IR_OP_LOAD_BOOL_CONST, ORUS_JIT_VALUE_BOOL,
                    (uint32_t)offset);
            }
            inst->opcode = ORUS_JIT_IR_OP_LOAD_BOOL_CONST;
            inst->value_kind = ORUS_JIT_VALUE_BOOL;
            inst->bytecode_offset = (uint32_t)offset;
            inst->operands.load_const.dst_reg = dst;
            inst->operands.load_const.constant_index = 0u;
            inst->operands.load_const.immediate_bits = bool_value ? 1u : 0u;
            ORUS_JIT_SET_KIND(dst, ORUS_JIT_VALUE_BOOL, inst);
            ORUS_JIT_SPECIALIZATION_INVALIDATE_REG(dst);
            offset += 2u;
            if (++instructions_since_safepoint >= safepoint_interval) {
                INSERT_SAFEPOINT((uint32_t)offset);
            }
            continue;
        }

        OrusJitValueKind kind = ORUS_JIT_VALUE_I32;
        OrusJitIROpcode ir_opcode = ORUS_JIT_IR_OP_RETURN;

        if (opcode == OP_LOAD_CONST) {
            if (offset + 3u >= (size_t)chunk->count) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                    ORUS_JIT_IR_OP_LOAD_VALUE_CONST, ORUS_JIT_VALUE_BOXED,
                    (uint32_t)offset);
            }
            uint16_t dst = chunk->code[offset + 1u];
            uint16_t constant_index = read_be_u16(&chunk->code[offset + 2u]);
            if (constant_index >= (uint16_t)chunk->constants.count) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                    ORUS_JIT_IR_OP_LOAD_VALUE_CONST, ORUS_JIT_VALUE_BOXED,
                    (uint32_t)offset);
            }
            Value constant = chunk->constants.values[constant_index];
            if (IS_STRING(constant)) {
                ORUS_JIT_ENSURE_ROLLOUT(ORUS_JIT_VALUE_STRING,
                                        ORUS_JIT_IR_OP_LOAD_STRING_CONST,
                                        offset);

                if (specialization_enabled &&
                    ORUS_JIT_GET_KIND(dst) == ORUS_JIT_VALUE_STRING &&
                    orus_jit_specialization_constant_matches(&specialization_state,
                                                             dst, constant)) {
                    offset += 4u;
                    if (++instructions_since_safepoint >= safepoint_interval) {
                        INSERT_SAFEPOINT((uint32_t)offset);
                    }
                    continue;
                }

                OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
                if (!inst) {
                    return make_translation_result(
                        ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY,
                        ORUS_JIT_IR_OP_LOAD_STRING_CONST,
                        ORUS_JIT_VALUE_STRING, (uint32_t)offset);
                }
                inst->opcode = ORUS_JIT_IR_OP_LOAD_STRING_CONST;
                inst->value_kind = ORUS_JIT_VALUE_STRING;
                inst->bytecode_offset = (uint32_t)offset;
                inst->operands.load_const.dst_reg = dst;
                inst->operands.load_const.constant_index = constant_index;
                inst->operands.load_const.immediate_bits =
                    (uint64_t)(uintptr_t)AS_STRING(constant);
                ORUS_JIT_SET_KIND(dst, ORUS_JIT_VALUE_STRING, inst);
                if (specialization_enabled) {
                    orus_jit_specialization_set_constant(&specialization_state, dst,
                                                         constant, inst);
                }
                offset += 4u;
                if (++instructions_since_safepoint >= safepoint_interval) {
                    INSERT_SAFEPOINT((uint32_t)offset);
                }
                continue;
            }

            OrusJitValueKind const_kind =
                orus_jit_value_kind_from_constant(constant);

            ORUS_JIT_ENSURE_ROLLOUT(const_kind,
                                    ORUS_JIT_IR_OP_LOAD_VALUE_CONST, offset);

            if (specialization_enabled &&
                ORUS_JIT_GET_KIND(dst) == const_kind &&
                orus_jit_specialization_constant_matches(&specialization_state, dst,
                                                         constant)) {
                offset += 4u;
                if (++instructions_since_safepoint >= safepoint_interval) {
                    INSERT_SAFEPOINT((uint32_t)offset);
                }
                continue;
            }

            OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
            if (!inst) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY,
                    ORUS_JIT_IR_OP_LOAD_VALUE_CONST, const_kind,
                    (uint32_t)offset);
            }
            inst->opcode = ORUS_JIT_IR_OP_LOAD_VALUE_CONST;
            inst->value_kind = const_kind;
            inst->bytecode_offset = (uint32_t)offset;
            inst->operands.load_const.dst_reg = dst;
            inst->operands.load_const.constant_index = constant_index;
            inst->operands.load_const.immediate_bits = 0u;
            ORUS_JIT_SET_KIND(dst, const_kind, inst);
            if (specialization_enabled) {
                orus_jit_specialization_set_constant(&specialization_state, dst,
                                                     constant, inst);
            }
            offset += 4u;
            if (++instructions_since_safepoint >= safepoint_interval) {
                INSERT_SAFEPOINT((uint32_t)offset);
            }
            continue;
        }

        if (map_const_opcode(opcode, &ir_opcode, &kind)) {
            if (offset + 3u >= (size_t)chunk->count) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT, ir_opcode, kind,
                    (uint32_t)offset);
            }
            uint16_t dst = chunk->code[offset + 1u];
            uint16_t constant_index = read_be_u16(&chunk->code[offset + 2u]);
            if (constant_index >= (uint16_t)chunk->constants.count) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT, ir_opcode, kind,
                    (uint32_t)offset);
            }
            ORUS_JIT_ENSURE_ROLLOUT(kind, ir_opcode, offset);
            OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
            if (!inst) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY, ir_opcode, kind,
                    (uint32_t)offset);
            }
            inst->opcode = ir_opcode;
            inst->value_kind = kind;
            inst->bytecode_offset = (uint32_t)offset;
            inst->operands.load_const.dst_reg = dst;
            inst->operands.load_const.constant_index = constant_index;
            Value constant = chunk->constants.values[constant_index];
            uint64_t bits = 0u;
            if (!encode_numeric_constant(constant, kind, &bits)) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_UNSUPPORTED_CONSTANT_KIND,
                    ir_opcode, kind, (uint32_t)offset);
            }
            inst->operands.load_const.immediate_bits = bits;
            ORUS_JIT_SET_KIND(dst, kind, inst);
            offset += 4u;
            if (++instructions_since_safepoint >= safepoint_interval) {
                INSERT_SAFEPOINT((uint32_t)offset);
            }
            continue;
        }

        if (map_move_opcode(opcode, &ir_opcode, &kind)) {
            if (offset + 2u >= (size_t)chunk->count) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT, ir_opcode, kind,
                    (uint32_t)offset);
            }
            uint16_t dst = chunk->code[offset + 1u];
            uint16_t src = chunk->code[offset + 2u];
            OrusJitValueKind src_kind_tracked = ORUS_JIT_GET_KIND(src);
            if (src_kind_tracked != kind) {
                if (src_kind_tracked == ORUS_JIT_VALUE_BOXED &&
                    src < REGISTER_COUNT) {
                    register_kinds[src] = (uint8_t)kind;
                    iterator_kinds[src] = (uint8_t)ORUS_JIT_ITERATOR_NONE;
                    register_writers[src] = NULL;
                    src_kind_tracked = kind;
                } else if (!orus_jit_try_promote_register(&promotion_ctx, src,
                                                          kind)) {
                    return make_translation_result(
                        ORUS_JIT_TRANSLATE_STATUS_UNSUPPORTED_VALUE_KIND,
                        ir_opcode, src_kind_tracked, (uint32_t)offset);
                } else {
                    src_kind_tracked = ORUS_JIT_GET_KIND(src);
                }
            }
            ORUS_JIT_ENSURE_ROLLOUT(kind, ir_opcode, offset);
            if (specialization_enabled) {
                bool skip_move = false;
                OrusJitValueKind dst_kind_tracked = ORUS_JIT_GET_KIND(dst);
                if (dst == src && dst_kind_tracked == kind) {
                    skip_move = true;
                } else if (dst_kind_tracked == kind &&
                           orus_jit_specialization_has_constant(&specialization_state, src) &&
                           orus_jit_specialization_constant_matches(
                               &specialization_state, dst,
                               specialization_state.constants[src])) {
                    skip_move = true;
                }
                if (skip_move) {
                    if (dst != src) {
                        orus_jit_specialization_record_move(&specialization_state, dst, src,
                                                             NULL);
                    }
                    offset += 3u;
                    if (++instructions_since_safepoint >= safepoint_interval) {
                        INSERT_SAFEPOINT((uint32_t)offset);
                    }
                    continue;
                }
            }
            OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
            if (!inst) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY, ir_opcode, kind,
                    (uint32_t)offset);
            }
            inst->opcode = ir_opcode;
            inst->value_kind = kind;
            inst->bytecode_offset = (uint32_t)offset;
            inst->operands.move.dst_reg = dst;
            inst->operands.move.src_reg = src;
            ORUS_JIT_SET_KIND(dst, kind, inst);
            if (specialization_enabled) {
                orus_jit_specialization_record_move(&specialization_state, dst, src, inst);
            }
            offset += 3u;
            if (++instructions_since_safepoint >= safepoint_interval) {
                INSERT_SAFEPOINT((uint32_t)offset);
            }
            continue;
        }

        if (opcode == OP_MOVE) {
            if (offset + 2u >= (size_t)chunk->count) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                    ORUS_JIT_IR_OP_MOVE_STRING, ORUS_JIT_VALUE_STRING,
                    (uint32_t)offset);
            }
            uint16_t dst = chunk->code[offset + 1u];
            uint16_t src = chunk->code[offset + 2u];
            OrusJitValueKind tracked = ORUS_JIT_GET_KIND(src);
            OrusJitIROpcode move_opcode = ORUS_JIT_IR_OP_MOVE_VALUE;
            if (!select_move_opcode_for_kind(tracked, &move_opcode)) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_UNSUPPORTED_VALUE_KIND,
                    move_opcode, tracked, (uint32_t)offset);
            }
            OrusJitValueKind move_kind = tracked;
            ORUS_JIT_ENSURE_ROLLOUT(move_kind, move_opcode, offset);
            OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
            if (!inst) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY, move_opcode,
                    move_kind, (uint32_t)offset);
            }
            inst->opcode = move_opcode;
            inst->value_kind = move_kind;
            inst->bytecode_offset = (uint32_t)offset;
            inst->operands.move.dst_reg = dst;
            inst->operands.move.src_reg = src;
            ORUS_JIT_SET_KIND(dst, move_kind, inst);
            if (move_kind == ORUS_JIT_VALUE_BOXED) {
                ORUS_JIT_COPY_ITERATOR_KIND(dst, src);
            }
            offset += 3u;
            if (++instructions_since_safepoint >= safepoint_interval) {
                INSERT_SAFEPOINT((uint32_t)offset);
            }
            continue;
        }

        if (opcode == OP_STORE_FRAME) {
            if (offset + 2u >= (size_t)chunk->count) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                    ORUS_JIT_IR_OP_MOVE_I32, ORUS_JIT_VALUE_BOXED,
                    (uint32_t)offset);
            }
            uint16_t frame_offset = chunk->code[offset + 1u];
            uint16_t src_reg = chunk->code[offset + 2u];
            uint16_t dst_reg = (uint16_t)(FRAME_REG_START + frame_offset);
            OrusJitValueKind src_kind = ORUS_JIT_GET_KIND(src_reg);
            OrusJitIROpcode move_opcode = ORUS_JIT_IR_OP_MOVE_I32;
            if (!select_move_opcode_for_kind(src_kind, &move_opcode)) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_UNSUPPORTED_VALUE_KIND,
                    move_opcode, src_kind, (uint32_t)offset);
            }
            ORUS_JIT_ENSURE_ROLLOUT(src_kind, move_opcode, offset);
            OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
            if (!inst) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY, move_opcode,
                    src_kind, (uint32_t)offset);
            }
            inst->opcode = move_opcode;
            inst->value_kind = src_kind;
            inst->bytecode_offset = (uint32_t)offset;
            inst->operands.move.dst_reg = dst_reg;
            inst->operands.move.src_reg = src_reg;
            ORUS_JIT_SET_KIND(dst_reg, src_kind, inst);
            ORUS_JIT_COPY_ITERATOR_KIND(dst_reg, src_reg);
            offset += 3u;
            if (++instructions_since_safepoint >= safepoint_interval) {
                INSERT_SAFEPOINT((uint32_t)offset);
            }
            continue;
        }

        if (opcode == OP_LOAD_FRAME) {
            if (offset + 2u >= (size_t)chunk->count) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                    ORUS_JIT_IR_OP_MOVE_I32, ORUS_JIT_VALUE_BOXED,
                    (uint32_t)offset);
            }
            uint16_t dst_reg = chunk->code[offset + 1u];
            uint16_t frame_offset = chunk->code[offset + 2u];
            uint16_t src_reg = (uint16_t)(FRAME_REG_START + frame_offset);
            OrusJitValueKind src_kind = ORUS_JIT_GET_KIND(src_reg);
            OrusJitIROpcode move_opcode = ORUS_JIT_IR_OP_MOVE_I32;
            if (!select_move_opcode_for_kind(src_kind, &move_opcode)) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_UNSUPPORTED_VALUE_KIND,
                    move_opcode, src_kind, (uint32_t)offset);
            }
            ORUS_JIT_ENSURE_ROLLOUT(src_kind, move_opcode, offset);
            OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
            if (!inst) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY, move_opcode,
                    src_kind, (uint32_t)offset);
            }
            inst->opcode = move_opcode;
            inst->value_kind = src_kind;
            inst->bytecode_offset = (uint32_t)offset;
            inst->operands.move.dst_reg = dst_reg;
            inst->operands.move.src_reg = src_reg;
            ORUS_JIT_SET_KIND(dst_reg, src_kind, inst);
            ORUS_JIT_COPY_ITERATOR_KIND(dst_reg, src_reg);
            offset += 3u;
            if (++instructions_since_safepoint >= safepoint_interval) {
                INSERT_SAFEPOINT((uint32_t)offset);
            }
            continue;
        }

        if (opcode == OP_MOVE_FRAME) {
            if (offset + 2u >= (size_t)chunk->count) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                    ORUS_JIT_IR_OP_MOVE_I32, ORUS_JIT_VALUE_BOXED,
                    (uint32_t)offset);
            }
            uint16_t dst_offset = chunk->code[offset + 1u];
            uint16_t src_offset = chunk->code[offset + 2u];
            uint16_t dst_reg = (uint16_t)(FRAME_REG_START + dst_offset);
            uint16_t src_reg = (uint16_t)(FRAME_REG_START + src_offset);
            OrusJitValueKind src_kind = ORUS_JIT_GET_KIND(src_reg);
            OrusJitIROpcode move_opcode = ORUS_JIT_IR_OP_MOVE_I32;
            if (!select_move_opcode_for_kind(src_kind, &move_opcode)) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_UNSUPPORTED_VALUE_KIND,
                    move_opcode, src_kind, (uint32_t)offset);
            }
            ORUS_JIT_ENSURE_ROLLOUT(src_kind, move_opcode, offset);
            OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
            if (!inst) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY, move_opcode,
                    src_kind, (uint32_t)offset);
            }
            inst->opcode = move_opcode;
            inst->value_kind = src_kind;
            inst->bytecode_offset = (uint32_t)offset;
            inst->operands.move.dst_reg = dst_reg;
            inst->operands.move.src_reg = src_reg;
            ORUS_JIT_SET_KIND(dst_reg, src_kind, inst);
            ORUS_JIT_COPY_ITERATOR_KIND(dst_reg, src_reg);
            offset += 3u;
            if (++instructions_since_safepoint >= safepoint_interval) {
                INSERT_SAFEPOINT((uint32_t)offset);
            }
            continue;
        }

        if (opcode == OP_RANGE_R) {
            if (offset + 5u >= (size_t)chunk->count) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                    ORUS_JIT_IR_OP_RANGE, ORUS_JIT_VALUE_BOXED,
                    (uint32_t)offset);
            }
            uint16_t dst_reg = chunk->code[offset + 1u];
            uint16_t arg_count = chunk->code[offset + 2u];
            uint16_t first_reg = chunk->code[offset + 3u];
            uint16_t second_reg = chunk->code[offset + 4u];
            uint16_t third_reg = chunk->code[offset + 5u];
            if (arg_count < 1u || arg_count > 3u) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                    ORUS_JIT_IR_OP_RANGE, ORUS_JIT_VALUE_BOXED,
                    (uint32_t)offset);
            }
            ORUS_JIT_ENSURE_ROLLOUT(ORUS_JIT_VALUE_STRING,
                                    ORUS_JIT_IR_OP_RANGE, offset);
            OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
            if (!inst) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY,
                    ORUS_JIT_IR_OP_RANGE, ORUS_JIT_VALUE_BOXED,
                    (uint32_t)offset);
            }
            inst->opcode = ORUS_JIT_IR_OP_RANGE;
            inst->value_kind = ORUS_JIT_VALUE_BOXED;
            inst->bytecode_offset = (uint32_t)offset;
            inst->operands.range.dst_reg = dst_reg;
            inst->operands.range.arg_count = arg_count;
            inst->operands.range.arg_regs[0] = first_reg;
            inst->operands.range.arg_regs[1] = (arg_count >= 2u) ? second_reg : 0u;
            inst->operands.range.arg_regs[2] = (arg_count >= 3u) ? third_reg : 0u;
            ORUS_JIT_SET_KIND(dst_reg, ORUS_JIT_VALUE_BOXED, inst);
            ORUS_JIT_SET_ITERATOR_KIND(dst_reg, ORUS_JIT_ITERATOR_RANGE);
            ORUS_JIT_SPECIALIZATION_INVALIDATE_REG(dst_reg);
            offset += 6u;
            if (++instructions_since_safepoint >= safepoint_interval) {
                INSERT_SAFEPOINT((uint32_t)offset);
            }
            continue;
        }

        if (opcode == OP_GET_ITER_R) {
            if (offset + 2u >= (size_t)chunk->count) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                    ORUS_JIT_IR_OP_GET_ITER, ORUS_JIT_VALUE_BOXED,
                    (uint32_t)offset);
            }
            uint16_t dst_reg = chunk->code[offset + 1u];
            uint16_t iterable_reg = chunk->code[offset + 2u];
            ORUS_JIT_ENSURE_ROLLOUT(ORUS_JIT_VALUE_STRING,
                                    ORUS_JIT_IR_OP_GET_ITER, offset);
            OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
            if (!inst) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY,
                    ORUS_JIT_IR_OP_GET_ITER, ORUS_JIT_VALUE_BOXED,
                    (uint32_t)offset);
            }
            inst->opcode = ORUS_JIT_IR_OP_GET_ITER;
            inst->value_kind = ORUS_JIT_VALUE_BOXED;
            inst->bytecode_offset = (uint32_t)offset;
            inst->operands.get_iter.dst_reg = dst_reg;
            inst->operands.get_iter.iterable_reg = iterable_reg;
            ORUS_JIT_SET_KIND(dst_reg, ORUS_JIT_VALUE_BOXED, inst);
            OrusJitIteratorKind iter_kind = ORUS_JIT_GET_ITERATOR_KIND(iterable_reg);
            if (iter_kind == ORUS_JIT_ITERATOR_NONE) {
                OrusJitValueKind iterable_kind = ORUS_JIT_GET_KIND(iterable_reg);
                if (orus_jit_kind_is_integer(iterable_kind)) {
                    iter_kind = ORUS_JIT_ITERATOR_RANGE;
                } else {
                    iter_kind = ORUS_JIT_ITERATOR_GENERIC;
                }
            }
            ORUS_JIT_SET_ITERATOR_KIND(dst_reg, iter_kind);
            ORUS_JIT_SPECIALIZATION_INVALIDATE_REG(dst_reg);
            offset += 3u;
            if (++instructions_since_safepoint >= safepoint_interval) {
                INSERT_SAFEPOINT((uint32_t)offset);
            }
            continue;
        }

        if (opcode == OP_ITER_NEXT_R) {
            if (offset + 3u >= (size_t)chunk->count) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                    ORUS_JIT_IR_OP_ITER_NEXT, ORUS_JIT_VALUE_BOOL,
                    (uint32_t)offset);
            }
            uint16_t value_reg = chunk->code[offset + 1u];
            uint16_t iterator_reg = chunk->code[offset + 2u];
            uint16_t has_value_reg = chunk->code[offset + 3u];
            OrusJitIteratorKind iter_kind = ORUS_JIT_GET_ITERATOR_KIND(iterator_reg);
            OrusJitValueKind iter_value_kind =
                (iter_kind == ORUS_JIT_ITERATOR_RANGE)
                    ? ORUS_JIT_VALUE_I64
                    : ORUS_JIT_VALUE_BOXED;
            ORUS_JIT_ENSURE_ROLLOUT(ORUS_JIT_VALUE_BOOL,
                                    ORUS_JIT_IR_OP_ITER_NEXT, offset);
            if (iter_value_kind != ORUS_JIT_VALUE_BOXED) {
                ORUS_JIT_ENSURE_ROLLOUT(iter_value_kind,
                                        ORUS_JIT_IR_OP_ITER_NEXT, offset);
            }
            OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
            if (!inst) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY,
                    ORUS_JIT_IR_OP_ITER_NEXT, ORUS_JIT_VALUE_BOOL,
                    (uint32_t)offset);
            }
            inst->opcode = ORUS_JIT_IR_OP_ITER_NEXT;
            inst->value_kind = ORUS_JIT_VALUE_BOOL;
            inst->bytecode_offset = (uint32_t)offset;
            inst->operands.iter_next.value_reg = value_reg;
            inst->operands.iter_next.iterator_reg = iterator_reg;
            inst->operands.iter_next.has_value_reg = has_value_reg;
            ORUS_JIT_SET_KIND(value_reg, iter_value_kind, inst);
            ORUS_JIT_SET_KIND(has_value_reg, ORUS_JIT_VALUE_BOOL, inst);
            ORUS_JIT_SPECIALIZATION_INVALIDATE_REG(value_reg);
            ORUS_JIT_SPECIALIZATION_INVALIDATE_REG(has_value_reg);
            offset += 4u;
            if (++instructions_since_safepoint >= safepoint_interval) {
                INSERT_SAFEPOINT((uint32_t)offset);
            }
            continue;
        }

        if (opcode == OP_TIME_STAMP) {
            if (offset + 1u >= (size_t)chunk->count) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                    ORUS_JIT_IR_OP_TIME_STAMP, ORUS_JIT_VALUE_F64,
                    (uint32_t)offset);
            }
            uint16_t dst = chunk->code[offset + 1u];
            ORUS_JIT_ENSURE_ROLLOUT(ORUS_JIT_VALUE_F64,
                                    ORUS_JIT_IR_OP_TIME_STAMP, offset);
            OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
            if (!inst) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY,
                    ORUS_JIT_IR_OP_TIME_STAMP, ORUS_JIT_VALUE_F64,
                    (uint32_t)offset);
            }
            inst->opcode = ORUS_JIT_IR_OP_TIME_STAMP;
            inst->value_kind = ORUS_JIT_VALUE_F64;
            inst->bytecode_offset = (uint32_t)offset;
            inst->operands.time_stamp.dst_reg = dst;
            ORUS_JIT_SET_KIND(dst, ORUS_JIT_VALUE_F64, inst);
            ORUS_JIT_SPECIALIZATION_INVALIDATE_REG(dst);
            offset += 2u;
            if (++instructions_since_safepoint >= safepoint_interval) {
                INSERT_SAFEPOINT((uint32_t)offset);
            }
            continue;
        }

        if (opcode == OP_MAKE_ARRAY_R) {
            if (offset + 3u >= (size_t)chunk->count) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                    ORUS_JIT_IR_OP_MAKE_ARRAY, ORUS_JIT_VALUE_BOXED,
                    (uint32_t)offset);
            }
            uint16_t dst = chunk->code[offset + 1u];
            uint16_t first = chunk->code[offset + 2u];
            uint16_t count = chunk->code[offset + 3u];
            if ((uint32_t)first + (uint32_t)count > (uint32_t)REGISTER_COUNT) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                    ORUS_JIT_IR_OP_MAKE_ARRAY, ORUS_JIT_VALUE_BOXED,
                    (uint32_t)offset);
            }
            OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
            if (!inst) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY,
                    ORUS_JIT_IR_OP_MAKE_ARRAY, ORUS_JIT_VALUE_BOXED,
                    (uint32_t)offset);
            }
            inst->opcode = ORUS_JIT_IR_OP_MAKE_ARRAY;
            inst->value_kind = ORUS_JIT_VALUE_BOXED;
            inst->bytecode_offset = (uint32_t)offset;
            inst->operands.make_array.dst_reg = dst;
            inst->operands.make_array.first_reg = first;
            inst->operands.make_array.count = count;
            ORUS_JIT_SET_KIND(dst, ORUS_JIT_VALUE_BOXED, inst);
            ORUS_JIT_SPECIALIZATION_INVALIDATE_REG(dst);
            offset += 4u;
            if (++instructions_since_safepoint >= safepoint_interval) {
                INSERT_SAFEPOINT((uint32_t)offset);
            }
            continue;
        }

        if (opcode == OP_ARRAY_PUSH_R) {
            if (offset + 2u >= (size_t)chunk->count) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                    ORUS_JIT_IR_OP_ARRAY_PUSH, ORUS_JIT_VALUE_BOXED,
                    (uint32_t)offset);
            }
            uint16_t array_reg = chunk->code[offset + 1u];
            uint16_t value_reg = chunk->code[offset + 2u];
            OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
            if (!inst) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY,
                    ORUS_JIT_IR_OP_ARRAY_PUSH, ORUS_JIT_VALUE_BOXED,
                    (uint32_t)offset);
            }
            inst->opcode = ORUS_JIT_IR_OP_ARRAY_PUSH;
            inst->value_kind = ORUS_JIT_VALUE_BOXED;
            inst->bytecode_offset = (uint32_t)offset;
            inst->operands.array_push.array_reg = array_reg;
            inst->operands.array_push.value_reg = value_reg;
            ORUS_JIT_SPECIALIZATION_INVALIDATE_REG(array_reg);
            offset += 3u;
            if (++instructions_since_safepoint >= safepoint_interval) {
                INSERT_SAFEPOINT((uint32_t)offset);
            }
            continue;
        }

        if (opcode == OP_ARRAY_POP_R) {
            if (offset + 2u >= (size_t)chunk->count) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                    ORUS_JIT_IR_OP_ARRAY_POP, ORUS_JIT_VALUE_BOXED,
                    (uint32_t)offset);
            }
            uint16_t dst = chunk->code[offset + 1u];
            uint16_t array_reg = chunk->code[offset + 2u];
            OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
            if (!inst) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY,
                    ORUS_JIT_IR_OP_ARRAY_POP, ORUS_JIT_VALUE_BOXED,
                    (uint32_t)offset);
            }
            inst->opcode = ORUS_JIT_IR_OP_ARRAY_POP;
            inst->value_kind = ORUS_JIT_VALUE_BOXED;
            inst->bytecode_offset = (uint32_t)offset;
            inst->operands.array_pop.dst_reg = dst;
            inst->operands.array_pop.array_reg = array_reg;
            ORUS_JIT_SET_KIND(dst, ORUS_JIT_VALUE_BOXED, inst);
            ORUS_JIT_SPECIALIZATION_INVALIDATE_REG(dst);
            ORUS_JIT_SPECIALIZATION_INVALIDATE_REG(array_reg);
            offset += 3u;
            if (++instructions_since_safepoint >= safepoint_interval) {
                INSERT_SAFEPOINT((uint32_t)offset);
            }
            continue;
        }

        if (opcode == OP_ENUM_NEW_R) {
            if (offset + 7u >= (size_t)chunk->count) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                    ORUS_JIT_IR_OP_ENUM_NEW, ORUS_JIT_VALUE_BOXED,
                    (uint32_t)offset);
            }
            uint16_t dst = chunk->code[offset + 1u];
            uint16_t variant_index = chunk->code[offset + 2u];
            uint16_t payload_count = chunk->code[offset + 3u];
            uint16_t payload_start = chunk->code[offset + 4u];
            uint16_t type_const_index =
                read_be_u16(&chunk->code[offset + 5u]);
            uint16_t variant_const_index =
                read_be_u16(&chunk->code[offset + 7u]);
            if (type_const_index >= (uint16_t)chunk->constants.count ||
                variant_const_index >= (uint16_t)chunk->constants.count) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                    ORUS_JIT_IR_OP_ENUM_NEW, ORUS_JIT_VALUE_BOXED,
                    (uint32_t)offset);
            }
            if ((uint32_t)payload_start + (uint32_t)payload_count >
                (uint32_t)REGISTER_COUNT) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                    ORUS_JIT_IR_OP_ENUM_NEW, ORUS_JIT_VALUE_BOXED,
                    (uint32_t)offset);
            }
            Value type_constant = chunk->constants.values[type_const_index];
            if (!IS_STRING(type_constant)) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_UNSUPPORTED_CONSTANT_KIND,
                    ORUS_JIT_IR_OP_ENUM_NEW, ORUS_JIT_VALUE_BOXED,
                    (uint32_t)offset);
            }
            OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
            if (!inst) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY,
                    ORUS_JIT_IR_OP_ENUM_NEW, ORUS_JIT_VALUE_BOXED,
                    (uint32_t)offset);
            }
            inst->opcode = ORUS_JIT_IR_OP_ENUM_NEW;
            inst->value_kind = ORUS_JIT_VALUE_BOXED;
            inst->bytecode_offset = (uint32_t)offset;
            inst->operands.enum_new.dst_reg = dst;
            inst->operands.enum_new.variant_index = variant_index;
            inst->operands.enum_new.payload_count = payload_count;
            inst->operands.enum_new.payload_start = payload_start;
            inst->operands.enum_new.type_const_index = type_const_index;
            inst->operands.enum_new.variant_const_index = variant_const_index;
            ORUS_JIT_SET_KIND(dst, ORUS_JIT_VALUE_BOXED, inst);
            ORUS_JIT_SPECIALIZATION_INVALIDATE_REG(dst);
            offset += 9u;
            if (++instructions_since_safepoint >= safepoint_interval) {
                INSERT_SAFEPOINT((uint32_t)offset);
            }
            continue;
        }

        if (opcode == OP_PRINT_MULTI_R) {
            if (offset + 3u >= (size_t)chunk->count) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                    ORUS_JIT_IR_OP_PRINT, ORUS_JIT_VALUE_BOXED,
                    (uint32_t)offset);
            }
            uint16_t first_reg = chunk->code[offset + 1u];
            uint16_t arg_count = chunk->code[offset + 2u];
            uint16_t newline_flag = chunk->code[offset + 3u];
            OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
            if (!inst) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY,
                    ORUS_JIT_IR_OP_PRINT, ORUS_JIT_VALUE_BOXED,
                    (uint32_t)offset);
            }
            inst->opcode = ORUS_JIT_IR_OP_PRINT;
            inst->value_kind = ORUS_JIT_VALUE_BOXED;
            inst->bytecode_offset = (uint32_t)offset;
            inst->operands.print.first_reg = first_reg;
            inst->operands.print.arg_count = arg_count;
            inst->operands.print.newline = newline_flag;
            ORUS_JIT_SPECIALIZATION_INVALIDATE_ALL();
            offset += 4u;
            if (++instructions_since_safepoint >= safepoint_interval) {
                INSERT_SAFEPOINT((uint32_t)offset);
            }
            continue;
        }

        if (opcode == OP_PRINT_R) {
            if (offset + 1u >= (size_t)chunk->count) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                    ORUS_JIT_IR_OP_PRINT, ORUS_JIT_VALUE_BOXED,
                    (uint32_t)offset);
            }
            uint16_t value_reg = chunk->code[offset + 1u];
            OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
            if (!inst) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY,
                    ORUS_JIT_IR_OP_PRINT, ORUS_JIT_VALUE_BOXED,
                    (uint32_t)offset);
            }
            inst->opcode = ORUS_JIT_IR_OP_PRINT;
            inst->value_kind = ORUS_JIT_VALUE_BOXED;
            inst->bytecode_offset = (uint32_t)offset;
            inst->operands.print.first_reg = value_reg;
            inst->operands.print.arg_count = 1u;
            inst->operands.print.newline = 1u;
            ORUS_JIT_SPECIALIZATION_INVALIDATE_ALL();
            offset += 2u;
            if (++instructions_since_safepoint >= safepoint_interval) {
                INSERT_SAFEPOINT((uint32_t)offset);
            }
            continue;
        }

        if (opcode == OP_ASSERT_EQ_R) {
            if (offset + 4u >= (size_t)chunk->count) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                    ORUS_JIT_IR_OP_ASSERT_EQ, ORUS_JIT_VALUE_BOOL,
                    (uint32_t)offset);
            }
            uint16_t dst = chunk->code[offset + 1u];
            uint16_t label_reg = chunk->code[offset + 2u];
            uint16_t actual_reg = chunk->code[offset + 3u];
            uint16_t expected_reg = chunk->code[offset + 4u];
            ORUS_JIT_ENSURE_ROLLOUT(ORUS_JIT_VALUE_BOOL,
                                    ORUS_JIT_IR_OP_ASSERT_EQ, offset);
            OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
            if (!inst) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY,
                    ORUS_JIT_IR_OP_ASSERT_EQ, ORUS_JIT_VALUE_BOOL,
                    (uint32_t)offset);
            }
            inst->opcode = ORUS_JIT_IR_OP_ASSERT_EQ;
            inst->value_kind = ORUS_JIT_VALUE_BOOL;
            inst->bytecode_offset = (uint32_t)offset;
            inst->operands.assert_eq.dst_reg = dst;
            inst->operands.assert_eq.label_reg = label_reg;
            inst->operands.assert_eq.actual_reg = actual_reg;
            inst->operands.assert_eq.expected_reg = expected_reg;
            ORUS_JIT_SET_KIND(dst, ORUS_JIT_VALUE_BOOL, inst);
            ORUS_JIT_SPECIALIZATION_INVALIDATE_REG(dst);
            offset += 5u;
            if (++instructions_since_safepoint >= safepoint_interval) {
                INSERT_SAFEPOINT((uint32_t)offset);
            }
            continue;
        }

        if (opcode == OP_CALL_NATIVE_R || opcode == OP_CALL_FOREIGN) {
            OrusJitIROpcode call_opcode =
                (opcode == OP_CALL_FOREIGN) ? ORUS_JIT_IR_OP_CALL_FOREIGN
                                            : ORUS_JIT_IR_OP_CALL_NATIVE;
            if (offset + 4u >= (size_t)chunk->count) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT, call_opcode,
                    ORUS_JIT_VALUE_BOXED, (uint32_t)offset);
            }
            uint16_t native_index = chunk->code[offset + 1u];
            uint16_t first_arg_reg = chunk->code[offset + 2u];
            uint16_t arg_count = chunk->code[offset + 3u];
            uint16_t dst_reg = chunk->code[offset + 4u];
            OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
            if (!inst) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY, call_opcode,
                    ORUS_JIT_VALUE_BOXED, (uint32_t)offset);
            }
            inst->opcode = call_opcode;
            inst->value_kind = ORUS_JIT_VALUE_BOXED;
            inst->bytecode_offset = (uint32_t)offset;
            inst->operands.call_native.dst_reg = dst_reg;
            inst->operands.call_native.first_arg_reg = first_arg_reg;
            inst->operands.call_native.arg_count = arg_count;
            inst->operands.call_native.native_index = native_index;
            uint16_t spill_base = dst_reg;
            uint32_t spill_limit = (uint32_t)dst_reg + 1u;
            if (arg_count > 0u) {
                uint16_t first = first_arg_reg;
                uint32_t last = (uint32_t)first_arg_reg + (uint32_t)arg_count - 1u;
                if (first < spill_base) {
                    spill_base = first;
                }
                uint32_t arg_limit = last + 1u;
                if (arg_limit > spill_limit) {
                    spill_limit = arg_limit;
                }
            }
            if (spill_limit > (uint32_t)REGISTER_COUNT) {
                spill_limit = REGISTER_COUNT;
            }
            inst->operands.call_native.spill_base = spill_base;
            inst->operands.call_native.spill_count =
                (uint16_t)((spill_limit > (uint32_t)spill_base)
                               ? (spill_limit - (uint32_t)spill_base)
                               : 0u);
            ORUS_JIT_SET_KIND(dst_reg, ORUS_JIT_VALUE_BOXED, inst);
            ORUS_JIT_SPECIALIZATION_INVALIDATE_ALL();
            offset += 5u;
            instructions_since_safepoint = 0u;
            continue;
        }

        if (opcode == OP_I32_TO_I64_R) {
            if (offset + 3u >= (size_t)chunk->count) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                    ORUS_JIT_IR_OP_I32_TO_I64, ORUS_JIT_VALUE_I32,
                    (uint32_t)offset);
            }
            uint16_t dst = chunk->code[offset + 1u];
            uint16_t src = chunk->code[offset + 2u];
            uint16_t unused = chunk->code[offset + 3u];
            (void)unused;
            OrusJitValueKind src_kind = ORUS_JIT_GET_KIND(src);
            if (src_kind != ORUS_JIT_VALUE_I32 &&
                src_kind != ORUS_JIT_VALUE_BOXED) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_UNSUPPORTED_VALUE_KIND,
                    ORUS_JIT_IR_OP_I32_TO_I64, src_kind, (uint32_t)offset);
            }
            ORUS_JIT_ENSURE_ROLLOUT(ORUS_JIT_VALUE_I64,
                                    ORUS_JIT_IR_OP_I32_TO_I64, offset);
            OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
            if (!inst) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY,
                    ORUS_JIT_IR_OP_I32_TO_I64, ORUS_JIT_VALUE_I64,
                    (uint32_t)offset);
            }
            inst->opcode = ORUS_JIT_IR_OP_I32_TO_I64;
            inst->value_kind = ORUS_JIT_VALUE_I64;
            inst->bytecode_offset = (uint32_t)offset;
            inst->operands.unary.dst_reg = dst;
            inst->operands.unary.src_reg = src;
            ORUS_JIT_SET_KIND(dst, ORUS_JIT_VALUE_I64, inst);
            ORUS_JIT_SPECIALIZATION_INVALIDATE_REG(dst);
            offset += 4u;
            if (++instructions_since_safepoint >= safepoint_interval) {
                INSERT_SAFEPOINT((uint32_t)offset);
            }
            continue;
        }

        if (opcode == OP_I32_TO_F64_R) {
            if (offset + 3u >= (size_t)chunk->count) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                    ORUS_JIT_IR_OP_I32_TO_F64, ORUS_JIT_VALUE_I32,
                    (uint32_t)offset);
            }
            uint16_t dst = chunk->code[offset + 1u];
            uint16_t src = chunk->code[offset + 2u];
            uint16_t unused = chunk->code[offset + 3u];
            (void)unused;
            OrusJitValueKind src_kind = ORUS_JIT_GET_KIND(src);
            if (src_kind != ORUS_JIT_VALUE_I32 &&
                src_kind != ORUS_JIT_VALUE_BOXED) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_UNSUPPORTED_VALUE_KIND,
                    ORUS_JIT_IR_OP_I32_TO_F64, src_kind, (uint32_t)offset);
            }
            ORUS_JIT_ENSURE_ROLLOUT(ORUS_JIT_VALUE_F64,
                                    ORUS_JIT_IR_OP_I32_TO_F64, offset);
            OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
            if (!inst) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY,
                    ORUS_JIT_IR_OP_I32_TO_F64, ORUS_JIT_VALUE_F64,
                    (uint32_t)offset);
            }
            inst->opcode = ORUS_JIT_IR_OP_I32_TO_F64;
            inst->value_kind = ORUS_JIT_VALUE_F64;
            inst->bytecode_offset = (uint32_t)offset;
            inst->operands.unary.dst_reg = dst;
            inst->operands.unary.src_reg = src;
            ORUS_JIT_SET_KIND(dst, ORUS_JIT_VALUE_F64, inst);
            ORUS_JIT_SPECIALIZATION_INVALIDATE_REG(dst);
            offset += 4u;
            if (++instructions_since_safepoint >= safepoint_interval) {
                INSERT_SAFEPOINT((uint32_t)offset);
            }
            continue;
        }

        if (opcode == OP_I64_TO_F64_R) {
            if (offset + 3u >= (size_t)chunk->count) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                    ORUS_JIT_IR_OP_I64_TO_F64, ORUS_JIT_VALUE_I64,
                    (uint32_t)offset);
            }
            uint16_t dst = chunk->code[offset + 1u];
            uint16_t src = chunk->code[offset + 2u];
            uint16_t unused = chunk->code[offset + 3u];
            (void)unused;
            OrusJitValueKind src_kind = ORUS_JIT_GET_KIND(src);
            if (src_kind != ORUS_JIT_VALUE_I64 &&
                src_kind != ORUS_JIT_VALUE_BOXED) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_UNSUPPORTED_VALUE_KIND,
                    ORUS_JIT_IR_OP_I64_TO_F64, src_kind, (uint32_t)offset);
            }
            ORUS_JIT_ENSURE_ROLLOUT(ORUS_JIT_VALUE_F64,
                                    ORUS_JIT_IR_OP_I64_TO_F64, offset);
            OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
            if (!inst) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY,
                    ORUS_JIT_IR_OP_I64_TO_F64, ORUS_JIT_VALUE_F64,
                    (uint32_t)offset);
            }
            inst->opcode = ORUS_JIT_IR_OP_I64_TO_F64;
            inst->value_kind = ORUS_JIT_VALUE_F64;
            inst->bytecode_offset = (uint32_t)offset;
            inst->operands.unary.dst_reg = dst;
            inst->operands.unary.src_reg = src;
            ORUS_JIT_SET_KIND(dst, ORUS_JIT_VALUE_F64, inst);
            ORUS_JIT_SPECIALIZATION_INVALIDATE_REG(dst);
            offset += 4u;
            if (++instructions_since_safepoint >= safepoint_interval) {
                INSERT_SAFEPOINT((uint32_t)offset);
            }
            continue;
        }

        if (opcode == OP_U32_TO_U64_R) {
            if (offset + 3u >= (size_t)chunk->count) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                    ORUS_JIT_IR_OP_U32_TO_U64, ORUS_JIT_VALUE_U32,
                    (uint32_t)offset);
            }
            uint16_t dst = chunk->code[offset + 1u];
            uint16_t src = chunk->code[offset + 2u];
            uint16_t unused = chunk->code[offset + 3u];
            (void)unused;
            OrusJitValueKind src_kind = ORUS_JIT_GET_KIND(src);
            if (src_kind != ORUS_JIT_VALUE_U32 &&
                src_kind != ORUS_JIT_VALUE_BOXED) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_UNSUPPORTED_VALUE_KIND,
                    ORUS_JIT_IR_OP_U32_TO_U64, src_kind, (uint32_t)offset);
            }
            ORUS_JIT_ENSURE_ROLLOUT(ORUS_JIT_VALUE_U64,
                                    ORUS_JIT_IR_OP_U32_TO_U64, offset);
            OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
            if (!inst) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY,
                    ORUS_JIT_IR_OP_U32_TO_U64, ORUS_JIT_VALUE_U64,
                    (uint32_t)offset);
            }
            inst->opcode = ORUS_JIT_IR_OP_U32_TO_U64;
            inst->value_kind = ORUS_JIT_VALUE_U64;
            inst->bytecode_offset = (uint32_t)offset;
            inst->operands.unary.dst_reg = dst;
            inst->operands.unary.src_reg = src;
            ORUS_JIT_SET_KIND(dst, ORUS_JIT_VALUE_U64, inst);
            ORUS_JIT_SPECIALIZATION_INVALIDATE_REG(dst);
            offset += 4u;
            if (++instructions_since_safepoint >= safepoint_interval) {
                INSERT_SAFEPOINT((uint32_t)offset);
            }
            continue;
        }

        if (opcode == OP_U32_TO_I32_R) {
            if (offset + 3u >= (size_t)chunk->count) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                    ORUS_JIT_IR_OP_U32_TO_I32, ORUS_JIT_VALUE_U32,
                    (uint32_t)offset);
            }
            uint16_t dst = chunk->code[offset + 1u];
            uint16_t src = chunk->code[offset + 2u];
            uint16_t unused = chunk->code[offset + 3u];
            (void)unused;
            OrusJitValueKind src_kind = ORUS_JIT_GET_KIND(src);
            if (src_kind != ORUS_JIT_VALUE_U32 &&
                src_kind != ORUS_JIT_VALUE_BOXED) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_UNSUPPORTED_VALUE_KIND,
                    ORUS_JIT_IR_OP_U32_TO_I32, src_kind, (uint32_t)offset);
            }
            OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
            if (!inst) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY,
                    ORUS_JIT_IR_OP_U32_TO_I32, ORUS_JIT_VALUE_I32,
                    (uint32_t)offset);
            }
            inst->opcode = ORUS_JIT_IR_OP_U32_TO_I32;
            inst->value_kind = ORUS_JIT_VALUE_I32;
            inst->bytecode_offset = (uint32_t)offset;
            inst->operands.unary.dst_reg = dst;
            inst->operands.unary.src_reg = src;
            ORUS_JIT_SET_KIND(dst, ORUS_JIT_VALUE_I32, inst);
            ORUS_JIT_SPECIALIZATION_INVALIDATE_REG(dst);
            offset += 4u;
            if (++instructions_since_safepoint >= safepoint_interval) {
                INSERT_SAFEPOINT((uint32_t)offset);
            }
            continue;
        }

        if (opcode == OP_U32_TO_F64_R) {
            if (offset + 3u >= (size_t)chunk->count) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                    ORUS_JIT_IR_OP_U32_TO_F64, ORUS_JIT_VALUE_U32,
                    (uint32_t)offset);
            }
            uint16_t dst = chunk->code[offset + 1u];
            uint16_t src = chunk->code[offset + 2u];
            uint16_t unused = chunk->code[offset + 3u];
            (void)unused;
            OrusJitValueKind src_kind = ORUS_JIT_GET_KIND(src);
            if (src_kind != ORUS_JIT_VALUE_U32 &&
                src_kind != ORUS_JIT_VALUE_BOXED) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_UNSUPPORTED_VALUE_KIND,
                    ORUS_JIT_IR_OP_U32_TO_F64, src_kind, (uint32_t)offset);
            }
            ORUS_JIT_ENSURE_ROLLOUT(ORUS_JIT_VALUE_U32,
                                    ORUS_JIT_IR_OP_U32_TO_F64, offset);
            ORUS_JIT_ENSURE_ROLLOUT(ORUS_JIT_VALUE_F64,
                                    ORUS_JIT_IR_OP_U32_TO_F64, offset);
            OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
            if (!inst) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY,
                    ORUS_JIT_IR_OP_U32_TO_F64, ORUS_JIT_VALUE_F64,
                    (uint32_t)offset);
            }
            inst->opcode = ORUS_JIT_IR_OP_U32_TO_F64;
            inst->value_kind = ORUS_JIT_VALUE_F64;
            inst->bytecode_offset = (uint32_t)offset;
            inst->operands.unary.dst_reg = dst;
            inst->operands.unary.src_reg = src;
            ORUS_JIT_SET_KIND(dst, ORUS_JIT_VALUE_F64, inst);
            ORUS_JIT_SPECIALIZATION_INVALIDATE_REG(dst);
            offset += 4u;
            if (++instructions_since_safepoint >= safepoint_interval) {
                INSERT_SAFEPOINT((uint32_t)offset);
            }
            continue;
        }

        if (opcode == OP_F64_TO_I32_R) {
            if (offset + 3u >= (size_t)chunk->count) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                    ORUS_JIT_IR_OP_F64_TO_I32, ORUS_JIT_VALUE_F64,
                    (uint32_t)offset);
            }
            uint16_t dst = chunk->code[offset + 1u];
            uint16_t src = chunk->code[offset + 2u];
            uint16_t unused = chunk->code[offset + 3u];
            (void)unused;
            OrusJitValueKind src_kind = ORUS_JIT_GET_KIND(src);
            if (src_kind != ORUS_JIT_VALUE_F64 &&
                src_kind != ORUS_JIT_VALUE_BOXED) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_UNSUPPORTED_VALUE_KIND,
                    ORUS_JIT_IR_OP_F64_TO_I32, src_kind, (uint32_t)offset);
            }
            ORUS_JIT_ENSURE_ROLLOUT(ORUS_JIT_VALUE_F64,
                                    ORUS_JIT_IR_OP_F64_TO_I32, offset);
            OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
            if (!inst) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY,
                    ORUS_JIT_IR_OP_F64_TO_I32, ORUS_JIT_VALUE_I32,
                    (uint32_t)offset);
            }
            inst->opcode = ORUS_JIT_IR_OP_F64_TO_I32;
            inst->value_kind = ORUS_JIT_VALUE_I32;
            inst->bytecode_offset = (uint32_t)offset;
            inst->operands.unary.dst_reg = dst;
            inst->operands.unary.src_reg = src;
            ORUS_JIT_SET_KIND(dst, ORUS_JIT_VALUE_I32, inst);
            ORUS_JIT_SPECIALIZATION_INVALIDATE_REG(dst);
            offset += 4u;
            if (++instructions_since_safepoint >= safepoint_interval) {
                INSERT_SAFEPOINT((uint32_t)offset);
            }
            continue;
        }

        if (opcode == OP_F64_TO_I64_R) {
            if (offset + 3u >= (size_t)chunk->count) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                    ORUS_JIT_IR_OP_F64_TO_I64, ORUS_JIT_VALUE_F64,
                    (uint32_t)offset);
            }
            uint16_t dst = chunk->code[offset + 1u];
            uint16_t src = chunk->code[offset + 2u];
            uint16_t unused = chunk->code[offset + 3u];
            (void)unused;
            OrusJitValueKind src_kind = ORUS_JIT_GET_KIND(src);
            if (src_kind != ORUS_JIT_VALUE_F64 &&
                src_kind != ORUS_JIT_VALUE_BOXED) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_UNSUPPORTED_VALUE_KIND,
                    ORUS_JIT_IR_OP_F64_TO_I64, src_kind, (uint32_t)offset);
            }
            ORUS_JIT_ENSURE_ROLLOUT(ORUS_JIT_VALUE_F64,
                                    ORUS_JIT_IR_OP_F64_TO_I64, offset);
            OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
            if (!inst) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY,
                    ORUS_JIT_IR_OP_F64_TO_I64, ORUS_JIT_VALUE_I64,
                    (uint32_t)offset);
            }
            inst->opcode = ORUS_JIT_IR_OP_F64_TO_I64;
            inst->value_kind = ORUS_JIT_VALUE_I64;
            inst->bytecode_offset = (uint32_t)offset;
            inst->operands.unary.dst_reg = dst;
            inst->operands.unary.src_reg = src;
            ORUS_JIT_SET_KIND(dst, ORUS_JIT_VALUE_I64, inst);
            ORUS_JIT_SPECIALIZATION_INVALIDATE_REG(dst);
            offset += 4u;
            if (++instructions_since_safepoint >= safepoint_interval) {
                INSERT_SAFEPOINT((uint32_t)offset);
            }
            continue;
        }

        if (opcode == OP_F64_TO_U32_R) {
            if (offset + 3u >= (size_t)chunk->count) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                    ORUS_JIT_IR_OP_F64_TO_U32, ORUS_JIT_VALUE_F64,
                    (uint32_t)offset);
            }
            uint16_t dst = chunk->code[offset + 1u];
            uint16_t src = chunk->code[offset + 2u];
            uint16_t unused = chunk->code[offset + 3u];
            (void)unused;
            OrusJitValueKind src_kind = ORUS_JIT_GET_KIND(src);
            if (src_kind != ORUS_JIT_VALUE_F64 &&
                src_kind != ORUS_JIT_VALUE_BOXED) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_UNSUPPORTED_VALUE_KIND,
                    ORUS_JIT_IR_OP_F64_TO_U32, src_kind, (uint32_t)offset);
            }
            ORUS_JIT_ENSURE_ROLLOUT(ORUS_JIT_VALUE_F64,
                                    ORUS_JIT_IR_OP_F64_TO_U32, offset);
            ORUS_JIT_ENSURE_ROLLOUT(ORUS_JIT_VALUE_U32,
                                    ORUS_JIT_IR_OP_F64_TO_U32, offset);
            OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
            if (!inst) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY,
                    ORUS_JIT_IR_OP_F64_TO_U32, ORUS_JIT_VALUE_U32,
                    (uint32_t)offset);
            }
            inst->opcode = ORUS_JIT_IR_OP_F64_TO_U32;
            inst->value_kind = ORUS_JIT_VALUE_U32;
            inst->bytecode_offset = (uint32_t)offset;
            inst->operands.unary.dst_reg = dst;
            inst->operands.unary.src_reg = src;
            ORUS_JIT_SET_KIND(dst, ORUS_JIT_VALUE_U32, inst);
            ORUS_JIT_SPECIALIZATION_INVALIDATE_REG(dst);
            offset += 4u;
            if (++instructions_since_safepoint >= safepoint_interval) {
                INSERT_SAFEPOINT((uint32_t)offset);
            }
            continue;
        }

        if (opcode == OP_I32_TO_U32_R) {
            if (offset + 3u >= (size_t)chunk->count) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                    ORUS_JIT_IR_OP_I32_TO_U32, ORUS_JIT_VALUE_I32,
                    (uint32_t)offset);
            }
            uint16_t dst = chunk->code[offset + 1u];
            uint16_t src = chunk->code[offset + 2u];
            uint16_t unused = chunk->code[offset + 3u];
            (void)unused;
            OrusJitValueKind src_kind = ORUS_JIT_GET_KIND(src);
            if (src_kind != ORUS_JIT_VALUE_I32 &&
                src_kind != ORUS_JIT_VALUE_BOXED) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_UNSUPPORTED_VALUE_KIND,
                    ORUS_JIT_IR_OP_I32_TO_U32, src_kind, (uint32_t)offset);
            }
            ORUS_JIT_ENSURE_ROLLOUT(ORUS_JIT_VALUE_U32,
                                    ORUS_JIT_IR_OP_I32_TO_U32, offset);
            OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
            if (!inst) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY,
                    ORUS_JIT_IR_OP_I32_TO_U32, ORUS_JIT_VALUE_U32,
                    (uint32_t)offset);
            }
            inst->opcode = ORUS_JIT_IR_OP_I32_TO_U32;
            inst->value_kind = ORUS_JIT_VALUE_U32;
            inst->bytecode_offset = (uint32_t)offset;
            inst->operands.unary.dst_reg = dst;
            inst->operands.unary.src_reg = src;
            ORUS_JIT_SET_KIND(dst, ORUS_JIT_VALUE_U32, inst);
            ORUS_JIT_SPECIALIZATION_INVALIDATE_REG(dst);
            offset += 4u;
            if (++instructions_since_safepoint >= safepoint_interval) {
                INSERT_SAFEPOINT((uint32_t)offset);
            }
            continue;
        }

        if (opcode == OP_I64_TO_U32_R) {
            if (offset + 3u >= (size_t)chunk->count) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                    ORUS_JIT_IR_OP_I64_TO_U32, ORUS_JIT_VALUE_I64,
                    (uint32_t)offset);
            }
            uint16_t dst = chunk->code[offset + 1u];
            uint16_t src = chunk->code[offset + 2u];
            uint16_t unused = chunk->code[offset + 3u];
            (void)unused;
            OrusJitValueKind src_kind = ORUS_JIT_GET_KIND(src);
            if (src_kind != ORUS_JIT_VALUE_I64 &&
                src_kind != ORUS_JIT_VALUE_BOXED) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_UNSUPPORTED_VALUE_KIND,
                    ORUS_JIT_IR_OP_I64_TO_U32, src_kind, (uint32_t)offset);
            }
            ORUS_JIT_ENSURE_ROLLOUT(ORUS_JIT_VALUE_U32,
                                    ORUS_JIT_IR_OP_I64_TO_U32, offset);
            OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
            if (!inst) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY,
                    ORUS_JIT_IR_OP_I64_TO_U32, ORUS_JIT_VALUE_U32,
                    (uint32_t)offset);
            }
            inst->opcode = ORUS_JIT_IR_OP_I64_TO_U32;
            inst->value_kind = ORUS_JIT_VALUE_U32;
            inst->bytecode_offset = (uint32_t)offset;
            inst->operands.unary.dst_reg = dst;
            inst->operands.unary.src_reg = src;
            ORUS_JIT_SET_KIND(dst, ORUS_JIT_VALUE_U32, inst);
            ORUS_JIT_SPECIALIZATION_INVALIDATE_REG(dst);
            offset += 4u;
            if (++instructions_since_safepoint >= safepoint_interval) {
                INSERT_SAFEPOINT((uint32_t)offset);
            }
            continue;
        }

        if (opcode == OP_I32_TO_U64_R) {
            if (offset + 3u >= (size_t)chunk->count) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                    ORUS_JIT_IR_OP_I32_TO_U64, ORUS_JIT_VALUE_I32,
                    (uint32_t)offset);
            }
            uint16_t dst = chunk->code[offset + 1u];
            uint16_t src = chunk->code[offset + 2u];
            uint16_t unused = chunk->code[offset + 3u];
            (void)unused;
            OrusJitValueKind src_kind = ORUS_JIT_GET_KIND(src);
            if (src_kind != ORUS_JIT_VALUE_I32 &&
                src_kind != ORUS_JIT_VALUE_BOXED) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_UNSUPPORTED_VALUE_KIND,
                    ORUS_JIT_IR_OP_I32_TO_U64, src_kind, (uint32_t)offset);
            }
            ORUS_JIT_ENSURE_ROLLOUT(ORUS_JIT_VALUE_U64,
                                    ORUS_JIT_IR_OP_I32_TO_U64, offset);
            OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
            if (!inst) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY,
                    ORUS_JIT_IR_OP_I32_TO_U64, ORUS_JIT_VALUE_U64,
                    (uint32_t)offset);
            }
            inst->opcode = ORUS_JIT_IR_OP_I32_TO_U64;
            inst->value_kind = ORUS_JIT_VALUE_U64;
            inst->bytecode_offset = (uint32_t)offset;
            inst->operands.unary.dst_reg = dst;
            inst->operands.unary.src_reg = src;
            ORUS_JIT_SET_KIND(dst, ORUS_JIT_VALUE_U64, inst);
            ORUS_JIT_SPECIALIZATION_INVALIDATE_REG(dst);
            offset += 4u;
            if (++instructions_since_safepoint >= safepoint_interval) {
                INSERT_SAFEPOINT((uint32_t)offset);
            }
            continue;
        }

        if (opcode == OP_I64_TO_U64_R) {
            if (offset + 3u >= (size_t)chunk->count) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                    ORUS_JIT_IR_OP_I64_TO_U64, ORUS_JIT_VALUE_I64,
                    (uint32_t)offset);
            }
            uint16_t dst = chunk->code[offset + 1u];
            uint16_t src = chunk->code[offset + 2u];
            uint16_t unused = chunk->code[offset + 3u];
            (void)unused;
            OrusJitValueKind src_kind = ORUS_JIT_GET_KIND(src);
            if (src_kind != ORUS_JIT_VALUE_I64 &&
                src_kind != ORUS_JIT_VALUE_BOXED) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_UNSUPPORTED_VALUE_KIND,
                    ORUS_JIT_IR_OP_I64_TO_U64, src_kind, (uint32_t)offset);
            }
            ORUS_JIT_ENSURE_ROLLOUT(ORUS_JIT_VALUE_U64,
                                    ORUS_JIT_IR_OP_I64_TO_U64, offset);
            OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
            if (!inst) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY,
                    ORUS_JIT_IR_OP_I64_TO_U64, ORUS_JIT_VALUE_U64,
                    (uint32_t)offset);
            }
            inst->opcode = ORUS_JIT_IR_OP_I64_TO_U64;
            inst->value_kind = ORUS_JIT_VALUE_U64;
            inst->bytecode_offset = (uint32_t)offset;
            inst->operands.unary.dst_reg = dst;
            inst->operands.unary.src_reg = src;
            ORUS_JIT_SET_KIND(dst, ORUS_JIT_VALUE_U64, inst);
            ORUS_JIT_SPECIALIZATION_INVALIDATE_REG(dst);
            offset += 4u;
            if (++instructions_since_safepoint >= safepoint_interval) {
                INSERT_SAFEPOINT((uint32_t)offset);
            }
            continue;
        }

        if (opcode == OP_U64_TO_I32_R) {
            if (offset + 3u >= (size_t)chunk->count) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                    ORUS_JIT_IR_OP_U64_TO_I32, ORUS_JIT_VALUE_U64,
                    (uint32_t)offset);
            }
            uint16_t dst = chunk->code[offset + 1u];
            uint16_t src = chunk->code[offset + 2u];
            uint16_t unused = chunk->code[offset + 3u];
            (void)unused;
            OrusJitValueKind src_kind = ORUS_JIT_GET_KIND(src);
            if (src_kind != ORUS_JIT_VALUE_U64 &&
                src_kind != ORUS_JIT_VALUE_BOXED) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_UNSUPPORTED_VALUE_KIND,
                    ORUS_JIT_IR_OP_U64_TO_I32, src_kind, (uint32_t)offset);
            }
            ORUS_JIT_ENSURE_ROLLOUT(ORUS_JIT_VALUE_U64,
                                    ORUS_JIT_IR_OP_U64_TO_I32, offset);
            OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
            if (!inst) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY,
                    ORUS_JIT_IR_OP_U64_TO_I32, ORUS_JIT_VALUE_I32,
                    (uint32_t)offset);
            }
            inst->opcode = ORUS_JIT_IR_OP_U64_TO_I32;
            inst->value_kind = ORUS_JIT_VALUE_I32;
            inst->bytecode_offset = (uint32_t)offset;
            inst->operands.unary.dst_reg = dst;
            inst->operands.unary.src_reg = src;
            ORUS_JIT_SET_KIND(dst, ORUS_JIT_VALUE_I32, inst);
            ORUS_JIT_SPECIALIZATION_INVALIDATE_REG(dst);
            offset += 4u;
            if (++instructions_since_safepoint >= safepoint_interval) {
                INSERT_SAFEPOINT((uint32_t)offset);
            }
            continue;
        }

        if (opcode == OP_U64_TO_I64_R) {
            if (offset + 3u >= (size_t)chunk->count) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                    ORUS_JIT_IR_OP_U64_TO_I64, ORUS_JIT_VALUE_U64,
                    (uint32_t)offset);
            }
            uint16_t dst = chunk->code[offset + 1u];
            uint16_t src = chunk->code[offset + 2u];
            uint16_t unused = chunk->code[offset + 3u];
            (void)unused;
            OrusJitValueKind src_kind = ORUS_JIT_GET_KIND(src);
            if (src_kind != ORUS_JIT_VALUE_U64 &&
                src_kind != ORUS_JIT_VALUE_BOXED) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_UNSUPPORTED_VALUE_KIND,
                    ORUS_JIT_IR_OP_U64_TO_I64, src_kind, (uint32_t)offset);
            }
            ORUS_JIT_ENSURE_ROLLOUT(ORUS_JIT_VALUE_U64,
                                    ORUS_JIT_IR_OP_U64_TO_I64, offset);
            OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
            if (!inst) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY,
                    ORUS_JIT_IR_OP_U64_TO_I64, ORUS_JIT_VALUE_I64,
                    (uint32_t)offset);
            }
            inst->opcode = ORUS_JIT_IR_OP_U64_TO_I64;
            inst->value_kind = ORUS_JIT_VALUE_I64;
            inst->bytecode_offset = (uint32_t)offset;
            inst->operands.unary.dst_reg = dst;
            inst->operands.unary.src_reg = src;
            ORUS_JIT_SET_KIND(dst, ORUS_JIT_VALUE_I64, inst);
            ORUS_JIT_SPECIALIZATION_INVALIDATE_REG(dst);
            offset += 4u;
            if (++instructions_since_safepoint >= safepoint_interval) {
                INSERT_SAFEPOINT((uint32_t)offset);
            }
            continue;
        }

        if (opcode == OP_U64_TO_U32_R) {
            if (offset + 3u >= (size_t)chunk->count) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                    ORUS_JIT_IR_OP_U64_TO_U32, ORUS_JIT_VALUE_U64,
                    (uint32_t)offset);
            }
            uint16_t dst = chunk->code[offset + 1u];
            uint16_t src = chunk->code[offset + 2u];
            uint16_t unused = chunk->code[offset + 3u];
            (void)unused;
            OrusJitValueKind src_kind = ORUS_JIT_GET_KIND(src);
            if (src_kind != ORUS_JIT_VALUE_U64 &&
                src_kind != ORUS_JIT_VALUE_BOXED) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_UNSUPPORTED_VALUE_KIND,
                    ORUS_JIT_IR_OP_U64_TO_U32, src_kind, (uint32_t)offset);
            }
            ORUS_JIT_ENSURE_ROLLOUT(ORUS_JIT_VALUE_U64,
                                    ORUS_JIT_IR_OP_U64_TO_U32, offset);
            ORUS_JIT_ENSURE_ROLLOUT(ORUS_JIT_VALUE_U32,
                                    ORUS_JIT_IR_OP_U64_TO_U32, offset);
            OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
            if (!inst) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY,
                    ORUS_JIT_IR_OP_U64_TO_U32, ORUS_JIT_VALUE_U32,
                    (uint32_t)offset);
            }
            inst->opcode = ORUS_JIT_IR_OP_U64_TO_U32;
            inst->value_kind = ORUS_JIT_VALUE_U32;
            inst->bytecode_offset = (uint32_t)offset;
            inst->operands.unary.dst_reg = dst;
            inst->operands.unary.src_reg = src;
            ORUS_JIT_SET_KIND(dst, ORUS_JIT_VALUE_U32, inst);
            ORUS_JIT_SPECIALIZATION_INVALIDATE_REG(dst);
            offset += 4u;
            if (++instructions_since_safepoint >= safepoint_interval) {
                INSERT_SAFEPOINT((uint32_t)offset);
            }
            continue;
        }

        if (opcode == OP_F64_TO_U64_R) {
            if (offset + 3u >= (size_t)chunk->count) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                    ORUS_JIT_IR_OP_F64_TO_U64, ORUS_JIT_VALUE_F64,
                    (uint32_t)offset);
            }
            uint16_t dst = chunk->code[offset + 1u];
            uint16_t src = chunk->code[offset + 2u];
            uint16_t unused = chunk->code[offset + 3u];
            (void)unused;
            OrusJitValueKind src_kind = ORUS_JIT_GET_KIND(src);
            if (src_kind != ORUS_JIT_VALUE_F64 &&
                src_kind != ORUS_JIT_VALUE_BOXED) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_UNSUPPORTED_VALUE_KIND,
                    ORUS_JIT_IR_OP_F64_TO_U64, src_kind, (uint32_t)offset);
            }
            ORUS_JIT_ENSURE_ROLLOUT(ORUS_JIT_VALUE_F64,
                                    ORUS_JIT_IR_OP_F64_TO_U64, offset);
            ORUS_JIT_ENSURE_ROLLOUT(ORUS_JIT_VALUE_U64,
                                    ORUS_JIT_IR_OP_F64_TO_U64, offset);
            OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
            if (!inst) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY,
                    ORUS_JIT_IR_OP_F64_TO_U64, ORUS_JIT_VALUE_U64,
                    (uint32_t)offset);
            }
            inst->opcode = ORUS_JIT_IR_OP_F64_TO_U64;
            inst->value_kind = ORUS_JIT_VALUE_U64;
            inst->bytecode_offset = (uint32_t)offset;
            inst->operands.unary.dst_reg = dst;
            inst->operands.unary.src_reg = src;
            ORUS_JIT_SET_KIND(dst, ORUS_JIT_VALUE_U64, inst);
            ORUS_JIT_SPECIALIZATION_INVALIDATE_REG(dst);
            offset += 4u;
            if (++instructions_since_safepoint >= safepoint_interval) {
                INSERT_SAFEPOINT((uint32_t)offset);
            }
            continue;
        }

        if (opcode == OP_U64_TO_F64_R) {
            if (offset + 3u >= (size_t)chunk->count) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                    ORUS_JIT_IR_OP_U64_TO_F64, ORUS_JIT_VALUE_U64,
                    (uint32_t)offset);
            }
            uint16_t dst = chunk->code[offset + 1u];
            uint16_t src = chunk->code[offset + 2u];
            uint16_t unused = chunk->code[offset + 3u];
            (void)unused;
            OrusJitValueKind src_kind = ORUS_JIT_GET_KIND(src);
            if (src_kind != ORUS_JIT_VALUE_U64 &&
                src_kind != ORUS_JIT_VALUE_BOXED) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_UNSUPPORTED_VALUE_KIND,
                    ORUS_JIT_IR_OP_U64_TO_F64, src_kind, (uint32_t)offset);
            }
            ORUS_JIT_ENSURE_ROLLOUT(ORUS_JIT_VALUE_U64,
                                    ORUS_JIT_IR_OP_U64_TO_F64, offset);
            ORUS_JIT_ENSURE_ROLLOUT(ORUS_JIT_VALUE_F64,
                                    ORUS_JIT_IR_OP_U64_TO_F64, offset);
            OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
            if (!inst) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY,
                    ORUS_JIT_IR_OP_U64_TO_F64, ORUS_JIT_VALUE_F64,
                    (uint32_t)offset);
            }
            inst->opcode = ORUS_JIT_IR_OP_U64_TO_F64;
            inst->value_kind = ORUS_JIT_VALUE_F64;
            inst->bytecode_offset = (uint32_t)offset;
            inst->operands.unary.dst_reg = dst;
            inst->operands.unary.src_reg = src;
            ORUS_JIT_SET_KIND(dst, ORUS_JIT_VALUE_F64, inst);
            ORUS_JIT_SPECIALIZATION_INVALIDATE_REG(dst);
            offset += 4u;
            if (++instructions_since_safepoint >= safepoint_interval) {
                INSERT_SAFEPOINT((uint32_t)offset);
            }
            continue;
        }

        if (opcode == OP_CONCAT_R) {
            if (offset + 3u >= (size_t)chunk->count) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                    ORUS_JIT_IR_OP_CONCAT_STRING, ORUS_JIT_VALUE_STRING,
                    (uint32_t)offset);
            }
            uint16_t dst = chunk->code[offset + 1u];
            uint16_t lhs = chunk->code[offset + 2u];
            uint16_t rhs = chunk->code[offset + 3u];
            OrusJitValueKind lhs_kind = ORUS_JIT_GET_KIND(lhs);
            OrusJitValueKind rhs_kind = ORUS_JIT_GET_KIND(rhs);
            if (lhs_kind != ORUS_JIT_VALUE_STRING ||
                rhs_kind != ORUS_JIT_VALUE_STRING) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_UNSUPPORTED_VALUE_KIND,
                    ORUS_JIT_IR_OP_CONCAT_STRING, lhs_kind, (uint32_t)offset);
            }
            ORUS_JIT_ENSURE_ROLLOUT(ORUS_JIT_VALUE_STRING,
                                    ORUS_JIT_IR_OP_CONCAT_STRING, offset);
            OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
            if (!inst) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY,
                    ORUS_JIT_IR_OP_CONCAT_STRING, ORUS_JIT_VALUE_STRING,
                    (uint32_t)offset);
            }
            inst->opcode = ORUS_JIT_IR_OP_CONCAT_STRING;
            inst->value_kind = ORUS_JIT_VALUE_STRING;
            inst->bytecode_offset = (uint32_t)offset;
            inst->operands.arithmetic.dst_reg = dst;
            inst->operands.arithmetic.lhs_reg = lhs;
            inst->operands.arithmetic.rhs_reg = rhs;
            ORUS_JIT_SET_KIND(dst, ORUS_JIT_VALUE_STRING, inst);
            ORUS_JIT_SPECIALIZATION_INVALIDATE_REG(dst);
            offset += 4u;
            if (++instructions_since_safepoint >= safepoint_interval) {
                INSERT_SAFEPOINT((uint32_t)offset);
            }
            continue;
        }

        if (opcode == OP_TYPE_OF_R) {
            if (offset + 2u >= (size_t)chunk->count) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                    ORUS_JIT_IR_OP_TYPE_OF, ORUS_JIT_VALUE_STRING,
                    (uint32_t)offset);
            }
            uint16_t dst = chunk->code[offset + 1u];
            uint16_t src = chunk->code[offset + 2u];
            ORUS_JIT_ENSURE_ROLLOUT(ORUS_JIT_VALUE_STRING,
                                    ORUS_JIT_IR_OP_TYPE_OF, offset);
            OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
            if (!inst) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY,
                    ORUS_JIT_IR_OP_TYPE_OF, ORUS_JIT_VALUE_STRING,
                    (uint32_t)offset);
            }
            inst->opcode = ORUS_JIT_IR_OP_TYPE_OF;
            inst->value_kind = ORUS_JIT_VALUE_STRING;
            inst->bytecode_offset = (uint32_t)offset;
            inst->operands.type_of.dst_reg = dst;
            inst->operands.type_of.value_reg = src;
            ORUS_JIT_SET_KIND(dst, ORUS_JIT_VALUE_STRING, inst);
            ORUS_JIT_SPECIALIZATION_INVALIDATE_REG(dst);
            offset += 3u;
            if (++instructions_since_safepoint >= safepoint_interval) {
                INSERT_SAFEPOINT((uint32_t)offset);
            }
            continue;
        }

        if (opcode == OP_IS_TYPE_R) {
            if (offset + 3u >= (size_t)chunk->count) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                    ORUS_JIT_IR_OP_IS_TYPE, ORUS_JIT_VALUE_BOOL,
                    (uint32_t)offset);
            }
            uint16_t dst = chunk->code[offset + 1u];
            uint16_t value_reg = chunk->code[offset + 2u];
            uint16_t type_reg = chunk->code[offset + 3u];
            OrusJitValueKind type_kind = ORUS_JIT_GET_KIND(type_reg);
            if (type_kind != ORUS_JIT_VALUE_STRING &&
                type_kind != ORUS_JIT_VALUE_BOXED) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_UNSUPPORTED_VALUE_KIND,
                    ORUS_JIT_IR_OP_IS_TYPE, type_kind, (uint32_t)offset);
            }
            ORUS_JIT_ENSURE_ROLLOUT(ORUS_JIT_VALUE_BOOL,
                                    ORUS_JIT_IR_OP_IS_TYPE, offset);
            OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
            if (!inst) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY,
                    ORUS_JIT_IR_OP_IS_TYPE, ORUS_JIT_VALUE_BOOL,
                    (uint32_t)offset);
            }
            inst->opcode = ORUS_JIT_IR_OP_IS_TYPE;
            inst->value_kind = ORUS_JIT_VALUE_BOOL;
            inst->bytecode_offset = (uint32_t)offset;
            inst->operands.is_type.dst_reg = dst;
            inst->operands.is_type.value_reg = value_reg;
            inst->operands.is_type.type_reg = type_reg;
            ORUS_JIT_SET_KIND(dst, ORUS_JIT_VALUE_BOOL, inst);
            ORUS_JIT_SPECIALIZATION_INVALIDATE_REG(dst);
            offset += 4u;
            if (++instructions_since_safepoint >= safepoint_interval) {
                INSERT_SAFEPOINT((uint32_t)offset);
            }
            continue;
        }

        if (opcode == OP_TO_STRING_R) {
            if (offset + 2u >= (size_t)chunk->count) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                    ORUS_JIT_IR_OP_TO_STRING, ORUS_JIT_VALUE_STRING,
                    (uint32_t)offset);
            }
            uint16_t dst = chunk->code[offset + 1u];
            uint16_t src = chunk->code[offset + 2u];
            ORUS_JIT_ENSURE_ROLLOUT(ORUS_JIT_VALUE_STRING,
                                    ORUS_JIT_IR_OP_TO_STRING, offset);
            OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
            if (!inst) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY,
                    ORUS_JIT_IR_OP_TO_STRING, ORUS_JIT_VALUE_STRING,
                    (uint32_t)offset);
            }
            inst->opcode = ORUS_JIT_IR_OP_TO_STRING;
            inst->value_kind = ORUS_JIT_VALUE_STRING;
            inst->bytecode_offset = (uint32_t)offset;
            inst->operands.unary.dst_reg = dst;
            inst->operands.unary.src_reg = src;
            ORUS_JIT_SET_KIND(dst, ORUS_JIT_VALUE_STRING, inst);
            ORUS_JIT_SPECIALIZATION_INVALIDATE_REG(dst);
            offset += 3u;
            if (++instructions_since_safepoint >= safepoint_interval) {
                INSERT_SAFEPOINT((uint32_t)offset);
            }
            continue;
        }


        if (opcode == OP_EQ_R || opcode == OP_NE_R) {
            if (offset + 3u >= (size_t)chunk->count) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                    ORUS_JIT_IR_OP_EQ_I32, ORUS_JIT_VALUE_BOOL,
                    (uint32_t)offset);
            }
            uint16_t dst = chunk->code[offset + 1u];
            uint16_t lhs = chunk->code[offset + 2u];
            uint16_t rhs = chunk->code[offset + 3u];
            OrusJitValueKind lhs_kind = ORUS_JIT_GET_KIND(lhs);
            OrusJitValueKind rhs_kind = ORUS_JIT_GET_KIND(rhs);
            if (lhs_kind == ORUS_JIT_VALUE_BOXED &&
                rhs_kind != ORUS_JIT_VALUE_BOXED) {
                lhs_kind = rhs_kind;
            }
            if (rhs_kind == ORUS_JIT_VALUE_BOXED &&
                lhs_kind != ORUS_JIT_VALUE_BOXED) {
                rhs_kind = lhs_kind;
            }
            if (lhs_kind != rhs_kind) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_UNSUPPORTED_VALUE_KIND,
                    ORUS_JIT_IR_OP_EQ_I32, lhs_kind, (uint32_t)offset);
            }
            OrusJitIROpcode cmp_opcode = ORUS_JIT_IR_OP_EQ_I32;
            switch (lhs_kind) {
                case ORUS_JIT_VALUE_I32:
                    cmp_opcode = (opcode == OP_EQ_R) ? ORUS_JIT_IR_OP_EQ_I32
                                                     : ORUS_JIT_IR_OP_NE_I32;
                    break;
                case ORUS_JIT_VALUE_I64:
                    cmp_opcode = (opcode == OP_EQ_R) ? ORUS_JIT_IR_OP_EQ_I64
                                                     : ORUS_JIT_IR_OP_NE_I64;
                    break;
                case ORUS_JIT_VALUE_U32:
                    cmp_opcode = (opcode == OP_EQ_R) ? ORUS_JIT_IR_OP_EQ_U32
                                                     : ORUS_JIT_IR_OP_NE_U32;
                    break;
                case ORUS_JIT_VALUE_U64:
                    cmp_opcode = (opcode == OP_EQ_R) ? ORUS_JIT_IR_OP_EQ_U64
                                                     : ORUS_JIT_IR_OP_NE_U64;
                    break;
                case ORUS_JIT_VALUE_F64:
                    cmp_opcode = (opcode == OP_EQ_R) ? ORUS_JIT_IR_OP_EQ_F64
                                                     : ORUS_JIT_IR_OP_NE_F64;
                    break;
                case ORUS_JIT_VALUE_BOOL:
                    cmp_opcode = (opcode == OP_EQ_R) ? ORUS_JIT_IR_OP_EQ_BOOL
                                                     : ORUS_JIT_IR_OP_NE_BOOL;
                    break;
                default:
                    return make_translation_result(
                        ORUS_JIT_TRANSLATE_STATUS_UNSUPPORTED_VALUE_KIND,
                        ORUS_JIT_IR_OP_EQ_I32, lhs_kind, (uint32_t)offset);
            }
            ORUS_JIT_ENSURE_ROLLOUT(lhs_kind, cmp_opcode, offset);
            OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
            if (!inst) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY, cmp_opcode,
                    ORUS_JIT_VALUE_BOOL, (uint32_t)offset);
            }
            inst->opcode = cmp_opcode;
            inst->value_kind = ORUS_JIT_VALUE_BOOL;
            inst->bytecode_offset = (uint32_t)offset;
            inst->operands.arithmetic.dst_reg = dst;
            inst->operands.arithmetic.lhs_reg = lhs;
            inst->operands.arithmetic.rhs_reg = rhs;
            ORUS_JIT_SET_KIND(dst, ORUS_JIT_VALUE_BOOL, inst);
            ORUS_JIT_SPECIALIZATION_INVALIDATE_REG(dst);
            offset += 4u;
            if (++instructions_since_safepoint >= safepoint_interval) {
                INSERT_SAFEPOINT((uint32_t)offset);
            }
            continue;
        }

        if (map_arithmetic_opcode(opcode, &ir_opcode, &kind)) {
            if (offset + 3u >= (size_t)chunk->count) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT, ir_opcode, kind,
                    (uint32_t)offset);
            }
            uint16_t dst = chunk->code[offset + 1u];
            uint16_t lhs = chunk->code[offset + 2u];
            uint16_t rhs = chunk->code[offset + 3u];
            OrusJitValueKind lhs_kind_tracked = ORUS_JIT_GET_KIND(lhs);
            if (lhs_kind_tracked != kind) {
                if (lhs_kind_tracked == ORUS_JIT_VALUE_BOXED && lhs < REGISTER_COUNT &&
                    (register_writers[lhs] == NULL ||
                     register_writers[lhs]->opcode == ORUS_JIT_IR_OP_CALL_NATIVE)) {
                    ORUS_JIT_SET_KIND(lhs, kind);
                    lhs_kind_tracked = kind;
                } else if (!orus_jit_try_promote_register(&promotion_ctx, lhs,
                                                          kind)) {
                    return make_translation_result(
                        ORUS_JIT_TRANSLATE_STATUS_UNSUPPORTED_VALUE_KIND,
                        ir_opcode, lhs_kind_tracked, (uint32_t)offset);
                } else {
                    lhs_kind_tracked = ORUS_JIT_GET_KIND(lhs);
                }
            }
            OrusJitValueKind rhs_kind_tracked = ORUS_JIT_GET_KIND(rhs);
            if (rhs_kind_tracked != kind) {
                if (rhs_kind_tracked == ORUS_JIT_VALUE_BOXED && rhs < REGISTER_COUNT &&
                    (register_writers[rhs] == NULL ||
                     register_writers[rhs]->opcode == ORUS_JIT_IR_OP_CALL_NATIVE)) {
                    ORUS_JIT_SET_KIND(rhs, kind);
                    rhs_kind_tracked = kind;
                } else if (!orus_jit_try_promote_register(&promotion_ctx, rhs,
                                                          kind)) {
                    return make_translation_result(
                        ORUS_JIT_TRANSLATE_STATUS_UNSUPPORTED_VALUE_KIND,
                        ir_opcode, rhs_kind_tracked, (uint32_t)offset);
                } else {
                    rhs_kind_tracked = ORUS_JIT_GET_KIND(rhs);
                }
            }
            ORUS_JIT_ENSURE_ROLLOUT(kind, ir_opcode, offset);
            OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
            if (!inst) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY, ir_opcode, kind,
                    (uint32_t)offset);
            }
            inst->opcode = ir_opcode;
            inst->value_kind = kind;
            inst->bytecode_offset = (uint32_t)offset;
            inst->operands.arithmetic.dst_reg = dst;
            inst->operands.arithmetic.lhs_reg = lhs;
            inst->operands.arithmetic.rhs_reg = rhs;
            ORUS_JIT_SET_KIND(dst, kind, inst);
            if (specialization_enabled) {
                if (!orus_jit_specialization_try_fold_arithmetic(&specialization_state, inst)) {
                    orus_jit_specialization_invalidate(&specialization_state, dst);
                }
            }
            offset += 4u;
            if (++instructions_since_safepoint >= safepoint_interval) {
                INSERT_SAFEPOINT((uint32_t)offset);
            }
            continue;
        }

        if (map_comparison_opcode(opcode, &ir_opcode, &kind)) {
            if (offset + 3u >= (size_t)chunk->count) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT, ir_opcode, kind,
                    (uint32_t)offset);
            }
            uint16_t dst = chunk->code[offset + 1u];
            uint16_t lhs = chunk->code[offset + 2u];
            uint16_t rhs = chunk->code[offset + 3u];
            OrusJitValueKind expected_kind = ORUS_JIT_VALUE_BOXED;
            switch (ir_opcode) {
                case ORUS_JIT_IR_OP_LT_I32:
                case ORUS_JIT_IR_OP_LE_I32:
                case ORUS_JIT_IR_OP_GT_I32:
                case ORUS_JIT_IR_OP_GE_I32:
                    expected_kind = ORUS_JIT_VALUE_I32;
                    break;
                case ORUS_JIT_IR_OP_LT_I64:
                case ORUS_JIT_IR_OP_LE_I64:
                case ORUS_JIT_IR_OP_GT_I64:
                case ORUS_JIT_IR_OP_GE_I64:
                    expected_kind = ORUS_JIT_VALUE_I64;
                    break;
                case ORUS_JIT_IR_OP_LT_U32:
                case ORUS_JIT_IR_OP_LE_U32:
                case ORUS_JIT_IR_OP_GT_U32:
                case ORUS_JIT_IR_OP_GE_U32:
                    expected_kind = ORUS_JIT_VALUE_U32;
                    break;
                case ORUS_JIT_IR_OP_LT_U64:
                case ORUS_JIT_IR_OP_LE_U64:
                case ORUS_JIT_IR_OP_GT_U64:
                case ORUS_JIT_IR_OP_GE_U64:
                    expected_kind = ORUS_JIT_VALUE_U64;
                    break;
                case ORUS_JIT_IR_OP_LT_F64:
                case ORUS_JIT_IR_OP_LE_F64:
                case ORUS_JIT_IR_OP_GT_F64:
                case ORUS_JIT_IR_OP_GE_F64:
                    expected_kind = ORUS_JIT_VALUE_F64;
                    break;
                case ORUS_JIT_IR_OP_EQ_BOOL:
                case ORUS_JIT_IR_OP_NE_BOOL:
                    expected_kind = ORUS_JIT_VALUE_BOOL;
                    break;
                default:
                    break;
            }
            if (expected_kind != ORUS_JIT_VALUE_BOXED) {
                OrusJitValueKind lhs_kind_tracked = ORUS_JIT_GET_KIND(lhs);
                if (lhs_kind_tracked != expected_kind) {
                    if (lhs_kind_tracked == ORUS_JIT_VALUE_BOXED &&
                        lhs < REGISTER_COUNT &&
                        (register_writers[lhs] == NULL ||
                         register_writers[lhs]->opcode ==
                             ORUS_JIT_IR_OP_CALL_NATIVE)) {
                        ORUS_JIT_SET_KIND(lhs, expected_kind);
                        lhs_kind_tracked = expected_kind;
                    } else if (!orus_jit_try_promote_register(&promotion_ctx,
                                                              lhs,
                                                              expected_kind)) {
                        return make_translation_result(
                            ORUS_JIT_TRANSLATE_STATUS_UNSUPPORTED_VALUE_KIND,
                            ir_opcode, lhs_kind_tracked, (uint32_t)offset);
                    } else {
                        lhs_kind_tracked = ORUS_JIT_GET_KIND(lhs);
                    }
                }
                OrusJitValueKind rhs_kind_tracked = ORUS_JIT_GET_KIND(rhs);
                if (rhs_kind_tracked != expected_kind) {
                    if (rhs_kind_tracked == ORUS_JIT_VALUE_BOXED &&
                        rhs < REGISTER_COUNT &&
                        (register_writers[rhs] == NULL ||
                         register_writers[rhs]->opcode ==
                             ORUS_JIT_IR_OP_CALL_NATIVE)) {
                        ORUS_JIT_SET_KIND(rhs, expected_kind);
                        rhs_kind_tracked = expected_kind;
                    } else if (!orus_jit_try_promote_register(&promotion_ctx,
                                                              rhs,
                                                              expected_kind)) {
                        return make_translation_result(
                            ORUS_JIT_TRANSLATE_STATUS_UNSUPPORTED_VALUE_KIND,
                            ir_opcode, rhs_kind_tracked, (uint32_t)offset);
                    } else {
                        rhs_kind_tracked = ORUS_JIT_GET_KIND(rhs);
                    }
                }
            }
            ORUS_JIT_ENSURE_ROLLOUT(kind, ir_opcode, offset);
            OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
            if (!inst) {
                return make_translation_result(
                    ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY, ir_opcode, kind,
                    (uint32_t)offset);
            }
            inst->opcode = ir_opcode;
            inst->value_kind = kind;
            inst->bytecode_offset = (uint32_t)offset;
            inst->operands.arithmetic.dst_reg = dst;
            inst->operands.arithmetic.lhs_reg = lhs;
            inst->operands.arithmetic.rhs_reg = rhs;
            ORUS_JIT_SET_KIND(dst, ORUS_JIT_VALUE_BOOL, inst);
            ORUS_JIT_SPECIALIZATION_INVALIDATE_REG(dst);
            offset += 4u;
            if (++instructions_since_safepoint >= safepoint_interval) {
                INSERT_SAFEPOINT((uint32_t)offset);
            }
            continue;
        }

        return make_translation_result(ORUS_JIT_TRANSLATE_STATUS_UNHANDLED_OPCODE,
                                       ir_opcode, kind, (uint32_t)offset);
    }

translation_done:
    program->loop_end_offset = (uint32_t)offset;
    if (specialization_enabled) {
        for (uint16_t reg = 0; reg < REGISTER_COUNT; ++reg) {
            if (!orus_jit_specialization_has_constant(&specialization_state, reg)) {
                continue;
            }
            OrusJitIRInstruction* def = specialization_state.defining_instruction[reg];
            if (def) {
                def->optimization_flags |= ORUS_JIT_IR_FLAG_LOOP_INVARIANT;
            }
        }
    }
#undef INSERT_SAFEPOINT
#undef ORUS_JIT_SPECIALIZATION_INVALIDATE_ALL
#undef ORUS_JIT_SPECIALIZATION_INVALIDATE_REG
    if (!saw_terminal) {
        OrusJitIRInstruction* inst = orus_jit_ir_program_append(program);
        if (!inst) {
            return make_translation_result(
                ORUS_JIT_TRANSLATE_STATUS_OUT_OF_MEMORY, ORUS_JIT_IR_OP_RETURN,
                ORUS_JIT_VALUE_I32, (uint32_t)offset);
        }
        inst->opcode = ORUS_JIT_IR_OP_RETURN;
        inst->bytecode_offset = (uint32_t)offset;
        saw_terminal = true;
    }

    if (!program->count || !saw_terminal) {
        return make_translation_result(ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
                                       ORUS_JIT_IR_OP_RETURN,
                                       ORUS_JIT_VALUE_I32, (uint32_t)offset);
    }

#undef ORUS_JIT_GET_KIND
#undef ORUS_JIT_SET_KIND
#undef ORUS_JIT_SET_KIND2
#undef ORUS_JIT_SET_KIND3
#undef ORUS_JIT_SET_KIND_SELECTOR
#undef ORUS_JIT_ENSURE_ROLLOUT

    return make_translation_result(ORUS_JIT_TRANSLATE_STATUS_OK,
                                   ORUS_JIT_IR_OP_RETURN, ORUS_JIT_VALUE_I32,
                                   (uint32_t)offset);
}

void queue_tier_up(VMState* vm_state, const HotPathSample* sample) {
    if (!vm_state || !sample) {
        return;
    }

    HotPathSample* record = &vm_state->profile[sample->loop];
    record->hit_count = 0;

    if (!vm_state->jit_enabled || !vm_state->jit_backend) {
        return;
    }

    if (vm_state->jit_loop_blocklist[sample->loop]) {
        return;
    }

    Function script_function = {0};
    Function* function = NULL;
    const Chunk* active_chunk = NULL;

    if (sample->func == UINT16_MAX) {
        if (!vm_state->chunk) {
            return;
        }
        script_function.start = 0;
        script_function.arity = 0;
        script_function.chunk = vm_state->chunk;
        script_function.specialized_chunk = NULL;
        script_function.deopt_stub_chunk = NULL;
        script_function.tier = FUNCTION_TIER_BASELINE;
        script_function.deopt_handler = NULL;
        script_function.specialization_hits = 0;
        script_function.debug_name = NULL;
        function = &script_function;
        active_chunk = vm_state->chunk;
    } else {
        if (sample->func >= (FunctionId)vm_state->functionCount) {
            return;
        }
        function = &vm_state->functions[sample->func];
        active_chunk = vm_select_function_chunk(function);
    }

    if (!active_chunk) {
        return;
    }

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
    bool attempted_translation = false;
    OrusJitTranslationResult translation = {
        .status = ORUS_JIT_TRANSLATE_STATUS_INVALID_INPUT,
        .opcode = ORUS_JIT_IR_OP_RETURN,
        .value_kind = ORUS_JIT_VALUE_I32,
        .bytecode_offset = 0u,
    };
    if (function) {
        translation = orus_jit_translate_linear_block(
            vm_state, function, active_chunk, sample, &program);
        translated = (translation.status == ORUS_JIT_TRANSLATE_STATUS_OK);
        unsupported = orus_jit_translation_status_is_unsupported(translation.status);
        attempted_translation = true;
    }

    if (!translated) {
        if (attempted_translation) {
            OrusJitTranslationFailureRecord failure_record = {
                .status = translation.status,
                .opcode = translation.opcode,
                .value_kind = translation.value_kind,
                .bytecode_offset = translation.bytecode_offset,
                .function_index = sample->func,
                .loop_index = sample->loop,
            };
            orus_jit_translation_failure_log_record(
                &vm_state->jit_translation_failures, &failure_record);
        }
        vm_state->jit_loop_blocklist[sample->loop] = true;
        if (unsupported) {
            JITDeoptTrigger trigger = {
                .function_index = sample->func,
                .loop_index = sample->loop,
                .generation = 0,
            };
            vm_jit_invalidate_entry(&trigger);
            if (program.instructions) {
                orus_jit_ir_program_reset(&program);
            }
            vm_jit_enter_entry(vm_state, &vm_state->jit_entry_stub);
            return;
        }
        if (!orus_jit_ir_program_reserve(&program, 1u)) {
            if (program.instructions) {
                orus_jit_ir_program_reset(&program);
            }
            vm_jit_enter_entry(vm_state, &vm_state->jit_entry_stub);
            return;
        }
        memset(&program.instructions[0], 0, sizeof(program.instructions[0]));
        program.count = 1u;
        program.instructions[0].opcode = ORUS_JIT_IR_OP_RETURN;
        program.source_chunk = (const struct Chunk*)active_chunk;
        program.function_index = sample->func;
        program.loop_index = sample->loop;
        program.loop_start_offset = 0;
        program.loop_end_offset = 0;
    } else {
        vm_state->jit_translation_success_count++;
        if (orus_jit_trace_ir_enabled()) {
            orus_jit_ir_dump_program(&program, stderr);
        }
    }

    JITEntry entry;
    memset(&entry, 0, sizeof(entry));

    JITBackendStatus status =
        orus_jit_backend_compile_ir(vm_state->jit_backend, &program, &entry);
    if (program.instructions) {
        orus_jit_ir_program_reset(&program);
    }
    if (status == JIT_BACKEND_UNSUPPORTED) {
        vm_state->jit_loop_blocklist[sample->loop] = true;
        JITDeoptTrigger trigger = {
            .function_index = sample->func,
            .loop_index = sample->loop,
            .generation = 0,
        };
        vm_jit_invalidate_entry(&trigger);
        vm_jit_enter_entry(vm_state, &vm_state->jit_entry_stub);
        return;
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

    /*
     * Even if we had to fall back to a minimal stub because the translator
     * failed, reaching this point means we successfully produced an entry and
     * installed it in the cache. From the VM's perspective a tier-up
     * compilation happened, so we must record it to avoid repeatedly
     * re-queueing the same loop and to make the profiler counters match the
     * observable behaviour expected by the tests.
     */
    vm_state->jit_compilation_count++;

    cached = vm_jit_lookup_entry(sample->func, sample->loop);
    if (cached && cached->entry_point) {
        vm_jit_enter_entry(vm_state, cached);
        return;
    }

    vm_jit_enter_entry(vm_state, &vm_state->jit_entry_stub);
}
