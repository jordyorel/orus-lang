# Orus Algorithm Stress-Test Roadmap

This document tracks the phased rollout of a dedicated `tests/algorithms/` suite
that exercises recursion depth, heavy numeric loops, dynamic data structures,
string processing, and probabilistic workloads. Each phase should land with
runnable `.orus` fixtures plus harness coverage in `test-all`.

## Phased Plan

| Phase | Focus | Representative Algorithms | Acceptance Criteria |
| --- | --- | --- | --- |
| Phase 0 | Test harness foundations | Shared helpers for timing, result assertions, and reproducible random seeds. | Reusable `assert_equals` and perf-logging utilities compiled into regression harnesses. |
| Phase 1 | Iteration & sorting | Bubble/Insertion sort, selection sort, Merge sort, Quick sort, Heap sort, Counting sort. | Stable outputs for sorted datasets, instrumentation for branch counts, and coverage of both recursive and iterative control flow. |
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

Counting and heap sort now round out the Phase 1 fixtures. The
`tests/algorithms/phase1/counting_sort.orus` script exercises range sizing,
frequency table initialisation, and duplicate-heavy workloads while recording
extent scans, slot allocations, count updates, and emission counts. The new
`tests/algorithms/phase1/heap_sort.orus` companion keeps the algorithm in
place to stress parent/child index arithmetic, repeated heapify passes, and
swap-heavy sifts. Its instrumentation logs heapify calls, comparisons, swaps,
and sift iterations so we can spot regressions in the optimiser’s arithmetic
folding or the VM’s in-place mutation handling.

While wiring that fixture we uncovered and fixed a long-standing interpreter
bug: the register allocator occasionally handed function calls a non-consecutive
set of argument registers, so the VM only saw the first two arguments of a
three-argument call. The compiler now reserves contiguous temporary blocks for
all call arguments, ensuring multi-argument helpers like `assert_sorted` keep
their inputs intact during stress runs.


## Phase 2 Kickoff – Graph Traversal & Shortest Paths

The Phase 2 fixtures now live alongside the Phase 1 sort suite inside
`tests/algorithms/phase1/` so that the harness can reuse the existing
telemetry plumbing while we continue expanding coverage. The new graph
programs focus on stressing recursion depth, queue pressure, and the
compiler's ability to juggle dense numeric updates.

- `depth_first_search.orus` introduces a recursive walker that counts
  recursion depth, edge traversals, and order construction across
  multi-component graphs. The tests exercise both a cyclic component and
  a tree-shaped graph to drive the depth counter toward double digits.
- `breadth_first_search.orus` mirrors those fixtures with a queue-backed
  traversal that records enqueue/dequeue operations, edge checks, and the
  high-water mark for queue occupancy. A layered graph fixture ensures we
  observe steady growth in breadth levels.
- `dijkstra.orus` uses an array-scanned priority selection so the VM's
  register allocator sees repeated minimum searches and relaxation
  updates. The script tracks how many nodes were settled and how many
  times distances improved while covering connected and disconnected
  graphs.
- `bellman_ford.orus` adds negative edge handling with an explicit edge
  list. The implementation logs total relaxation attempts and exposes the
  early-out path when no updates occur before the full `V-1` passes.
- `floyd_warshall.orus` stresses triple-nested loops and sentinel
  `infinity` guards while counting how many matrix cells improved. A
  follow-up fixture keeps unreachable nodes intact to confirm the guard
  logic avoids overflow.
- `topological_sort.orus` rounds out the phase with Kahn's algorithm,
  capturing enqueue/dequeue counts, edge visits, and queue high-water
  marks across DAGs with single roots, multiple roots, and wide fan-out.

These additions give us depth-first, breadth-first, single-source, and
all-pairs coverage along with DAG ordering. Combined with the Phase 1
sorters we now touch recursion, queues, dense numeric loops, and
conditional edge relaxations—enough variety to start capturing VM
profiling snapshots for backend tuning.

## Phase 3 Kickoff – Fibonacci Recursion & Memoization

The first Phase 3 fixture now lives in `tests/algorithms/phase3/` with paired naive and memoized Fibonacci implementations. The script tracks recursion depth, total calls, cache hits, and cache writes so we can observe how memoization tames exponential growth. It validates multiple small inputs against golden numbers, confirms the memoized and naive results agree, and then executes a deeper `stress_n = 25` run that would explode without caching. The output lets us compare naive depth/call counts against memoized cache efficiency while setting the stage for the heavier dynamic programming fixtures that follow.

The follow-on `0-1` knapsack fixture contrasts the exponential recursion tree against a bottom-up dynamic programming table. The naive helper reports call counts, maximum recursion depth, and how many branch splits occurred when the algorithm weighed including versus excluding an item. The DP variant logs how many table cells were populated, how many transitions considered both states, and when an existing value could be reused from a prior row. Together they verify shared datasets, assert agreement with golden profits, and finish with a DP-only stress run that pushes ten items and a 50-unit capacity without paying the recursive explosion.

The new longest common subsequence (LCS) program extends the phase with string-aligned datasets that drive both the naive recursion tree and a bottom-up DP table. The naive helper tracks total calls, maximum depth, and branch splits while the DP variant records populated cells, match-extend counts, direction reuses, and tie-breaks. Shared fixtures validate that both paths agree on subsequence length and that the DP reconstruction returns the expected sequence, and a DP-only stress case scales the inputs beyond what the exponential recursion can tolerate.

The follow-up edit distance harness keeps the string focus but exercises tri-branch recursion and Levenshtein grid dynamics. The naive helper records call counts, maximum depth, branch splits, match advances, and which branch ultimately delivered the minimum cost, including tie tracking across substitution, insertion, and deletion paths. The DP version logs base-row/column initializations, populated cells, and how often each operation kind wins when evaluating candidates. Shared fixtures validate both implementations against golden distances and compare packed operation scripts, while a DP-only stress run drives the longer `INTENTION` ↔ `EXECUTION` dataset that would explode under the naive recursion.

The newest N-Queens backtracking suite pivots from string alignment to combinatorial search. It explores the placement tree while counting recursive calls, tracking the maximum depth, and tallying how often diagonal/column guards prune candidate squares. Each solution snapshot copies the row-by-column placements so the harness can print representative boards, and aggregated metrics surface placement successes, conflict rejections, and backtracks alongside the golden solution counts for `n = 1`, `4`, and `5`. A final stress run on the classic 8×8 board emphasises how aggressively the pruning tables cut the 92-solution search space.
