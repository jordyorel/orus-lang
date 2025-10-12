#define _POSIX_C_SOURCE 199309L
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "vm/jit_backend.h"
#include "vm/jit_benchmark.h"
#include "vm/jit_translation.h"
#include "vm/vm.h"
#include "vm/vm_profiling.h"
#include "vm/vm_tiering.h"
#include "vm/vm_comparison.h"
#include "vm/vm_dispatch.h"
#include "vm/vm_string_ops.h"

#include "errors/error_interface.h"
#include "errors/features/type_errors.h"
#include "errors/features/variable_errors.h"
#include "internal/error_reporting.h"

#define TEST_CASE(name) static bool name(void)
#define ASSERT_TRUE(cond)                                                        \
    do {                                                                         \
        if (!(cond)) {                                                           \
            fprintf(stderr, "Assertion failed: %s (%s:%d)\n", #cond, __FILE__,   \
                    __LINE__);                                                   \
            return false;                                                        \
        }                                                                        \
    } while (0)

static bool prepare_fixture_function(Function* function) {
    if (!function) {
        return false;
    }

    if (!function->chunk) {
        function->chunk = (Chunk*)malloc(sizeof(Chunk));
        if (!function->chunk) {
            return false;
        }
    } else {
        freeChunk(function->chunk);
    }

    initChunk(function->chunk);

    function->start = 0;
    function->arity = 0;
    function->tier = FUNCTION_TIER_BASELINE;
    return true;
}

static bool write_load_const(Chunk* chunk,
                             uint8_t opcode,
                             uint16_t dst,
                             Value value,
                             const char* file_tag,
                             int line,
                             int column) {
    if (!chunk) {
        return false;
    }

    int constant_index = addConstant(chunk, value);
    if (constant_index < 0) {
        return false;
    }

    writeChunk(chunk, opcode, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst, line, column, file_tag);
    writeChunk(chunk, (uint8_t)((constant_index >> 8) & 0xFF), line, column,
               file_tag);
    writeChunk(chunk, (uint8_t)(constant_index & 0xFF), line, column, file_tag);
    return true;
}

static bool install_linear_i32_fixture(Function* function) {
    const char* file_tag = "jit_benchmark";
    const int line = 1;
    const int column = 1;

    if (!prepare_fixture_function(function)) {
        return false;
    }

    Chunk* chunk = function->chunk;
    const uint16_t dst0 = 0u;
    const uint16_t dst1 = 1u;
    const uint16_t dst2 = 2u;
    const uint16_t dst3 = 3u;

    if (!write_load_const(chunk, OP_LOAD_I32_CONST, dst0, I32_VAL(0), file_tag,
                          line, column) ||
        !write_load_const(chunk, OP_LOAD_I32_CONST, dst1, I32_VAL(1), file_tag,
                          line, column) ||
        !write_load_const(chunk, OP_LOAD_I32_CONST, dst2, I32_VAL(2), file_tag,
                          line, column) ||
        !write_load_const(chunk, OP_LOAD_I32_CONST, dst3, I32_VAL(3), file_tag,
                          line, column)) {
        return false;
    }

    writeChunk(chunk, OP_ADD_I32_TYPED, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst0, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst0, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst1, line, column, file_tag);

    writeChunk(chunk, OP_ADD_I32_TYPED, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst0, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst0, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst2, line, column, file_tag);

    writeChunk(chunk, OP_SUB_I32_TYPED, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst0, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst0, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst1, line, column, file_tag);

    writeChunk(chunk, OP_MUL_I32_TYPED, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst0, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst0, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst3, line, column, file_tag);

    writeChunk(chunk, OP_RETURN_VOID, line, column, file_tag);

    vm_store_i32_typed_hot(dst0, 0);
    vm_store_i32_typed_hot(dst1, 0);
    vm_store_i32_typed_hot(dst2, 0);
    vm_store_i32_typed_hot(dst3, 0);

    return true;
}

