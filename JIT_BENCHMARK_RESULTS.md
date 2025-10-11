# Orus JIT Benchmark Results

The following measurements were collected by running `make test` on the release build profile inside the Orus repository. You can now reproduce the real-program run directly with `make jit-benchmark-orus`, which invokes `./orus --jit-benchmark tests/benchmarks/optimized_loop_benchmark.orus`.

- **Cross-architecture guard rail:** AArch64 builds now emit native baseline blocks before falling back to the shared helper
  stub, so the Arm path exercises the same typed helpers as x86_64. CI still runs `ORUS_JIT_FORCE_HELPER_STUB=1` while executing
  the benchmark matrix to validate the fallback path, and `make jit-cross-arch-tests` replays the backend smoke suite in helper
  mode prior to running the translator checks.
- **Trivial stub retired:** Unsupported translations now stay blocklisted without compiling a per-loop return stub, so benchmark
  counters only rise when the backend emits real native blocks. Catastrophic failures (out-of-memory, invalid input) still fall
  back to the helper stub for safety.

- **Average tier-up latency:** 309,221 ns over 1 run
- **Interpreter latency:** 335.49 ns per call (2.98 million calls/sec)
- **Native entry latency:** 11.07 ns per call (90.30 million calls/sec)
- **Speedup vs interpreter:** 30.30×
- **Native compilations recorded:** 1
- **Native invocations recorded:** 1,000,000
- **Translations:** 1 succeeded, 0 failed
- **Failure breakdown:** None recorded; per-type counters (i32, i64, u64, f64, string) all report 0 bailouts.
- **Native dispatches:** 0, **Type guard bailouts:** 0
- **Cache hits:** 5, **Cache misses:** 1, **Deopts:** 0
- **Rollout stage:** `strings` (mask `0x3F`) – harness kernels explicitly advance the rollout to exercise every value kind.

These numbers come from the `tests/unit/test_vm_jit_benchmark.c` harness, which now times both the interpreter and native tiers while surfacing cache health and deopt counters from the VM state. The straight-line i32 kernel translates successfully on the first tier-up, and every subsequent profiler sample now reuses the cached entry, so the speedup column reflects the gap between dispatching through the bytecode interpreter and jumping directly into the cached baseline stub while the cache metrics confirm stable reuse.

## Optimized Loop Benchmark (`tests/benchmarks/optimized_loop_benchmark.orus`)

- **Interpreter runtime (JIT disabled):** 15,163.79 ms
- **JIT-enabled runtime:** 15,070.03 ms
- **Observed speedup:** 1.01×
- **Translations:** 0 succeeded, 0 failed
- **Native dispatches:** 0, **Cache hits:** 0, **Cache misses:** 0, **Deopts:** 0
- **Rollout stage:** `strings` (mask `0x3F`); translation still never triggers despite the full rollout, leaving execution in the interpreter for now.

Running the full optimized loop benchmark through the harness exercises the compiler and VM end-to-end. Advancing the rollout to `strings` removes the staging guard that previously blocked the workload, but the profiler still never emits a successful tier-up for this program, so the optimized loop continues to execute in the interpreter. The nearly identical runtimes give us a clear signal that additional lowering work (beyond value-kind enablement) is still required before the benchmark benefits from the baseline tier.

## Per-Type Tier-Up Tracker

| Value kind | First successful tier-up | Notes |
|------------|--------------------------|-------|
| `i32`      | `tests/unit/test_vm_jit_benchmark.c` synthetic kernel | Baseline path that established the harness, continues to succeed. |
| `i64`      | `tests/unit/test_vm_jit_benchmark.c` synthetic kernel | Widens integer loads and arithmetic through the shared templates. |
| `u64`      | `tests/unit/test_vm_jit_benchmark.c` synthetic kernel | Exercises unsigned widening helpers added in Phase B. |
| `f64`      | `tests/unit/test_vm_jit_benchmark.c` synthetic kernel | Validates the SSE2 emission paths and constant encoding. |
| `string`   | `tests/unit/test_vm_jit_benchmark.c` synthetic kernel | Uses helper-backed concatenation to confirm heap values survive tier-up. |

Every `--jit-benchmark` run now emits both reason-oriented and per-value-kind failure percentages alongside the active rollout stage. When staged value kinds trigger a translation, the run reports a dedicated `rollout_disabled` reason so regressions can be triaged separately from unsupported opcodes.
