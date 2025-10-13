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
- [ ] Provide typed deopt landing pads so guard exits reuse interpreter frames instead of regenerating helper stubs.
- [ ] Update backend unit tests (`tests/unit/test_vm_jit_backend.c`) to assert DynASM parity with the linear emitter across value kinds.

## Stage 2 – Translator Hardening & Loop Resilience

- [ ] Extend translator diagnostics to categorize unsupported bytecodes, malformed loop shapes, and disabled rollout stages.
- [ ] Support loop patterns with conditional exits, nested branches, and in-loop helper calls while preserving safety invariants.
- [ ] Track value-kind promotion so widened locals (i32→i64/u64) reuse existing IR nodes instead of triggering bailouts.
- [ ] Annotate FFI call sites with spill metadata so translated loops can cross the C boundary and resume in native code.
- [ ] Add translator unit tests for mixed-width arithmetic, FFI loops, and conditional branches to prevent regressions.

## Stage 3 – Runtime Integration & Services

- [ ] Introduce shared ABI-compliant DynASM call stubs for GC, string operations, and other runtime helpers.
- [ ] Implement safepoint awareness in native frames, including register flushing and slow-path transitions during collection.
- [ ] Ensure string operations (comparisons, concatenation, length) either inline fast paths or fall back to helper calls without deopt.
- [ ] Support exception propagation and error reporting from native frames back to interpreter handlers.
- [ ] Harden the tiering controller to handle tier transitions, invalidation, and cache reuse across recompilations.

## Stage 4 – Optimization & Performance Scaling

- [ ] Add basic block scheduling and peephole optimizations to the DynASM backend for reduced register pressure and branch stalls.
- [ ] Implement simple profiling-guided specialization (e.g., constant folding, invariant hoisting) within the translator.
- [ ] Enable register allocation improvements (live-range splitting, spill minimization) for hot loops.
- [ ] Add support for vectorized math kernels and inline caching of property accesses where applicable.
- [ ] Integrate warm-up heuristics that minimize thrashing between interpreter and JIT tiers on marginal loops.

## Stage 5 – Observability & Regression Shielding

- [ ] Split profiling counters by opcode family, value kind, and bailout reason with CLI/JSON exports.
- [ ] Automate `make jit-benchmark-orus` in CI with pass/fail thresholds for uplift (≥3×) and native coverage (≥90%).
- [ ] Capture disassembly dumps, guard exit traces, and per-loop telemetry toggles for developer debugging.
- [ ] Document DynASM workflows, translation troubleshooting, and benchmarking procedures in `docs/IMPLEMENTATION_GUIDE.md`.
- [ ] Maintain `JIT_BENCHMARK_RESULTS.md` with up-to-date metrics and narrative explanations after each milestone lands.

## Stage 6 – Platform Parity & Release Readiness

- [ ] Validate DynASM backend parity on AArch64 and RISC-V, including cross-arch test harnesses and CI coverage.
- [ ] Stress test JIT execution with long-running workloads, high-concurrency scenarios, and GC-heavy programs.
- [ ] Finalize security review: guard stack integrity, enforce W^X policies, and audit helper call ABI compliance.
- [ ] Update user-facing documentation (`docs/ROADMAP_PERFORMANCE.md`, release notes) to reflect milestones and capabilities.
- [ ] Define exit criteria for GA: sustained uplift across benchmarks, zero unsupported opcode bailouts, and stable FFI integration.

Use this roadmap as the living source of truth for JIT progress. After each landing, update the relevant checkbox, add links to
supporting benchmarks or design docs, and ensure the broader roadmap (`docs/ROADMAP_PERFORMANCE.md`, `JIT_BENCHMARK_RESULTS.md`)
mirrors the latest state.
