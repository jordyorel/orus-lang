# Orus VM Instruction Set Reference

This document describes the Orus register virtual machine as it is implemented today. It is
structured to be easy to browse while still exposing every opcode, operand layout, and runtime
component that the compiler targets.

---

## 1. Execution Model Overview

### 1.1 Register File Layout

The VM exposes 256 architected registers split into four contiguous banks that align with the
compiler's allocator. These ranges are fixed at compile time and are shared by all instructions and
optimization passes.【F:include/vm/vm_constants.h†L14-L33】

| Range | Symbolic name | Purpose |
|-------|---------------|---------|
| `R0` – `R63` | Global registers | Module-level bindings and exported globals.
| `R64` – `R191` | Frame registers | Function parameters and locals belonging to the active call frame.
| `R192` – `R239` | Temporary registers | Scratch registers that code generation uses for expression evaluation.
| `R240` – `R255` | Module registers | Slots reserved for module loader bookkeeping and cross-module imports.

Additional logical register classes are layered on top of the primary file:

- **Spill registers** are backed by the spill manager once an allocation requires more than 256 live
  values. `OP_LOAD_SPILL` and `OP_STORE_SPILL` provide access using 16-bit spill identifiers.
- **Typed register windows** maintain unboxed mirrors for numeric and boolean values. Each window
  caches `i32`, `i64`, `u32`, `u64`, `f64`, and `bool` views along with a boxed `Value` mirror so
  the interpreter can bypass boxing costs when a typed opcode executes.【F:include/vm/vm.h†L1081-L1124】

### 1.2 Value Representation

All general-purpose instructions operate on the boxed `Value` union. It carries an explicit
`ValueType` tag that distinguishes primitive scalars, heap-allocated objects (strings, arrays,
closures, files, errors, iterators), and iterator specializations.【F:include/vm/vm.h†L18-L126】

Typed instructions use the cached numeric/boolean windows when the compiler can prove a register's
static type. The VM keeps the boxed and unboxed copies coherent through helper routines declared in
`register_file.c` and `register_cache.c`. When a typed register is mutated, its `dirty` flag forces a
synchronization back to the boxed `Value` view before any instruction that expects general values is
run.【F:include/vm/vm.h†L1081-L1124】

### 1.3 Call Frames and Control Stack

The runtime maintains an array of call frames (`FRAMES_MAX = 256`) and supports structured
exception handling via an explicit try-frame stack (`TRY_MAX = 16`). Each frame tracks the current
instruction pointer, the base register window, and the callee closure/function metadata so that
`OP_CALL_*`, `OP_RETURN_*`, and the try instructions can restore state deterministically.【F:include/vm/vm.h†L27-L104】

### 1.4 Bytecode Containers

Bytecode is emitted into `Chunk` objects. Each chunk stores:

- `code`: raw instruction stream.
- `constants`: a constant pool referenced by load instructions.
- `lines` / `columns`: per-instruction source mapping for diagnostics.

The disassembler (`src/vm/utils/debug.c`) and both dispatchers (`vm_dispatch_switch.c` and
`vm_dispatch_goto.c`) interpret the stream as a sequence of variable-length instructions.

---

## 2. Instruction Encoding Conventions

Every instruction starts with an 8-bit opcode. Operands are encoded inline using helper macros that
advance the instruction pointer.【F:include/vm/vm_dispatch.h†L58-L80】 Common operand patterns are
summarized below:

| Pattern | Encoding | Meaning |
|---------|----------|---------|
| `reg` | `READ_BYTE()` | Index into the 256-register file.
| `const16` | `READ_SHORT()` | 16-bit constant pool index.
| `offset16` | `READ_SHORT()` interpreted as signed | Relative jump offset for long jumps and loops.
| `offset8` | Single byte | Unsigned forward/backward offsets for short jumps.
| `imm8` | Single byte | Small immediate literal embedded directly in the opcode stream.
| `spill16` | Two bytes | Encoded high/low bytes composing a 16-bit spill identifier.
| `var_count` | Single byte | Number of operands that follow (used by variadic instructions like `OP_PRINT_MULTI_R`).
| `upvalue_indices` | Inline stream | Sequence of bytes emitted immediately after `OP_CLOSURE_R` describing captured slots.

