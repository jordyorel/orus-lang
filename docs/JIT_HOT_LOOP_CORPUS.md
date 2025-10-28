# Phase 4 Hot Loop Benchmark Corpus

To make the Phase 4 performance exit criteria reproducible, we now treat three
hot-loop workloads as the canonical benchmark corpus. Each program focuses on a
different pathological behavior we expect the JIT to accelerate over the
interpreter. The CLI `--jit-benchmark` harness is used to collect comparable
interpreter and tiered runtimes.

## Measurement Protocol

* Build the release configuration once with `zig build -Dprofile=release`.
* Run each benchmark with `./orus --jit-benchmark <program>` so the harness
  executes a warm interpreter pass followed by a tiered run.
* Record the interpreter-only runtime that the harness prints. When the JIT has
  not translated a workload yet, the interpreter runtime is still emitted and
  provides the baseline we must beat once native codegen lands.
* Capture the raw logs under `artifacts/benchmarks/` for later comparison (the
  `scripts/measure_hot_loop_baselines.py` helper does this automatically).

> **Note:** The interpreter timings below were gathered on the reference
> container image on 2024-07-05 using `zig build -Dprofile=release`. Later runs
> should be recorded alongside the exact git revision and rollout stage.

## Baseline Interpreter Timings

| Workload focus        | Program path                                           | Trials | Interpreter runtime (ms) | Notes |
|-----------------------|--------------------------------------------------------|--------|---------------------------|-------|
| Numeric loops (fused) | `tests/benchmarks/optimized_loop_benchmark.orus`       | 5      | 4002.94                   | Exercises fused increment/compare/jump loops that now translate in the baseline tier. |
| Mixed object access   | `tests/benchmarks/string_concat_benchmark.orus`        | 5      | 239.46                    | Stresses string builder churn and boxed value traffic during repeated concatenation. |
| Numeric micro loops   | `tests/benchmarks/typed_fastpath_benchmark.orus`       | 5      | 950.36                    | Confirms typed register windows stay hot when we loop on i32 math and cache moves. |
| FFI ping/pong         | `tests/benchmarks/ffi_ping_pong_benchmark.orus`        | 5      | 2,096.45                  | Translator now lowers `OP_CALL_FOREIGN`; harness mirrors nested foreign roundtrips while dedicated FFI registry work continues. |

The FFI workload now bounces across the host boundary with a tight foreign call
loop so we can measure allocator pressure and safepoint cadence. Translator
coverage keeps the path native-ready while the remaining GC-safe foreign call
infrastructure lands. Numeric and mixed workloads continue to provide the
regression guard rails for the 3–5× throughput target.

## Automation Helper

The `scripts/measure_hot_loop_baselines.py` helper script encapsulates the
protocol above. It iterates over the corpus, runs the harness, extracts the
interpreter runtime, and emits a machine-readable JSON summary alongside the raw
logs so CI can track drift over time.