static bool install_linear_i64_fixture(Function* function) {
    const char* file_tag = "jit_benchmark";
    const int line = 1;
    const int column = 1;

    if (!prepare_fixture_function(function)) {
        return false;
    }

    Chunk* chunk = function->chunk;
    const uint16_t dst0 = 0u;
    const uint16_t dst1 = 1u;
    const uint16_t dst2 = 2u;
    const uint16_t dst3 = 3u;

    if (!write_load_const(chunk, OP_LOAD_I64_CONST, dst0, I64_VAL(40), file_tag,
                          line, column) ||
        !write_load_const(chunk, OP_LOAD_I64_CONST, dst1, I64_VAL(1), file_tag,
                          line, column) ||
        !write_load_const(chunk, OP_LOAD_I64_CONST, dst2, I64_VAL(2), file_tag,
                          line, column) ||
        !write_load_const(chunk, OP_LOAD_I64_CONST, dst3, I64_VAL(3), file_tag,
                          line, column)) {
        return false;
    }

    writeChunk(chunk, OP_ADD_I64_TYPED, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst0, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst0, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst1, line, column, file_tag);

    writeChunk(chunk, OP_ADD_I64_TYPED, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst0, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst0, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst2, line, column, file_tag);

    writeChunk(chunk, OP_SUB_I64_TYPED, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst0, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst0, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst1, line, column, file_tag);

    writeChunk(chunk, OP_MUL_I64_TYPED, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst0, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst0, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst3, line, column, file_tag);

    writeChunk(chunk, OP_RETURN_VOID, line, column, file_tag);

    vm_store_i64_typed_hot(dst0, 0);
    vm_store_i64_typed_hot(dst1, 0);
    vm_store_i64_typed_hot(dst2, 0);
    vm_store_i64_typed_hot(dst3, 0);

    return true;
}

#ifdef OP_LOAD_U64_CONST
static bool install_linear_u64_fixture(Function* function) {
    const char* file_tag = "jit_benchmark";
    const int line = 1;
    const int column = 1;

    if (!prepare_fixture_function(function)) {
        return false;
    }

    Chunk* chunk = function->chunk;
    const uint16_t dst0 = 0u;
    const uint16_t dst1 = 1u;
    const uint16_t dst2 = 2u;
    const uint16_t dst3 = 3u;

    if (!write_load_const(chunk, OP_LOAD_U64_CONST, dst0,
                          U64_VAL(5000000000ULL), file_tag, line, column) ||
        !write_load_const(chunk, OP_LOAD_U64_CONST, dst1, U64_VAL(7ULL), file_tag,
                          line, column) ||
        !write_load_const(chunk, OP_LOAD_U64_CONST, dst2, U64_VAL(11ULL),
                          file_tag, line, column) ||
        !write_load_const(chunk, OP_LOAD_U64_CONST, dst3, U64_VAL(13ULL),
                          file_tag, line, column)) {
        return false;
    }

    writeChunk(chunk, OP_ADD_U64_TYPED, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst0, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst0, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst1, line, column, file_tag);

    writeChunk(chunk, OP_ADD_U64_TYPED, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst0, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst0, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst2, line, column, file_tag);

    writeChunk(chunk, OP_SUB_U64_TYPED, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst0, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst0, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst1, line, column, file_tag);

    writeChunk(chunk, OP_MUL_U64_TYPED, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst0, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst0, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst3, line, column, file_tag);

    writeChunk(chunk, OP_RETURN_VOID, line, column, file_tag);

    vm_store_u64_typed_hot(dst0, 0u);
    vm_store_u64_typed_hot(dst1, 0u);
    vm_store_u64_typed_hot(dst2, 0u);
    vm_store_u64_typed_hot(dst3, 0u);

    return true;
}
#endif  // OP_LOAD_U64_CONST

