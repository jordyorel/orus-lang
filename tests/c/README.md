# C Tests

This directory contains C-level tests for the Orus VM implementation.

## Structure

```
c/
├── unit_tests/     # Unit tests for individual components
├── integration_tests/ # Integration tests for C components
└── framework/      # Test framework and utilities
```

## Test Categories

### Unit Tests
- Individual VM component tests
- Lexer tests
- Memory management tests
- Core VM functionality tests

### Integration Tests
- Cross-component interaction tests
- Break/continue mechanism tests
- Debug functionality tests

### Framework
- Test framework headers and utilities
- Common test infrastructure
- Helper functions and macros

## Files

### Unit Tests
- `test_lexer.c` - Lexical analysis tests
- `test_memory.c` - Memory management tests
- `test_vm.c` - Virtual machine core tests

### Integration Tests
- `test_break_continue.c` - Control flow integration tests
- `test_indentation_debug.c` - Debug system integration tests

### Framework
- `test_framework.h` - Main test framework header
