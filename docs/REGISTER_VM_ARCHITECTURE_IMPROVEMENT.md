# Register-Based VM Architecture Roadmap for Orus

## Overview

This roadmap captures the verified status of Orus's register-based virtual machine and documents the remaining engineering work needed to reach the scalable, typed execution model described in the performance roadmap. The intent is to keep this file aligned with the codebase so design discussions always begin from an accurate baseline.

## Current Architecture Snapshot (2025 Baseline)

- **Hierarchical register windows are live.** `CallFrame` instances carry dedicated frame and temporary arrays plus a typed register window pointer. Pushing a frame binds a fresh typed window and synchronises shared global/module ranges, while popping a frame restores the parent's window.
- **Typed register caches back hot paths.** Helpers such as `vm_store_*_typed_hot`, `vm_try_read_*_typed`, and the fused arithmetic opcodes operate directly on the typed arrays and skip boxed writes whenever the register stays in the same numeric bank and no upvalues are observing it.
- **Boxing now happens on demand.** `vm_get_register_safe()` repopulates boxed `Value` slots only when a dirty typed slot is read through the legacy interface, ensuring steady-state loops execute entirely on typed data.
- **Spill infrastructure exists but is manual.** The spill manager can allocate IDs and store values, and the VM exposes `OP_LOAD_SPILL`/`OP_STORE_SPILL`, but the pressure detection hook still returns `false` so no automatic spilling occurs yet.
- **Dual-mode register allocation is wired in.** The compiler’s dual allocator prefers typed banks for arithmetic hot paths and falls back to the legacy logical register allocator when the typed banks cannot satisfy a request, keeping both tracking systems coherent during the transition.

### Representative Structures

```c
// include/vm/vm.h
typedef struct CallFrame {
    Value registers[FRAME_REGISTERS];
    Value temps[TEMP_REGISTERS];
    TypedRegisterWindow* typed_window;
    TypedRegisterWindow* previous_typed_window;
    // ... frame metadata elided ...
} CallFrame;

// src/vm/register_file.c
CallFrame* allocate_frame(RegisterFile* rf) {
    // Binds a fresh typed window, syncs shared globals/modules,
    // and points vm.typed_regs.* views at the new window.
}

void deallocate_frame(RegisterFile* rf) {
    // Restores the parent window and returns the frame to the free list.
}
```

## Gaps vs. Roadmap Targets

### Spilling automation
> **In progress:**
> - [ ] Implement pressure heuristics in [`register_file_needs_spilling()`](../src/vm/register_file.c) so the VM can trigger spills automatically.
> - [ ] Replace the stubbed `needs_spilling()` logic in [`src/vm/spill_manager.c`](../src/vm/spill_manager.c) with thresholds and telemetry that surface register pressure back to the compiler.
> - [ ] Teach the compiler backend to reserve spill slots and emit `OP_STORE_SPILL`/`OP_LOAD_SPILL` when pressure crosses the threshold (see [`src/compiler/backend/register_allocator.c`](../src/compiler/backend/register_allocator.c)).

### Persistent typed loops
> **In progress:**
> - [ ] Extend the optimizer to keep loop-invariant operands in stable typed registers across iterations, matching the Phase 2 goals in [`docs/ROADMAP_PERFORMANCE.md`](ROADMAP_PERFORMANCE.md).
> - [ ] Add validation hooks that fail compilation when a loop marked hot would fall back to boxed accessors, ensuring `vm_get_register_safe()` stays out of steady-state paths (touch points in [`include/vm/vm_comparison.h`](../include/vm/vm_comparison.h)).

### Module register orchestration
> **In progress:**
> - [ ] Integrate the module manager with compiler symbol resolution so module-scoped registers are allocated deliberately (see [`src/vm/module_manager.c`](../src/vm/module_manager.c)).
> - [ ] Emit dedicated opcodes or helpers for module register loads/stores to avoid bouncing through the legacy global array (update [`src/vm/dispatch`](../src/vm/dispatch)).

## Implementation Phases

### ✅ Phase 1: Frame Registers
**Status:** Implemented.

- `RegisterFile` now owns distinct banks for globals, frame locals, temporaries, and module slots. Frame entry allocates a dedicated typed window and temp array so local execution is decoupled from the global bank.
- Dispatch handlers such as `OP_LOAD_FRAME`, `OP_STORE_FRAME`, and the fused arithmetic opcodes target these banks directly, enabling loop bodies to stay within the typed arrays.

### ⚠️ Phase 2: Register Spilling
**Status:** In progress.

- Spill data structures (`SpillManager`) and VM opcodes exist, but `register_file_needs_spilling()` and `needs_spilling()` still short-circuit to `false`, so no automatic spill decisions are made.
- Compiler integration is pending—`DualRegisterAllocator` never requests spill slots and the bytecode emitter does not generate `OP_*_SPILL` sequences.
- See the "Spilling automation" task list above for the concrete work required to complete this phase.

### 🚧 Phase 3: Module Registers
**Status:** In progress.

- Module metadata structures and allocation hooks are in place, but the compiler has not yet been taught to populate module banks or emit dedicated module instructions. Runtime loads therefore still bounce through compatibility paths when modules are involved.
- The "Module register orchestration" tasks outline the remaining steps.

### 🔭 Phase 4+: Tiered execution and native backends
**Status:** Planned.

- Profiling, tier-up, and native/JIT execution remain future work as described in the performance roadmap. These phases depend on finishing the typed-loop guarantees and spill automation above before specialization data can safely drive tiered backends.

## Next Checkpoint

Once the tasks flagged as “In progress” above are complete, the VM should satisfy Phase 2 of the performance roadmap: hot loops will keep their operands in typed registers without boxed fallbacks, and spill slots will activate automatically under register pressure. At that point the roadmap can advance to profiling and tier-up infrastructure.
