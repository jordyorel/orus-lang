# Changelog

All notable changes to this project will be documented in this file.

The format is based on Keep a Changelog, and this project adheres to Semantic Versioning.

## [0.6.4] - 2025-10-07

### Added
- Expanded random number generator regression coverage with new PCG32 and Mersenne Twister algorithm suites.
- Published an EBNF grammar reference alongside refreshed documentation for the math module.

### Changed
- Normalized module resolver import paths and broadened search directories for more reliable module loading.
- Tuned interpreter benchmarks and supporting assets to stabilize string concatenation measurements.

### Fixed
- Corrected `for`-loop iterator scoping and match statement environments so temporary bindings remain isolated.
- Resolved rope release stack overflows during large string concatenations.

## [0.6.3] - 2025-10-02

### Added
- Introduced the `pass` statement to the language with parser, type-checker, and code generation support.
- Added a WebAssembly build target and JavaScript bridge for running Orus programs in the browser.

### Changed
- Reworked loop telemetry, typed branch caches, and fused-loop lowering for faster counter-based iteration.
- Strengthened runtime tests with broader `assert_eq` coverage and updated control-flow fixtures.

### Fixed
- Deduplicated compiler diagnostics to prevent repeated error messages.
- Improved CLI guidance and examples around numeric input conversion.

## [0.6.2] - 2025-09-24

### Added
- Support destructuring bindings, `const` declarations, and hexadecimal literals in the compiler front-end.
- Ship built-in `Result` enum helpers with accompanying CLI smoke coverage.
- Introduce additional algorithm stress suites, including N-Queens, Sudoku, heap sort, selection sort, and merge-sort fixtures for later phases.

### Changed
- Release builds now emit both native and WebAssembly artifacts from the same target.
- Array and string formatting was polished for clearer runtime output.
- Tightened loop execution performance with typed register dirty tracking, typed branch opcodes, and constant-folding enhancements.

### Fixed
- Corrected garbage collector root handling for call frames and chunk constants.
- Resolved numerous interpreter regressions around array indexing, register allocation, and selection sort behavior.
- Improved diagnostics for argument count mismatches, indentation issues, missing brackets, and print syntax errors.

## [0.5.2] - 2025-09-22

### Changed
- Bumped version to 0.5.2.

## [0.5.1] - 2025-09-21

### Changed
- Bumped version to 0.5.1.

## [0.5.0] - 2025-09-20

### Changed
- Bumped version to 0.5.0.

## [0.4.0] - 2025-09-19

### Changed
- Bumped version to 0.4.0.

## [0.3.0] - 2025-09-17

### Added
- Lambda function syntax and execution support.
- Generic constraints for higher-order functions.
- Compiler scope tracking and aggregated diagnostics.
- Jump patch infrastructure for accurate control flow codegen.
- Cast warning system to surface potentially unsafe conversions.
- VM profiling hooks and supporting utilities.

### Changed
- While loop semantics enhanced with mutability support.
- Register handling and allocator improved to preserve frame and temp registers, stabilizing recursion and nested execution.
- Type inference, binary expression compilation, and error reporting refined for clarity and correctness.
- Dispatch improvements and debugging output for better traceability.

### Fixed
- Multiple crashes and incorrect results in recursive and nested loop scenarios.
- For-range compilation to handle arbitrary expressions reliably.
- Various issues in code generation leading to register corruption.

### Notes
- Version is now centralized in `include/public/version.h` and consumed by the CLI via `config_print_version()`.

## [0.2.4] - 2025-07-20
- Bumped version to 0.2.4.

## [0.2.3] - 2025-07-16
- Lexer state refactor to remove global state; context-based architecture.

## [0.2.2] - 2025-07-16
- Introduced public `version.h` and initial 0.2.x series setup.

[0.6.4]: https://github.com/jordyorel/orus-lang/compare/v0.6.3...v0.6.4
[0.6.3]: https://github.com/jordyorel/orus-lang/compare/v0.6.2...v0.6.3
[0.6.0]: https://github.com/jordyorel/orus-lang/compare/v0.5.2...v0.6.0
[0.5.2]: https://github.com/jordyorel/orus-lang/compare/v0.5.1...v0.5.2
[0.5.1]: https://github.com/jordyorel/orus-lang/compare/v0.5.0...v0.5.1
[0.5.0]: https://github.com/jordyorel/orus-lang/compare/v0.4.0...v0.5.0
[0.4.0]: https://github.com/jordyorel/orus-lang/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/jordyorel/orus-lang/compare/v0.2.4...v0.3.0
[0.2.4]: https://github.com/jordyorel/orus-lang/compare/v0.2.3...v0.2.4
[0.2.3]: https://github.com/jordyorel/orus-lang/compare/v0.2.2...v0.2.3
