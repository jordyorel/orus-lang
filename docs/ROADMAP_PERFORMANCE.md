# 🧭 Performance Roadmap: Toward Lua/Go-Class Throughput

This roadmap defines the engineering work required for Orus to evolve from a fast bytecode interpreter into a hybrid JIT + AOT system matching Lua-class throughput and approaching Go-class native efficiency.
Each phase is validated by dedicated **C unit tests** ensuring correctness and measurable performance gains.

---

## Phase 1 — Make Typed Register Storage Primary

**Objective:** Eliminate per-iteration boxing so hot code runs purely on typed arrays.

### Implementation Steps

[x] Decouple interpreter fast paths from the boxed `Value` array and defer boxing to reconciliation points (control-flow exits, GC barriers, or FFI crossings).
    - `vm_get_register_lazy()` now returns typed snapshots without mutating `vm.registers`, keeping the legacy boxed mirror untouched until `vm_get_register_safe()` or `register_file_reconcile_active_window()` flush the window.
[x] Split boxed storage into a lazily materialized cold path; update GC barriers and debugger hooks to trigger reconciliation explicitly.
    - Boxed register mirrors only materialize when heap values are cached, and `collectGarbage()` plus debugging traces force
      `register_file_reconcile_active_window()` so the legacy `Value` array stays coherent at safepoints.
[x] Track dirtiness per register window instead of per write to reduce flag churn.
    - `TypedRegisterWindow` now maintains a `dirty_mask` bitset mirrored in `TypedRegisters`,
      letting the VM mark or clear dirty spans with a single word update while keeping the
      existing per-slot booleans for diagnostics.
[x] Audit VM opcodes so typed operands never invoke boxed accessors during steady-state loops.
    - Introduced `vm_read_i32_hot()` so comparison and fused arithmetic opcodes reload typed caches
      before touching boxed mirrors, ensuring fallback reads only occur when metadata is missing.

### Code Template — Typed Register Window

