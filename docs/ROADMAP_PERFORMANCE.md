# 🧭 Performance Roadmap: Toward Lua/Go-Class Throughput

This roadmap defines the engineering work required for Orus to evolve from a fast bytecode interpreter into a hybrid JIT + AOT system matching Lua-class throughput and approaching Go-class native efficiency.
Each phase is validated by dedicated **C unit tests** ensuring correctness and measurable performance gains.

---

## Phase 1 — Make Typed Register Storage Primary

**Objective:** Eliminate per-iteration boxing so hot code runs purely on typed arrays.

### Implementation Steps

* Decouple interpreter fast paths from the boxed `Value` array and defer boxing to reconciliation points (control-flow exits, GC barriers, or FFI crossings).
* Split boxed storage into a lazily materialized cold path; update GC barriers and debugger hooks to trigger reconciliation explicitly.
* Track dirtiness per register window instead of per write to reduce flag churn.
* Audit VM opcodes so typed operands never invoke boxed accessors during steady-state loops.

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

* Interpreter fast paths operate exclusively on typed register windows.
* All typed-register tests pass with zero GC regressions.

---

## Phase 2 — Lock In Typed Loops and Registers During Compilation

**Objective:** Preserve typed register assignments so loop bodies never fall back to boxed accessors.

### Implementation Steps

* Extend optimizer passes to prove loop-invariant operand types and allocate persistent typed registers across loop bodies.
* Update bytecode emitter to emit typed opcodes directly.
* Extend register allocator to reserve contiguous spans per type, emitting reconciliation stubs only on loop exits.

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

* Typed loops validated to execute without boxed fallback.
* Compiler emits contiguous type-homogeneous register blocks.

---

## Phase 3 — Deliver Profiling and Tier-Up Infrastructure

**Objective:** Detect hot code and feed specialization data back into the compiler automatically.

### Implementation Steps

* Add lightweight sampling probes for loop and function hit counts.
* Feed profiling metadata back to the compiler for re-specialization.
* Generate specialized bytecode guarded by deoptimization stubs.
* Provide `orus-prof` CLI to visualize hotness and specialization status.

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

* Tier-up system triggers specialization after N iterations.
* Profiling data round-trips between runtime and compiler.

---

## Phase 4 — Introduce a Native + JIT Execution Tier

**Objective:** Escape the interpreter ceiling by generating machine code for stabilized hot paths.

### Implementation Steps

* Integrate a portable JIT backend (DynASM).
* Insert **GC safepoints** before each allocation call to cooperate with `collectGarbage()`.
* Implement deoptimization stubs for type-mismatch recovery.
* Keep identical frame and register layouts across interpreter and JIT tiers.

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

* JIT’d code executes hot regions at 3–5× interpreter speed.
* GC operates correctly during JIT execution.

---

## Phase 5 — Add Ahead-of-Time (AOT) Compilation

**Objective:** Deliver self-contained native binaries.

### Implementation Steps

* Emit LLVM IR or Orus IR → native code using the same backend as JIT.
* Link runtime (`liborus_rt.a`) and embed GC metadata.
* Expose `orus build main.orus -o main`.

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

* Compiled binaries run standalone with full GC support.
* Performance within 10–15% of Go.

---

## Phase 6 — Runtime and GC Enhancements

**Objective:** Improve memory efficiency and pause times.

### Implementation Steps

* Convert the existing mark-and-sweep GC into incremental tri-color.
* Add generational pools using existing `freeLists[]`.
* Optimize `markRoots()` for frame-local scanning.
* Profile allocation sites and create region allocators.

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

* GC pauses < 5 ms per 64 MB heap.
* Stable memory footprint under sustained allocation churn.

---

## Phase 7 — SSA Optimizer and Native Parity

**Objective:** Close the remaining gap with Go.

### Implementation Steps

* Add SSA-based passes: constant folding, DCE, copy propagation, inlining.
* Implement escape analysis for stack allocation.
* Benchmark against LuaJIT 2.1 and Go.

### Unit Test — SSA Optimization Validation

```c
TEST_CASE(test_constant_folding) {
    compile("let x = 2 + 3;");
    ASSERT_EMITTED_OP(OP_CONST_5);
}
```

### Milestone Exit Criteria

* Tight numeric loops reach 90%+ of Go speed.
* Mixed workloads 70–90× faster than Python.

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
make test
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

