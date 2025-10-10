# Performance Roadmap: Matching Lua/Go-class Throughput

This roadmap outlines the concrete engineering initiatives required for the current Orus compiler and VM pipeline to reach the throughput of highly optimized interpreters like Lua and approach the efficiency envelope of native backends such as Go. Each phase builds upon the multi-pass architecture described in `docs/COMPILER_DESIGN.md` and assumes the self-hosting stack from `docs/ROADMAP_SELF_HOSTING.md` is in place.

## Phase 1 — Make Typed Register Storage Primary
**Objective:** Eliminate per-iteration boxing so hot code runs purely on typed arrays.

### Implementation Steps
- Decouple interpreter fast paths from the boxed `Value` array and defer boxing to explicit reconciliation points (control-flow exits, GC barriers, C FFI crossings).
- Split boxed storage into a cold path that materializes lazily; update GC barriers and debugger/FFI hooks to request reconciliation explicitly.
- Track dirtiness per register window instead of per write so typed stores avoid redundant flag churn.
- Audit VM opcodes to ensure typed operands never call the boxed accessors during the steady state of a loop.

### Zero-Cost Abstractions
Expose typed register windows through inline-only helpers so the hot path compiles down to raw loads/stores.

```c
typedef struct {
    int32_t* i32;    // points into the typed register arena
    int64_t* i64;
    double*  f64;
    uint8_t* dirty_bitmap; // lazily consulted by slow paths only
} TypedRegisterWindow;

inline static TypedRegisterWindow
typed_window_acquire(struct VMState* vm, RegisterSpan span);

inline static void
typed_window_release(struct VMState* vm, TypedRegisterWindow* window);

inline static int32_t
typed_read_i32(const TypedRegisterWindow* window, RegisterIndex reg);

inline static void
typed_write_i32(TypedRegisterWindow* window, RegisterIndex reg, int32_t value);
```

### Milestone Exit Criteria
- Interpreter fast paths operate exclusively on typed register windows, with boxed reconciliation measured in cold paths only.
- Boxing of typed registers occurs only through the explicit reconciliation API.

## Phase 2 — Lock In Typed Loops and Registers During Compilation
**Objective:** Preserve typed register assignments so loop bodies never fall back to boxed accessors.

### Implementation Steps
- Extend optimizer passes to prove loop-invariant operand types and allocate persistent typed registers for them across loop bodies.
- Update the bytecode emitter to honor these allocations, emitting typed opcodes whose operands remain in typed arrays without falling back to boxed accessors.
- Teach the register allocator to reserve contiguous spans per type to avoid cross-type eviction and to emit reconciliation stubs on loop exits only when required.
- Instrument the compiler with validation asserts that fail builds when a typed loop would require boxed fallback during steady-state execution.

### Zero-Cost Abstractions
Provide typed register planning helpers used by both optimizer and emitter, avoiding redundant scans of the typed AST.

```c
typedef struct {
    RegisterIndex reg;
    const Type*   type;
    LoopId        loop_id;
} TypedLoopBinding;

inline static TypedLoopBinding
plan_typed_loop_binding(CompilerContext* ctx, const TypedASTNode* node);

inline static void
materialize_typed_binding(BytecodeEmitter* emit, const TypedLoopBinding* binding);
```

### Milestone Exit Criteria
- Compiler backend emits stable typed register assignments for hot loops without repeated boxing/unboxing.
- Typed loop validation tooling reports zero fallback violations in optimized builds.

## Phase 3 — Deliver Profiling and Tier-Up Infrastructure
**Objective:** Detect hot code and feed specialization data back into the compiler automatically.

### Implementation Steps
- Implement the profiling hooks from Phase 6 of the existing roadmap so the runtime can detect hot loops and functions in optimized builds.
- Surface heat information back to the compiler via a tiering channel; include loop headers, observed types, and iteration counts.
- Add a specialization pipeline that recompiles hot paths with aggressive typed fast paths, guarded by deoptimization checks for type assumption failures.
- Build tooling (`orus-prof`) to visualize tiering decisions and confirm that specialized bytecode replaces interpreter-only variants.

### Zero-Cost Abstractions
Keep profiling state out of hot loops by writing lightweight sampling probes and metadata structures.

```c
typedef struct {
    FunctionId func;
    LoopId     loop;
    uint64_t   hit_count;
    uint64_t   last_hot_cycle;
} HotPathSample;

inline static bool
vm_profile_tick(struct VMState* vm, FunctionId func, LoopId loop);

inline static void
queue_tier_up(struct CompilerService* svc, const HotPathSample* sample);
```

### Milestone Exit Criteria
- Profiling and tiering infrastructure automatically specializes hot functions in optimized builds.
- Tier-up pipeline emits specialized bytecode within one scheduling quantum after a loop crosses the hotness threshold.

## Phase 4 — Introduce a Native or JIT Execution Tier
**Objective:** Escape the interpreter ceiling by generating machine code for stabilized hot paths.

### Implementation Steps
- Reuse typed AST metadata and specialization data to generate native machine code or JIT-compiled stubs for hot loops/functions.
- Embed guards and deoptimization support so specialized native code can safely fall back to the interpreter when assumptions break.
- Integrate native-tier execution with GC, debugging, and FFI tooling to ensure feature parity with the interpreter while delivering Lua/Go-class throughput.
- Establish cross-platform codegen backends (x86-64, ARM64, RISC-V) and shared runtime services (code cache, relocation, safepoints).

### Zero-Cost Abstractions
Define the interface between the interpreter and native tier to avoid redundant state translation.

```c
typedef struct {
    void (*enter)(struct VMState* vm, const JITEntry* entry);
    void (*invalidate)(struct VMState* vm, const DeoptTrigger* trigger);
    void (*flush)(struct VMState* vm);
} JITBackendVTable;

inline static JITEntry*
lookup_jit_entry(struct VMState* vm, FunctionId func, LoopId loop);

inline static void
install_jit_entry(struct VMState* vm, const JITEntryDescriptor* desc);
```

### Milestone Exit Criteria
- A native or JIT backend executes hot code regions, closing the remaining performance gap with Lua and approaching Go's compiled throughput.
- Native tier shares the same register windows and frame layout as the interpreter, avoiding state marshalling overhead.

---

**Global Success Metrics**
- End-to-end benchmarks show hot loops running without interpreter dispatch in specialized tiers.
- Debugger, profiler, and FFI continue to operate across interpreter and native tiers with unified register semantics.
- Performance telemetry demonstrates sustained Lua-class throughput and measurable progress toward Go-class execution speeds on representative workloads.