Unless noted otherwise, operands are ordered as they appear in the enumeration comments. The debug
utilities in `src/vm/utils/debug.c` provide concrete formatting examples for every implemented
opcode.

---

## 3. Opcode Catalogue

The tables below list every opcode defined in `include/vm/vm.h`. Each entry describes its operand
layout, the runtime behavior, and any notable implementation detail. When an opcode is declared but
not yet wired into the dispatch loop, it is marked as **reserved**.

### 3.1 Literal Loading and Register Moves

| Opcode | Operands | Behavior |
|--------|----------|----------|
| `OP_LOAD_CONST` | `dst, const_hi, const_lo` | Loads a boxed value from the chunk constant pool into `dst`.【F:include/vm/vm.h†L492-L527】【F:src/vm/handlers/vm_memory_handlers.c†L16-L23】
| `OP_LOAD_TRUE` / `OP_LOAD_FALSE` | `dst` | Writes the boolean literal into `dst` and updates the typed boolean cache.【F:include/vm/vm.h†L492-L527】【F:src/vm/handlers/vm_memory_handlers.c†L35-L48】
| `OP_MOVE` | `dst, src` | Copies a value between registers while consulting the typed cache to avoid boxing when possible.【F:include/vm/vm.h†L499-L502】【F:src/vm/handlers/vm_memory_handlers.c†L51-L86】
| `OP_LOAD_GLOBAL` | `dst, global_index` | Reads a module/global slot and stores it in `dst`. Type declarations are enforced before the value is returned.【F:include/vm/vm.h†L503-L504】【F:src/vm/dispatch/vm_dispatch_goto.c†L490-L521】
| `OP_STORE_GLOBAL` | `global_index, src` | Writes `src` into a global slot, applying smart coercions for declared numeric types and validating user-defined types.【F:include/vm/vm.h†L503-L504】【F:src/vm/dispatch/vm_dispatch_goto.c†L522-L612】

### 3.2 Arithmetic and Bitwise Operations

All arithmetic instructions store their result in `dst`. Overflow and domain checks occur in the
handler and emit runtime errors when violated.

| Family | Opcodes | Operands | Notes |
|--------|---------|----------|-------|
| 32-bit signed arithmetic | `OP_ADD_I32_R`, `OP_SUB_I32_R`, `OP_MUL_I32_R`, `OP_DIV_I32_R`, `OP_MOD_I32_R` | `dst, lhs, rhs` | Basic binary math on boxed `Value` operands.【F:include/vm/vm.h†L506-L547】
| 32-bit increment/decrement | `OP_INC_I32_R`, `OP_INC_I32_CHECKED`, `OP_DEC_I32_R` | `reg` | Increment variants share a slow-path helper today; the `*_CHECKED` opcodes reserve space for overflow-aware versions.【F:include/vm/vm.h†L511-L528】【F:src/vm/dispatch/vm_dispatch_goto.c†L938-L971】
| 64-bit signed arithmetic | `OP_ADD_I64_R`, `OP_SUB_I64_R`, `OP_MUL_I64_R`, `OP_DIV_I64_R`, `OP_MOD_I64_R`, `OP_INC_I64_R`, `OP_INC_I64_CHECKED` | Binary ops use three registers; increments are unary.【F:include/vm/vm.h†L528-L555】【F:src/vm/dispatch/vm_dispatch_goto.c†L938-L997】
| Unsigned arithmetic | `OP_ADD_U32_R`, `OP_SUB_U32_R`, `OP_MUL_U32_R`, `OP_DIV_U32_R`, `OP_MOD_U32_R`, `OP_INC_U32_R`, `OP_INC_U32_CHECKED`, `OP_ADD_U64_R`, `OP_SUB_U64_R`, `OP_MUL_U64_R`, `OP_DIV_U64_R`, `OP_MOD_U64_R`, `OP_INC_U64_R`, `OP_INC_U64_CHECKED` | Same operand layout as signed families. Division and modulus perform zero checks per width.【F:include/vm/vm.h†L531-L571】【F:src/vm/dispatch/vm_dispatch_goto.c†L882-L933】
| Floating-point arithmetic | `OP_ADD_F64_R`, `OP_SUB_F64_R`, `OP_MUL_F64_R`, `OP_DIV_F64_R`, `OP_MOD_F64_R` | `dst, lhs, rhs` | Uses IEEE-754 `double`; modulus delegates to `fmod` with overflow checks.【F:include/vm/vm.h†L571-L585】【F:src/vm/dispatch/vm_dispatch_goto.c†L901-L923】
| Bitwise operations | `OP_AND_I32_R`, `OP_OR_I32_R`, `OP_XOR_I32_R`, `OP_NOT_I32_R`, `OP_SHL_I32_R`, `OP_SHR_I32_R` | `dst, lhs, rhs` (unary for NOT) | Operate on 32-bit integers. Shift counts come from registers and are masked inside the handler.【F:include/vm/vm.h†L587-L598】
| Integer negation | `OP_NEG_I32_R` | `dst, src` | Dedicated negation used by optimizer fusions; currently implemented in the dispatch slow path.【F:include/vm/vm.h†L784-L788】【F:src/vm/dispatch/vm_dispatch_switch.c†L554-L571】

