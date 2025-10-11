# Phase 4 Baseline JIT Acceleration Plan

## Current Limitations

The current DynASM-backed tier only emits a return stub through `OrusJitIRProgram`, so every tier-up currently bails back to the interpreter without doing useful work. The IR defines just a single opcode (`ORUS_JIT_IR_OP_RETURN`), and the x86-64/AArch64 backends refuse to assemble anything else, which is why `queue_tier_up()` simply installs a no-op entry today. 【F:include/vm/jit_ir.h†L16-L30】【F:src/vm/jit/orus_jit_backend.c†L260-L302】【F:src/vm/profiling/vm_profiling.c†L553-L609】

## Goal

Deliver measurable tier-4 speedups without waiting for trace recording or inline caches by turning the stub backend into a straight-line compiler for the hot loop/function that triggered the promotion. This keeps the roadmap intact while allowing the VM to demonstrate tangible performance wins before the "Future Enhancement" work lands.

## Step-by-Step Improvements

1. **Expand the OrusJit IR** ✅ *(typed arithmetic and short control-flow skeleton)*
   - Split the monolithic opcodes into type-specific variants for loads, moves, and arithmetic so the backend no longer has to branch on `value_kind` at runtime while still short-circuiting on unsupported types. 【F:include/vm/jit_ir.h†L27-L74】【F:src/vm/jit/orus_jit_backend.c†L1118-L1182】
   - Teach the tier-up translator to emit the richer opcodes while keeping the baseline tier gated to i32 arithmetic, preserving the existing “translation fails for non-i32” behavior until the emitter grows more templates. 【F:src/vm/profiling/vm_profiling.c†L808-L885】
   - Record forward/conditional short jump metadata in the IR so future lowering passes can swap loop sentinels for native branches instead of interpreter fallbacks. 【F:include/vm/jit_ir.h†L55-L66】

2. **Build a Linear Translator in `queue_tier_up()`** ✅ *(loop-scoped straight-line lowering)*
   - `queue_tier_up()` now recovers the sampled function chunk, seeds the IR builder with the loop start offset, and feeds bytecode through the typed opcode maps so real instruction streams reach the backend instead of the old hard-coded return stub. 【F:src/vm/profiling/vm_profiling.c†L702-L897】
   - Translation stays loop scoped by requiring the terminating `OP_LOOP_SHORT` to jump back to the sampled start offset; unsupported branches or value kinds flag the loop for interpreter fallback so guards and exits still execute in the VM. 【F:src/vm/profiling/vm_profiling.c†L760-L820】

3. **Implement Minimal Machine-Code Templates** ✅ *(x86_64 i32 arithmetic loops)*
   - `orus_jit_backend_compile_ir()` now attempts an x86_64 linear emitter before falling back to the helper stub. The emitter hoists typed register pointers, synthesizes straight-line integer arithmetic loops, and patches branch targets so hot loops stay entirely in native code until a safepoint or bailout fires. 【F:src/vm/jit/orus_jit_backend.c†L180-L454】
   - The baseline currently specializes `ORUS_JIT_VALUE_I32` opcodes; other value kinds still deopt to the helper path until additional templates land. Layout constants in `vm/jit_layout.h` guarantee the native code stays ABI-compatible with the interpreter. 【F:include/vm/jit_layout.h†L17-L20】

4. **Insert GC and Type Safety Checks** ✅ *(safepoints and typed guards)*
   - Hoisted the shared `GC_SAFEPOINT` helper into the JIT translator and native dispatch loops so tier-up allocation bursts now trigger `collectGarbage()` just like the interpreter. 【F:include/vm/vm_profiling.h†L209-L219】【F:src/vm/profiling/vm_profiling.c†L748-L821】【F:src/vm/jit/orus_jit_backend.c†L602-L734】
   - Added x86_64 type guards that compare the live register metadata and jump to the bailout stub when mismatches occur, plumbing the failure through `vm_default_deopt_stub()` via the existing deopt pathway. 【F:src/vm/jit/orus_jit_backend.c†L900-L1258】【F:src/vm/runtime/vm_tiering.c†L300-L340】

5. **Cache and Reuse Native Entries** ✅ *(unsupported bytecode invalidation)*
   - Tier-up records now invalidate any previously cached native block when translation bails on unsupported bytecodes, ensuring stale code can’t be re-entered once new opcodes appear. The same path block-lists the loop so we fail fast until support is implemented. 【F:src/vm/profiling/vm_profiling.c†L899-L922】
   - Native cache installation continues to rely on `vm_jit_install_entry()` and the existing deopt plumbing; subsequent tier-ups reuse compiled code as before when translation succeeds. 【F:src/vm/profiling/vm_profiling.c†L919-L936】

6. **Add Microbenchmarks and Telemetry** ✅ *(interpreter vs. native harness instrumentation)*
   - The benchmark harness now drives the interpreter via `vm_run_dispatch()` before re-enabling tiering, so it can print per-call latency, calls-per-second, and the resulting speedup alongside the native path measurements. 【F:tests/unit/test_vm_jit_benchmark.c†L151-L276】
   - `JIT_BENCHMARK_RESULTS.md` captures the expanded telemetry (speedup, cache stats, deopts) so roadmap readers can track translation health and native reuse from a single report. 【F:JIT_BENCHMARK_RESULTS.md†L5-L15】
   - A second harness run executes the real-world `tests/benchmarks/optimized_loop_benchmark.orus` program with and without tiering so the telemetry spells out whether native code ever replaces the interpreter on end-to-end workloads; the latest data shows zero translations because the loop upgrades to i64 arithmetic, underscoring the next opcode families the baseline emitter must support. 【F:tests/unit/test_vm_jit_benchmark.c†L278-L345】
   - The CLI now exposes `--jit-benchmark` so release builds can execute the same interpreter-vs-JIT comparison directly against `.orus` sources, printing the collected counters without running the C test harness. 【F:src/main.c†L158-L215】

## Expected Outcome

These steps keep the current infrastructure (hot-loop sampling, shared frame layout, deopt pathway) but finally let the tier emit useful code. Even without full trace specialization or inline caches, replacing interpreter dispatch on tight numeric loops should deliver the "few ×" wins promised in the Phase 4 exit criteria while laying the groundwork for the later optimization passes.
