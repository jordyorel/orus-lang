# Test Categorization

This document defines the mandatory categories for all Orus tests. Each test file must reside in one of the directories described below.

## Categories

- **variables/** – tests for variable declarations, assignments and scoping rules
- **literals/** – numeric and string literal handling
- **expressions/** – arithmetic and boolean operations
- **formatting/** – string formatting and print output
- **conditionals/** – `if` statements and boolean branching
- **control_flow/** – loops, break/continue and other flow constructs
- **types/** – built-in numeric types and conversions
- **loop_safety/** – guard system for large loops
- **c/** – C-level VM tests
- **benchmarks/** – performance measurements
- **edge_cases/** – unusual or boundary scenarios
- **errors/** – error handling behavior

See `tests/README.md` for additional details on the directory layout.

## New Test Entry

When adding a test, update this document with the file name and category. Example:

```text
- variables/assignment.orus – verifies simple assignment
- variables/lexical_scoping.orus – demonstrates variable shadowing semantics
- variables/lexical_scoping_nested.orus – nested scopes and loop shadowing
- edge_cases/lexical_scoping_edge_cases.orus – complex shadowing edge cases
- variables/lexical_scoping_deep.orus – deep nested scope shadowing
- edge_cases/nested_loop_scope_exit.orus – verifies scope cleanup in nested loops
- c/unit_tests/test_symbol_table.c – symbol table unit tests
- variables/symbol_table_basic.orus – simple symbol table usage
- variables/symbol_table_shadowing.orus – shadowing with inner scope
- variables/symbol_table_loop_scope.orus – variable updates inside loops
- edge_cases/symbol_table_many_vars.orus – large number of variables
- edge_cases/symbol_table_long_names.orus – very long variable names
- edge_cases/symbol_table_scope_cleanup.orus – reuse names after scope exit
```