### 3.3 Comparison and Logic

| Family | Opcodes | Operands | Behavior |
|--------|---------|----------|----------|
| Equality | `OP_EQ_R`, `OP_NE_R` | `dst, lhs, rhs` | Boxed value comparison via `valuesEqual` or pointer equality depending on type.【F:include/vm/vm.h†L600-L604】【F:src/vm/dispatch/vm_dispatch_goto.c†L726-L823】
| Signed comparisons | `OP_LT_I32_R`, `OP_LE_I32_R`, `OP_GT_I32_R`, `OP_GE_I32_R`, same set for `I64` | `dst, lhs, rhs` | Produce boolean results; handlers perform type validation before comparison.【F:include/vm/vm.h†L604-L623】
| Unsigned comparisons | `OP_LT_U32_R`, `OP_LE_U32_R`, `OP_GT_U32_R`, `OP_GE_U32_R`, and 64-bit variants | `dst, lhs, rhs` | Comparisons assume unsigned semantics and throw on signed operands.【F:include/vm/vm.h†L623-L639】
| Floating-point comparisons | `OP_LT_F64_R`, `OP_LE_F64_R`, `OP_GT_F64_R`, `OP_GE_F64_R` | `dst, lhs, rhs` | Handles NaN semantics explicitly in the runtime handler.【F:include/vm/vm.h†L615-L623】
| Logical | `OP_AND_BOOL_R`, `OP_OR_BOOL_R`, `OP_NOT_BOOL_R` | `dst, lhs, rhs` (unary for NOT) | Implements short-circuit semantics at the bytecode level by consuming boolean registers only.【F:include/vm/vm.h†L641-L646】

### 3.4 Type Conversions

Each conversion validates the source register type before coercing. Failing validation yields a
runtime `ERROR_TYPE` exception.

