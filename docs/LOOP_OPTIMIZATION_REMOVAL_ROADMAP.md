# Loop Optimization Removal Roadmap

All loop-specific optimizations have been removed from the Orus toolchain. Monotonic range hints, typed iterator fast paths, branch caches, loop telemetry, and their configuration toggles no longer exist in the compiler or the virtual machine. Loops now execute exclusively through the generic control-flow instructions emitted by the baseline code generator.

## Status
- Compiler passes no longer stamp loop metadata or monotonic flags.
- The virtual machine only supports boxed iterator execution; typed iterator helpers and monotonic increment shortcuts have been deleted.
- Loop telemetry, perf harnesses, and associated documentation have been retired.
- The comprehensive `make test` harness now invokes the error-reporting regression suite so diagnostics remain covered post-optimisation removal.

This document is retained as an acknowledgement that the removal effort is complete. Any future optimisation work must be planned afresh with dedicated documentation.

## Benchmark Snapshot – 2025-10-03
- `make benchmark` now executes against the release-profile binary so that the long-running suites complete in a reasonable time even without the old fast paths.
- The **Pure Arithmetic** scenario succeeds for every supported runtime: Orus completes in ~42 ms, JavaScript in ~162 ms, and Python in ~938 ms; the reference Lua script still fails to start.
- The **Optimized Loop** scenario reflects the boxed slow-path cost. Orus records ~2892 ms while JavaScript finishes in ~258 ms and Python in ~3404 ms; Lua again fails to execute.
- These measurements capture the post-optimisation-removal state and provide the starting point for any future baseline performance work.
