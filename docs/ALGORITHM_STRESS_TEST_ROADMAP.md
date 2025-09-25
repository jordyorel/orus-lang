# Orus Algorithm Stress-Test Roadmap

This document tracks the phased rollout of a dedicated `tests/algorithms/` suite
that exercises recursion depth, heavy numeric loops, dynamic data structures,
string processing, and probabilistic workloads. Each phase should land with
runnable `.orus` fixtures plus harness coverage in `test-all`.

## Phased Plan

| Phase | Focus | Implemented Fixtures | Status |
| :--- | :--- | :--- | :--- |
| Phase 0 | Harness foundations | Shared timing/assert helpers embedded in the `tests/algorithms/` harness | Completed |
| Phase 1 | Iteration & sorting | `bubble_sort`, `insertion_sort`, `selection_sort`, `merge_sort`, `quick_sort`, `heap_sort`, `counting_sort` | Completed |
| Phase 2 | Graph & traversal | `depth_first_search`, `breadth_first_search`, `dijkstra`, `bellman_ford`, `floyd_warshall`, `topological_sort` | Completed |
| Phase 3 | Dynamic programming & combinatorics | `fibonacci`, `knapsack`, `lcs`, `edit_distance`, `n_queens`, `sudoku` | Completed |
| Phase 4 | Numeric heavy + probabilistic | Planned: `sieve`, `mod_exp`, `miller_rabin`, `mandelbrot`, `game_of_life` | Not started |

## Directory Overview
- `tests/algorithms/phase1/` – Sorting and graph fixtures share the common instrumentation harness.
- `tests/algorithms/phase3/` – Dynamic programming and combinatorics suite with golden-output checks.
- `tests/algorithms/regressions/` – Regression snapshots (selection sort) to guard instrumentation changes.

## Supporting Actions

- **Documentation** – Record notable findings in `docs/IMPLEMENTATION_GUIDE.md` and list fixtures in `docs/TEST_CATEGORIZATION.md` under "Algorithm Stress".
- **Diagnostics hooks** – Capture bytecode/typed-AST dumps for at least one algorithm per phase to catch optimizer regressions early.
- **Performance tracking** – Track runtime, allocation counts, and register utilisation so VM/compiler regressions surface quickly.
- **Stretch goal** – Once Phase 4 lands, evaluate FAST vs OPTIMIZED backends on the full suite and gather comparative telemetry.
- Automate loop telemetry dashboards and CI perf digests once instrumentation APIs stabilize.

## Phase 0 – Harness Foundations (Completed)

Reusable helpers now live alongside the fixtures, providing deterministic random seeds, timing instrumentation, and assertion utilities. Every new test plugs into the same harness so telemetry and pass/fail reporting stay consistent across phases.

## Phase 1 – Iteration & Sorting (Completed)

`tests/algorithms/phase1/` holds the full sorting lineup: bubble, insertion, selection, merge, quick, heap, and counting sort. Each script clones datasets, checks sorted output, and logs pass/comparison/swap counters or heapify iterations. The shared utilities originated here and still provide the comparison baselines used by later phases.

## Phase 2 – Graph & Traversal (Completed)

Graph fixtures co-reside in `tests/algorithms/phase1/` to reuse the same harness. DFS and BFS capture recursion depth and queue pressure; Dijkstra, Bellman–Ford, and Floyd–Warshall validate shortest-path tables and relaxation counts; Topological sort exercises Kahn's algorithm with enqueue/dequeue telemetry. Together they stress recursion, queue management, and dense numeric updates.

## Phase 3 – Dynamic Programming & Combinatorics (Completed)

The `tests/algorithms/phase3/` directory delivers Fibonacci (iterative + memoised), Knapsack, LCS, Edit Distance, N-Queens, and Sudoku fixtures. Golden answers and cache metrics verify correctness while recursion depth and branching factors are recorded to monitor VM pressure.

## Phase 4 – Numeric Heavy + Probabilistic (Planned)

Upcoming coverage will target prime sieving, modular arithmetic, probabilistic primality (Miller–Rabin), fractal iteration, and Game of Life simulations. Tasks remaining:
- Design shared numeric fixtures and golden data for deterministic validation.
- Extend harness helpers to capture floating-point drift and probabilistic outcome statistics.
- Add VM profiling checkpoints to measure register usage and interpreter stability under float-heavy workloads.
