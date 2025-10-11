// Orus Language Project
// ---------------------------------------------------------------------------
// File: include/vm/jit_benchmark.h
// Description: Shared helpers for executing Orus programs under the JIT
//              benchmark harness. Provides reusable structures for collecting
//              JIT telemetry and utility runners that manage VM lifecycle
//              around benchmark executions.

#ifndef ORUS_VM_JIT_BENCHMARK_H
#define ORUS_VM_JIT_BENCHMARK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Aggregated counters captured after executing a program with the JIT either
// disabled (interpreter baseline) or enabled. Timings are recorded in
// nanoseconds using CLOCK_MONOTONIC to align with other VM benchmarking code.
typedef struct OrusJitRunStats {
    double duration_ns;
    uint64_t compilation_count;
    uint64_t translation_success;
    uint64_t translation_failure;
    uint64_t cache_hits;
    uint64_t cache_misses;
    uint64_t invocations;
    uint64_t native_dispatches;
    uint64_t native_type_deopts;
    uint64_t deopts;
} OrusJitRunStats;

// Execute the provided source buffer under either interpreter or JIT mode and
// populate |stats| with the resulting telemetry. The helper fully manages VM
// lifecycle (string table, profiling, error reporting) so callers need not
// perform any setup beyond providing the source buffer.
bool vm_jit_run_source_benchmark(const char* source,
                                 size_t source_len,
                                 const char* path,
                                 bool enable_jit,
                                 OrusJitRunStats* stats);

// Convenience wrapper that loads |path|, runs it once with the JIT disabled to
// capture interpreter baseline statistics, then runs again with the JIT
// enabled. Results are written into |interpreter_stats| and |jit_stats|
// respectively. Returns false if either execution fails.
bool vm_jit_benchmark_file(const char* path,
                           OrusJitRunStats* interpreter_stats,
                           OrusJitRunStats* jit_stats);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // ORUS_VM_JIT_BENCHMARK_H
