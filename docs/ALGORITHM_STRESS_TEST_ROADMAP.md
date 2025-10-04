# Orus Algorithm Stress-Test Roadmap

This roadmap tracks the growth of the `tests/algorithms/` directory. The aim is to
expand functional coverage without relying on optimisation-specific tooling.

## Phases
| Phase | Focus | Status |
| :--- | :--- | :--- |
| Phase 0 | Harness foundations | Complete |
| Phase 1 | Sorting workloads | Complete |
| Phase 2 | Graph traversals | Complete |
| Phase 3 | Dynamic programming | Complete |
| Phase 4 | Numeric and probabilistic programs | Planned |

## Actions
- Keep fixtures runnable through `make test`.
- Document noteworthy behaviours in the implementation guide as phases land.
- Add new programs once their expected results are easy to assert within the
  baseline interpreter.