```c
typedef struct {
    int32_t* i32;
    int64_t* i64;
    double*  f64;
    uint8_t* dirty_bitmap;
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

### Unit Test — Typed Register Operations

```c
TEST_CASE(test_typed_register_i32) {
    VMState vm = {0};
    TypedRegisterWindow win = typed_window_acquire(&vm, REG_SPAN_TEMP);
    typed_write_i32(&win, 0, 42);
    ASSERT_EQ(typed_read_i32(&win, 0), 42);
    typed_window_release(&vm, &win);
}
```

### Milestone Exit Criteria

[x] Interpreter fast paths operate exclusively on typed register windows.
[x] All typed-register tests pass with zero GC regressions (validated by the typed register coherence suite).

---

## Phase 2 — Lock In Typed Loops and Registers During Compilation

**Objective:** Preserve typed register assignments so loop bodies never fall back to boxed accessors.

### Implementation Steps

[x] Extend optimizer passes to prove loop-invariant operand types and allocate persistent typed registers across loop bodies.
    - `loop_type_residency_pass` now walks range and while loops, proving operand types stay stable by
      tracking identifier mutations and rejecting hints unless reassignments preserve the resolved type.
    - Residency plans only materialize when operands remain type-invariant, allowing the register allocator
      to pin guard and range operands in typed registers through each iteration without risking type drift.
[x] Update bytecode emitter to emit typed opcodes directly.
[x] Extend register allocator to reserve contiguous spans per type, emitting reconciliation stubs only on loop exits.
    - Added typed-span reservation APIs to the dual allocator so contiguous windows can be reserved per bank and
      reclaimed with explicit reconciliation metadata captured for loop exits.
    - Pending reconciliation spans are now queued for deferred emission, ensuring boxed mirrors synchronize once per
      loop rather than on every iteration.

### Code Template — Typed Loop Planning

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

### Unit Test — Loop Register Persistence

```c
TEST_CASE(test_typed_loop_persistence) {
    compile_and_run("for i in range(0, 10): x = i * 2");
    ASSERT_NO_BOX_FALLBACK();
}
```

### Milestone Exit Criteria

[x] Typed loops validated to execute without boxed fallback.
    - Residency analysis plans now flow through codegen, and the `tests/types` and
      optimizer-driven loop suites execute without triggering boxed reconciliation
      within steady-state iterations.
[x] Compiler emits contiguous type-homogeneous register blocks.
    - The dual allocator's typed span reservations hand out aligned windows per type,
      with unit coverage ensuring reconciliation stubs appear only at loop exits.

---

## Phase 3 — Deliver Profiling and Tier-Up Infrastructure

**Objective:** Detect hot code and feed specialization data back into the compiler automatically.

### Implementation Steps
[x] Add lightweight sampling probes for loop and function hit counts.
[x] Feed profiling metadata back to the compiler for re-specialization.
    - The specialization feedback bridge harvests VM samples and hands them to
      the compiler via `specialization_feedback.c`, letting phase 2 artifacts
      trigger tier-up recompiles automatically.
[x] Generate specialized bytecode guarded by deoptimization stubs.
    - The backend now clones hot functions, emits guard checks, and wires
      deoptimization stubs so the interpreter can recover when profiles drift.
[x] Provide `orus-prof` CLI to visualize hotness and specialization status.
    - Shipped the standalone `orus-prof` binary that ingests exported profiling
      JSON, surfaces the hottest opcodes, and flags which functions are ready
      for specialization promotion.
[x] Profiling & Tier-Up
    - Instruction & Loop Counters

        ```c 
        vm->ticks++;
        if (++loop->hit_count == HOT_THRESHOLD)
            queue_tier_up(&vm->compiler, func_id, loop_id);
        ```
    - Hot Path Sample Struct

        ```c
        typedef struct {
            FunctionId func;
            LoopId     loop;
            uint64_t   hit_count;
        } HotPathSample;
        ```
    - Profiling Hook

        ```c
        inline static bool
        vm_profile_tick(VMState* vm, FunctionId f, LoopId l) {
            HotPathSample* s = &vm->profile[l];
            if (++s->hit_count == HOT_THRESHOLD) {
                queue_tier_up(vm, s);
                return true;
            }
            return false;
        }
        ```

    - Tier-Up Stub (future JIT entrypoint)
        ```c
        void queue_tier_up(VMState* vm, const HotPathSample* s) {
            /* placeholder for Phase 4 JIT */
        }
        ```

    - GC Safepoint in Profiler
        ```c
        #define PROF_SAFEPOINT(vm) \
            if ((vm)->bytesAllocated > gcThreshold) collectGarbage();
        ```

    - Minimal Unit Test
        ```c
        TEST_CASE(test_hot_loop_detection) {
            run_hot_loop(10000);
            ASSERT_TRUE(vm_profile_tick(&vm, FUNC_MAIN, LOOP_0));
        }
        ```
### Code Template — Runtime Hot Path Sampling

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

### Unit Test — Profiling Hooks

```c
TEST_CASE(test_hot_loop_detection) {
    run_program_with_profiling("loop_hot.orus");
    ASSERT_TRUE(vm_profile_tick(&vm, FUNC_MAIN, LOOP_0));
}
```

### Milestone Exit Criteria

[x] Tier-up system triggers specialization after N iterations.
    - Runtime tier metadata now promotes hot functions once sampling crosses
      the specialization threshold and queues recompilation jobs.
[x] Profiling data round-trips between runtime and compiler.
    - Collected loop/function counters are exported to the compiler service
      during tier-up, enabling it to regenerate bytecode with guard metadata.

### Follow-Up Work

- [x] Ship the `orus-prof` CLI to visualize sampling hotness and tier status.
- [x] Add regression coverage for guard selection, deoptimization stubs, and
      cold-start recompilation paths once the testing harness lands.
    - Test harness now exercises guard selection regressions, ensures
      deoptimization stubs remain intact, and validates cold-start
      recompilation flows.

---

## Phase 4 — Introduce a Native + JIT(OrusJit) Execution Tier

**Objective:** Escape the interpreter ceiling by generating machine code for stabilized hot paths.

### Implementation Steps

[x] Integrate a portable JIT backend (DynASM).
    - Vendored DynASM 1.3.0 (MIT) into `third_party/dynasm/` and exposed
      an `OrusJitBackend` abstraction that emits executable stubs through the
      DynASM encoder. The VM now initializes the backend during `initVM()` and
      caches a native entry stub to validate executable code generation.
    - Added an AArch64 path that emits the same return stub using a direct
      encoding and macOS `MAP_JIT` write toggles so Apple Silicon builds share
      the native tier bootstrap story with x86-64.
[x] Insert **GC safepoints** before each allocation call to cooperate with `collectGarbage()`.
    - Added a centralized `gc_safepoint()` that saturates projected heap
      growth, invokes `collectGarbage()` when thresholds are exceeded, and
      recomputes `gcThreshold` with a minimum 1 MiB guard band.
    - Routed `reallocate()`, `allocateObject()`, and raw string copies through
      the safepoint helper so every GC-managed allocation cooperates with
      register reconciliation and sweeping.
[x] Implement deoptimization stubs for type-mismatch recovery.
    - Runtime propagates type errors through a deoptimization shim that
      clears profiled parameter caches, invokes the specialization handler,
      and falls back to baseline bytecode after a mismatch.
[x] Keep identical frame and register layouts across interpreter and JIT tiers.
    - Published `vm/jit_layout.h` with shared offsets and compile-time
      invariants so native entry stubs can assume the same frame/register
      layout as the interpreter. Divergences now fail the build instead of
      silently corrupting state during tier transitions.
[x] Implement OrusJit IR → DynASM codegen pipeline.
    - Introduced a minimal `OrusJitIRProgram` to capture stub logic in an
      architecture-neutral format before hitting DynASM.
    - Generated DynASM action lists for each IR opcode on x86-64 (via
      `DASM_ESC` byte streams) and mirrored the same return stub using direct
      encodings on AArch64 so Apple Silicon stays in sync with x86 builds.
[x] Maintain JITEntry cache with invalidation and reuse.
    - Added a VM-owned cache of `JITEntry` records keyed by function/loop IDs,
      recycling executable buffers when recompiling hot paths and tracking a
      generation counter so invalidation can surgically retire stale entries.
    - Wired the backend vtable’s `invalidate`/`flush` hooks to purge the cache
      and taught VM teardown to release every cached allocation before the
      backend shuts down.
[x] Support deoptimization fallback to interpreter.
    - `vm_default_deopt_stub()` now retargets the currently executing
      specialized frame to the baseline bytecode, preserving the instruction
      offset so a bailout resumes inside the interpreter without replaying the
      call from the top.
    - Tiering demotions continue to log the transition, making it clear when
      runtime profiling has forced execution back to the baseline tier.

# 🧩 Future Enhancement — Loop Tracing and Inline Caching
[] Extend `queue_tier_up()` to record and linearize hot bytecode traces (**trace-based JIT**).  
[] Implement **trace guards** for type stability; fall back to interpreter on deopt.  
[] Add **inline caches (IC)** for property/method lookups and arithmetic ops, patching machine code directly on type changes.  
[] Merge stable traces into a **trace tree** for loop variants, feeding into the SSA optimizer (Phase 8).  
[] Expected gain: **~4–10× speedup** on hot arithmetic loops and repeated object paths.  



### Code Template — JIT Backend Interface

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

### Code Template — GC Safepoint in JIT

```c
#define GC_SAFEPOINT(vm) do {                  \
    if ((vm)->bytesAllocated > gcThreshold) {  \
        collectGarbage();                      \
    }                                          \
} while (0)
```

### Unit Test — JIT and GC Safety

```c
TEST_CASE(test_jit_gc_safepoint) {
    enable_jit();
    run_gc_intensive_hotloop();
    ASSERT_NO_MEMORY_CORRUPTION();
}
```

### Milestone Exit Criteria

[ ] JIT’d code executes hot regions at 3–5× interpreter speed.
[ ] GC operates correctly during JIT execution.

### Remaining Work to Hit the Exit Criteria

1. **Quantify hot loop uplift against the interpreter**
   - [x] Lock in a stable benchmark corpus covering numeric loops, mixed object access, and FFI churn candidates. See `docs/JIT_HOT_LOOP_CORPUS.md` for the canonical list.
   - [x] Record interpreter-only baselines for numeric loop and mixed object workloads (`optimized_loop`, `typed_fastpath`, and `string_concat`). Interpreter runtimes are now checked into the corpus doc and exported via `scripts/measure_hot_loop_baselines.py`.
   - [x] Capture the FFI churn baseline once `tests/benchmarks/ffi_ping_pong_benchmark.orus` lands.
     - Baselines now live in `JIT_BENCHMARK_RESULTS.md`; the interpreter path runs at 2.29 s while the JIT attempt remains in the interpreter after bailing on opcode 0, so the log documents both the measurement and the missing lowering.
   - [x] Enable tier-up in the harness so the JIT runs long enough to amortize translation and cache warming costs.
     - The harness now drives warmup + steady-state sampling, recording multiple tiered invocations per loop before comparing against the interpreter averages.
   - [x] Instrument the `JITEntry` cache with cycle counters around `enter()` to capture steady-state throughput and regress if the 3–5× goal is missed.
     - `zig build jit-benchmark -Dprofile=release [-Dstrict-jit=true]` surfaces per-entry warmup and steady-state latency along with sample counts, letting us track cache reuse directly in the CLI output.
   - [x] Capture regression tests under `zig build jit-benchmark -Dprofile=release -Dstrict-jit=true` so the uplift target is automatically enforced in CI.
     - The target now executes both optimized-loop and FFI workloads under JIT + interpreter configurations, emitting structured metrics for CI diffing.

   - [x] Extend lowering coverage so boxed values no longer trigger `unsupported_value_kind` bailouts during optimized loop tier-up.
     - Mixed-value loop kernels now route boxed temporaries through the shared helper-backed IR so tier-up emits native branches
       without tripping the bailout guard. The profiler logs no longer record failed translations on boxed loop counters, keeping
       optimized loops resident in the JIT tier. The translator now preserves boxed metadata when only one side of the loop guard
       is typed, ensuring the helper-backed lowering is selected and validated by the mixed-counter regression test.
   - [x] Teach the translator to handle opcode 0 (`OP_LOAD_CONST`) inside the FFI ping/pong harness so the native tier can materialize constants before host calls.
     - Boxed constants now translate to a shared `LOAD_VALUE_CONST` IR opcode that funnels through a helper capable of materializing any constant pool entry. The x86-64 and AArch64 backends lower the opcode by invoking `orus_jit_native_load_value_const`, which validates the expected kind, warms the typed caches, and reconciles boxed registers before dispatching to FFI helpers. The baseline tier no longer bails on the first constant load, unblocking native execution of the FFI ping/pong benchmark.

2. **Finish JIT-side GC cooperation**
   - [x] Audit DynASM emission to ensure every allocation, write barrier, and safepoint macro expands to `GC_SAFEPOINT(vm)` before returning to Orus code.
     - All JIT helpers now funnel through `orus_jit_helper_safepoint()` so translated DynASM paths, boxed allocations, write barriers, and native call returns consistently invoke `GC_SAFEPOINT(vm)` before resuming Orus execution.
     - A new baseline regression drives `CALL_NATIVE` through the helper-backed IR and asserts the shared safepoint counter increments after native dispatch, preventing host hooks from bypassing GC enforcement.
   - [x] Add stress tests that trigger tiered code during heap growth (`run_gc_intensive_hotloop()`), verifying reconciliation of typed registers during collections.
     - The baseline backend suite now drives `run_gc_intensive_hotloop()` inside `tests/unit/test_vm_jit_backend.c`, forcing a safepoint while typed arithmetic values remain live.
     - The stress harness spikes `bytesAllocated` past a reduced `gcThreshold`, asserts `vm.gcCount` increments, and confirms accumulator registers still contain the expected post-loop totals after the collector reconciles boxed mirrors.
  - [x] Exercise deoptimization paths mid-GC to guarantee interpreter + JIT frame layouts remain in sync when the collector walks the stack.
    - `tests/unit/test_vm_jit_backend.c` now drives a bool constant through a safepoint immediately before a type guard bailout, forcing `vm_default_deopt_stub()` to rebind the active frame while GC reconciliation is in flight. The regression asserts boxed and typed mirrors stay identical, the pending invalidate trigger is recorded, and the specialized function drops back to baseline execution without drifting interpreter state.
   - Extend the GC telemetry dashboard to flag missed safepoints or reconciliation drift while native frames are active.

---

## Phase 5 — Lightweight Concurrency (Oroutines)

**Objective:** Enable thousands of concurrent Orus routines with minimal memory and safe GC integration.

### Implementation Steps

* [] Add **scheduler loop** to `vm_run()` handling `oroutine_yield()` and `oroutine_resume()`.
* [] Use **2–4 KB stacks** allocated from a shared arena (`oroutine_alloc_stack()`).
* [] Integrate **GC safepoints** at yield boundaries and allocation calls.
* [] Support **blocking I/O suspension** via continuation capture.
* [] Maintain identical frame layout between VM, JIT, and oroutines.

### Code Template — Scheduler Hooks

```c
typedef struct {
    void* stack_base;
    void* stack_top;
    bool  ready;
} Oroutine;

