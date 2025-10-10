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

## Native Tier Bootstrap Notes
- Vendor DynASM 1.3.0 (MIT) under `third_party/dynasm/` to keep the toolchain
  hermetic.
- Wrap DynASM through `OrusJitBackend` so the VM can request native entries
  without exposing assembler details to the rest of the runtime.
- Emit architecture-specific return stubs so both x86-64 (DynASM assembled)
  and AArch64 (direct encoding with macOS `MAP_JIT` support) builds expose the
  same entry-point contract to the VM.

## OrusJit IR → DynASM Pipeline
- Describe hot-path kernels through a compact `OrusJitIRProgram` so both
  interpreter tiers and the DynASM backend share the same architecture-neutral
  representation before assembly.
- Build a DynASM action list at runtime by translating each IR opcode to its
  machine encoding (`DASM_ESC`-prefixed byte streams for x86-64) and then link
  and encode it into an executable buffer guarded by MAP_JIT/`mprotect()`.
- AArch64 falls back to direct 32-bit encodings for the same IR opcodes to keep
  stub behaviour identical across CPUs.

```c
OrusJitIRInstruction ir[] = {
    { .opcode = ORUS_JIT_IR_OP_RETURN },
};
OrusJitIRProgram program = {
    .instructions = ir,
    .count = sizeof(ir) / sizeof(ir[0]),
};

JITEntry entry = {0};
if (orus_jit_backend_compile_ir(vm->jit_backend, &program, &entry) ==
        JIT_BACKEND_OK) {
    vm->jit_entry_stub = entry;
}
```

```c
JITEntry stub = {0};
if (orus_jit_backend_compile_noop(vm->jit_backend, &stub) == JIT_BACKEND_OK) {
    vm->jit_entry_stub = stub;
    vm->jit_enabled = true;
}
```

## JIT Entry Cache Lifecycle
- Cache compiled entries inside the VM so hot paths can jump back into native
  code without rebuilding DynASM buffers every time a loop trips the tiering
  heuristics.
- Each cache slot tracks the owning function/loop pair alongside a monotonically
  increasing generation so deoptimization can retire stale code without
  flushing the entire tier.
- Backend invalidation hooks delegate to `vm_jit_invalidate_entry()` and the VM
  teardown path calls `vm_jit_flush_entries()` before destroying the backend to
  guarantee executable pages are released.

```c
JITEntry entry = {0};
if (orus_jit_backend_compile_ir(vm->jit_backend, &program, &entry) ==
        JIT_BACKEND_OK) {
    uint64_t generation = vm_jit_install_entry(function_id, loop_id, &entry);
    if (generation == 0) {
        // Cache rejected the entry (likely OOM); fall back to interpreter.
        orus_jit_backend_release_entry(vm->jit_backend, &entry);
    }
}

const JITEntry* cached = vm_jit_lookup_entry(function_id, loop_id);
if (cached && cached->entry_point) {
    orus_jit_backend_vtable()->enter(&vm, cached);
}
```

## Type-Mismatch Deoptimization Stubs
- Specialised functions now receive a compact metadata stub that records the
  arity so the runtime can flush cached parameter types before falling back to
  baseline bytecode.
- Every `ERROR_TYPE` reported through `VM_ERROR_RETURN` triggers
  `vm_handle_type_error_deopt()`, ensuring tiered execution reverts to the
  interpreter without leaking stale typed-window state.

```c
void vm_handle_type_error_deopt(void) {
    CallFrame* frame = vm.register_file.current_frame;
    if (!frame || frame->functionIndex == UINT16_MAX) {
        return;
    }

    Function* function = &vm.functions[frame->functionIndex];
    if (!function || function->tier != FUNCTION_TIER_SPECIALIZED) {
        return;
    }

    if (function->deopt_stub_chunk && function->deopt_stub_chunk->code) {
        uint8_t arity = function->deopt_stub_chunk->code[0];
        uint16_t base = frame->parameterBaseRegister;
        for (uint8_t i = 0; i < arity; ++i) {
            uint16_t reg = (uint16_t)(base + i);
            if (vm_typed_reg_in_range(reg)) {
                vm_clear_typed_register_slot(reg,
                                             vm_active_typed_window()->reg_types[reg]);
            }
        }
    }

    if (function->deopt_handler) {
        function->deopt_handler(function);
    }
}
```

## Deoptimization Fallback to Interpreter
- The default deoptimization handler now retargets the active specialised frame
  to the baseline chunk so execution resumes inside the interpreter instead of
  replaying specialised bytecode after a bailout.
- The instruction pointer offset is preserved when swapping chunks, keeping the
  interpreter aligned with the original control-flow position.

```c
static void vm_fallback_to_interpreter(Function* function) {
    if (!function || !function->chunk || vm.chunk != function->specialized_chunk) {
        return;
    }

    uint8_t* specialized_start = function->specialized_chunk->code + function->start;
    size_t delta = (size_t)(vm.ip - specialized_start);
    size_t baseline_span = (size_t)(function->chunk->count - function->start);
    if (delta > baseline_span) {
        delta = baseline_span;
    }

    vm.chunk = function->chunk;
    vm.ip = function->chunk->code + function->start + delta;
}

void vm_default_deopt_stub(Function* function) {
    if (function->tier == FUNCTION_TIER_SPECIALIZED) {
        function->tier = FUNCTION_TIER_BASELINE;
        function->specialization_hits = 0;
        vm_fallback_to_interpreter(function);
    }
}
```

## Frame Layout Parity Between Interpreter and JIT
- Export the interpreter's frame/register layout through
  `vm/jit_layout.h` so DynASM templates can rely on the same structure
  offsets as the C interpreter.
- Lock offsets in place with `_Static_assert` guards; any divergence between
  interpreter structs and the layout expected by native code now fails at
  compile time instead of surfacing as runtime corruption.

```c
#include "vm/jit_layout.h"

_Static_assert(ORUS_JIT_OFFSET_FRAME_TEMPS ==
                   (FRAME_REGISTERS * sizeof(Value)),
               "Frame temps must follow registers contiguously");

static inline Value*
vm_jit_frame_reg_ptr(CallFrame* frame, uint16_t reg) {
    return &frame->registers[reg - FRAME_REG_START];
}
```
