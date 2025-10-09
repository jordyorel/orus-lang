# Orus Standard Library Self-Hosting Assessment

## Executive Summary
- Orus resolves `.orus` modules relative to the importer directory, the executable's directory, and any `ORUSPATH` entries while caching results for reuse—enough plumbing to distribute future self-hosted modules.【F:src/vm/runtime/vm.c†L1498-L1667】
- The shipped standard library tree is currently empty by design; `std/` only carries a placeholder README while the new self-hosted modules are authored.【F:std/README.md†L1-L4】
- The runtime currently relies on built-in C helpers for essentials such as printing, array mutation, and byte buffer management, so Orus code cannot yet reimplement those services on its own.【F:src/vm/runtime/builtin_print.c†L1-L154】【F:src/vm/runtime/builtin_array_push.c†L1-L18】【F:src/vm/core/vm_memory.c†L155-L214】

**Conclusion:** The current Orus implementation cannot self-host a full standard library written purely in Orus. Critical subsystems still rely on native helpers, and several expected modules (filesystem, random, collections utilities) are missing entirely. Delivering the self-hosted std system requires significant runtime work before Orus code can stand alone.

## Current Capabilities

### Module Loading and Resolution
The runtime normalizes module names, ensures they end with `.orus`, and searches the importer directory, the executable directory, and the paths listed in `ORUSPATH`. Resolved locations are cached so later loads reuse the same path. This is sufficient infrastructure to deliver an Orus-written standard library when it exists.【F:src/vm/runtime/vm.c†L1498-L1667】

### Existing Standard Library Surface
No Orus modules currently live under `std/`; the directory documents its placeholder state via `std/README.md`, and the VM only exposes built-in operations provided by the runtime itself.【F:std/README.md†L1-L4】【F:src/vm/runtime/vm.c†L1544-L1667】

## Feature-by-Feature Readiness

| Area | What Exists Today | Blocking Issues for Pure Orus Implementation |
| --- | --- | --- |
| Math | Arithmetic and comparisons are implemented as VM opcodes and C helpers; there is no Orus-level module. | Replacements must provide numeric algorithms in Orus or expose new VM primitives without depending on native helpers. |【F:src/vm/operations/vm_arithmetic.c†L1-L201】
| Bytes / Encoding | The byte buffer type and conversions are implemented by C helpers inside the runtime. | Orus code still lacks memory primitives, so a future bytes module needs kernel support before it can be implemented. |【F:src/vm/core/vm_memory.c†L155-L214】
| Filesystem | No Orus-facing filesystem API ships today, and the repo intentionally leaves `std/` empty. | Even if a wrapper were written, it would still depend on new VM syscalls or helpers that do not yet exist. |【F:std/README.md†L1-L4】
| Random Numbers | There is no RNG module and no runtime API for entropy or deterministic generators. | A real RNG requires VM support (syscalls, entropy, or opcodes). Without it, Orus code cannot supply randomness. |【F:src/vm/runtime/vm.c†L1544-L1667】
| I/O | `print` compiles to dedicated opcodes handled by `builtin_print` in C; other IO helpers are absent. | Plain Orus code cannot interact with stdout/stderr without those opcodes, so a self-hosted IO module would still depend on the runtime implementation. |【F:src/compiler/backend/codegen/statements.c†L1738-L1804】【F:src/vm/runtime/builtin_print.c†L1-L154】
| Collections / Vectors | Arrays are VM objects with push/pop/repeat implemented natively. | Advanced data structures cannot manage memory directly and must call the existing C builtins (`builtin_array_push`, etc.). |【F:src/vm/runtime/builtin_array_push.c†L1-L18】【F:src/vm/runtime/builtin_array_pop.c†L1-L18】【F:src/vm/runtime/builtin_array_repeat.c†L1-L77】

## Missing Infrastructure for Self-Hosting

1. **Orus-level system interface:** There is no mechanism for Orus code to perform file IO, randomness, or process interaction without new runtime primitives.【F:src/vm/runtime/vm.c†L1544-L1667】
2. **Memory management primitives:** Byte buffers, arrays, and strings are all managed by VM helpers. Orus has no language constructs for pointer arithmetic or manual allocation required to reimplement them.【F:src/vm/core/vm_memory.c†L155-L214】【F:src/vm/runtime/builtin_array_push.c†L1-L18】
3. **Bootstrap strategy:** The VM currently relies on built-in helpers and does not load Orus-written shims during startup, so the new standard library will need a clear initialization hook.【F:src/vm/runtime/vm.c†L1544-L1667】

## Roadmap to Enable Self-Hosting

To make a self-hosted standard library possible, the project would need to:

1. **Define stable low-level opcodes or syscalls** that expose filesystem, randomness, networking, and clock access as primitives the Orus language can call without C wrappers.
2. **Extend the language with safe memory APIs** (e.g., mutable byte buffers, allocator handles) so that Orus code can implement collections and encoders instead of delegating to runtime helpers.
3. **Provide bootstrapping hooks** allowing the VM to load initial Orus modules before user code runs, supplying print, error reporting, and container utilities written in Orus but backed by the new primitives.
4. **Gradually transition runtime helpers** by introducing kernel primitives, rewriting each helper in Orus, and removing the corresponding C implementation once parity and performance are validated.
Until these runtime capabilities exist, any attempt to deliver a full standard library would still be forced to rely on the current C helpers and opcodes, defeating the goal of a fully self-hosted library.

## Recommendation
Focus on expanding runtime primitives first (filesystem, randomness, IO streams, allocation) and then author Orus wrappers on top. Only after the VM can provide these services without hard-coded C helpers will it be realistic to rewrite the standard library entirely in Orus.