| Target | Opcodes | Notes |
|--------|---------|-------|
| Floating-point | `OP_I32_TO_F64_R`, `OP_I64_TO_F64_R`, `OP_U32_TO_F64_R`, `OP_U64_TO_F64_R`, `OP_F64_TO_BOOL_R`, `OP_F64_TO_I32_R`, `OP_F64_TO_I64_R`, `OP_F64_TO_U32_R`, `OP_F64_TO_U64_R` | Converts between integers and IEEE-754 doubles while checking for overflow where required.【F:include/vm/vm.h†L648-L676】
| Signed integers | `OP_I32_TO_I64_R`, `OP_I64_TO_I32_R`, `OP_BOOL_TO_I32_R`, `OP_BOOL_TO_I64_R`, `OP_U64_TO_I32_R`, `OP_U64_TO_I64_R` | Downcasts enforce bounds; boolean conversions map `true/false` to `1/0`.
| Unsigned integers | `OP_I32_TO_U32_R`, `OP_I64_TO_U32_R`, `OP_BOOL_TO_U32_R`, `OP_BOOL_TO_U64_R`, `OP_I32_TO_U64_R`, `OP_I64_TO_U64_R`, `OP_U32_TO_U64_R`, `OP_U64_TO_U32_R` | Maintains unsigned semantics, performing sign and range checks.
| Boolean | `OP_I32_TO_BOOL_R`, `OP_I64_TO_BOOL_R`, `OP_U32_TO_BOOL_R`, `OP_U64_TO_BOOL_R` | Treats zero as `false` and non-zero as `true`.

### 3.5 Strings, Arrays, Enums, and Iterators

| Opcode | Operands | Behavior |
|--------|----------|----------|
| `OP_CONCAT_R` | `dst, lhs, rhs` | Concatenates two strings (or values convertible to strings) into a new `ObjString`.
| `OP_TO_STRING_R` | `dst, src` | Converts arbitrary values to their string representation.
| `OP_STRING_INDEX_R` / `OP_STRING_GET_R` | `dst, string_reg, index_reg` | Bounds-checked string indexing and code-point extraction.【F:include/vm/vm.h†L678-L691】
| `OP_MAKE_ARRAY_R` | `dst, start_reg, count` | Materializes an array from a contiguous register window.【F:include/vm/vm.h†L693-L705】
| `OP_ENUM_NEW_R` | `dst, variant_idx, payload_count, payload_start, type_const` | Constructs an enum instance using a constant pool type descriptor.
| `OP_ENUM_TAG_EQ_R` | `dst, enum_reg, variant_idx` | Compares an enum's active variant tag.
| `OP_ENUM_PAYLOAD_R` | `dst, enum_reg, variant_idx, field_idx` | Extracts a specific payload element; errors if tags mismatch.
| `OP_ARRAY_GET_R` / `OP_ARRAY_SET_R` | `dst?, array_reg, index_reg[, value_reg]` | Performs bounds-checked element access and mutation.
| `OP_ARRAY_LEN_R` | `dst, array_reg` | Returns the logical length of an array.
| `OP_ARRAY_PUSH_R` / `OP_ARRAY_POP_R` | `array_reg[, value_reg/dst]` | Mutates dynamic arrays, returning the popped value when applicable.
| `OP_ARRAY_SORTED_R` | `dst, array_reg` | Returns a sorted copy using the runtime's comparison helpers.
| `OP_ARRAY_REPEAT_R` | `dst, array_reg, count_reg` | Produces a repeated array sequence.
| `OP_ARRAY_SLICE_R` | `dst, array_reg, start_reg, end_reg` | Clones a slice with inclusive/exclusive bounds checks.【F:include/vm/vm.h†L693-L705】
| `OP_GET_ITER_R` | `dst_iter, iterable_reg` | Creates an iterator over arrays, ranges, or enum payloads.【F:include/vm/vm.h†L711-L720】
| `OP_ITER_NEXT_R` | `dst_value, iter_reg, has_value_reg` | Advances an iterator, setting `has_value_reg` to signal completion.

### 3.6 Control Flow and Exception Handling

