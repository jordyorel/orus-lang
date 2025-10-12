# Baseline JIT Fused Loop Roadmap

## Objective
Enable the baseline JIT to translate the VM's fused loop bytecodes (`OP_INC_CMP_JMP` and `OP_DEC_CMP_JMP`) into native code so that optimized interpreter loops tier up successfully.

## Current Situation
- Hot loops in the "optimized loop" benchmark execute entirely in the interpreter because the baseline translator marks the fused opcodes as unhandled.
- The IR lacks opcodes that model the fused increment/compare/jump semantics, so the backend has no way to generate native code for them.
- Counters for translations and JIT runtime execution stay at zero for these loops, even when the rollout enables strings.

## Milestones
1. **IR Support**
   - [x] Introduce dedicated IR opcodes that capture increment/decrement plus compare and back-edge jump semantics.
   - [x] Extend the IR operand union to store the counter register, limit register, and signed jump displacement.
   - [x] Update IR introspection helpers and debug utilities to print the new instructions.
     - Dump routines now format `IR_OP_INC_CMP_JMP` / `IR_OP_DEC_CMP_JMP` with counter, limit, and displacement operands so fused loops appear in dumps and traces.

2. **Translator Updates**
   - [x] Teach `orus_jit_translate_linear_block` how to decode `OP_INC_CMP_JMP` / `OP_DEC_CMP_JMP`.
   - [x] Infer the numeric kind (i32/i64/u32/u64) from the tracked register state, rejecting mixed or boxed operands.
   - [x] Emit the fused IR opcodes, preserving safepoint cadence and loop metadata.
   - [x] Add unit tests that exercise translation of fused loops and assert the resulting IR stream.

3. **Backend Lowering**
   - [x] Implement backend emission for the fused opcodes:
     - [x] x86-64 linear backend templates that update the counter, compare against the limit, and branch to the recorded back-edge.
     - [x] Fallback helper stubs for architectures without inline templates (or reuse the interpreter helpers).
       - ARM64 now calls `orus_jit_native_fused_loop_step` so fused loops keep running even when the backend relies on helper dispatch.
   - [x] Integrate the new operations with existing bail-out and deoptimization plumbing.
   - [x] Extend backend tests to cover both incrementing and decrementing loops across numeric kinds.
     - Added `test_backend_emits_fused_increment_loops` / `test_backend_emits_fused_decrement_loops` to exercise i32/i64/u32/u64 fused counters end-to-end in the baseline backend.

4. **Benchmark Validation**
   - [ ] Re-run the "optimized loop" benchmark to confirm translations succeed and runtime counters increment inside the JIT tier.
   - [ ] Capture before/after timing deltas and update `JIT_BENCHMARK_RESULTS.md` once the win is verified.

## Open Questions
- Do we need saturation/overflow checks beyond what the interpreter performs, or can we rely on the typed register caches to guarantee safety?
- Should we surface per-kind rollout toggles for fused loops, or reuse the existing integer rollout gating?
- How do we reconcile the signed offset in the bytecode with the relocation mechanics in the backend (e.g., extend the branch patching helpers or lower to short jumps)?

## Next Steps
1. Land the IR and translator changes alongside regression tests.
2. Prototype backend lowering using helper stubs to validate semantics quickly.
3. Optimize the stubs into inline DynASM templates once correctness is established.
