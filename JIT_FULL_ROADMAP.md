# Orus JIT Full Roadmap

The Orus JIT has progressed from an experimental i32-only tier to a partially functional baseline that can accelerate synthetic
microkernels. To reach a fully operational, production-ready tier we need a comprehensive roadmap that stitches together
infrastructure, opcode coverage, runtime integration, optimization, and observability. Each item below uses checkboxes to
communicate status at a glance—mark a task with `[x]` when it lands, keep `[ ]` for pending work, and split large items into
subtasks as the implementation evolves.

## Stage 0 – Delivered Foundations

- [x] Establish typed `OrusJitIRProgram` with per-value-kind opcodes for arithmetic, loads, moves, guards, and branches.
- [x] Implement the linear tier-up translator that recovers loop bodies, enforces `OP_LOOP_SHORT` invariants, and emits typed IR.
- [x] Add baseline x86_64 linear emitter for i32 arithmetic plus helper-stub bailouts for unsupported value kinds.
- [x] Integrate GC safepoints, type guards, and deopt shims so native frames stay coherent with interpreter semantics.
- [x] Add benchmarking harnesses (`make jit-benchmark-orus`, `tests/unit/test_vm_jit_benchmark.c`) that compare interpreter vs JIT runs.

## Stage 1 – Baseline Native Coverage Expansion

- [x] Lower wide integer arithmetic (`*_I64`, `*_U64`) and comparisons into DynASM templates on x86_64 and AArch64.
- [x] Lower floating-point arithmetic (`*_F64`) and comparisons with SSE2/NEON parity and flag propagation for branches.
- [x] Implement DynASM constant materialization for all value kinds, including pool loads, frame spills, and pointer moves.
- [x] Emit DynASM helper calls for GC safepoints, runtime math helpers, and VM service routines without forcing deopts.
- [x] Provide typed deopt landing pads so guard exits reuse interpreter frames instead of regenerating helper stubs.
  - `vm_handle_type_error_deopt()` now applies landing pad metadata that clears frame, parameter, and temp typed windows before
    handing control back to baseline bytecode, and guard exits reuse this path instead of re-materializing helper stubs.
- [x] Update backend unit tests (`tests/unit/test_vm_jit_backend.c`) to assert DynASM parity with the linear emitter across value kinds.

## Stage 2 – Translator Hardening & Loop Resilience

- [x] Extend translator diagnostics to categorize unsupported bytecodes, malformed loop shapes, and disabled rollout stages.
- [x] Support loop patterns with conditional exits, nested branches, and in-loop helper calls while preserving safety invariants.
- [x] Track value-kind promotion so widened locals (i32→i64/u64) reuse existing IR nodes instead of triggering bailouts.
- [x] Annotate FFI call sites with spill metadata so translated loops can cross the C boundary and resume in native code.
- [x] Add translator unit tests for mixed-width arithmetic, FFI loops, and conditional branches to prevent regressions.

## Stage 3 – Runtime Integration & Services

- [x] Introduce shared ABI-compliant DynASM call stubs for GC, string operations, and other runtime helpers.
- [x] Implement safepoint awareness in native frames, including register flushing and slow-path transitions during collection.
- [x] Ensure string operations (comparisons, concatenation, length) either inline fast paths or fall back to helper calls without deopt.
- [x] Support exception propagation and error reporting from native frames back to interpreter handlers.
- [x] Harden the tiering controller to handle tier transitions, invalidation, and cache reuse across recompilations.

## Stage 4 – Optimization & Performance Scaling

- [x] Add basic block scheduling and peephole optimizations to the DynASM backend for reduced register pressure and branch stalls.
  - DynASM now computes unique register pressure metrics per block, prefers hot successors with fewer live values, and folds redundant loads and arithmetic into constants during scheduling.
  - Loop-invariant writes lower successor weights, and the peephole pass removes self-moves and redundant helper setups before the emitter runs.
- [x] Implement simple profiling-guided specialization (e.g., constant folding, invariant hoisting) within the translator.
  - The translator tracks profiling-enabled constant definitions, hoists invariant loads, and annotates IR so the backend can preserve loop constants without reloading.
  - Specialization-propagated moves maintain defining instructions, enabling the backend peephole pass to fold arithmetic and skip redundant work.
- [x] Enable register allocation improvements (live-range splitting, spill minimization) for hot loops.
  - Hot loop metadata now records live ranges for counters, bounds, and steps so the allocator can plan register splits.
  - Register allocator exposes loop-aware APIs and optimizer state to minimize spills across loop iterations.
