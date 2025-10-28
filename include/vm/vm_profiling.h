// Orus Language Project

#ifndef VM_PROFILING_H
#define VM_PROFILING_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include "vm/vm.h"

// Profiling configuration flags
typedef enum {
    PROFILE_NONE = 0,
    PROFILE_INSTRUCTIONS = 1 << 0,     // Profile instruction execution counts
    PROFILE_HOT_PATHS = 1 << 1,        // Detect hot paths/loops
    PROFILE_REGISTER_USAGE = 1 << 2,   // Profile register allocation patterns
    PROFILE_MEMORY_ACCESS = 1 << 3,    // Profile memory access patterns
    PROFILE_BRANCH_PREDICTION = 1 << 4, // Profile branch prediction accuracy
    PROFILE_FUNCTION_CALLS = 1 << 5,   // Profile function invocation frequency
    PROFILE_OPCODE_WINDOWS = 1 << 6,   // Detect hot opcode windows for fusion
    PROFILE_ALL = 0x7F
} ProfilingFlags;

// Hot path detection thresholds
#define HOT_PATH_THRESHOLD 1000        // Executions to consider hot
#define HOT_LOOP_THRESHOLD 10000       // Loop iterations to consider hot
#define HOT_THRESHOLD HOT_LOOP_THRESHOLD

#define ORUS_JIT_WARMUP_REQUIRED 2u
#define ORUS_JIT_WARMUP_DECAY_TICKS (HOT_THRESHOLD * 3ULL)
#define ORUS_JIT_WARMUP_BASE_COOLDOWN (HOT_THRESHOLD / 2ULL)
#define ORUS_JIT_WARMUP_MAX_BACKOFF 5u
#define ORUS_JIT_WARMUP_SUPPRESS_LIMIT 4u
#define ORUS_JIT_WARMUP_ON_COOLDOWN_RESET                                             \
    (HOT_THRESHOLD - (HOT_THRESHOLD / 4u))
#define ORUS_JIT_WARMUP_PARTIAL_RESET (HOT_THRESHOLD / 2u)
#define PROFILING_SAMPLE_RATE 100      // Sample every N instructions when enabled
#define LOOP_HIT_SAMPLE_RATE 64        // Sample loop hit counts every 64 iterations
#define FUNCTION_HIT_SAMPLE_RATE 32    // Sample function hits every 32 calls

#define LOOP_PROFILE_SLOTS 1024
#define FUNCTION_PROFILE_SLOTS 512

// Opcode family taxonomy for aggregated profiling exports
typedef enum {
    ORUS_OPCODE_FAMILY_LITERAL = 0,
    ORUS_OPCODE_FAMILY_MOVES,
    ORUS_OPCODE_FAMILY_ARITHMETIC,
    ORUS_OPCODE_FAMILY_BITWISE,
    ORUS_OPCODE_FAMILY_COMPARISON,
    ORUS_OPCODE_FAMILY_LOGIC,
    ORUS_OPCODE_FAMILY_CONVERSION,
    ORUS_OPCODE_FAMILY_STRING,
    ORUS_OPCODE_FAMILY_COLLECTION,
    ORUS_OPCODE_FAMILY_ITERATOR,
    ORUS_OPCODE_FAMILY_CONTROL,
    ORUS_OPCODE_FAMILY_CALL,
    ORUS_OPCODE_FAMILY_FRAME,
    ORUS_OPCODE_FAMILY_SPILL,
    ORUS_OPCODE_FAMILY_MODULE,
    ORUS_OPCODE_FAMILY_CLOSURE,
    ORUS_OPCODE_FAMILY_RUNTIME,
    ORUS_OPCODE_FAMILY_TYPED,
    ORUS_OPCODE_FAMILY_EXTENDED,
    ORUS_OPCODE_FAMILY_OTHER,
    ORUS_OPCODE_FAMILY_COUNT,
} OrusOpcodeFamily;

typedef struct {
    uint64_t executions;
    uint64_t cycles;
} OpcodeFamilyProfile;

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

typedef struct {
    uintptr_t start_address;
    uint64_t hit_count;
    uint64_t last_seen;
    uint8_t length;
    bool metadata_requested;
    uint8_t opcodes[VM_MAX_FUSION_WINDOW];
} OpcodeWindowProfile;