static bool install_linear_f64_fixture(Function* function) {
    const char* file_tag = "jit_benchmark";
    const int line = 1;
    const int column = 1;

    if (!prepare_fixture_function(function)) {
        return false;
    }

    Chunk* chunk = function->chunk;
    const uint16_t dst0 = 0u;
    const uint16_t dst1 = 1u;
    const uint16_t dst2 = 2u;
    const uint16_t dst3 = 3u;

    if (!write_load_const(chunk, OP_LOAD_F64_CONST, dst0, F64_VAL(1.5), file_tag,
                          line, column) ||
        !write_load_const(chunk, OP_LOAD_F64_CONST, dst1, F64_VAL(2.5), file_tag,
                          line, column) ||
        !write_load_const(chunk, OP_LOAD_F64_CONST, dst2, F64_VAL(3.5), file_tag,
                          line, column) ||
        !write_load_const(chunk, OP_LOAD_F64_CONST, dst3, F64_VAL(4.5), file_tag,
                          line, column)) {
        return false;
    }

    writeChunk(chunk, OP_ADD_F64_TYPED, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst0, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst0, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst1, line, column, file_tag);

    writeChunk(chunk, OP_ADD_F64_TYPED, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst0, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst0, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst2, line, column, file_tag);

    writeChunk(chunk, OP_SUB_F64_TYPED, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst0, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst0, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst1, line, column, file_tag);

    writeChunk(chunk, OP_MUL_F64_TYPED, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst0, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst0, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst3, line, column, file_tag);

    writeChunk(chunk, OP_RETURN_VOID, line, column, file_tag);

    vm_store_f64_typed_hot(dst0, 0.0);
    vm_store_f64_typed_hot(dst1, 0.0);
    vm_store_f64_typed_hot(dst2, 0.0);
    vm_store_f64_typed_hot(dst3, 0.0);

    return true;
}

static bool install_linear_string_fixture(Function* function) {
    const char* file_tag = "jit_benchmark";
    const int line = 1;
    const int column = 1;

    if (!prepare_fixture_function(function)) {
        return false;
    }

    Chunk* chunk = function->chunk;
    const uint16_t dst0 = 0u;
    const uint16_t dst1 = 1u;
    const uint16_t dst2 = 2u;
    const uint16_t dst3 = 3u;

    ObjString* part_a = allocateString("alpha", 5);
    ObjString* part_b = allocateString("beta", 4);
    ObjString* part_c = allocateString("gamma", 5);
    if (!part_a || !part_b || !part_c) {
        return false;
    }

    if (!write_load_const(chunk, OP_LOAD_CONST, dst0, STRING_VAL(part_a),
                          file_tag, line, column) ||
        !write_load_const(chunk, OP_LOAD_CONST, dst1, STRING_VAL(part_b),
                          file_tag, line, column) ||
        !write_load_const(chunk, OP_LOAD_CONST, dst2, STRING_VAL(part_c),
                          file_tag, line, column)) {
        return false;
    }

    writeChunk(chunk, OP_CONCAT_R, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst3, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst0, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst1, line, column, file_tag);

    writeChunk(chunk, OP_CONCAT_R, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst0, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst3, line, column, file_tag);
    writeChunk(chunk, (uint8_t)dst2, line, column, file_tag);

    writeChunk(chunk, OP_RETURN_VOID, line, column, file_tag);

    vm_set_register_safe(dst0, STRING_VAL(part_a));
    vm_set_register_safe(dst1, STRING_VAL(part_b));
    vm_set_register_safe(dst2, STRING_VAL(part_c));
    vm_set_register_safe(dst3, STRING_VAL(part_a));

    return true;
}

typedef bool (*JitFixtureInstaller)(Function* function);

static double
timespec_diff_ns(const struct timespec* start, const struct timespec* end) {
    if (!start || !end) {
        return 0.0;
    }

    time_t sec = end->tv_sec - start->tv_sec;
    long nsec = end->tv_nsec - start->tv_nsec;

    if (nsec < 0) {
        --sec;
        nsec += 1000000000L;
    }

    return (double)sec * 1e9 + (double)nsec;
}

