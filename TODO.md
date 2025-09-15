# Orus Compiler TODOs (tracked from COMPILER_IMPLEMENTATION_PLAN)

Priority key: [H] High, [M] Medium, [L] Low

## Language and Frontend
- [H] Implement `match` parsing and codegen (lexer token exists)

## Optimizations
- [H] Dead Code Elimination (control-flow aware)
- [H] Constant Propagation + Copy Propagation
- [H] Common Subexpression Elimination (CSE)
- [M] Loop Invariant Code Motion (LICM)
- [M] Function Inlining (small functions)
- [M] Tail Recursion Optimization
- [M] Register Coalescing improvements

## VM Feature Utilization
- [H] Emit OP_INC_CMP_JMP for `for`-like induction loops
- [M] Prefer OP_ADD_I32_IMM and other immediate forms when a source is constant
- [M] Expand typed register usage heuristics in hot arithmetic regions

## Codegen Infrastructure
- [M] Centralize jump patching with `emit_jump_placeholder/patch_jump`
- [M] Improve bytecode debug dump to handle variable-length instructions correctly

## Diagnostics and Safety
- [M] Compiler-pass error reporting unification (consistent diagnostics with source locations)
- [L] Cast safety analysis + warnings (per plan’s Cast Safety section)

## Testing
- [H] Unit tests for optimizer passes (folding, DCE, propagation)
- [H] Integration tests for for-loops (range/inclusive/step), while, and conditionals
- [M] Golden tests for bytecode emission patterns (short jumps, loop fusion)

