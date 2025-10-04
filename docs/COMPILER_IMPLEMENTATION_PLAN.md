# Compiler Implementation Plan

## Current State
- Loop optimisations, typed iterator caches, and telemetry hooks have been completely removed.
- The compiler emits baseline bytecode without attaching monotonic or fast-path metadata.
- The virtual machine executes bytecode using boxed values only.

## Next Steps
1. Keep the bytecode generator aligned with the simplified VM semantics.
2. Expand the regression suite when new language features are introduced.
3. Update documentation alongside code changes so the plan always reflects the current architecture.