static bool run_jit_benchmark_case(const char* label,
                                   JitFixtureInstaller installer,
                                   OrusJitRolloutStage stage,
                                   uint64_t interpreter_iterations,
                                   uint64_t jit_iterations,
                                   size_t cache_trials) {
    initVM();

    if (!vm.jit_enabled || !vm.jit_backend) {
        printf("[JIT Benchmark] DynASM backend unavailable - skipping %s kernel.\n",
               label);
        freeVM();
        return true;
    }

    orus_jit_rollout_set_stage(&vm, stage);

    printf("\n[JIT Benchmark] Running %s kernel\n", label);

    vm.functionCount = 1;

    ASSERT_TRUE(installer(&vm.functions[0]));

    bool saved_jit_enabled = vm.jit_enabled;
    vm.jit_enabled = false;

    struct timespec interp_start = {0};
    struct timespec interp_end = {0};

    clock_gettime(CLOCK_MONOTONIC, &interp_start);
    for (uint64_t i = 0; i < interpreter_iterations; ++i) {
        vm.chunk = vm.functions[0].chunk;
        vm.ip = vm.functions[0].chunk ? vm.functions[0].chunk->code : NULL;
        InterpretResult result = vm_run_dispatch();
        ASSERT_TRUE(result == INTERPRET_OK);
    }
    clock_gettime(CLOCK_MONOTONIC, &interp_end);

    double interpreter_total_ns = timespec_diff_ns(&interp_start, &interp_end);
    double interpreter_ns_per_call =
        interpreter_total_ns / (double)interpreter_iterations;
    double interpreter_calls_per_second =
        interpreter_ns_per_call > 0.0 ? 1e9 / interpreter_ns_per_call : 0.0;

    vm.jit_enabled = saved_jit_enabled;
    vm.chunk = NULL;
    vm.ip = NULL;

    memset(vm.profile, 0, sizeof(vm.profile));

    ASSERT_TRUE(installer(&vm.functions[0]));

    double total_compile_ns = 0.0;
    size_t compile_events = 0u;
    uint64_t base_compilations = vm.jit_compilation_count;
    uint64_t base_translation_success = vm.jit_translation_success_count;
    OrusJitTranslationFailureLog base_failure_log = vm.jit_translation_failures;
    uint64_t base_cache_hits = vm.jit_cache_hit_count;
    uint64_t base_cache_misses = vm.jit_cache_miss_count;
    uint64_t base_deopts = vm.jit_deopt_count;

    vm_jit_flush_entries();

    HotPathSample* sample = &vm.profile[0];
    sample->func = 0;
    sample->loop = 0;
    sample->hit_count = HOT_THRESHOLD - 1;

    struct timespec compile_start = {0};
    struct timespec compile_end = {0};

    clock_gettime(CLOCK_MONOTONIC, &compile_start);
    ASSERT_TRUE(vm_profile_tick(&vm, 0, 0));
    clock_gettime(CLOCK_MONOTONIC, &compile_end);

    bool recorded_translation =
        vm.jit_translation_success_count > base_translation_success ||
        vm.jit_compilation_count > base_compilations;
    ASSERT_TRUE(recorded_translation);

    total_compile_ns += timespec_diff_ns(&compile_start, &compile_end);
    ++compile_events;

    JITEntry* entry = vm_jit_lookup_entry(0, 0);
    ASSERT_TRUE(entry != NULL);
    ASSERT_TRUE(entry->entry_point != NULL);

    for (size_t trial = 0; trial < cache_trials; ++trial) {
        sample->func = 0;
        sample->loop = 0;
        sample->hit_count = HOT_THRESHOLD - 1;

        ASSERT_TRUE(vm_profile_tick(&vm, 0, 0));

        entry = vm_jit_lookup_entry(0, 0);
        ASSERT_TRUE(entry != NULL);
        ASSERT_TRUE(entry->entry_point != NULL);
    }

    double avg_compile_ns =
        (compile_events > 0) ? total_compile_ns / (double)compile_events : 0.0;
    uint64_t compilations_recorded = vm.jit_compilation_count - base_compilations;
    uint64_t translation_success_delta =
        vm.jit_translation_success_count - base_translation_success;
    uint64_t translation_failure_delta =
        vm.jit_translation_failures.total_failures - base_failure_log.total_failures;

    uint64_t reason_delta[ORUS_JIT_TRANSLATE_STATUS_COUNT] = {0};
    for (size_t i = 0; i < ORUS_JIT_TRANSLATE_STATUS_COUNT; ++i) {
        reason_delta[i] = vm.jit_translation_failures.reason_counts[i] -
                          base_failure_log.reason_counts[i];
    }

    uint64_t kind_delta[ORUS_JIT_VALUE_KIND_COUNT] = {0};
    for (size_t kind = 0; kind < ORUS_JIT_VALUE_KIND_COUNT; ++kind) {
        kind_delta[kind] = vm.jit_translation_failures.value_kind_counts[kind] -
                           base_failure_log.value_kind_counts[kind];
    }

    entry = vm_jit_lookup_entry(0, 0);
    ASSERT_TRUE(entry != NULL);
    ASSERT_TRUE(entry->entry_point != NULL);

    const JITBackendVTable* vtable = orus_jit_backend_vtable();
    ASSERT_TRUE(vtable != NULL);
    ASSERT_TRUE(vtable->enter != NULL);

    uint64_t base_invocations = vm.jit_invocation_count;
    uint64_t base_dispatches = vm.jit_native_dispatch_count;
    uint64_t base_type_deopts = vm.jit_native_type_deopts;

    struct timespec jit_start = {0};
    struct timespec jit_end = {0};

    clock_gettime(CLOCK_MONOTONIC, &jit_start);
    for (uint64_t i = 0; i < jit_iterations; ++i) {
        vtable->enter((struct VM*)&vm, entry);
        vm.jit_invocation_count++;
    }
    clock_gettime(CLOCK_MONOTONIC, &jit_end);

    double total_jit_ns = timespec_diff_ns(&jit_start, &jit_end);
    double ns_per_call = total_jit_ns / (double)jit_iterations;
    double calls_per_second = ns_per_call > 0.0 ? 1e9 / ns_per_call : 0.0;

    uint64_t invocations_recorded = vm.jit_invocation_count - base_invocations;
    uint64_t dispatches_recorded = vm.jit_native_dispatch_count - base_dispatches;
    uint64_t type_deopts_recorded = vm.jit_native_type_deopts - base_type_deopts;

    double speedup = (ns_per_call > 0.0)
                         ? (interpreter_ns_per_call / ns_per_call)
                         : 0.0;

    printf("[JIT Benchmark:%s] average tier-up latency: %.0f ns over %zu runs\n",
           label, avg_compile_ns, compile_events);
    printf("[JIT Benchmark:%s] interpreter latency: %.2f ns per call (%.2f M calls/sec)\n",
           label, interpreter_ns_per_call, interpreter_calls_per_second / 1e6);
    printf("[JIT Benchmark:%s] native entry latency: %.2f ns per call (%.2f M calls/sec)\n",
           label, ns_per_call, calls_per_second / 1e6);
    printf("[JIT Benchmark:%s] speedup vs interpreter: %.2fx\n",
           label, speedup);
    printf("[JIT Benchmark:%s] native compilations recorded: %" PRIu64 "\n",
           label, compilations_recorded);
    printf("[JIT Benchmark:%s] native invocations recorded: %" PRIu64 "\n",
           label, invocations_recorded);
    printf("[JIT Benchmark:%s] translations: %" PRIu64 " succeeded, %" PRIu64
           " failed\n",
           label, translation_success_delta, translation_failure_delta);
    printf("[JIT Benchmark:%s] rollout stage: %s (mask=0x%X)\n", label,
           orus_jit_rollout_stage_name(vm.jit_rollout.stage),
           vm.jit_rollout.enabled_kind_mask);
    uint64_t rollout_blocked =
        reason_delta[ORUS_JIT_TRANSLATE_STATUS_ROLLOUT_DISABLED];

    if (translation_failure_delta > 0) {
        printf("[JIT Benchmark:%s] failure breakdown:\n", label);
        for (size_t i = 0; i < ORUS_JIT_TRANSLATE_STATUS_COUNT; ++i) {
            if (reason_delta[i] == 0) {
                continue;
            }
            printf("    - %s: %" PRIu64 "\n",
                   orus_jit_translation_status_name(
                       (OrusJitTranslationStatus)i),
                   reason_delta[i]);
        }
        printf("    - failure by value kind:\n");
        double failure_total = (double)translation_failure_delta;
        for (size_t kind = 0; kind < ORUS_JIT_VALUE_KIND_COUNT; ++kind) {
            uint64_t count = kind_delta[kind];
            if (count == 0) {
                continue;
            }
            double share =
                (failure_total > 0.0) ? (100.0 * (double)count / failure_total)
                                      : 0.0;
            printf("        * %s: %" PRIu64 " (%.1f%%)\n",
                   orus_jit_value_kind_name((OrusJitValueKind)kind),
                   count,
                   share);
        }
        if (vm.jit_translation_failures.count > 0) {
            size_t history_size = ORUS_JIT_TRANSLATION_FAILURE_HISTORY;
            size_t last_index =
                (vm.jit_translation_failures.next_index + history_size - 1u) %
                history_size;
            const OrusJitTranslationFailureRecord* last_failure =
                &vm.jit_translation_failures.records[last_index];
            printf("    - last failure: reason=%s opcode=%u kind=%u func=%u "
                   "loop=%u bytecode=%u\n",
                   orus_jit_translation_status_name(last_failure->status),
                   (unsigned)last_failure->opcode,
                   (unsigned)last_failure->value_kind,
                   (unsigned)last_failure->function_index,
                   (unsigned)last_failure->loop_index,
                   last_failure->bytecode_offset);
        }
    }
    if (rollout_blocked > 0) {
        printf("[JIT Benchmark:%s] notice: %" PRIu64
               " translations blocked by rollout stage %s\n",
               label, rollout_blocked,
               orus_jit_rollout_stage_name(vm.jit_rollout.stage));
    }

    printf("[JIT Benchmark:%s] native dispatches: %" PRIu64
           ", type guard bailouts: %" PRIu64 "\n",
           label, dispatches_recorded, type_deopts_recorded);
    printf("[JIT Benchmark:%s] cache hits: %" PRIu64
           ", cache misses: %" PRIu64 ", deopts: %" PRIu64 "\n",
           label, vm.jit_cache_hit_count - base_cache_hits,
           vm.jit_cache_miss_count - base_cache_misses,
           vm.jit_deopt_count - base_deopts);

    freeVM();
    return true;
}

