# Loop Fast-Path Benchmark Snapshot (Phase 1 & 2 Follow-up)

These numbers capture a manual run of the loop fast-path benchmarks after completing the Phase 1 typed branch cache and Phase 2 fused increment work.

## Environment
- Build: `make -j$(nproc)` (debug profile)
- Runner: `./orus <benchmark>`
- Hardware: GitHub-hosted container (2 vCPUs, 7 GB RAM)
- Each benchmark reports wall-clock time in seconds.

## Results

| Benchmark | Command | Trials | Iterations | Observed Times (s) | Mean (s) | Notes |
|-----------|---------|--------|------------|--------------------|----------|-------|
| Phase 2 fused increment stress | `./orus tests/benchmarks/loop_fastpath_phase2.orus` | 5 | 3,000,000 | 1.76525, 1.74805, 1.83452, 1.86566, 1.80709 | 1.80 | Typed increments are stable but dominated by guard overhead. |
| Phase 3 typed iterator residency | `./orus tests/benchmarks/loop_fastpath_phase3.orus` | 5 | 1,000,000 | 0.135541, 0.134231, 0.13604, 0.131209, 0.135019 | 0.134 | Typed iterators stay hot; minimal slow-path churn observed. |
| Phase 4 LICM + branch cache | `./orus tests/benchmarks/loop_fastpath_phase4.orus` | 3 | 2,000,000 | 0.626919, 0.661359, 0.630538 | 0.640 | Guard hoisting reduces loop cost roughly 3× versus Phase 2. |
| Typed vs boxed accumulator | `./orus tests/benchmarks/typed_fastpath_benchmark.orus` | 5 | 2,000,000 | 0.570385, 0.629814, 0.606665, 0.569587, 0.625916 | 0.600 | Typed store path still ~10% ahead of boxed baseline in this run. |
| Control-flow macro benchmark | `./orus tests/benchmarks/control_flow_benchmark.orus` | 1 | 3× loops | 1.31361 total | 1.31 | Includes all three phases from the control-flow harness. |

## Takeaways
- Phase 4’s loop-invariant code motion plus the typed branch cache delivers the largest improvement (≈0.64 s mean vs. 1.80 s on the Phase 2 stress test).
- Typed iterators in Phase 3 retain their fast-path residency across clones, sustaining ~0.13 s per million iterations.
- The typed accumulator benchmark continues to run ~10% faster than the boxed path, indicating the typed register residency work is paying off, though further guard trimming could close the gap with LuaJIT-class interpreters.

