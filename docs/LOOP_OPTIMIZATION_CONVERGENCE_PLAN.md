# Loop Optimization Convergence Plan

## Context
Benchmark telemetry shows Orus still lags LuaJIT/JavaScript on loop-heavy
workloads despite the VM shipping the complete typed fast-path roadmap. The
remaining gap comes from compiler-side issues: the optimizer does not yet
canonicalize loops into the VM-friendly shapes, register allocation sometimes
spills induction variables, and guard churn reintroduces boxed fallbacks.

This document describes the staged plan for converging the compiler pipeline with
the VM's fast paths so hot loops consistently execute in typed registers.

## Completed Foundation
- **VM loop fast paths**: Typed branch caches, overflow-checked increment ops,
  zero-allocation iterators, LICM integration, and regression gating are all
  landed and enabled by default.
- **Telemetry**: `vm.profile.loop_trace`, the loop fast-path benchmark harness,
  and CI gating dashboards already report typed hit ratios, overflow bailouts,
  and iterator allocation savings.
- **Optimizer infrastructure**: Constant folding, LICM, register reuse, and the
  peephole emitter are in place and instrumented.

## Goals
1. Generate loop bytecode that remains resident on typed registers across
   iterations, avoiding boxed fallbacks except on genuine type instability.
2. Shrink guard traffic by hoisting invariants and eliminating redundant
   slow-path exits.
3. Keep telemetry and benchmarks green throughout the rollout to preserve the
   performance gains already achieved by the VM work.

## Phased Execution

### Phase A – Loop Canonicalization in the Optimizer (Week 1)
- **Deliverables**
  - Introduce `opt_loop_canonicalize.c` to normalize desugared `for`/`while`
    constructs into a canonical `(init → guard → increment)` form with explicit
    typed metadata.
  - Add optimizer hooks to record `LoopCanonicalForm` descriptors that the
    codegen pass can consume when selecting opcodes.
  - Extend LICM to respect the canonical form when hoisting invariants so guard
    dominance is preserved.
- **Exit Criteria**
  - Optimizer dumps show canonical descriptors on all loops produced by the
    frontend desugar.
  - Existing regression suite passes with canonicalization enabled by default.
  - Telemetry reports no drop in typed branch hit rates (>95%).

### Phase B – Register Residency Guarantees (Week 2)
- **Deliverables**
  - Teach the register allocator to reserve dedicated typed slots for loop
    induction variables and bounds using the canonical descriptors.
  - Emit residency hints so the VM keeps counters and bounds hot across backedges
    without spilling.
  - Update optimizer register reuse heuristics to avoid clobbering residency slots
    mid-loop.
- **Exit Criteria**
  - Loop telemetry shows typed increment hit rates ≥98% on the benchmark suite.
  - `scripts/benchmarks/loop_perf.py` demonstrates ≥2× speedup versus the current
    compiler output without any VM changes.
  - No increase in spill count metrics on non-loop workloads (`make test-regalloc`).

### Phase C – Guard Thinning and Typed Exit Fusion (Week 3)
- **Deliverables**
  - Merge redundant guard chains emitted by the frontend (`<=` + range checks,
    iterator bounds) into fused typed exits that reuse existing witnesses.
  - Introduce optimizer analysis to convert provably invariant guards into
    counter increments recorded in telemetry rather than executed checks.
  - Add targeted regression programs in `tests/optimizer/loop_guard_fusion/` to
    cover nested loops, iterator invalidation, and type flips.
- **Exit Criteria**
  - Guard execution counts drop by ≥40% on `loop_perf.py` telemetry.
  - Typed hit ratios remain ≥95% even under mixed-type stress suites.
  - New tests pass with LICM on/off and with fast paths forcibly disabled.

### Phase D – Codegen & VM Interface Validation (Week 4)
- **Deliverables**
  - Wire `LoopCanonicalForm` descriptors into `codegen.c` so loops emit the
    correct typed opcodes (`OP_INC_I32_CHECKED`, branch cache entrypoints, typed
    iterator hydration) without ad-hoc pattern matching.
  - Add assertions ensuring the emitted bytecode matches the VM fast-path
    expectations (typed register ranges, residency flags, metadata indices).
  - Produce bytecode snapshots for representative loops and store them under
    `tests/golden/bytecode/loops/` for diffable review.
- **Exit Criteria**
  - Bytecode diff tests remain stable across rebuilds.
  - VM assertions stay silent across the regression suite.
  - Telemetry confirms no regression in typed hit ratios or overflow bailouts.

### Phase E – Benchmark & Telemetry Gates (Week 5)
- **Deliverables**
  - Promote loop fast-path microbenchmarks to required CI gates with thresholds
    aligned to LuaJIT parity targets.
  - Add nightly dashboard diffing typed hit ratios, guard counts, and runtime
    throughput.
  - Document rollback SOP for compiler-side regressions leveraging existing VM
    fast-path kill switches.
- **Exit Criteria**
  - CI fails on ≥5% regressions in loop throughput or typed hit ratios.
  - Nightly dashboards published with historical comparisons.
  - Rollback runbook reviewed and stored in `docs/OPERATIONS.md` (to be created).

## Ownership & Cadence
- **Compiler Backend** owns Phases A, B, and D.
- **Optimizer Team** co-owns Phases A and C.
- **Tooling/QA** owns Phase E and telemetry integration.
- Weekly sync reviews telemetry deltas and updates this plan; adjustments are
  committed alongside status PRs.

## Dependencies & Risks
- Regressions in canonicalization could surface latent parser desugar bugs;
  guard with staged rollout flags.
- Register residency changes risk spilling pressure on complex functions; keep
  spill telemetry under review and consider heuristics to opt out in debug mode.
- Guard thinning must preserve semantic equivalence; invest in fuzzers that mix
  type changes mid-loop.

## Success Metrics
- Loop throughput within 1.5× of LuaJIT on the shared benchmark suite.
- Typed execution hit ratio ≥95% across canonical benchmarks.
- Zero additional boxed guard executions on steady-state loops compared to the
  VM-only baseline.