typedef struct {
    uintptr_t recent_addresses[VM_MAX_FUSION_WINDOW];
    uint8_t recent_opcodes[VM_MAX_FUSION_WINDOW];
    uint8_t recent_count;
} OpcodeWindowSampler;

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
    OpcodeFamilyProfile familyStats[ORUS_OPCODE_FAMILY_COUNT];
    
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

    // Opcode window sampling for tiered fusion
    OpcodeWindowSampler window_sampler;
    OpcodeWindowProfile window_profiles[256];
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

// Opcode window fusion sampling
void vm_profiling_record_opcode_window(const uint8_t* start_addr, uint8_t opcode);

// Runtime profiling hooks (inline for performance)
static inline OrusOpcodeFamily vm_opcode_family(uint8_t opcode) {
    switch ((OpCode)opcode) {
        case OP_LOAD_CONST:
        case OP_LOAD_TRUE:
        case OP_LOAD_FALSE:
            return ORUS_OPCODE_FAMILY_LITERAL;

        case OP_MOVE:
        case OP_LOAD_GLOBAL:
        case OP_STORE_GLOBAL:
            return ORUS_OPCODE_FAMILY_MOVES;

        case OP_ADD_I32_R:
        case OP_SUB_I32_R:
        case OP_MUL_I32_R:
        case OP_DIV_I32_R:
        case OP_MOD_I32_R:
        case OP_INC_I32_R:
        case OP_INC_I32_CHECKED:
        case OP_DEC_I32_R:
        case OP_ADD_I64_R:
        case OP_SUB_I64_R:
        case OP_MUL_I64_R:
        case OP_DIV_I64_R:
        case OP_MOD_I64_R:
        case OP_INC_I64_R:
        case OP_INC_I64_CHECKED:
        case OP_ADD_U32_R:
        case OP_SUB_U32_R:
        case OP_MUL_U32_R:
        case OP_DIV_U32_R:
        case OP_MOD_U32_R:
        case OP_INC_U32_R:
        case OP_INC_U32_CHECKED:
        case OP_ADD_U64_R:
        case OP_SUB_U64_R:
        case OP_MUL_U64_R:
        case OP_DIV_U64_R:
        case OP_MOD_U64_R:
        case OP_INC_U64_R:
        case OP_INC_U64_CHECKED:
        case OP_ADD_F64_R:
        case OP_SUB_F64_R:
        case OP_MUL_F64_R:
        case OP_DIV_F64_R:
        case OP_MOD_F64_R:
        case OP_LOAD_ADD_I32:
        case OP_LOAD_INC_STORE:
        case OP_MUL_ADD_I32:
        case OP_ADD_I32_IMM:
        case OP_SUB_I32_IMM:
        case OP_MUL_I32_IMM:
        case OP_CMP_I32_IMM:
        case OP_NEG_I32_R:
            return ORUS_OPCODE_FAMILY_ARITHMETIC;

        case OP_AND_I32_R:
        case OP_OR_I32_R:
        case OP_XOR_I32_R:
        case OP_NOT_I32_R:
        case OP_SHL_I32_R:
        case OP_SHR_I32_R:
            return ORUS_OPCODE_FAMILY_BITWISE;

        case OP_EQ_R:
        case OP_NE_R:
        case OP_LT_I32_R:
        case OP_LE_I32_R:
        case OP_GT_I32_R:
        case OP_GE_I32_R:
        case OP_LT_I64_R:
        case OP_LE_I64_R:
        case OP_GT_I64_R:
        case OP_GE_I64_R:
        case OP_LT_F64_R:
        case OP_LE_F64_R:
        case OP_GT_F64_R:
        case OP_GE_F64_R:
        case OP_LT_U32_R:
        case OP_LE_U32_R:
        case OP_GT_U32_R:
        case OP_GE_U32_R:
        case OP_LT_U64_R:
        case OP_LE_U64_R:
        case OP_GT_U64_R:
        case OP_GE_U64_R:
        case OP_LOAD_CMP_I32:
            return ORUS_OPCODE_FAMILY_COMPARISON;

        case OP_AND_BOOL_R:
        case OP_OR_BOOL_R:
        case OP_NOT_BOOL_R:
            return ORUS_OPCODE_FAMILY_LOGIC;

        case OP_I32_TO_F64_R:
        case OP_I32_TO_I64_R:
        case OP_I64_TO_I32_R:
        case OP_I64_TO_F64_R:
        case OP_U32_TO_I32_R:
        case OP_F64_TO_U32_R:
        case OP_U32_TO_F64_R:
        case OP_I32_TO_U64_R:
        case OP_I64_TO_U64_R:
        case OP_U64_TO_I32_R:
        case OP_U64_TO_I64_R:
        case OP_U32_TO_U64_R:
        case OP_U64_TO_U32_R:
        case OP_F64_TO_U64_R:
        case OP_U64_TO_F64_R:
        case OP_I32_TO_BOOL_R:
        case OP_I64_TO_BOOL_R:
        case OP_U32_TO_BOOL_R:
        case OP_U64_TO_BOOL_R:
        case OP_BOOL_TO_I32_R:
        case OP_BOOL_TO_I64_R:
        case OP_BOOL_TO_U32_R:
        case OP_BOOL_TO_U64_R:
        case OP_BOOL_TO_F64_R:
        case OP_F64_TO_I32_R:
        case OP_F64_TO_I64_R:
        case OP_F64_TO_BOOL_R:
        case OP_I32_TO_U32_R:
        case OP_I64_TO_U32_R:
            return ORUS_OPCODE_FAMILY_CONVERSION;

        case OP_CONCAT_R:
        case OP_TO_STRING_R:
        case OP_STRING_INDEX_R:
        case OP_STRING_GET_R:
            return ORUS_OPCODE_FAMILY_STRING;

        case OP_MAKE_ARRAY_R:
        case OP_ENUM_NEW_R:
        case OP_ENUM_TAG_EQ_R:
        case OP_ENUM_PAYLOAD_R:
        case OP_ARRAY_GET_R:
        case OP_ARRAY_SET_R:
        case OP_ARRAY_LEN_R:
        case OP_ARRAY_PUSH_R:
        case OP_ARRAY_POP_R:
        case OP_ARRAY_SORTED_R:
        case OP_ARRAY_REPEAT_R:
        case OP_ARRAY_SLICE_R:
            return ORUS_OPCODE_FAMILY_COLLECTION;

        case OP_GET_ITER_R:
        case OP_ITER_NEXT_R:
            return ORUS_OPCODE_FAMILY_ITERATOR;

        case OP_TRY_BEGIN:
        case OP_TRY_END:
        case OP_JUMP:
        case OP_JUMP_IF_R:
        case OP_JUMP_IF_NOT_R:
        case OP_JUMP_IF_NOT_I32_TYPED:
        case OP_LOOP:
        case OP_JUMP_SHORT:
        case OP_JUMP_BACK_SHORT:
        case OP_JUMP_IF_NOT_SHORT:
        case OP_LOOP_SHORT:
        case OP_BRANCH_TYPED:
        case OP_INC_CMP_JMP:
        case OP_DEC_CMP_JMP:
            return ORUS_OPCODE_FAMILY_CONTROL;

        case OP_CALL_R:
        case OP_CALL_NATIVE_R:
        case OP_CALL_FOREIGN:
        case OP_TAIL_CALL_R:
        case OP_RETURN_R:
        case OP_RETURN_VOID:
            return ORUS_OPCODE_FAMILY_CALL;

        case OP_LOAD_FRAME:
        case OP_STORE_FRAME:
        case OP_ENTER_FRAME:
        case OP_EXIT_FRAME:
        case OP_MOVE_FRAME:
            return ORUS_OPCODE_FAMILY_FRAME;

        case OP_LOAD_SPILL:
        case OP_STORE_SPILL:
            return ORUS_OPCODE_FAMILY_SPILL;

        case OP_LOAD_MODULE:
        case OP_STORE_MODULE:
        case OP_LOAD_MODULE_NAME:
        case OP_SWITCH_MODULE:
        case OP_EXPORT_VAR:
        case OP_IMPORT_VAR:
        case OP_IMPORT_R:
            return ORUS_OPCODE_FAMILY_MODULE;

        case OP_CLOSURE_R:
        case OP_GET_UPVALUE_R:
        case OP_SET_UPVALUE_R:
        case OP_CLOSE_UPVALUE_R:
            return ORUS_OPCODE_FAMILY_CLOSURE;

        case OP_PARSE_INT_R:
        case OP_PARSE_FLOAT_R:
        case OP_TYPE_OF_R:
        case OP_IS_TYPE_R:
        case OP_INPUT_R:
        case OP_RANGE_R:
        case OP_PRINT_MULTI_R:
        case OP_PRINT_R:
        case OP_ASSERT_EQ_R:
        case OP_TIME_STAMP:
        case OP_GC_PAUSE:
        case OP_GC_RESUME:
            return ORUS_OPCODE_FAMILY_RUNTIME;

        case OP_ADD_I32_TYPED:
        case OP_SUB_I32_TYPED:
        case OP_MUL_I32_TYPED:
        case OP_DIV_I32_TYPED:
        case OP_MOD_I32_TYPED:
        case OP_ADD_I64_TYPED:
        case OP_SUB_I64_TYPED:
        case OP_MUL_I64_TYPED:
        case OP_DIV_I64_TYPED:
        case OP_MOD_I64_TYPED:
        case OP_ADD_F64_TYPED:
        case OP_SUB_F64_TYPED:
        case OP_MUL_F64_TYPED:
        case OP_DIV_F64_TYPED:
        case OP_MOD_F64_TYPED:
        case OP_ADD_U32_TYPED:
        case OP_SUB_U32_TYPED:
        case OP_MUL_U32_TYPED:
        case OP_DIV_U32_TYPED:
        case OP_MOD_U32_TYPED:
        case OP_ADD_U64_TYPED:
        case OP_SUB_U64_TYPED:
        case OP_MUL_U64_TYPED:
        case OP_DIV_U64_TYPED:
        case OP_MOD_U64_TYPED:
        case OP_LT_I32_TYPED:
        case OP_LE_I32_TYPED:
        case OP_GT_I32_TYPED:
        case OP_GE_I32_TYPED:
        case OP_LT_I64_TYPED:
        case OP_LE_I64_TYPED:
        case OP_GT_I64_TYPED:
        case OP_GE_I64_TYPED:
        case OP_LT_F64_TYPED:
        case OP_LE_F64_TYPED:
        case OP_GT_F64_TYPED:
        case OP_GE_F64_TYPED:
        case OP_LT_U32_TYPED:
        case OP_LE_U32_TYPED:
        case OP_GT_U32_TYPED:
        case OP_GE_U32_TYPED:
        case OP_LT_U64_TYPED:
        case OP_LE_U64_TYPED:
        case OP_GT_U64_TYPED:
        case OP_GE_U64_TYPED:
        case OP_LOAD_I32_CONST:
        case OP_LOAD_I64_CONST:
        case OP_LOAD_U32_CONST:
        case OP_LOAD_U64_CONST:
        case OP_LOAD_F64_CONST:
        case OP_MOVE_I32:
        case OP_MOVE_I64:
        case OP_MOVE_F64:
            return ORUS_OPCODE_FAMILY_TYPED;

        case OP_LOAD_CONST_EXT:
        case OP_MOVE_EXT:
        case OP_STORE_EXT:
        case OP_LOAD_EXT:
            return ORUS_OPCODE_FAMILY_EXTENDED;

        case OP_HALT:
            return ORUS_OPCODE_FAMILY_CONTROL;

        default:
            break;
    }
    return ORUS_OPCODE_FAMILY_OTHER;
}

