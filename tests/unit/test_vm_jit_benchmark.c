#define _POSIX_C_SOURCE 199309L
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include "vm/jit_backend.h"
#include "vm/vm.h"
#include "vm/vm_profiling.h"
#include "vm/vm_tiering.h"

#define TEST_CASE(name) static bool name(void)
#define ASSERT_TRUE(cond)                                                        \
    do {                                                                         \
        if (!(cond)) {                                                           \
            fprintf(stderr, "Assertion failed: %s (%s:%d)\n", #cond, __FILE__,   \
                    __LINE__);                                                   \
            return false;                                                        \
        }                                                                        \
    } while (0)

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

    const size_t compile_trials = 5;
    double total_compile_ns = 0.0;
    uint64_t base_compilations = vm.jit_compilation_count;

    for (size_t trial = 0; trial < compile_trials; ++trial) {
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

        total_compile_ns += timespec_diff_ns(&compile_start, &compile_end);

        JITEntry* entry = vm_jit_lookup_entry(0, 0);
        ASSERT_TRUE(entry != NULL);
        ASSERT_TRUE(entry->entry_point != NULL);
    }

    double avg_compile_ns = total_compile_ns / (double)compile_trials;
    uint64_t compilations_recorded = vm.jit_compilation_count - base_compilations;

    JITEntry* entry = vm_jit_lookup_entry(0, 0);
    ASSERT_TRUE(entry != NULL);
    ASSERT_TRUE(entry->entry_point != NULL);

    const JITBackendVTable* vtable = orus_jit_backend_vtable();
    ASSERT_TRUE(vtable != NULL);
    ASSERT_TRUE(vtable->enter != NULL);

    const uint64_t iterations = 1000000ULL;
    uint64_t base_invocations = vm.jit_invocation_count;

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

    printf("[JIT Benchmark] average tier-up latency: %.0f ns over %zu runs\n",
           avg_compile_ns, compile_trials);
    printf("[JIT Benchmark] native entry latency: %.2f ns per call (%.2f M calls/sec)\n",
           ns_per_call, calls_per_second / 1e6);
    printf("[JIT Benchmark] native compilations recorded: %" PRIu64 "\n",
           compilations_recorded);
    printf("[JIT Benchmark] native invocations recorded: %" PRIu64 "\n",
           invocations_recorded);

    freeVM();
    return true;
}

int main(void) {
    if (!test_jit_backend_benchmark()) {
        return 1;
    }

    puts("JIT benchmark completed.");
    return 0;
}
