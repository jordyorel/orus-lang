# Optimized Loop JIT Enablement Roadmap

This roadmap tracks the implementation work needed for the optimized loop benchmark to execute inside the baseline JIT tier.

## Status Overview
- **Control-flow lowering:** ✅ Completed – branch bytecodes, typed comparisons, and their backend emission now translate end-to-end on both x86-64 and ARM64 with regression coverage in place.
- **Local/iterator bytecodes:** ✅ Completed – translator keeps frame window ops, range construction, and iterator traversal inside the baseline tier on both backends with regression coverage.
- **Runtime helper calls:** ✅ Completed – builtin call, print, assert, and array helpers stay inside the tier through dedicated IR and backend stubs.
- **Fused loop bytecodes:** 🛠️ In progress – IR models the fused increment/decrement patterns and the translator now records their counter, bounds, and step metadata ahead of backend lowering.

## Completed: Control-flow lowering
1. ✅ `orus_jit_translate_linear_block` now translates the VM's branch and comparison opcodes into IR instead of bailing out with `UNHANDLED_OPCODE`.
2. ✅ The x86-64 backend encodes forward/backward branches and dispatches conditional jumps through helper stubs.
3. ✅ The ARM64 backend mirrors the new branch emission logic with patchable short branches and comparison helpers.
4. ✅ Regression coverage ensures loops using `if`/`while` constructs translate without aborting (see `test_vm_jit_translation.c`).

## Completed: Local/iterator bytecodes
1. ✅ `orus_jit_translate_linear_block` emits IR moves for `OP_STORE_FRAME`, `OP_MOVE_FRAME`, and `OP_LOAD_FRAME`, keeping frame-resident locals in the JIT register tracker.
2. ✅ Range materialization through `OP_RANGE_R` lowers into dedicated IR with helper-backed execution on x86-64 and ARM64 so iterator inputs stay inside the baseline tier.
3. ✅ Iterator lowering covers `OP_GET_ITER_R` and `OP_ITER_NEXT_R` end-to-end via new IR instructions, helper-stub execution, and translator plumbing, with regression coverage in `test_vm_jit_translation.c`.

## Next Steps (after control-flow lowering)
- **Benchmark validation:** Confirm the optimized loop benchmark tiers up under the expanded helper set and update rollout thresholds if additional bytecodes appear during translation.
- **Helper Calls:** ✅ Baseline IR now handles `OP_TIME_STAMP`, `OP_ARRAY_PUSH_R`, `OP_ARRAY_POP_R`, `OP_PRINT_*`, `OP_ASSERT_EQ_R`, `OP_CALL_NATIVE_R`, and `OP_CALL_FOREIGN` via native helpers on both backends with regression coverage.
- **Fused Loop Lowering:** Implement backend templates or helper stubs for the new fused loop IR opcodes on x86-64 and ARM64, then re-run the optimized loop benchmark to confirm tier-up.

Progress on each section should be reflected here as work completes.