| Opcode | Operands | Behavior |
|--------|----------|----------|
| `OP_TRY_BEGIN` / `OP_TRY_END` | `handler_offset`, implicit | Pushes/pops a try frame enabling structured exception handling. The catch register is configured by the compiler when emitting the try body.【F:include/vm/vm.h†L707-L720】【F:src/vm/runtime/vm.c†L269-L336】
| `OP_JUMP` | `offset16` | Relative jump (forward or backward) using a signed 16-bit displacement.【F:include/vm/vm_dispatch.h†L58-L80】
| `OP_LOOP` | `offset16` | Backward jump optimized for loops; cooperates with profiling hooks.
| `OP_JUMP_IF_R` / `OP_JUMP_IF_NOT_R` | `cond_reg, offset16` | Branches based on boxed boolean truthiness.
| `OP_JUMP_IF_NOT_I32_TYPED` | `lhs, rhs, offset16` | Specialized typed comparison used by hot loops to bypass boxing for integer guard conditions.【F:include/vm/vm.h†L707-L720】
| `OP_JUMP_SHORT`, `OP_JUMP_BACK_SHORT`, `OP_JUMP_IF_NOT_SHORT`, `OP_LOOP_SHORT` | `offset8` | Single-byte variants for compact short-range branches.【F:include/vm/vm.h†L736-L745】
| `OP_BRANCH_TYPED` | `loop_id_hi, loop_id_lo, predicate_reg, offset16` | Hybrid loop branch that couples typed guards with optimizer metadata.

### 3.7 Function Calls and Frames

| Opcode | Operands | Behavior |
|--------|----------|----------|
| `OP_CALL_R` | `func_reg, first_arg_reg, arg_count, result_reg` | Invokes a closure/function stored in `func_reg` using registers for arguments; result is written to `result_reg` unless the call is void.【F:include/vm/vm.h†L722-L730】
| `OP_CALL_NATIVE_R` | `native_index, first_arg_reg, arg_count, result_reg` | Dispatches into the native builtin table.
| `OP_CALL_FOREIGN` | `foreign_index, first_arg_reg, arg_count, result_reg` | Invokes a registered foreign host binding. Foreign entries reuse the native table today so the opcode is JIT-friendly while the dedicated FFI registry lands.【F:src/vm/dispatch/vm_dispatch_goto.c†L3655-L3687】
| `OP_TAIL_CALL_R` | Same as `OP_CALL_R` | Reuses the current frame when the call is in tail position.
| `OP_RETURN_R` / `OP_RETURN_VOID` | `value_reg` or none | Restores the caller's frame, optionally writing a return value.
| `OP_ENTER_FRAME` | `frame_size` | Allocates local register space for a callee frame.【F:include/vm/vm.h†L732-L738】
| `OP_EXIT_FRAME` | — | Pops the current call frame.
| `OP_LOAD_FRAME` | `dst, frame_offset` | Reads from the active frame's register window.
| `OP_STORE_FRAME` | `frame_offset, src` | Writes to a frame slot.
| `OP_MOVE_FRAME` | `dst_offset, src_offset` | Moves data inside the current frame without touching globals.

### 3.8 Spill and Module Operations

| Opcode | Operands | Behavior |
|--------|----------|----------|
| `OP_LOAD_SPILL` | `dst, spill_hi, spill_lo` | Loads a spilled value identified by a 16-bit ID managed by `spill_manager.c`.【F:include/vm/vm.h†L740-L748】
| `OP_STORE_SPILL` | `spill_hi, spill_lo, src` | Stores a register into the spill arena.
| `OP_LOAD_MODULE` | `dst, module_id, module_offset` | **Reserved.** Planned for multi-module execution.
| `OP_STORE_MODULE` | `module_id, module_offset, src` | **Reserved.**
| `OP_LOAD_MODULE_NAME` | `const16` | **Reserved.** Will resolve module handles by name.
| `OP_SWITCH_MODULE` | `module_id` | **Reserved.**
| `OP_EXPORT_VAR` | `name_index, src` | **Reserved.**
| `OP_IMPORT_VAR` | `name_index, module_id` | **Reserved.**

### 3.9 Closures and Upvalues

