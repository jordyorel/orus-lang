# Test Categorization Update

## Structs
- `tests/structs/basic_struct.orus` – Positive coverage for struct defaults and impl registration.
- `tests/structs/method_call.orus` – Exercises namespaced method dispatch and struct literal returns.
- `tests/structs/implicit_self_call.orus` – Confirms instance method calls inject the implicit `self` receiver.

## Type Safety Fails
- `tests/type_safety_fails/struct_field_type_mismatch.orus` – Ensures mismatched default triggers a type error.
- `tests/type_safety_fails/impl_missing_struct.orus` – Verifies impl blocks require a previously-declared struct.
- `tests/type_safety_fails/non_exhaustive_match.orus` – Confirms non-exhaustive enum matches raise diagnostics instead of compiling.
- `tests/type_safety_fails/duplicate_match_arm.orus` – Guards against duplicate enum match arms by requiring diagnostics when variants repeat.
- `tests/type_safety_fails/duplicate_literal_match_arm.orus` – Validates that duplicate literal arms in expression-form matches report diagnostics instead of compiling.
- `tests/type_safety_fails/enum_match_payload_mismatch.orus` – Ensures destructuring patterns bind the exact payload arity before bodies execute.
- `tests/type_safety_fails/enum_match_unknown_variant.orus` – Validates that referencing undeclared enum variants inside `match` arms reports errors.
- `tests/type_safety_fails/global_lowercase_fail.orus` – Fails compilation when a `global` name violates the enforced uppercase convention.
- `tests/type_safety_fails/pub_inside_block_fail.orus` – Confirms `'pub'` declarations trigger diagnostics if they appear outside of module scope.
- `tests/type_safety_fails/import_missing_symbol_fail.orus` – Emits a compile error when a `use` clause references a symbol the target module does not export.
- `tests/type_safety_fails/import_alias_type_mismatch_fail.orus` – Ensures aliased `use` clauses retain their function type information and reject mismatched argument types.

## Modules
- `tests/modules/module_imports.orus` – Demonstrates using public globals and functions from a sibling module.
- `tests/modules/module_import_alias.orus` – Exercises selective `use` with aliases for globals and functions.
- `tests/modules/package_import.orus` – Verifies dotted package imports resolve to nested module files and load colon-based module bodies.

## Types
- `tests/types/enum_declarations.orus` – Validates that enum declarations, payload annotations, and cross-type references resolve in the type system.
- `tests/types/enum_constructors.orus` – Ensures enum variant constructors type-check for both payload-free and payload-carrying variants.
- `tests/types/enum_matches.orus` – Covers the `matches` syntax sugar by checking variant comparisons on payload-free enums.
- `tests/types/enum_match_statement.orus` – Exercises the `match` lowering by ensuring payload-free variants and wildcard arms dispatch correctly.
- `tests/types/enum_match_destructuring.orus` – Verifies destructuring patterns bind enum payload fields and ignore `_` placeholders while emitting the expected runtime values.
- `tests/types/enum_match_patterns.orus` – Demonstrates multi-arm destructuring, `_` placeholders, and fallbacks across enums with differing payload shapes.
- `tests/types/match_expression_values.orus` – Exercises expression-form `match` arms that return values across enum payloads and literal fallbacks.

## Comprehensive
- `tests/comprehensive/enum_runtime.orus` – Confirms enum constructors lower to runtime values by constructing and reassigning variants before printing.

## Loop Fast Paths
- `tests/control_flow/loop_typed_fastpath_correctness.orus` – Baseline regression for typed loop semantics and telemetry.
- `tests/loop_fastpaths/phase1/bool_branch_short_circuit.orus` – Exercises the typed boolean branch cache under short-circuit `and`/`or` loops and mixed boolean guards.
- `tests/loop_fastpaths/phase2/inc_checked.orus` – Covers overflow-checked `inc_i32` fast paths and typed-miss fallbacks.
- `tests/loop_fastpaths/phase3/iterator_zero_alloc.orus` – Verifies zero-allocation typed iterators for numeric range loops.
- `tests/loop_fastpaths/phase3/iterator_array_zero_alloc.orus` – Confirms array-backed iterators stay in typed registers without heap churn.
- `tests/optimizer/loop_typed_phase4/licm_guard.orus` – Ensures LICM-hoisted guards cooperate with typed metadata.
- `tests/loop_fastpaths/phase5/telemetry_smoke.orus` – Smoke test mixing typed and boxed fallbacks for telemetry drift detection.

## Algorithm Stress – Phase 1
- `tests/algorithms/phase1/bubble_sort.orus` – Validates the adjacent-swap baseline with instrumentation for passes, comparisons, and swaps across varied datasets.
- `tests/algorithms/phase1/insertion_sort.orus` – Exercises incremental inserts on partially sorted, duplicate, and negative inputs while tracking pass and shift counts.
- `tests/algorithms/phase1/selection_sort.orus` – Tracks passes and swaps across diverse datasets to ensure the classic selection sort stays correct under the optimized VM pipeline.

## Algorithm Regressions
- `tests/algorithms/regressions/selection_sort_basic.orus` – Single-case regression to ensure the core selection sort implementation returns a sorted array for a representative dataset.
- `tests/algorithms/regressions/selection_sort_metrics.orus` – Verifies the labelled selection sort variant emits telemetry and produces the expected ordering for a known input.

## Algorithm Stress – Phase 3
- `tests/algorithms/phase3/fibonacci.orus` – Compares naive recursion against memoization while logging call counts, recursion depth, cache hits, and cache writes, and validates outputs against golden values plus a deeper stress case.
- `tests/algorithms/phase3/knapsack.orus` – Contrasts the exponential recursion tree against a bottom-up DP table, reporting call/depth/split metrics for the naive version and table cell, transition, and reuse counts for the dynamic-programming path alongside shared golden validations and a DP-only stress run.
- `tests/algorithms/phase3/lcs.orus` – Exercises longest common subsequence datasets with both the naive recursion tree and a DP table, recording call, depth, and branch metrics alongside DP cell, match, reuse, and tie counts while validating shared fixtures and a DP-only stress case.
- `tests/algorithms/phase3/edit_distance.orus` – Benchmarks Levenshtein distance by pairing tri-branch naive recursion with the DP grid, logging call/depth/branch metrics and per-branch winners plus base initialization, cell fill, and tie counts for the DP path, validating packed operation scripts on manageable datasets, and printing a DP-only stress run.
- `tests/algorithms/phase3/n_queens.orus` – Explores the N-Queens placement search while tracking recursive calls, maximum depth, pruning guard hits, successful placements, and backtracks before echoing the latest solution layout and stressing the classic 8×8 board.