void oroutine_yield(VMState* vm);
void oroutine_resume(VMState* vm, Oroutine* coro);
```

### GC Integration

```c
#define ORUTINE_SAFEPOINT(vm) do { \
    if ((vm)->bytesAllocated > gcThreshold) collectGarbage(); \
} while (0)
```

### Unit Test — Concurrent Execution

```c
TEST_CASE(test_oroutine_spawn_yield) {
    oroutine_spawn(task_a);
    oroutine_spawn(task_b);
    run_scheduler();
    ASSERT_ALL_COMPLETED();
}
```

### Milestone Exit Criteria

* [] 10 000 oroutines execute concurrently under 100 MB total memory.
* [] GC pause < 5 ms per 64 MB heap.
* [] Works transparently with JIT-compiled frames.

---

## Phase 6 — Add Ahead-of-Time (AOT) Compilation

**Objective:** Deliver self-contained native binaries.

### Implementation Steps

[] Orus IR → native code using the same backend as JIT.
[] Link runtime (`liborus_rt.a`) and embed GC metadata.
[] Expose `orus main.orus -o main`.

### Code Template — AOT Entrypoint

```c
int main(int argc, char** argv) {
    VMState vm;
    initVM(&vm);
    run_orus_main(&vm, "main.orus");
    freeVM(&vm);
    return 0;
}
```

### Unit Test — AOT Build Validation

```c
TEST_CASE(test_aot_binary_executes) {
    system("orus build test.orus -o test_bin");
    ASSERT_EQ(system("./test_bin"), 0);
}
```

### Milestone Exit Criteria

[] Compiled binaries run standalone with full GC support.
[] Performance within 10–15% of Go.

### Phase 6 Updates — Native Tier Resilience

- Established executable-heap W^X enforcement with tracked allocation regions and added native-frame canaries so the runtime aborts immediately if helper stubs or stack metadata are corrupted.
- Introduced `test_vm_jit_stress` to run long-duration arithmetic kernels, GC-heavy string churn, and multi-process JIT invocations, ensuring the new safeguards hold under sustained and concurrent load.

---

## Phase 7 — Runtime and GC Enhancements

**Objective:** Improve memory efficiency and pause times.

### Implementation Steps

[] Convert the existing mark-and-sweep GC into incremental tri-color.
[] Add generational pools using existing `freeLists[]`.
[] Optimize `markRoots()` for frame-local scanning.
[] Profile allocation sites and create region allocators.

### Code Template — Incremental GC Step

```c
void gc_step(VMState* vm) {
    if (vm->gcPaused) return;
    markRoots();
    sweep();
    gcThreshold = vm->bytesAllocated * GC_HEAP_GROW_FACTOR;
}
```

### Unit Test — GC Stress

```c
TEST_CASE(test_incremental_gc) {
    run_gc_stress();
    ASSERT_NO_LEAKS();
    ASSERT_PAUSE_TIME_BELOW(5); // ms
}
```

### Milestone Exit Criteria

[] GC pauses < 5 ms per 64 MB heap.
[] Stable memory footprint under sustained allocation churn.

---

## Phase 8 — SSA Optimizer and Native Parity

**Objective:** Close the remaining gap with Go.

### Implementation Steps

[] Add SSA-based passes: constant folding, DCE, copy propagation, inlining.
[] Implement escape analysis for stack allocation.
[] Benchmark against LuaJIT 2.1 and Go.

### Unit Test — SSA Optimization Validation

```c
TEST_CASE(test_constant_folding) {
    compile("let x = 2 + 3;");
    ASSERT_EMITTED_OP(OP_CONST_5);
}
```

### Milestone Exit Criteria

[] Tight numeric loops reach 90%+ of Go speed.
[] Mixed workloads 70–90× faster than Python.

---

## 🧪 Global Testing Framework

All phases must pass incremental unit tests using a minimal internal test harness:

```c
#define TEST_CASE(name) static void name(void)
#define ASSERT_EQ(a, b) if ((a) != (b)) fail(__FILE__, __LINE__, #a, #b)
#define ASSERT_TRUE(x)  ASSERT_EQ((x), true)
#define RUN_ALL_TESTS() \
    for (TestFn* t = tests; t < tests_end; ++t) (*t)();
```

Run via:

```bash
zig build test
```

---

## ✅ Success Metrics

| Tier               | Goal                  | Speed Target        |
| ------------------ | --------------------- | ------------------- |
| Interpreter (goto) | Beat Lua 5.4          | ×1.5–×2 vs baseline |
| JIT Tier           | LuaJIT-level hot path | 70–80% of native    |
| AOT Tier           | Go-class binaries     | 85–95% of native    |
| GC Pause           | Incremental mode      | <5 ms per 64 MB     |
| SSA Tier           | Constant-folded loops | 90–100% of Go speed |

---
