# Orus Test Categorization

This document outlines the canonical directory structure for Orus tests and the purpose of each category. All new tests must be placed in the appropriate directory and referenced from the makefile.

| Directory | Purpose |
|-----------|---------|
| `tests/types/` | Valid type system behaviors and examples that compile and run. |
| `tests/type_safety_fails/` | Programs that should fail to compile due to type errors. |
| `tests/edge_cases/` | Runtime error cases and boundary value tests such as overflow or division by zero. |
| `tests/expressions/` | Basic expression parsing and evaluation examples. |
| `tests/literals/` | Literal parsing and printing. |
| `tests/variables/` | Variable declarations and assignment semantics. |
| `tests/benchmarks/` | Performance benchmarks. |

All tests added in this repository should follow these categories. When introducing a new directory, update this file and the makefile accordingly.
