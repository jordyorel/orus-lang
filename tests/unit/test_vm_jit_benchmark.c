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

static bool write_load_i32_const(Chunk* chunk,
                                 uint8_t dst,
                                 uint16_t constant_index,
                                 const char* file,
                                 int line,
                                 int column) {
    if (!chunk) {
        return false;
    }

    writeChunk(chunk, OP_LOAD_I32_CONST, line, column, file);
    writeChunk(chunk, dst, line, column, file);
    writeChunk(chunk, (uint8_t)((constant_index >> 8) & 0xFF), line, column, file);
    writeChunk(chunk, (uint8_t)(constant_index & 0xFF), line, column, file);
    return true;
}

static bool install_linear_jit_fixture(Function* function) {
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

    const char* file = "jit_benchmark";
    const int line = 1;
    const int column = 1;

    int const_zero = addConstant(function->chunk, I32_VAL(0));
    int const_one = addConstant(function->chunk, I32_VAL(1));
    int const_two = addConstant(function->chunk, I32_VAL(2));
    int const_three = addConstant(function->chunk, I32_VAL(3));
    if (const_zero < 0 || const_one < 0 || const_two < 0 || const_three < 0) {
        return false;
    }

    if (!write_load_i32_const(function->chunk, 0, (uint16_t)const_zero, file, line,
                              column)) {
        return false;
    }
    if (!write_load_i32_const(function->chunk, 1, (uint16_t)const_one, file, line,
                              column)) {
        return false;
    }
    if (!write_load_i32_const(function->chunk, 2, (uint16_t)const_two, file, line,
                              column)) {
        return false;
    }
    if (!write_load_i32_const(function->chunk, 3, (uint16_t)const_three, file, line,
                              column)) {
        return false;
    }

    writeChunk(function->chunk, OP_ADD_I32_TYPED, line, column, file);
    writeChunk(function->chunk, 0, line, column, file);
    writeChunk(function->chunk, 0, line, column, file);
    writeChunk(function->chunk, 1, line, column, file);

    writeChunk(function->chunk, OP_ADD_I32_TYPED, line, column, file);
    writeChunk(function->chunk, 0, line, column, file);
    writeChunk(function->chunk, 0, line, column, file);
    writeChunk(function->chunk, 2, line, column, file);

    writeChunk(function->chunk, OP_SUB_I32_TYPED, line, column, file);
    writeChunk(function->chunk, 0, line, column, file);
    writeChunk(function->chunk, 0, line, column, file);
    writeChunk(function->chunk, 1, line, column, file);

    writeChunk(function->chunk, OP_MUL_I32_TYPED, line, column, file);
    writeChunk(function->chunk, 0, line, column, file);
    writeChunk(function->chunk, 0, line, column, file);
    writeChunk(function->chunk, 3, line, column, file);

    writeChunk(function->chunk, OP_RETURN_VOID, line, column, file);

    vm_store_i32_typed_hot(0, 0);
    vm_store_i32_typed_hot(1, 0);
    vm_store_i32_typed_hot(2, 0);
    vm_store_i32_typed_hot(3, 0);

    return true;
}

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

TEST_CASE(test_jit_backend_benchmark) {
    initVM();

    if (!vm.jit_enabled || !vm.jit_backend) {
        puts("[JIT Benchmark] DynASM backend unavailable - skipping benchmark.");
        freeVM();
        return true;
    }

    vm.functionCount = 1;

    ASSERT_TRUE(install_linear_jit_fixture(&vm.functions[0]));

    const uint64_t interpreter_iterations = 100000ULL;
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

    ASSERT_TRUE(install_linear_jit_fixture(&vm.functions[0]));

    const size_t cache_trials = 5;
    double total_compile_ns = 0.0;
    size_t compile_events = 0;
    uint64_t base_compilations = vm.jit_compilation_count;
    uint64_t base_translation_success = vm.jit_translation_success_count;
    uint64_t base_translation_failure = vm.jit_translation_failure_count;
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
        vm.jit_translation_failure_count - base_translation_failure;

    entry = vm_jit_lookup_entry(0, 0);
    ASSERT_TRUE(entry != NULL);
    ASSERT_TRUE(entry->entry_point != NULL);

    const JITBackendVTable* vtable = orus_jit_backend_vtable();
    ASSERT_TRUE(vtable != NULL);
    ASSERT_TRUE(vtable->enter != NULL);

    const uint64_t iterations = 1000000ULL;
    uint64_t base_invocations = vm.jit_invocation_count;
    uint64_t base_dispatches = vm.jit_native_dispatch_count;
    uint64_t base_type_deopts = vm.jit_native_type_deopts;

    struct timespec jit_start = {0};
    struct timespec jit_end = {0};

    clock_gettime(CLOCK_MONOTONIC, &jit_start);
    for (uint64_t i = 0; i < iterations; ++i) {
        vtable->enter((struct VM*)&vm, entry);
        vm.jit_invocation_count++;
    }
    clock_gettime(CLOCK_MONOTONIC, &jit_end);

    double total_jit_ns = timespec_diff_ns(&jit_start, &jit_end);
    double ns_per_call = total_jit_ns / (double)iterations;
    double calls_per_second = ns_per_call > 0.0 ? 1e9 / ns_per_call : 0.0;

    uint64_t invocations_recorded = vm.jit_invocation_count - base_invocations;
    uint64_t dispatches_recorded = vm.jit_native_dispatch_count - base_dispatches;
    uint64_t type_deopts_recorded = vm.jit_native_type_deopts - base_type_deopts;

    printf("[JIT Benchmark] average tier-up latency: %.0f ns over %zu runs\n",
           avg_compile_ns, compile_events);
    printf("[JIT Benchmark] interpreter latency: %.2f ns per call (%.2f M calls/sec)\n",
           interpreter_ns_per_call, interpreter_calls_per_second / 1e6);
    printf("[JIT Benchmark] native entry latency: %.2f ns per call (%.2f M calls/sec)\n",
           ns_per_call, calls_per_second / 1e6);
    double speedup = (ns_per_call > 0.0)
                         ? (interpreter_ns_per_call / ns_per_call)
                         : 0.0;
    printf("[JIT Benchmark] speedup vs interpreter: %.2fx\n", speedup);
    printf("[JIT Benchmark] native compilations recorded: %" PRIu64 "\n",
           compilations_recorded);
    printf("[JIT Benchmark] native invocations recorded: %" PRIu64 "\n",
           invocations_recorded);
    printf("[JIT Benchmark] translations: %" PRIu64 " succeeded, %" PRIu64
           " failed\n",
           translation_success_delta, translation_failure_delta);
    printf("[JIT Benchmark] native dispatches: %" PRIu64
           ", type guard bailouts: %" PRIu64 "\n",
           dispatches_recorded, type_deopts_recorded);
    printf("[JIT Benchmark] cache hits: %" PRIu64
           ", cache misses: %" PRIu64 ", deopts: %" PRIu64 "\n",
           vm.jit_cache_hit_count - base_cache_hits,
           vm.jit_cache_miss_count - base_cache_misses,
           vm.jit_deopt_count - base_deopts);

    freeVM();
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