- [x] Add support for vectorized math kernels and inline caching of property accesses where applicable.
  - Native backend detects vectorizable opcode pairs, routes them through SSE-assisted helpers, and exposes interpreter fallbacks.
  - Inline caching infrastructure covers numeric `toString` conversions with signature-based caching to stay in native code.
- [x] Integrate warm-up heuristics that minimize thrashing between interpreter and JIT tiers on marginal loops.
  - Tiering samples now track cooldown windows, warm-up levels, and backoff generations so marginal loops must demonstrate sustained heat before re-tiering.
  - Deoptimizations escalate the cooldown exponent and saturating thresholds to prevent oscillation while still allowing loops to recover over time.

## Stage 5 – Observability & Regression Shielding

- [x] Split profiling counters by opcode family, value kind, and bailout reason with CLI/JSON exports.
  - Runtime profiling now aggregates sampled instructions per opcode family, surfaces JIT failure counts by translation status, category, and value kind, and exposes the same breakdown in `dumpProfilingStats` plus the JSON exporter.
  - CLI exports also propagate the new family aggregates so downstream tooling can monitor hot spots without parsing individual opcode samples.
- [x] Automate `make jit-benchmark-orus` in CI with pass/fail thresholds for uplift (≥3×) and native coverage (≥90%).
  - Added a Python harness that runs the benchmark suite, enforces ≥3× speedup and ≥90% native coverage, and prints actionable failures when the thresholds are missed.
  - Wired the harness into `make jit-benchmark-orus`, taught the CLI to report native coverage, and execute the target inside the release workflow so regressions trip CI immediately.
- [x] Capture disassembly dumps, guard exit traces, and per-loop telemetry toggles for developer debugging.
  - Added `OrusJitDebug` instrumentation hooks that snapshot IR listings, hex-encoded machine code, guard exit ring buffers, and loop-level counters gated by per-loop toggles.
  - Native entry stubs publish loop entries, slow-path requests, and guard exits so developers can target specific loops without drowning in global counters.
- [x] Document DynASM workflows, translation troubleshooting, and benchmarking procedures in `docs/IMPLEMENTATION_GUIDE.md`.
- [x] Maintain `JIT_BENCHMARK_RESULTS.md` with up-to-date metrics and narrative explanations after each milestone lands.

## Stage 6 – Platform Parity & Release Readiness

- [x] Validate DynASM backend parity on AArch64 and RISC-V, including cross-arch test harnesses and CI coverage.
  - Added a cross-architecture parity harness that classifies IR programs and asserts identical coverage across x86_64, AArch64, and RISC-V targets as part of CI test suites.
  - Extended make targets so cross-arch parity runs alongside helper-stub and translation smoke tests during `jit-cross-arch-tests`.
- [x] Stress test JIT execution with long-running workloads, high-concurrency scenarios, and GC-heavy programs.
  - Added `test_vm_jit_stress` harness that drives long-running arithmetic loops, GC-heavy string workloads, and fork-based concurrency stress to exercise native dispatch stability.
- [x] Finalize security review: guard stack integrity, enforce W^X policies, and audit helper call ABI compliance.
  - Native frames now embed canaries with fatal verification on corruption, DynASM helper calls are validated against a registry, and executable heaps enforce W^X transitions via tracked regions.
- [x] Update user-facing documentation (`docs/ROADMAP_PERFORMANCE.md`, release notes) to reflect milestones and capabilities.
- [ ] Define exit criteria for GA: sustained uplift across benchmarks, zero unsupported opcode bailouts, and stable FFI integration.
  - [ ] **FFI-heavy workload parity**
    - [x] Build a translation stress harness around the `ffi_ping_pong` benchmark that exercises nested foreign calls, boxed argument shuffles, and interpreter-to-native returns.
      - `tests/unit/test_vm_jit_translation.c:test_translates_ffi_ping_pong_foreign_bridge` now mirrors the benchmark loop, issues nested foreign invocations, and asserts the translator emits `ORUS_JIT_IR_OP_CALL_FOREIGN` without exhausting the spill set.
      - `tests/unit/test_vm_jit_benchmark.c:test_ffi_ping_pong_translation_foreign` reuses the benchmark harness to confirm the synthesized program translates under the tiered compiler.
    - [x] Extend the translator to lower `OP_CALL_FOREIGN` and related bookkeeping opcodes without forcing bailouts; cover boxed argument packing/unboxing paths with unit and integration tests.
      - `OP_CALL_FOREIGN` is wired through the dispatcher, profiling surface, IR, and backend emitters, with `orus_jit_native_call_foreign` bridging to the existing helper table until the dedicated registry lands.
      - Regression coverage asserts that boxed arguments survive lowering and foreign-call helpers surface in the generated IR.
    - [ ] Add tiered fallbacks that keep native frames alive across slow foreign calls (e.g., cooperative safepoints, helper-stub trampolines) and assert through tests that tiering no longer drops to the interpreter on foreign call boundaries.
