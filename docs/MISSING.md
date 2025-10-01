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
- Compiler loop canonicalization and register residency work to unlock the VM loop fast paths already shipped (see roadmap below).

## Next Milestones
1. Ship the structured diagnostic renderer with regression snapshots.
2. Integrate lifecycle diagnostics into the error reporting pipeline.
3. Allow `for item in collection` loops backed by existing iterator support.
4. Finalise print/format APIs once escape handling lands.

## Loop Performance Roadmap

### Current State
- VM typed fast paths (branch caches, fused increments, zero-allocation iterators, LICM metadata preservation, and regression gates) are complete and enabled by default.
- Telemetry and benchmarks highlight that the compiler still emits bytecode that fails to stay on those fast paths consistently, causing boxed fallbacks and inflated guard traffic.

### Next Steps (Compiler-Focused)
1. **Loop Canonicalization** – Normalize desugared loops into canonical `(init → guard → increment)` forms with typed metadata so the backend can select the correct opcodes.
2. **Register Residency Guarantees** – Reserve typed registers for induction variables/bounds and propagate residency hints so the VM keeps hot counters in place.
3. **Guard Thinning & Fusion** – Merge redundant guards and convert invariant checks into telemetry counters to cut slow-path churn without losing safety.
4. **Codegen Integration** – Feed canonical descriptors into the emitter to guarantee the optimized bytecode uses the VM's typed entrypoints.
5. **Benchmark & Telemetry Gates** – Promote loop microbenchmarks and typed-hit telemetry thresholds to required CI gates to lock in improvements.

Full details live in `docs/LOOP_OPTIMIZATION_CONVERGENCE_PLAN.md`.

## Later Initiatives
- First-class generics with constraint handling and monomorphisation.
- Standard library modules once module plumbing stabilises.
- Additional optimisation passes (loop unrolling, strength reduction) and expanded VM telemetry.
- Performance telemetry expansion for VM opcode stats and tooling surfacing.

## Ownership & Cadence
- Roadmap reviewed at the start of each release cycle; updates land alongside status PRs.
- Core maintainers rotate ownership for active work items to keep load balanced.
