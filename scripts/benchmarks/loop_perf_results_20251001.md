# Loop Fast-Path Benchmark Results (2025-10-01)

This snapshot captures the measured throughput and telemetry for the loop fast-path roadmap after all three phases landed.

## Phase 2 – Typed Increment Fast Path

| Variant | Avg Seconds | Std Dev | Iterations / Second | Typed Hits | Typed Misses | Notes |
|---------|-------------|---------|---------------------|------------|--------------|-------|
| typed-fastpath | 1.858730 | 0.129191 | 1,614,005.26 | 15,000,005 | 0 | Baseline with typed increments enabled. |
| kill-switch | 1.774802 | 0.039271 | 1,690,329.40 | 15,000,005 | 0 | Fast path disabled via `ORUS_DISABLE_INC_TYPED_FASTPATH=1`. |

*Speed ratio (kill-switch vs typed-fastpath): 1.047× faster.*

## Phase 3 – Zero-Allocation Iterators

| Variant | Avg Seconds | Std Dev | Iterations / Second | Typed Hits | Typed Misses | Iter Alloc Saved | Iter Fallbacks |
|---------|-------------|---------|---------------------|------------|--------------|------------------|----------------|
| typed-iter | 0.137066 | 0.007277 | 7,295,776.62 | 11,315,973 | 0 | 5,125 | 0 |
| force-boxed | 0.150944 | 0.015459 | 6,624,953.02 | 5,660,613 | 5,125 | 0 | 5,125 |

*Typed iterators ran 10.1% faster than the boxed fallback while saving 5,125 allocations per run.*

## Phase 4 – LICM Typed Guards

| Variant | Avg Seconds | Std Dev | Iterations / Second | Typed Hits |
|---------|-------------|---------|---------------------|------------|
| licm-on | 0.647874 | 0.018288 | 3,087,020.54 | 12,000,003 |
| licm-off | 0.605248 | 0.015620 | 3,304,431.19 | 12,000,003 |

*With LICM typed guards enabled, throughput trailed the guard-disabled run by roughly 6.9%. Further tuning may be required to recover the expected speedup.*

