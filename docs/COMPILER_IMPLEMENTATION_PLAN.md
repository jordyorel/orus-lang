# Compiler Implementation Plan

## Current State

### Typed register execution baseline
- Call frames now allocate dedicated typed register windows, keeping the active window in sync with the frame stack so arithmetic executes on the unboxed arrays in `vm.typed_regs` instead of the legacy `Value` array.
- Hot-path helpers such as `vm_store_i32_typed_hot` write directly into the typed window and only touch the boxed array when the store crosses a frame boundary or would invalidate an upvalue, aligning with the Phase 1 goal of running loops on typed storage from the performance roadmap.
- Typed fast-path opcodes and fused immediates (e.g. `OP_ADD_I32_IMM`, `OP_SUB_I32_IMM`, `OP_MUL_I32_IMM`) are wired to these helpers and cache APIs so tight loops stay in typed space by default.

### Boxing fallbacks and reconciliation
- `vm_mark_typed_register_dirty` only skips the boxed write when the register stays in the same numeric bank and no open upvalues require boxing; otherwise it forces an immediate reconciliation so slow paths remain consistent.
- `vm_get_register_safe` lazily rebuilds boxed `Value` slots from dirty typed caches, clearing the dirty flag once the value is synchronized. This is the current reconciliation point for callers that still read through the boxed API.
- Helpers such as `vm_try_read_i32_typed` first consult the typed cache and only fall back to boxed reads when the type tracking was lost, ensuring steady-state loops avoid the fallback entirely.

### Spilling and register windows
- The register file maintains hierarchical banks (globals, frame locals, temps, module window) and attaches typed windows when frames are pushed so cached numeric state survives calls.
- A spill manager and `OP_*_SPILL` opcodes exist, but `register_file_needs_spilling()` simply forwards to a stub that always returns `false`, so spilling currently requires explicit orchestration rather than automatic pressure-based handoff.

### Compiler backend integration
- The dual register allocator initializes typed banks per numeric kind and prefers them for arithmetic hot paths, falling back to standard logical registers only when the typed bank cannot satisfy the request.
- Register frees release either the typed bank slot or the legacy logical register, keeping both tracking systems coherent during the hybrid transition.

## Next Steps
- **Finish Phase 1 exit criteria**: audit remaining VM handlers so hot paths never route through `vm_set_register_safe()` when a typed slot is live, and introduce explicit reconciliation hooks for GC/debugger boundaries rather than opportunistic boxed writes.
- **Promote persistent typed loops (Phase 2)**: extend the optimizer/allocator to reserve stable typed registers across loop bodies and emit validation that fails when a hot loop would trigger a boxed fallback, matching the roadmap milestones.
- **Activate spill heuristics**: implement pressure detection in `register_file_needs_spilling()` and wire the compiler to the spill manager so frames can overflow into the spill arena without manual intervention.
- **Telemetry for typed caches**: add instrumentation around `vm_mark_typed_register_dirty()` and `vm_get_register_safe()` so future phases can prove how often the boxed reconciliation path fires before tiering kicks in.
