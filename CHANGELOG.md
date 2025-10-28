# Changelog

All notable changes to this project will be documented in this file.

The format is based on Keep a Changelog, and this project adheres to Semantic Versioning.

## [Unreleased]

### Removed
- Completed the experimental stdlib retirement by deleting the `module_manager_alias_module` API, its dedicated legacy-alias tests, and all build wiring.

### Added
- Documented the intrinsic stdlib contribution workflow and recorded the historical audit for the removed modules.
- Introduced a JIT stress harness (`jit-stress-tests`) that hammers long-running loops, GC-heavy string churn, and multi-process
  invocations to validate native tier resilience.

### Changed
- Hardened the JIT backend with executable-heap W^X enforcement, helper ABI validation, and native-frame canaries to prevent
  stack corruption from propagating across native frames.

### Fixed
- Ensured module resolution now exclusively accepts canonical intrinsic module names, preventing the registry from recreating legacy `std/` aliases.

## [0.1.1] - 2025-10-28

### Added
- Expanded random number generator regression coverage with new PCG32 and Mersenne Twister algorithm suites.
- Published an EBNF grammar reference alongside refreshed documentation for the math module.