TEST_CASE(test_jit_backend_benchmark) {
    initVM();
    bool backend_ready = vm.jit_enabled && vm.jit_backend;
    freeVM();

    if (!backend_ready) {
        puts("[JIT Benchmark] DynASM backend unavailable - skipping benchmark.");
        return true;
    }

    const struct {
        const char* label;
        JitFixtureInstaller installer;
        OrusJitRolloutStage stage;
    } cases[] = {
        {"i32", install_linear_i32_fixture, ORUS_JIT_ROLLOUT_STAGE_I32_ONLY},
        {"i64", install_linear_i64_fixture, ORUS_JIT_ROLLOUT_STAGE_WIDE_INTS},
#ifdef OP_LOAD_U64_CONST
        {"u64", install_linear_u64_fixture, ORUS_JIT_ROLLOUT_STAGE_WIDE_INTS},
#endif
        {"f64", install_linear_f64_fixture, ORUS_JIT_ROLLOUT_STAGE_FLOATS},
        {"string", install_linear_string_fixture,
         ORUS_JIT_ROLLOUT_STAGE_STRINGS},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        if (!run_jit_benchmark_case(cases[i].label, cases[i].installer,
                                    cases[i].stage, 100000ULL, 1000000ULL, 5u)) {
            return false;
        }
    }

    puts("JIT benchmark suite completed.");
    return true;
}

