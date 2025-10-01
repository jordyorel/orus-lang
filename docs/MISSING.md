# Orus Roadmap

## Why This Exists
Keeping the roadmap short and direct makes it easier for the team to track what is stable today, what is being built next, and which bets are queued for later.

## Delivered Capabilities
- End-to-end toolchain: lexer → parser → Hindley–Milner inference → optimising compiler → 256-register VM.
- Language surface: functions, `mut` variables, control flow, pattern matching, arrays, strings, numeric + boolean types.

- Runtime now supports direct string indexing with bounds-checked `OP_STRING_INDEX_R` emission.
- Modules: `module` headers, `use` imports with visibility checks and aliasing.
- Diagnostics today: scope validation and control-flow checks (rich formatter still pending).

## Built-in Coverage
- Shipped native helpers: `len`, `push`, `pop`, `timestamp`, `input`, `float`, `int`, `type_of`, `is_type`, `range`
- Pending runtime hooks: `substring`, `reserve`, `sum`, `min`, `max`, `sorted`, `module_name`, `module_path`.
- Planned standard library math helpers: `pow`, `sqrt`.

## Active Work
- Structured diagnostics shared by CLI/REPL with richer context blocks.
- Variable lifecycle analysis (duplicate bindings, use-before-init, immutable mutations).
- Algorithm stress suite expansion across graph, dynamic programming, and numeric workloads (see `docs/ALGORITHM_STRESS_TEST_ROADMAP.md`).
- Loop performance fast-pathing to eliminate the current boxed execution bottleneck (see roadmap below).

## Next Milestones
1. Ship the structured diagnostic renderer with regression snapshots.
2. Integrate lifecycle diagnostics into the error reporting pipeline.
3. Allow `for item in collection` loops backed by existing iterator support.
4. Finalise print/format APIs once escape handling lands.

## Loop Performance Roadmap

### Current Bottleneck
- Profiling confirms loop iterations still run through the generic boxed path, forcing register resynchronisation, iterator allocation, and runtime guard traffic every iteration.
- Safety guards remain essential, but without a dedicated typed fast path the VM cannot stay entirely in typed registers for hot loops.

### Implementation Phases

#### Phase 1 – Typed Branch Cache ✅
- **Objective**: Keep hot loop conditions in typed form so the dispatch path can branch without re-materialising boxed `Value`s.
- **Scope**:
  - Install a per-loop branch cache keyed by `(loop_id, predicate_slot)`.
  - Emit `BRANCH_TYPED` opcodes that consult the cache before falling back to the boxed handler.
  - Add guard invalidation hooks when the predicate source mutates outside the cached type envelope.
- **Status**: ✅ `OP_BRANCH_TYPED` now drives a per-loop cache with guard-versioned invalidation and exposes `loop_branch_cache_hits/misses` counters through the diagnostics console.
- **Acceptance**: Micro-benchmarks (`tests/benchmarks/control_flow_benchmark.*`) show branch-only loops avoiding boxed transitions; telemetry proves cache hit rates >95% for simple counters.
- **Telemetry**: `loop_branch_cache_hits/misses` counters flow through `vm_dump_loop_trace` and the diagnostics console.

- **Objective**: Replace the generic arithmetic opcode with a fused increment that keeps counters in typed registers while honouring overflow guarantees.
- **Scope**: Introduce `INC_Typed` opcodes with inline overflow checks and boxed slow-path fallback.
- **Status**: Fused helpers now drive both dispatch backends; `i32`, `i64`, `u32`, and `u64` induction variables stay in the typed bank with overflow bailouts for signed counters and wrap-preserving unsigned updates. Slow-path fallbacks rehydrate the appropriate typed register via the new `vm_store_*_typed_hot` helpers so steady-state loops stay resident after a single miss. The monotonic fused loop helper now covers all four integer widths so `OP_INC_CMP_JMP` stays hot when both counter and limit remain typed.
- **Telemetry**: VM stats expose `inc_fast_hits/inc_fast_misses`, `inc_overflow_bailouts`, and `inc_type_instability` through the diagnostics console and loop telemetry harness.
- **Dependency**: Requires Phase 1 cache invalidation hooks to notify increments of type transitions.

#### Phase 3 – Zero-Allocation Iterators ✅
- **Objective**: Eliminate heap churn for range/array iterators by storing descriptors in typed scratch registers.
- **Scope**: Provide stack-allocated iterator frames and update the optimizer to lift iterator construction out of the loop body when safe.
- **Dependency**: Builds on Phase 1 and Phase 2 typed register residency guarantees.
- **Status**: Dispatchers clone typed iterator descriptors for `range()` and
  array iterables, array helpers reuse descriptors via
  `vm_typed_iterator_bind_array_at`, and LICM now hoists invariant `range()` /
  `.iter()` calls so iterator setup stays out of the hot loop body. Regression
  coverage in `tests/loop_fastpaths/phase3/iterator_range_alias.orus` and
  `iterator_range_resume.orus` proves descriptors survive register moves and can
  resume partially-consumed iterators without falling back to boxed objects. A
  runtime audit confirmed `range()` is the only iterator-producing builtin
  today and its handler now populates typed descriptors immediately after
  allocation so no additional heap work is required on the hot path.

Loops will remain on the conservative boxed path until all three fast paths land and are wired into the runtime; tracking this phased roadmap keeps the focus on those deliverables in order.

## Later Initiatives
- First-class generics with constraint handling and monomorphisation.
- Standard library modules once module plumbing stabilises.
- Additional optimisation passes (loop unrolling, strength reduction) and expanded VM telemetry.
- Performance telemetry expansion for VM opcode stats and tooling surfacing.

## Ownership & Cadence
- Roadmap reviewed at the start of each release cycle; updates land alongside status PRs.
- Core maintainers rotate ownership for active work items to keep load balanced.
