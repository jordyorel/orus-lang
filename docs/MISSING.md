# Orus Roadmap

## Delivered
- Lexer, parser, and Hindley–Milner type inference.
- Baseline compiler emitting bytecode without loop-specific optimisations.
- Virtual machine executing all opcodes through boxed value handlers.

## In Progress
- Improve diagnostics and error reporting quality.
- Expand language surface with additional standard library helpers.
- Grow the regression suite to cover more user programs.

## Next Steps
1. Finish the structured diagnostic renderer.
2. Add lifecycle analysis for variables.
3. Introduce more iterator-friendly syntax once the baseline execution paths are fully covered by tests.

## Notes
Loop telemetry, fast paths, and related benchmarks have been removed. Future performance work should be
planned separately once the functional baseline is stable.