static inline void profileInstruction(uint8_t opcode, uint64_t cycles) {
    if (!(g_profiling.enabledFlags & PROFILE_INSTRUCTIONS) || !g_profiling.isActive) return;

    // Sample-based profiling to reduce overhead
    if (++g_profiling.sampleCounter % PROFILING_SAMPLE_RATE != 0) return;

    InstructionProfile* profile = &g_profiling.instructionStats[opcode];
    profile->executionCount++;
    profile->totalCycles += cycles;
    profile->averageCycles = (double)profile->totalCycles / profile->executionCount;

    OrusOpcodeFamily family = vm_opcode_family(opcode);
    if ((size_t)family < ORUS_OPCODE_FAMILY_COUNT) {
        OpcodeFamilyProfile* family_profile = &g_profiling.familyStats[family];
        family_profile->executions++;
        family_profile->cycles += cycles;
    }

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
    
    // Mark as currently hot if iterations exceed threshold
    if (iterations > HOT_LOOP_THRESHOLD) {
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

static inline void profileOpcodeWindow(const uint8_t* start_addr, uint8_t opcode) {
    if (!g_profiling.isActive || !(g_profiling.enabledFlags & PROFILE_OPCODE_WINDOWS)) {
        return;
    }
    vm_profiling_record_opcode_window(start_addr, opcode);
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

extern size_t gcThreshold;

#ifndef GC_SAFEPOINT
#define GC_SAFEPOINT(vm_ptr)                                         \
    do {                                                             \
        if ((vm_ptr) && (vm_ptr)->bytesAllocated > gcThreshold) {    \
            collectGarbage();                                        \
        }                                                            \
    } while (0)
#endif

#ifndef PROF_SAFEPOINT
#define PROF_SAFEPOINT(vm_ptr) GC_SAFEPOINT(vm_ptr)
#endif

void queue_tier_up(VMState* vm, const HotPathSample* sample);

const char* orus_jit_tier_skip_reason_name(OrusJitTierSkipReason reason);

uint64_t orus_jit_tier_skip_total(const OrusJitTierSkipStats* stats);

static inline uint64_t
orus_jit_warmup_compute_cooldown(uint8_t backoff_shift) {
    uint64_t cooldown = ORUS_JIT_WARMUP_BASE_COOLDOWN;
    uint8_t clamped =
        backoff_shift > ORUS_JIT_WARMUP_MAX_BACKOFF ? ORUS_JIT_WARMUP_MAX_BACKOFF
                                                    : backoff_shift;
    return cooldown << clamped;
}

static inline bool vm_profile_tick(VMState* vm, FunctionId func, LoopId loop) {
    if (!vm) {
        return false;
    }

    vm->ticks++;

    HotPathSample* sample = &vm->profile[loop];
    sample->func = func;
    sample->loop = loop;

    if (sample->cooldown_until_tick && vm->ticks >= sample->cooldown_until_tick) {
        if (sample->cooldown_exponent > 0) {
            sample->cooldown_exponent--;
        }
        sample->cooldown_until_tick = 0;
        sample->suppressed_triggers = 0;
    }

    if (++sample->hit_count < HOT_THRESHOLD) {
        return false;
    }

    uint64_t previous_threshold_tick = sample->last_threshold_tick;
    sample->last_threshold_tick = vm->ticks;

    if (previous_threshold_tick != 0 &&
        (vm->ticks - previous_threshold_tick) > ORUS_JIT_WARMUP_DECAY_TICKS) {
        sample->warmup_level = 0;
    }

    if (sample->cooldown_until_tick != 0 && vm->ticks < sample->cooldown_until_tick) {
        if (sample->warmup_level > 0) {
            sample->warmup_level--;
        }
        if (sample->suppressed_triggers < UINT32_MAX) {
            sample->suppressed_triggers++;
        }
        if (sample->suppressed_triggers > ORUS_JIT_WARMUP_SUPPRESS_LIMIT &&
            sample->cooldown_exponent > 0) {
            sample->cooldown_exponent--;
            sample->suppressed_triggers = 0;
        }
        sample->hit_count = ORUS_JIT_WARMUP_ON_COOLDOWN_RESET;
        return false;
    }

    sample->suppressed_triggers = 0;

    uint8_t warmup_level = sample->warmup_level;
    if (warmup_level < UINT8_MAX) {
        warmup_level++;
    }
    sample->warmup_level = warmup_level;

    if (warmup_level < ORUS_JIT_WARMUP_REQUIRED) {
        sample->hit_count = ORUS_JIT_WARMUP_PARTIAL_RESET;
        return false;
    }

    sample->warmup_level = 0;

    uint8_t backoff_shift = sample->cooldown_exponent;
    if (backoff_shift > 0) {
        backoff_shift--;
    }
    sample->cooldown_exponent = backoff_shift;

    uint64_t cooldown = orus_jit_warmup_compute_cooldown(backoff_shift);
    if (cooldown > UINT64_MAX - vm->ticks) {
        sample->cooldown_until_tick = UINT64_MAX;
    } else {
        sample->cooldown_until_tick = vm->ticks + cooldown;
    }

    queue_tier_up(vm, sample);
    return true;

    return false;
}

static inline void vm_profile_record_loop_hit(VMState* vm_state, LoopId loop_id) {
    if (!vm_state) {
        return;
    }

    FunctionId function_id = UINT16_MAX;
    CallFrame* frame = vm_state->register_file.current_frame;
    if (frame && frame->functionIndex != UINT16_MAX &&
        frame->functionIndex < (uint16_t)vm_state->functionCount) {
        function_id = frame->functionIndex;
    } else if (vm_state->chunk) {
        for (int i = 0; i < vm_state->functionCount; ++i) {
            if (vm_state->functions[i].chunk == vm_state->chunk) {
                function_id = (FunctionId)i;
                break;
            }
        }
    }

    vm_profile_tick(vm_state, function_id, loop_id);
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