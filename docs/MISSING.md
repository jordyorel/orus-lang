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
- Shipped native helpers: `len`, `push`, `pop`, `timestamp`.
- Pending runtime hooks: `substring`, `reserve`, `range`, `sum`, `min`, `max`, `type_of`, `is_type`, `input`, `int`, `float`, `sorted`, `module_name`, `module_path`.
- Planned standard library math helpers: `pow`, `sqrt`.

## Active Work
- Structured diagnostics shared by CLI/REPL with richer context blocks.
- Variable lifecycle analysis (duplicate bindings, use-before-init, immutable mutations).
- Algorithm stress suite expansion across graph, dynamic programming, and numeric workloads (see `docs/ALGORITHM_STRESS_TEST_ROADMAP.md`).

## Next Milestones
1. Ship the structured diagnostic renderer with regression snapshots.
2. Integrate lifecycle diagnostics into the error reporting pipeline.
3. Allow `for item in collection` loops backed by existing iterator support.
4. Finalise print/format APIs once escape handling lands.

## Later Initiatives
- First-class generics with constraint handling and monomorphisation.
- Standard library modules once module plumbing stabilises.
- Additional optimisation passes (loop unrolling, strength reduction) and expanded VM telemetry.
- Performance telemetry expansion for VM opcode stats and tooling surfacing.

## Ownership & Cadence
- Roadmap reviewed at the start of each release cycle; updates land alongside status PRs.
- Core maintainers rotate ownership for active work items to keep load balanced.