| Opcode | Operands | Behavior |
|--------|----------|----------|
| `OP_CLOSURE_R` | `dst, function_reg, upvalue_count, upvalue_stream…` | Allocates a closure capturing listed upvalues. The compiler emits one byte per captured slot following the opcode.【F:include/vm/vm.h†L750-L757】
| `OP_GET_UPVALUE_R` | `dst, upvalue_index` | Reads from a captured variable.
| `OP_SET_UPVALUE_R` | `upvalue_index, src` | Mutates an upvalue.
| `OP_CLOSE_UPVALUE_R` | `local_reg` | Closes any open upvalues pointing to `local_reg` before the stack slot goes out of scope.

### 3.10 Runtime Services and I/O

| Opcode | Operands | Behavior |
|--------|----------|----------|
| `OP_PARSE_INT_R` | `dst, value_reg` | Parses strings/bytes into integers with runtime validation.【F:include/vm/vm.h†L759-L771】
| `OP_PARSE_FLOAT_R` | `dst, value_reg` | Parses into `f64`.
| `OP_TYPE_OF_R` | `dst, value_reg` | Returns a string describing the runtime type tag.
| `OP_IS_TYPE_R` | `dst, value_reg, type_reg` | Compares a runtime type descriptor.
| `OP_INPUT_R` | `dst, arg_count, prompt_reg` | Reads console input with optional prompt.
| `OP_RANGE_R` | `dst, arg_count, start, end, step` | Constructs a range iterator over numeric bounds.
| `OP_PRINT_MULTI_R` | `first_reg, count, newline_flag` | Prints a sequence of registers, optionally ending with a newline.
| `OP_PRINT_R` | `value_reg` | Convenience single-value print.
| `OP_ASSERT_EQ_R` | `dst, label_reg, actual_reg, expected_reg` | Raises a runtime error when `actual` and `expected` differ; used by the test harness.
| `OP_TIME_STAMP` | `dst` | Returns a high-resolution timestamp (nanoseconds) for profiling.【F:include/vm/vm.h†L771-L782】

### 3.11 Typed Fast-Path Operations

Typed opcodes operate exclusively on the typed register window and skip boxing/unboxing overhead.
Handlers live in `src/vm/operations/vm_typed_ops.c` and `vm_arithmetic.c`.

| Family | Opcodes | Behavior |
|--------|---------|----------|
| Arithmetic | `OP_ADD_I32_TYPED`, `OP_SUB_I32_TYPED`, `OP_MUL_I32_TYPED`, `OP_DIV_I32_TYPED`, `OP_MOD_I32_TYPED`, and corresponding `I64`, `U32`, `U64`, `F64` variants | Perform arithmetic directly on unboxed arrays. Division checks for zero and propagates errors through the standard runtime path.【F:include/vm/vm.h†L747-L770】【F:src/vm/operations/vm_typed_ops.c†L33-L210】
| Comparisons | `OP_LT_*_TYPED`, `OP_LE_*_TYPED`, `OP_GT_*_TYPED`, `OP_GE_*_TYPED` for each numeric width | Write boolean results into the typed boolean cache and synchronize to the boxed register file when needed.【F:include/vm/vm.h†L712-L745】
| Typed loads | `OP_LOAD_I32_CONST`, `OP_LOAD_I64_CONST`, `OP_LOAD_F64_CONST` | Load constants of the declared type from the pool and seed both the boxed register and the typed cache.【F:include/vm/vm.h†L746-L752】【F:src/vm/handlers/vm_memory_handlers.c†L269-L315】
| Typed moves | `OP_MOVE_I32`, `OP_MOVE_I64`, `OP_MOVE_F64` | Copy typed values while updating cache metadata so downstream typed ops can reuse them.【F:include/vm/vm.h†L752-L758】【F:src/vm/handlers/vm_memory_handlers.c†L317-L360】

### 3.12 Fused and Optimized Instructions

These opcodes exist to collapse common multi-instruction sequences in hot loops. Not all of them are
emitted today, but they have dedicated handlers for future optimizer passes.

