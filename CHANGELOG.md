# Changelog

All notable changes to this project will be documented in this file.

The format is based on Keep a Changelog, and this project adheres to Semantic Versioning.

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

[0.5.0]: https://github.com/jordyorel/orus-lang/compare/v0.4.0...v0.5.0
[0.4.0]: https://github.com/jordyorel/orus-lang/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/jordyorel/orus-lang/compare/v0.2.4...v0.3.0
[0.2.4]: https://github.com/jordyorel/orus-lang/compare/v0.2.3...v0.2.4
[0.2.3]: https://github.com/jordyorel/orus-lang/compare/v0.2.2...v0.2.3
