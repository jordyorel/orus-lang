# Orus Implementation Guide

## Overview
The Orus toolchain now focuses on the simplest possible execution model. The compiler translates the
high-level syntax into bytecode without applying loop-specific optimisations, and the virtual machine
executes those instructions using the generic control-flow and arithmetic handlers.

## Guiding Principles
- Keep the code paths easy to reason about. Prefer straightforward control flow over speculative fast paths.
- Represent every runtime value through the boxed `Value` abstraction so the interpreter logic remains
  consistent across opcodes.
- When adding new features, update the accompanying documentation and regression tests to match the
  baseline interpreter behaviour.

## Testing
Use `make test` to exercise the interpreter after any change. The suite now runs without auxiliary
telemetry or benchmark harnesses, so the standard target is sufficient for validation.