| Opcode | Operands | Notes |
|--------|----------|-------|
| `OP_ADD_I32_IMM`, `OP_SUB_I32_IMM`, `OP_MUL_I32_IMM` | `dst, src, imm8` | Apply arithmetic with an 8-bit immediate operand.
| `OP_CMP_I32_IMM` | `dst, src, imm8` | Compares a register against an immediate.
| `OP_LOAD_ADD_I32` | `dst, addr_reg, operand_reg` | Loads, adds, and stores in a single fused pattern.
| `OP_LOAD_CMP_I32` | `dst, addr_reg, operand_reg` | Loads and compares in one step.
| `OP_INC_CMP_JMP` | `counter_reg, limit_reg, offset16` | Fused increment + compare + jump used by `for` loops.
| `OP_DEC_CMP_JMP` | `counter_reg, zero_reg, offset16` | Fused decrement loops.
| `OP_MUL_ADD_I32` | `dst, lhs, rhs, addend` | Multiply-add fusion.
| `OP_LOAD_INC_STORE` | `addr_reg` | Atomic increment of a memory slot.

### 3.13 Extended Register Instructions

| Opcode | Operands | Behavior |
|--------|----------|----------|
| `OP_LOAD_CONST_EXT` | `reg_hi, reg_lo, const_hi, const_lo` | Loads a constant into an extended (16-bit) register index for future >256 register files.【F:include/vm/vm.h†L789-L796】【F:src/vm/handlers/vm_memory_handlers.c†L20-L34】
| `OP_MOVE_EXT` | `dst_hi, dst_lo, src_hi, src_lo` | Moves values between extended registers.
| `OP_STORE_EXT` | `reg_hi, reg_lo, addr_hi, addr_lo` | **Reserved.** Planned for memory-mapped storage.
| `OP_LOAD_EXT` | `reg_hi, reg_lo, addr_hi, addr_lo` | **Reserved.**

### 3.14 Garbage Collection and Imports

| Opcode | Status | Notes |
|--------|--------|-------|
| `OP_IMPORT_R` | **Reserved** | Placeholder for high-level module import helpers; dispatch handlers are not yet implemented.【F:include/vm/vm.h†L782-L788】
| `OP_GC_PAUSE` / `OP_GC_RESUME` | **Reserved** | Planned hooks to coordinate with the garbage collector.

### 3.15 Program Termination

| Opcode | Operands | Behavior |
|--------|----------|----------|
| `OP_HALT` | — | Stops execution and returns control to the embedding host.【F:include/vm/vm.h†L797-L800】

---

## 4. Runtime Instrumentation

The VM tracks an `instruction_count`, exposes fine-grained profiling (`vm_profiling.c`), and
supports optional bytecode tracing when `vm.trace` is enabled. Both dispatchers increment
instruction statistics before executing the handler, allowing external tools to correlate opcodes
with performance metrics.【F:include/vm/vm.h†L1151-L1188】【F:src/vm/dispatch/vm_dispatch_goto.c†L145-L205】

---

## 5. Integration Notes for the Compiler

1. **Register allocation:** Frontend passes must honor the register bank layout in §1.1 to keep
   globals, locals, and temporaries isolated. The allocator in `compiler/register_allocator.c` tracks
   live ranges and spill counts that feed directly into the VM contract.【F:include/vm/vm.h†L812-L983】
2. **Typed opcode emission:** Emit typed instructions only after the type inference pipeline has
   annotated expressions. Each typed opcode requires that the destination register's typed window be
   initialized via `OP_LOAD_*_CONST` or `OP_MOVE_*`.
3. **Exception safety:** When emitting `OP_TRY_BEGIN`, allocate a catch register in the frame range
   so that runtime unwinding can safely store the caught error value before resuming execution.
4. **Future modules:** Module opcodes (§3.8) are declared but intentionally unused. Backends should
   continue to synthesize imports/exports through compiler-managed data structures until the runtime
   module manager advertises stable hooks.

This reference stays in lock-step with the implementation—consult it before adding or removing
opcodes, and update it in tandem with any changes to the dispatch handlers or typed register
machinery.
