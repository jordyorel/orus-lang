# Orus JIT Benchmark Results

The following measurements were collected by running `make test` on the release build profile inside the Orus repository. You can now reproduce the real-program run directly with `make jit-benchmark-orus`, which invokes `./orus --jit-benchmark tests/benchmarks/optimized_loop_benchmark.orus`.

- **Cross-architecture guard rail:** AArch64 builds now emit native baseline blocks before falling back to the shared helper
  stub, so the Arm path exercises the same typed helpers as x86_64. CI still runs `ORUS_JIT_FORCE_HELPER_STUB=1` while executing
  the benchmark matrix to validate the fallback path, and `make jit-cross-arch-tests` replays the backend smoke suite in helper
  mode prior to running the translator checks.
- **Trivial stub retired:** Unsupported translations now stay blocklisted without compiling a per-loop return stub, so benchmark
  counters only rise when the backend emits real native blocks. Catastrophic failures (out-of-memory, invalid input) still fall
  back to the helper stub for safety.
- **Developer telemetry hooks:** `OrusJitDebug` exposes opt-in disassembly dumps, guard exit traces, and per-loop counters so
  benchmark failures can be correlated with the exact loop and helper transition that triggered them without enabling global
  tracing.

- **Interpreter baseline corpus:** `scripts/measure_hot_loop_baselines.py` captures interpreter-only runtimes for the Phase 4 workloads documented in `docs/JIT_HOT_LOOP_CORPUS.md`. Current release-build measurements (2024-07-05) are summarized below.

| Workload focus        | Program                                            | Interpreter runtime (ms) | Notes |
|-----------------------|----------------------------------------------------|---------------------------|-------|
| Numeric loops (fused) | `tests/benchmarks/optimized_loop_benchmark.orus`   | 4002.94                   | Matches the fused-loop workload used in the tier-up roadmap reruns. |
| Mixed object access   | `tests/benchmarks/string_concat_benchmark.orus`    | 239.46                    | Heavily exercises boxed value churn and the string builder path. |
| Numeric micro loops   | `tests/benchmarks/typed_fastpath_benchmark.orus`   | 950.36                    | Validates typed register windows over tight i32 arithmetic. |
| FFI ping/pong         | `tests/benchmarks/ffi_ping_pong_benchmark.orus`             | 2,096.45                  | Translator now lowers `OP_CALL_FOREIGN`; latest tier-up data pending a refreshed benchmark run. |

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

- **Interpreter runtime (JIT disabled):** 16,152.16 ms
- **JIT-enabled runtime:** 16,355.90 ms
- **Observed speedup:** 0.99×
- **Translations:** 2 succeeded, 2 failed (`unsupported_value_kind` guard rails still fire on unknown kinds)
- **Native dispatches:** 524, **Cache hits:** 522, **Cache misses:** 4, **Deopts:** 3, **Type guard bailouts:** 1
- **Rollout stage:** `strings` (mask `0x7F`); cache reuse remains healthy, but the remaining failures erase any net speedup.

The baseline tier still emits native translations for two of the fused loops, yet the outstanding value kind gaps hold the overall speedup under 1×. We need either additional rollout coverage or lower interpreter overhead to regain the 5% improvement observed prior to the latest changes.

## FFI Ping/Pong Benchmark (`tests/benchmarks/ffi_ping_pong_benchmark.orus`)

- **Interpreter runtime (JIT disabled):** 2,096.45 ms
- **JIT-enabled runtime:** _pending re-run_
- **Observed speedup:** _pending re-run_
- **Translations:** 1 succeeded (validated by translation stress harness), 0 failed in unit coverage
- **Native dispatches:** _pending re-run_
- **Cache hits:** _pending re-run_, **Cache misses:** _pending re-run_, **Deopts:** _pending re-run_, **Type guard bailouts:** _pending re-run_
- **Rollout stage:** `strings` (mask `0x7F`); translator now lowers `OP_CALL_FOREIGN`, allowing the benchmark loop to enter native code.

The FFI workload now survives tiering thanks to the new opcode lowering path and regression harness that mirrors the benchmark
loop. Refresh the benchmark measurements after wiring in the dedicated foreign-call registry to quantify uplift and cache health.

## Per-Type Tier-Up Tracker

| Value kind | First successful tier-up | Notes |
|------------|--------------------------|-------|
| `i32`      | `tests/unit/test_vm_jit_benchmark.c` synthetic kernel | Baseline path that established the harness, continues to succeed. |
| `i64`      | `tests/unit/test_vm_jit_benchmark.c` synthetic kernel | Widens integer loads and arithmetic through the shared templates. |
| `u64`      | `tests/unit/test_vm_jit_benchmark.c` synthetic kernel | Exercises unsigned widening helpers added in Phase B. |
| `f64`      | `tests/unit/test_vm_jit_benchmark.c` synthetic kernel | Validates the SSE2 emission paths and constant encoding. |
| `string`   | `tests/unit/test_vm_jit_benchmark.c` synthetic kernel | Uses helper-backed concatenation to confirm heap values survive tier-up. |

Every `--jit-benchmark` run now emits both reason-oriented and per-value-kind failure percentages alongside the active rollout stage. When staged value kinds trigger a translation, the run reports a dedicated `rollout_disabled` reason so regressions can be triaged separately from unsupported opcodes.

## 2025-02-14 Forced DynASM Run

Command: `ORUS_JIT_FORCE_DYNASM=1 make jit-benchmark-orus`

- **Optimized Loop Benchmark:** interpreter 5033.20 ms, JIT 4996.94 ms, speedup 1.01×
- **FFI Ping/Pong Benchmark:** interpreter 811.24 ms, JIT 809.55 ms, speedup 1.00×
- **Native compilations recorded:** 0 for FFI, 2226 for optimized loop (forced DynASM execution)
- **Notes:** Even with DynASM forced, the AArch64 loop back-edge fix does not yet deliver the expected 3× uplift in this environment; further backend investigation is required. Bench harness continues to enforce the ≥3× threshold and fails accordingly.
