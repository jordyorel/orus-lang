# Orus Algorithm Stress-Test Roadmap

This document tracks the phased rollout of a dedicated `tests/algorithms/` suite
that exercises recursion depth, heavy numeric loops, dynamic data structures,
string processing, and probabilistic workloads. Each phase should land with
runnable `.orus` fixtures plus harness coverage in `test-all`.

## Phased Plan

| Phase | Focus | Representative Algorithms | Acceptance Criteria |
| --- | --- | --- | --- |
| Phase 0 | Test harness foundations | Shared helpers for timing, result assertions, and reproducible random seeds. | Reusable `assert_equals` and perf-logging utilities compiled into regression harnesses. |
| Phase 1 | Iteration & sorting | Bubble/Insertion sort, Merge sort, Quick sort, Heap sort, Counting sort. | Stable outputs for sorted datasets, instrumentation for branch counts, and coverage of both recursive and iterative control flow. |
| Phase 2 | Graph & traversal | DFS, BFS, Dijkstra, Bellman–Ford, Floyd–Warshall, Topological sort. | Valid shortest-path tables on weighted fixtures, queue/stack behaviour under stress, and VM profiling snapshots to watch register pressure. |
| Phase 3 | Dynamic programming & combinatorics | Fibonacci (naïve + memoized), Knapsack, LCS, Edit distance, N-Queens, Sudoku solver. | Agreement with golden answers, memoization cache validation, and stress runs that highlight recursion-depth handling. |
| Phase 4 | Numeric heavy + probabilistic | Sieve of Eratosthenes, Modular exponentiation, Miller–Rabin, Mandelbrot iterations, Conway’s Game of Life. | Deterministic prime tables, statistically sound primality checks across seeds, and float-heavy benchmarks without VM instability. |

## Supporting Actions

- **Documentation** – For each phase, append findings and performance baselines
  to `docs/IMPLEMENTATION_GUIDE.md` and link the new fixtures inside
  `docs/TEST_CATEGORIZATION.md` under an "Algorithm Stress" heading.
- **Diagnostics hooks** – Capture bytecode/typed-AST dumps for at least one
  algorithm per phase to spot optimizer regressions early.
- **Performance tracking** – Record runtime, allocation counts, and register
  utilisation in the roadmap so we can flag regressions in the VM or compiler.
- **Stretch goal** – Once all phases are green, evaluate hybrid backend options
  (FAST vs OPTIMIZED) on the same fixtures to measure compile-time vs runtime
  trade-offs.
- Automate loop telemetry dashboards and CI perf digests for the new fast paths
  once the instrumentation APIs are ready.

## Phase 1 Kickoff – Bubble & Insertion Sort

Two smoke suites now anchor Phase 1 inside `tests/algorithms/phase1/`:

- `bubble_sort.orus` runs the classic adjacent-swap implementation across
  random, already sorted, reversed, duplicate-heavy, and single-element inputs.
  The harness clones each dataset before sorting, checks the resulting order,
  and logs pass/comparison/swap counters so we can watch for control-flow
  regressions as the optimizer evolves.
- `insertion_sort.orus` covers the incremental insert algorithm against
  partially sorted, negative, duplicate, and one-element arrays while
  reporting the number of outer passes and shifts that occurred. These metrics
  surface the gap between best- and worst-case behaviour and give a baseline
  for future telemetry integration.

Early dry-runs show the VM correctly handles nested `while` loops, array
mutations, and repeated `len()`/`push()` calls under both algorithms. The next
step is to extend coverage to merge/quick/heap variants while promoting the
duplicated verification code into reusable utilities for the remainder of the
suite.