- [x] **GC-safe native execution**
    - [x] Implement precise root maps for active JIT frames so the collector can walk locals, temps, and spill slots without relying on deopts. Backed by the `test_backend_gc_root_map_preserves_string_register` regression to guard string roots across native safepoints.
    - [x] Install GC interruption points in long-running native loops and verify with stress tests that collections triggered mid-loop preserve program state; the helper safepoint counter now drives both numeric and string-bearing loops through GC without losing typed state.
    - [x] Wire CI to run the GC stress harness under `jit-backend-tests`, failing the build if any collection forces a tier drop or corrupts live values.
  - [x] **Complete opcode coverage**
    - [x] Audit unsupported bytecodes surfaced by the translator diagnostics channel and promote them to native IR one family at a time (string helpers, iterator protocols, table mutations, etc.). Division and modulo opcodes for every numeric kind now lower through the translator and backend without falling back to helper stubs.
    - [x] Maintain a running “unsupported opcode count” metric in `make jit-translation-tests` and gate GA readiness on the counter reaching zero. `test_queue_tier_up_installs_native_entry_for_div_mod_loop` drives `queue_tier_up` end-to-end and asserts that the translation failure log records zero unsupported reasons when the loop compiles.
    - [x] Author regression tests for each newly lowered opcode family to ensure no future refactor silently revives interpreter-only paths. Backend coverage exercises the div/mod opcodes across i32/i64/u32/u64/f64 via `test_backend_executes_div_mod_opcodes`.

Use this roadmap as the living source of truth for JIT progress. After each landing, update the relevant checkbox, add links to
supporting benchmarks or design docs, and ensure the broader roadmap (`docs/ROADMAP_PERFORMANCE.md`, `JIT_BENCHMARK_RESULTS.md`)
mirrors the latest state.

## Mini Roadmap – Path to 2–3× Lua-Class Speed

To close the remaining performance gap and reach the Lua-class uplift targets, tackle the work in the following order. Treat each step as a milestone with explicit telemetry sign-off before advancing.

1. **Restore Native Coverage for Hot Loops**
   - Land the remaining DynASM backend fixes so `JIT_BACKEND_UNSUPPORTED` no longer blocklists the optimized loop benchmark.
   - Re-run `./orus --jit-benchmark tests/benchmarks/optimized_loop_benchmark.orus` and require ≥90 % native dispatch share before proceeding.
   - Linear emitter code generation is now enabled by default (set `ORUS_JIT_DISABLE_LINEAR_EMITTER=1` to return to the helper-stub path), so fresh tier-ups no longer fall back to the stub unless the backend reports an error.

2. **Stabilize Foreign-Call Tiering**
   - ✅ Implement the slow-path trampolines that keep native frames resident across long-running FFI calls (`jit_foreign_slow_path_trampolines` now counts serviced trampolines and unit coverage exercises a slow-path foreign binding).
   - ✅ Re-ran the FFI benchmark suite and updated `JIT_BENCHMARK_RESULTS.md` with the latest uplift and native coverage telemetry; the loop still blocklists on string value kinds even though the native frame survives the foreign call.

3. **Make Native Execution GC-Safe**
   - ✅ Delivered precise root maps for native frames and exercised safepoint-driven collections via the backend GC root regression.
   - ✅ Added backend coverage so CI fails when GC safepoints drop native frames or corrupt live values.

4. **Finish Opcode Coverage & Regression Shielding**
   - ✅ Div/mod opcodes for every numeric value kind now translate and execute natively, and the queue-tier-up regression ensures unsupported counters stay at zero.
   - ✅ Translator and backend suites assert the div/mod coverage so future refactors cannot silently reintroduce interpreter-only fallbacks.

5. **Confirm Lua-Class Throughput**  
   - Run `make jit-benchmark-orus` and require ≥3× uplift across the suite with narrative notes in the benchmark log.  
   - Publish disassembly snapshots and telemetry deltas alongside the updated roadmap to document the milestone.
