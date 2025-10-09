# Performance Roadmap: Matching Lua/Go-class Throughput

This roadmap outlines the concrete engineering initiatives required for the current Orus compiler and VM pipeline to reach the throughput of highly optimized interpreters like Lua and approach the efficiency envelope of native backends such as Go. Each phase builds upon the multi-pass architecture described in `docs/COMPILER_DESIGN.md` and assumes the self-hosting stack from `docs/ROADMAP_SELF_HOSTING.md` is in place.

## 1. Make Typed Register Storage Primary
- Decouple the interpreter fast paths from the boxed `Value` array and defer boxing to explicit reconciliation points (control-flow exits, GC barriers, C FFI crossings).
- Split boxed storage into a cold path that only materializes when required, allowing arithmetic and loop bodies to execute purely on typed register arrays.
- Teach typed stores to avoid per-iteration boxing and dirty-bit churn by updating the boxed mirror only when an observer requires it.

## 2. Lock In Typed Loops and Registers During Compilation
- Extend optimizer passes to prove loop-invariant operand types and allocate persistent typed registers for them across loop bodies.
- Update the bytecode emitter to honor these allocations, emitting typed opcodes whose operands remain in typed arrays without falling back to boxed accessors.
- Ensure the register allocator and frame layout preserve typed register lifetimes through loop headers, bodies, and exits.

## 3. Deliver Profiling and Tier-Up Infrastructure
- Implement the profiling hooks from Phase 6 of the roadmap so the runtime can detect hot loops and functions in optimized builds.
- Surface heat information back to the compiler to trigger re-compilation with the typed assumptions established in the optimizer.
- Add a specialization pipeline that recompiles hot paths with aggressive typed fast paths, guarded by deoptimization checks for type assumption failures.

## 4. Introduce a Native or JIT Execution Tier
- Reuse typed AST metadata and specialization data to generate native machine code or JIT-compiled stubs for hot loops/functions.
- Embed guards and deoptimization support so specialized native code can safely fall back to the interpreter when assumptions break.
- Integrate native-tier execution with GC, debugging, and FFI tooling to ensure feature parity with the interpreter while delivering Lua/Go-class throughput.

---

**Milestone Criteria**
- Interpreter fast paths operate exclusively on typed register windows, with boxed reconciliation measured in cold paths only.
- Compiler backend emits stable typed register assignments for hot loops without repeated boxing/unboxing.
- Profiling and tiering infrastructure automatically specializes hot functions in optimized builds.
- A native or JIT backend executes hot code regions, closing the remaining performance gap with Lua and approaching Go's compiled throughput.
