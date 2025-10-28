# Orus JIT Benchmark Results

The following measurements were collected by running `zig build test -Dprofile=release` inside the Orus repository. You can now reproduce the real-program run directly with `zig build jit-benchmark -Dprofile=release [-Dstrict-jit=true]`, which invokes `./orus --jit-benchmark tests/benchmarks/optimized_loop_benchmark.orus`.

- **Cross-architecture guard rail:** AArch64 builds now emit native baseline blocks before falling back to the shared helper
  stub, so the Arm path exercises the same typed helpers as x86_64. CI still runs `ORUS_JIT_FORCE_HELPER_STUB=1` while executing
  the benchmark matrix to validate the fallback path, and the backend smoke suite continues to replay the helper mode checks
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

- **Interpreter runtime (JIT disabled):** 4,625.41 ms
- **JIT-enabled runtime:** 4,444.85 ms
- **Observed speedup:** 1.04×
- **Translations:** 3 succeeded, 0 failed (baseline IR is produced)
- **Tier-up skips:** 2,226 total – 2,223 `loop-blocklisted`, 3 `backend-unsupported`
- **Native compilations recorded:** 0
- **Native dispatches:** 3, **Cache hits:** 0, **Cache misses:** 3, **Deopts:** 0, **Type guard bailouts:** 0
- **Rollout stage:** `strings` (mask `0x7F`); loops now enter the linear emitter by default, but previously blocklisted loops still dominate the sample stream so the headline uplift remains well below target.

Enabling the linear emitter by default removes the helper-stub fallback for the hot numeric loops, trimming roughly 180 ms from the JIT run. The translation pipeline still blocklists the majority of loop candidates, so the workload only reaches a 1.04× uplift—further backend and translator work is required before the benchmark can approach the 2–3× goal.

## FFI Ping/Pong Benchmark (`tests/benchmarks/ffi_ping_pong_benchmark.orus`)

- **Interpreter runtime (JIT disabled):** 545.42 ms
- **JIT-enabled runtime:** 533.09 ms
- **Observed speedup:** 1.02×
- **Translations:** 0 succeeded, 1 failed (`unsupported_value_kind` for string operands)
- **Native dispatches:** 1, **Cache hits:** 0, **Cache misses:** 1, **Deopts:** 1, **Type guard bailouts:** 0
- **Rollout stage:** `strings` (mask `0x7F`); the slow-path trampoline keeps the native frame resident during the foreign call, but the translator still blocklists the loop on string value kinds.

The refreshed run confirms that foreign calls no longer tear down the native frame—the VM records one native dispatch and the
`jit_foreign_slow_path_trampolines` counter increments during tiering—but the translator still returns `unsupported_value_kind`
as soon as the loop executes the surrounding string operations. The workload therefore remains interpreter-bound even though the
FFI trampoline now preserves native frames.

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

Command: `ORUS_JIT_FORCE_DYNASM=1 zig build jit-benchmark -Dprofile=release -Dstrict-jit=true`

- **Optimized Loop Benchmark:** interpreter 5033.20 ms, JIT 4996.94 ms, speedup 1.01×
- **FFI Ping/Pong Benchmark:** interpreter 811.24 ms, JIT 809.55 ms, speedup 1.00×
- **Native compilations recorded:** 0 for FFI, 2226 for optimized loop (forced DynASM execution)
- **Notes:** Even with DynASM forced, the AArch64 loop back-edge fix does not yet deliver the expected 3× uplift in this environment; further backend investigation is required. Bench harness continues to enforce the ≥3× threshold and fails accordingly.
