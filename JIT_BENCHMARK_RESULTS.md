# Orus JIT Benchmark Results

The following measurements were collected by running `make test` on the release build profile inside the Orus repository. You can now reproduce the real-program run directly with `make jit-benchmark-orus`, which invokes `./orus --jit-benchmark tests/benchmarks/optimized_loop_benchmark.orus`.

- **Average tier-up latency:** 309,221 ns over 1 run
- **Interpreter latency:** 335.49 ns per call (2.98 million calls/sec)
- **Native entry latency:** 11.07 ns per call (90.30 million calls/sec)
- **Speedup vs interpreter:** 30.30×
- **Native compilations recorded:** 1
- **Native invocations recorded:** 1,000,000
- **Translations:** 1 succeeded, 0 failed
- **Native dispatches:** 0, **Type guard bailouts:** 0
- **Cache hits:** 5, **Cache misses:** 1, **Deopts:** 0

These numbers come from the `tests/unit/test_vm_jit_benchmark.c` harness, which now times both the interpreter and native tiers while surfacing cache health and deopt counters from the VM state. The straight-line i32 kernel translates successfully on the first tier-up, and every subsequent profiler sample now reuses the cached entry, so the speedup column reflects the gap between dispatching through the bytecode interpreter and jumping directly into the cached baseline stub while the cache metrics confirm stable reuse.

## Optimized Loop Benchmark (`tests/benchmarks/optimized_loop_benchmark.orus`)

- **Interpreter runtime (JIT disabled):** 15,163.79 ms
- **JIT-enabled runtime:** 15,070.03 ms
- **Observed speedup:** 1.01×
- **Translations:** 0 succeeded, 0 failed
- **Native dispatches:** 0, **Cache hits:** 0, **Cache misses:** 0, **Deopts:** 0

Running the full optimized loop benchmark through the harness exercises the compiler and VM end-to-end. The telemetry confirms the baseline tier never translates this workload yet, because the loop’s arithmetic promotes to 64-bit integers and falls outside the current i32-only emitter. The nearly identical runtimes show execution still falls back to the interpreter, giving us a concrete target for the next opcode families to port into the baseline code generator.