TEST_CASE(test_jit_real_program_benchmark) {
    const char* path = "tests/benchmarks/optimized_loop_benchmark.orus";
    OrusJitRunStats interpreter_stats = {0};
    OrusJitRunStats jit_stats = {0};

    ASSERT_TRUE(vm_jit_benchmark_file(path, &interpreter_stats, &jit_stats));

    double interpreter_ms = interpreter_stats.duration_ns / 1e6;
    double jit_ms = jit_stats.duration_ns / 1e6;
    double speedup = (jit_ms > 0.0) ? (interpreter_ms / jit_ms) : 0.0;

    printf("[JIT Real Benchmark] interpreter runtime: %.2f ms\n", interpreter_ms);
    printf("[JIT Real Benchmark] jit runtime: %.2f ms\n", jit_ms);
    printf("[JIT Real Benchmark] speedup: %.2fx\n", speedup);
    printf("[JIT Real Benchmark] translations: %" PRIu64
           " succeeded, %" PRIu64 " failed\n",
           jit_stats.translation_success, jit_stats.translation_failure);
    printf("[JIT Real Benchmark] rollout stage: %s (mask=0x%X)\n",
           orus_jit_rollout_stage_name(jit_stats.rollout_stage),
           jit_stats.rollout_mask);
    if (jit_stats.failure_log.total_failures > 0) {
        printf("[JIT Real Benchmark] failure breakdown:\n");
        for (size_t i = 0; i < ORUS_JIT_TRANSLATE_STATUS_COUNT; ++i) {
            uint64_t count = jit_stats.failure_log.reason_counts[i];
            if (count == 0) {
                continue;
            }
            printf("    - %s: %" PRIu64 "\n",
                   orus_jit_translation_status_name(
                       (OrusJitTranslationStatus)i),
                   count);
        }
        printf("    - failure by value kind:\n");
        double total_failures = (double)jit_stats.failure_log.total_failures;
        for (size_t kind = 0; kind < ORUS_JIT_VALUE_KIND_COUNT; ++kind) {
            uint64_t count = jit_stats.failure_log.value_kind_counts[kind];
            if (count == 0) {
                continue;
            }
            double share =
                (total_failures > 0.0)
                    ? (100.0 * (double)count / total_failures)
                    : 0.0;
            printf("        * %s: %" PRIu64 " (%.1f%%)\n",
                   orus_jit_value_kind_name((OrusJitValueKind)kind),
                   count,
                   share);
        }
        if (jit_stats.failure_log.count > 0) {
            size_t history_size = ORUS_JIT_TRANSLATION_FAILURE_HISTORY;
            size_t last_index =
                (jit_stats.failure_log.next_index + history_size - 1u) %
                history_size;
            const OrusJitTranslationFailureRecord* last_failure =
                &jit_stats.failure_log.records[last_index];
            printf("    - last failure: reason=%s opcode=%u kind=%u func=%u "
                   "loop=%u bytecode=%u\n",
                   orus_jit_translation_status_name(last_failure->status),
                   (unsigned)last_failure->opcode,
                   (unsigned)last_failure->value_kind,
                   (unsigned)last_failure->function_index,
                   (unsigned)last_failure->loop_index,
                   last_failure->bytecode_offset);
        }
    }
    uint64_t real_rollout_blocked =
        jit_stats.failure_log
            .reason_counts[ORUS_JIT_TRANSLATE_STATUS_ROLLOUT_DISABLED];
    if (real_rollout_blocked > 0) {
        printf("[JIT Real Benchmark] notice: %" PRIu64
               " translations blocked by rollout stage %s\n",
               real_rollout_blocked,
               orus_jit_rollout_stage_name(jit_stats.rollout_stage));
    }
    printf("[JIT Real Benchmark] native dispatches: %" PRIu64
           ", cache hits: %" PRIu64 ", cache misses: %" PRIu64
           ", deopts: %" PRIu64 "\n",
           jit_stats.native_dispatches,
           jit_stats.cache_hits,
           jit_stats.cache_misses,
           jit_stats.deopts);

    if (jit_stats.translation_success == 0 || jit_stats.native_dispatches == 0) {
        printf("[JIT Real Benchmark] warning: baseline tier did not translate this program; "
               "execution remained in the interpreter.\n");
    }

    return true;
}

int main(void) {
    if (!test_jit_backend_benchmark()) {
        return 1;
    }

    if (!test_jit_real_program_benchmark()) {
        return 1;
    }

    puts("JIT benchmark suite completed.");
    return 0;
}
