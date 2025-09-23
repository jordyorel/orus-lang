# Orus Agent Development Guidelines

## 🎯 **Mission Statement**

As an AI agent working on the Orus programming language, you MUST follow these strict guidelines to ensure every contribution meets the highest standards of performance and engineering excellence. **NO COMPROMISES** on performance or code quality are acceptable.

### **🚨 FIRST: Always Consult Documentation**
Before implementing ANYTHING, you MUST:
1. **Read `docs/LANGUAGE.md`** - Understand the canonical Orus language specification
2. **Check `docs/MISSING.md`** - See what features need implementation and current progress  
3. **Study `docs/IMPLEMENTATION_GUIDE.md`** - Learn the high-performance patterns and best practices
4. **Update roadmap** - Mark progress and document new patterns after implementation

**NEVER implement features without first understanding the existing architecture and roadmap!**

### **🔥 CRITICAL REMINDER: Multi-Pass Compiler Architecture (CURRENT PLAN)**
**Orus follows the multi-pass compiler blueprint captured in `docs/COMPILER_DESIGN.md`:**

#### **✅ VALIDATED COMPONENTS**
- ✅ **Lexer** - Complete tokenization with 105+ token types and type suffixes
- ✅ **Parser** - Precedence-climbing parser with comprehensive AST (45+ node types)
- ✅ **HM Type System** - Phase 5 complete with typed AST output and casting rules
- ✅ **Typed AST Visualizer** - `typed_ast_visualizer.c` for post-pass inspection
- ✅ **VM** - 256-register architecture with 135+ optimized opcodes

#### **❌ ACTIVE IMPLEMENTATION AREA: COMPILER PIPELINE**
- ❌ **Compiler Pipeline**: Multi-pass transformation from typed AST to bytecode (optimization → codegen)

#### **🎯 COMPILER PIPELINE PRINCIPLES**
- ✅ **Typed AST as Source of Truth**: HM inference annotates every node before backend work begins
- ✅ **Pass-Oriented Architecture**: Distinct passes handle typed AST optimization and bytecode emission
- ✅ **Register-Aware Codegen**: Final pass aligns with the 256-register VM layout and calling conventions
- ✅ **Visualization Hooks**: `typed_ast_visualizer.c` runs after HM inference and optimization passes
- ✅ **Adaptive Backends**: FAST, OPTIMIZED, and HYBRID modes share the same frontend analysis but vary backend cost

#### **🎯 Register Allocation Strategy**
```c
// Register allocation for 256-register VM
typedef enum {
    REGISTER_GLOBAL   = 0,   // R0-R63:   Global variables (64 registers)
    REGISTER_LOCAL    = 64,  // R64-R191: Function locals and parameters (128 registers)  
    REGISTER_TEMP     = 192, // R192-R239: Temporary expressions (48 registers)
    REGISTER_MODULE   = 240  // R240-R255: Module imports/exports (16 registers)
} RegisterRange;

// Example codegen output (final pass): x = 42 + y
LOAD_CONST_I32 R192, 0    // Load 42 into temp register
LOAD_LOCAL R193, y_slot   // Load y into temp register  
ADD_I32_R R0, R192, R193  // Add and store in global x register
```

**Why This Architecture Matters**: 
- **Separation of Concerns**: Multi-pass pipeline separates typed AST optimization from bytecode emission
- **Performance**: Optimization passes operate on typed AST data to feed a register-aware emitter
- **Integration**: Typed AST connects the HM type system to backend passes without additional IRs
- **Observability**: Visualization hooks make every pass debuggable and auditable

### **⚡ Critical Implementation Priorities**
Focus on these features in order for a functional Orus language:
1. **Compiler Backend** (Phase 1.0) - AST-to-bytecode compilation with register allocation
2. **Print Function** (Phase 1.2) - Essential output with `print()` and string interpolation
3. **Main Function** (Phase 2.4) - Program entry point with `fn main:` syntax
4. **Variable Assignments** (Phase 1.4) - `x = value` operations  
5. **Control Flow** (Phase 2.1-2.2) - `if/else` and `while/for` loops
6. **Function Calls** (Phase 3.1) - Function definitions and invocations
7. **Arrays** (Phase 4.1) - Dynamic collections with indexing

---

## 🚀 **Core Performance Principles**

### **0. Multi-Pass Compiler Architecture (CRITICAL - CURRENT DESIGN)**
**The Orus compiler implements the multi-pass strategy defined in `docs/COMPILER_DESIGN.md`.**

#### **📋 MULTI-PASS COMPILER PRINCIPLES**
- **TYPED AST FIRST**: HM inference produces the typed AST consumed by every backend pass
- **STRUCTURED PASSES**: Optimization and bytecode emission run as distinct phases with shared typed AST
- **BYTECODE BUFFER**: Final emission writes into a VM-aware `BytecodeBuffer` with constant pool support
- **REGISTER PLANNING**: Register allocator coordinates global, frame, and temporary ranges before emission
- **VISUALIZATION**: `typed_ast_visualizer.c` inspects the AST after HM inference and optimization passes
- **ADAPTIVE MODES**: FAST/OPTIMIZED/HYBRID backends share the same frontend but vary the optimization depth

```c
// ✅ CORRECT: Multi-pass compilation driver (COMPILER_DESIGN.md)
CompilationResult compile_program(TypedASTNode* root, CompilerContext* ctx) {
    visualize_typed_ast(ctx, root);              // Snapshot after HM inference
    optimize_typed_ast(root, ctx->opt_ctx);      // Constant folding, peephole, register reuse
    visualize_typed_ast(ctx, root);              // Optional post-optimization dump
    return emit_bytecode(ctx, root);             // Register-aware bytecode emission
}

// ✅ CORRECT: Optimization pass operating on typed nodes
void optimize_typed_ast(TypedASTNode* root, OptimizationContext* opt) {
    fold_constants(root, opt);
    simplify_control_flow(root, opt);
    reuse_registers(root, opt);
}
```

#### **🎯 COMPILER IMPLEMENTATION STRATEGY**
- **Pass 0**: HM type inference (complete) generates the typed AST
- **Pass 1**: Typed AST optimization applies constant folding, peephole rewrites, and register reuse strategies
- **Pass 2**: Code generation emits bytecode into `BytecodeBuffer` using the shared `RegisterAllocator`
- **Tooling**: Visualization hooks and metrics instrumentation run after Pass 0 and Pass 1

```c
// ✅ CORRECT: Register allocation consults shared allocator + symbol table
int allocate_register_for_symbol(CompilerContext* ctx, const TypedSymbol* symbol) {
    SymbolBinding* binding = lookup_symbol(ctx->symbols, symbol->name);
    if (binding != NULL) {
        return binding->reg;
    }
    int reg = allocate_temp_register(&ctx->allocator);
    register_symbol(ctx, symbol->name, reg, symbol->type);
    return reg;
}

// ✅ CORRECT: Type-specific opcode selection using typed AST metadata
Opcode select_arithmetic_opcode(BinaryOp op, const Type* type) {
    switch (op) {
        case BINOP_ADD:
            switch (type->kind) {
                case TYPE_I32: return OP_ADD_I32_R;
                case TYPE_I64: return OP_ADD_I64_R;
                case TYPE_F64: return OP_ADD_F64_R;
                default:       return OP_INVALID;
            }
        case BINOP_SUB:
            switch (type->kind) {
                case TYPE_I32: return OP_SUB_I32_R;
                case TYPE_I64: return OP_SUB_I64_R;
                case TYPE_F64: return OP_SUB_F64_R;
                default:       return OP_INVALID;
            }
        default:
            return OP_INVALID;
    }
}
```

**Why Multi-Pass Design is Critical:**
1. **Observability**: Typed AST visualization between passes prevents silent regressions
2. **Performance**: Optimization passes run where they pay off without slowing the frontend
3. **Safety**: HM annotations flow into bytecode generation with no lossy conversion
4. **Maintainability**: Clear pass boundaries limit surface area per contribution
5. **VM Alignment**: Register allocation decisions are prepared before emission, preventing ad hoc assignment

**Implementation Requirements:**
- **Pass Discipline**: Respect pass responsibilities—never mix optimization with emission logic
- **Register Management**: Use the shared allocator and symbol bindings produced during optimization
- **Type Integration**: Reference typed AST metadata for opcode selection and validation
- **Instrumentation**: Update visualization hooks and metrics after each pass
- **Error Handling**: Route diagnostics through the modular error system defined in `src/errors`

### **1. Zero-Cost Abstractions**
- Every abstraction MUST compile to optimal assembly code
- Runtime overhead of abstractions MUST be zero or negative
- Template/generic specialization MUST be compile-time only
- No virtual calls in hot paths unless proven necessary

```c
// ✅ CORRECT: Zero-cost generic with compile-time specialization
#define GENERIC_SORT(T, compare_fn) \
    static inline void sort_##T(T* arr, size_t len) { \
        /* Optimized sorting algorithm for type T */ \
    }

// ❌ WRONG: Runtime polymorphism with vtable overhead
typedef struct {
    void (*compare)(void* a, void* b);
} GenericComparator;
```

### **2. Memory Performance First**
- ALWAYS use arena allocation for predictable lifetime objects
- Implement object pooling for frequently allocated/deallocated objects
- Cache-align all hot data structures (64-byte alignment)
- Prefetch memory for predictable access patterns
- Use NUMA-aware allocation for multi-socket systems

```c
// ✅ CORRECT: Cache-aligned, arena-allocated structure
typedef struct alignas(64) {
    uint64_t hash;
    Type* type;
    uint32_t use_count;
    uint32_t flags;
    // Pad to cache line boundary
    uint8_t padding[64 - sizeof(uint64_t) - sizeof(Type*) - 2*sizeof(uint32_t)];
} CacheAlignedTypeEntry;

// ❌ WRONG: Malloc for every allocation
TypeEntry* entry = malloc(sizeof(TypeEntry));
```

### **3. SIMD-First Computing**
- Use SIMD instructions for ALL bulk operations
- Implement multiple code paths for different SIMD capabilities (AVX-512, AVX2, NEON)
- Vectorize string operations, hashing, and constraint checking
- Process data in SIMD-friendly chunk sizes (16, 32, 64 elements)

```c
// ✅ CORRECT: SIMD-optimized bulk constraint checking
static bool check_constraints_avx512(ConstraintFlags* type_flags, 
                                     ConstraintFlags* required_flags,
                                     size_t count) {
    for (size_t i = 0; i < count; i += 64) {
        __m512i types = _mm512_load_si512(&type_flags[i]);
        __m512i required = _mm512_load_si512(&required_flags[i]);
        __m512i result = _mm512_and_si512(types, required);
        __m512i cmp = _mm512_cmpeq_epi8(result, required);
        
        if (_mm512_movepi8_mask(cmp) != UINT64_MAX) {
            return false;
        }
    }
    return true;
}

// ❌ WRONG: Scalar processing one element at a time
for (size_t i = 0; i < count; i++) {
    if ((type_flags[i] & required_flags[i]) != required_flags[i]) {
        return false;
    }
}
```

### **4. Compiler Performance (CRITICAL FOR VM STARTUP)**
**The compiler performance DIRECTLY impacts VM runtime startup. Architecture MUST balance speed and optimization.**

#### **🚀 FRONTEND PERFORMANCE (ALWAYS FAST)**
- **Sub-Millisecond Frontend**: Parsing + analysis must complete in < 1ms for small programs
- **Linear Frontend Scaling**: Frontend MUST scale O(n) at 1MB/sec minimum  
- **Zero Allocation Frontend**: Avoid malloc/free in parsing hot paths
- **Shared Analysis Efficiency**: Single analysis pass for all backends
- **Cache-Friendly Parsing**: Sequential memory access patterns only

#### **⚡ BACKEND PERFORMANCE (ADAPTIVE)**
- **FAST Backend**: < 1ms total compilation for development iteration
- **OPTIMIZED Backend**: Advanced optimizations worth the compilation cost
- **HYBRID Backend**: Smart cost/benefit analysis for optimization decisions
- **Profile-Guided Selection**: Hot path detection guides backend choice

```c
// ✅ CORRECT: Adaptive compilation performance metrics
typedef struct {
    // Shared frontend metrics (ALWAYS fast)
    uint64_t frontend_tokens_per_second;    // Target: 2M+ tokens/sec
    uint64_t frontend_latency;              // Target: < 0.5ms for small files
    uint64_t shared_analysis_time;          // Target: < 0.3ms for analysis
    
    // Backend-specific metrics (adaptive)
    uint64_t fast_backend_latency;          // Target: < 0.5ms additional
    uint64_t optimized_backend_latency;     // Target: < 10ms for complex optimization
    uint64_t profile_lookup_time;           // Target: < 0.1ms for hot path detection
    
    // Overall compilation metrics
    uint64_t total_compilation_time;        // Varies by backend selection
    uint64_t backend_selection_time;        // Target: < 0.1ms decision time
} AdaptiveCompilerMetrics;

// ✅ CORRECT: Fast shared frontend with adaptive backends
CompilationResult compileWithAdaptiveBackend(ASTNode* ast, CompilerContext* ctx) {
    uint64_t start_time = get_time_ns();
    
    // STEP 1: Fast shared frontend (ALWAYS optimized)
    TypedExpression* typed = analyzeExpression(ast, ctx->compiler);  // < 0.5ms
    
    // STEP 2: Smart backend selection (profile-guided)
    CompilerBackend backend = chooseOptimalBackend(ast, ctx);        // < 0.1ms
    
    // STEP 3: Backend-specific compilation (adaptive performance)
    int result;
    switch (backend) {
        case BACKEND_FAST:                                           // < 0.5ms
            result = compileFastPath(typed, ctx->compiler);
            break;
        case BACKEND_OPTIMIZED:                                      // < 10ms
            result = compileOptimizedPath(typed, ctx);
            break;  
        case BACKEND_HYBRID:                                         // < 2ms
            result = compileHybridPath(typed, ctx);
            break;
    }
    
    // Track performance for future backend decisions
    uint64_t total_time = get_time_ns() - start_time;
    updateCompilationMetrics(backend, total_time, ast);
    
    return result;
}

// ❌ WRONG: Uniform slow compilation (FORBIDDEN)
void slow_uniform_compilation(Compiler* c) {
    // ❌ Always use expensive optimization regardless of code characteristics
    build_complex_symbol_table(c);          // Expensive for simple code
    perform_global_optimization(c);         // Overkill for development
    advanced_register_allocation(c);        // Unnecessary for cold code
    interprocedural_analysis(c);            // Too slow for small functions
}
```

**Adaptive Compilation Speed Targets:**

#### **🚀 Frontend Targets (ALWAYS FAST):**
- **Interactive REPL**: < 0.5ms frontend latency for single expressions
- **Small Scripts**: < 2ms frontend for programs under 10KB  
- **Large Programs**: Frontend scales at 2MB/sec minimum
- **Memory Usage**: < 1.5× source size for frontend memory overhead

#### **⚡ Backend Targets (ADAPTIVE):**
- **FAST Backend**: + 0.5ms additional (total < 1ms for REPL)
- **OPTIMIZED Backend**: + 10ms for complex optimization (production builds)
- **HYBRID Backend**: + 2ms with smart optimization selection
- **Profile Lookup**: < 0.1ms to determine hot path status

#### **🎯 Total Compilation Targets:**
- **Development Mode**: < 1ms total (frontend + FAST backend)
- **Production Mode**: < 15ms total (frontend + OPTIMIZED backend)  
- **Adaptive Mode**: 1-5ms total (frontend + HYBRID backend)
- **Hot Path Recompilation**: < 20ms for runtime optimization

**Performance Monitoring:**
- **Always Profile**: Measure frontend + backend time separately
- **Backend Selection Analytics**: Track backend choice effectiveness
- **Hot Path Detection**: Monitor profile-guided optimization benefits
- **Regression Testing**: Compilation speed tests for ALL backends in CI/CD
- **Memory Profiling**: Track allocation patterns for frontend + backends
- **Cache Analysis**: Monitor CPU cache misses in compilation hot paths

---

## 🛡️ **Engineering Excellence Standards**

### **1. Error Handling**
- Use structured error handling with error codes, NOT exceptions
- Implement comprehensive error recovery strategies
- Batch error reporting for better user experience
- Use static analysis tools to verify error handling paths

```c
// ✅ CORRECT: Structured error handling with recovery
typedef enum {
    COMPILE_SUCCESS = 0,
    COMPILE_ERROR_SYNTAX = 1,
    COMPILE_ERROR_TYPE = 2,
    COMPILE_ERROR_MEMORY = 3,
    COMPILE_ERROR_IO = 4
} CompileResult;

typedef struct {
    CompileResult result;
    ErrorBuffer* errors;    // Batched errors
    size_t bytes_processed;
    RecoveryInfo* recovery; // How to continue compilation
} CompilationStatus;

// ❌ WRONG: Exception-based error handling
try {
    compile_function(func);
} catch (CompileException& e) {
    // Exceptions have performance overhead
}
```

### **2. Memory Safety**
- Use AddressSanitizer and Valgrind in ALL development builds
- Implement bounds checking for all array accesses
- Use RAII patterns for automatic resource cleanup
- Static analysis with Clang Static Analyzer and PVS-Studio

```c
// ✅ CORRECT: Bounds-checked array access with RAII
typedef struct {
    Value* data;
    size_t length;
    size_t capacity;
    Arena* arena;  // RAII cleanup
} SafeArray;

static inline Value* safe_array_get(SafeArray* arr, size_t index) {
    if (unlikely(index >= arr->length)) {
        handle_bounds_error(arr, index);
        return NULL;
    }
    return &arr->data[index];
}

// ❌ WRONG: Unchecked array access
return arr->data[index];  // Potential buffer overflow
```

### **3. Concurrency & Threading**
- Use lock-free data structures wherever possible
- Implement work-stealing queues for load balancing
- Use atomic operations instead of mutexes for counters
- Thread-local storage for hot path variables

```c
// ✅ CORRECT: Lock-free hash table with linear probing
typedef struct {
    _Atomic(HashEntry*) buckets;
    _Atomic(size_t) count;
    size_t capacity;
} LockFreeHashMap;

static bool lockfree_insert(LockFreeHashMap* map, uint64_t key, void* value) {
    size_t index = key % map->capacity;
    
    for (size_t i = 0; i < map->capacity; i++) {
        size_t probe = (index + i) % map->capacity;
        HashEntry* expected = NULL;
        
        HashEntry* new_entry = create_entry(key, value);
        if (atomic_compare_exchange_weak(&map->buckets[probe], &expected, new_entry)) {
            atomic_fetch_add(&map->count, 1);
            return true;
        }
        
        if (expected->key == key) {
            return false;  // Key already exists
        }
    }
    return false;  // Table full
}

// ❌ WRONG: Mutex-protected data structure
pthread_mutex_lock(&map->mutex);
insert_entry(map, key, value);
pthread_mutex_unlock(&map->mutex);
```

---

## 📊 **Performance Benchmarking Requirements**

### **1. Continuous Performance Monitoring**
- Every commit MUST include performance regression tests
- Use hardware performance counters (CPU cycles, cache misses, branch mispredictions)
- Implement automated performance alerts for >5% regressions
- Profile with multiple workloads (synthetic and real-world)

```c
// ✅ REQUIRED: Performance monitoring for every major function
typedef struct {
    uint64_t start_cycles;
    uint64_t start_instructions;
    uint64_t start_cache_misses;
    const char* function_name;
} PerfMeasurement;

#define PERF_MEASURE_START(name) \
    PerfMeasurement _perf = start_perf_measurement(name)

#define PERF_MEASURE_END() \
    end_perf_measurement(&_perf)

// Usage in every performance-critical function
static CompileResult compile_function(Function* func) {
    PERF_MEASURE_START("compile_function");
    
    // Implementation here...
    
    PERF_MEASURE_END();
    return result;
}
```

### **2. Adaptive Benchmark Targets**

#### **🚀 Frontend Benchmarks (Shared across all backends):**
- **Frontend Speed**: >2M tokens/sec, >2MB source/sec
- **Frontend Memory**: <1.5× source size for parsing + analysis
- **Frontend Latency**: <0.5ms for small programs, <2ms for 10KB programs

#### **⚡ Backend Benchmarks (Specialized performance):**
- **FAST Backend**: >1M lines/sec compilation, <1ms total latency
- **OPTIMIZED Backend**: >100K lines/sec with advanced optimization, <15ms total
- **HYBRID Backend**: >500K lines/sec with selective optimization, <5ms total
- **Profile-Guided Selection**: >10M backend decisions/sec, <0.1ms lookup

#### **🎯 VM Runtime Performance:**
- **Hot Path Detection**: Detect hot loops within 100 iterations
- **Profile-Guided Recompilation**: <20ms recompilation for hot paths  
- **Register VM Optimization**: 256-register utilization >80% for complex code
- **Overall Runtime**: 10x faster than Python, 2x faster than JavaScript
- **VM Startup**: <5ms cold start, <1ms warm start with profiling enabled

#### **🔄 Adaptive Optimization Targets:**
- **Backend Selection Accuracy**: >90% optimal backend choice
- **Hot Path Optimization Gain**: >2x performance improvement for detected hot paths
- **Profile Overhead**: <5% runtime overhead when profiling enabled
- **Memory Efficiency**: <10MB total for 1M lines compilation (all backends)

---

## 🔧 **Code Quality Requirements**

### **1. Static Analysis**
ALL code MUST pass these tools with ZERO warnings:
- Clang Static Analyzer
- PVS-Studio
- Cppcheck
- PC-lint Plus
- Custom lint rules for Orus-specific patterns

### **2. Dynamic Analysis**
ALL code MUST pass these tools in CI:
- AddressSanitizer (ASan)
- MemorySanitizer (MSan)
- ThreadSanitizer (TSan)
- UndefinedBehaviorSanitizer (UBSan)
- Valgrind Memcheck

### **3. Testing Standards - COMPREHENSIVE EDGE CASE COVERAGE REQUIRED**
**MANDATORY**: Every single test MUST include multiple edge cases and boundary conditions. **NO EXCEPTIONS.**

- **100% line coverage + 100% branch coverage** for all new code
- **Property-based testing** with AFL++ fuzzing  
- **Performance regression tests** for every algorithm
- **Stress testing** with realistic workloads
- **ALWAYS test edge cases**: empty inputs, null pointers, overflow conditions, boundary values
- **ALWAYS test error paths**: out-of-memory, invalid input, system failures
- **ALWAYS test concurrent access**: race conditions, deadlocks, memory ordering

#### **Required Edge Case Categories for EVERY Test:**

1. **Boundary Values**: 0, 1, MAX_INT, MIN_INT, -1, empty collections
2. **Invalid Inputs**: NULL pointers, negative sizes, malformed data
3. **Resource Exhaustion**: out-of-memory, stack overflow, file handle limits
4. **Concurrent Access**: multiple threads, interrupts, async operations
5. **Performance Extremes**: tiny inputs, massive inputs, degenerate cases
6. **Error Injection**: simulated failures at every possible point

```c
// ✅ REQUIRED: Comprehensive test coverage with ALL edge cases
TEST(constraint_checking_simd_comprehensive) {
    // EDGE CASE 1: Empty input
    ASSERT_TRUE(check_constraints_simd(NULL, NULL, 0));
    
    // EDGE CASE 2: Single element
    ConstraintFlags single_type = CONSTRAINT_NUMERIC;
    ConstraintFlags single_constraint = CONSTRAINT_NUMERIC;
    ASSERT_TRUE(check_constraints_simd(&single_type, &single_constraint, 1));
    
    // EDGE CASE 3: Maximum batch size (boundary testing)
    test_simd_constraint_checking(MAX_SIMD_BATCH_SIZE);
    test_simd_constraint_checking(MAX_SIMD_BATCH_SIZE + 1); // Overflow case
    
    // EDGE CASE 4: All constraint combinations (exhaustive testing)
    for (uint8_t type_flags = 0; type_flags < 64; type_flags++) {
        for (uint8_t constraint_flags = 0; constraint_flags < 64; constraint_flags++) {
            ConstraintFlags type = type_flags;
            ConstraintFlags constraint = constraint_flags;
            
            bool expected = (type & constraint) == constraint;
            bool actual = check_constraints_simd(&type, &constraint, 1);
            
            ASSERT_EQ(expected, actual) 
                << "Failed for type=" << (int)type_flags 
                << " constraint=" << (int)constraint_flags;
        }
    }
    
    // EDGE CASE 5: Memory alignment testing
    test_unaligned_memory_access();
    test_cache_line_boundaries();
    
    // EDGE CASE 6: Performance scaling validation
    for (size_t count = 1; count <= 1024; count *= 2) {
        ConstraintFlags* types = generate_random_type_flags(count);
        ConstraintFlags* constraints = generate_random_constraints(count);
        
        // Correctness validation
        bool scalar_result = check_constraints_scalar(types, constraints, count);
        bool simd_result = check_constraints_simd(types, constraints, count);
        ASSERT_EQ(scalar_result, simd_result);
        
        // Performance requirement validation
        uint64_t scalar_cycles = benchmark_scalar(types, constraints, count);
        uint64_t simd_cycles = benchmark_simd(types, constraints, count);
        ASSERT_LT(simd_cycles * 4, scalar_cycles) 
            << "SIMD not 4x faster at count=" << count;
    }
    
    // EDGE CASE 7: Error injection testing
    test_with_out_of_memory_injection();
    test_with_signal_interruption();
    test_with_invalid_cpu_features();
}

// ✅ REQUIRED: Test helper for memory corruption detection
static void test_unaligned_memory_access() {
    // Test all possible misalignments
    for (int offset = 0; offset < 16; offset++) {
        uint8_t buffer[128];
        ConstraintFlags* unaligned = (ConstraintFlags*)(buffer + offset);
        
        // Initialize with pattern
        for (int i = 0; i < 32; i++) {
            unaligned[i] = CONSTRAINT_NUMERIC | CONSTRAINT_COMPARABLE;
        }
        
        // Test SIMD operations on unaligned data
        bool result = check_constraints_simd(unaligned, unaligned, 32);
        ASSERT_TRUE(result); // Should handle unaligned access gracefully
    }
}

// ✅ REQUIRED: Error injection framework
static void test_with_out_of_memory_injection() {
    // Install memory allocation failure injector
    install_malloc_failure_injector();
    
    for (int failure_point = 0; failure_point < 10; failure_point++) {
        set_malloc_failure_point(failure_point);
        
        // Function should handle OOM gracefully
        CompileResult result = compile_function_with_constraints(test_function);
        
        // Should either succeed or fail gracefully with proper error code
        ASSERT_TRUE(result == COMPILE_SUCCESS || result == COMPILE_ERROR_MEMORY);
        ASSERT_NO_MEMORY_LEAKS(); // Even on failure, no leaks allowed
    }
    
    uninstall_malloc_failure_injector();
}
```

#### **Edge Case Testing Checklist - MANDATORY for Every Function:**
- [ ] **Null/Empty inputs**: Test with NULL pointers, empty strings, zero-length arrays
- [ ] **Boundary values**: Test with 0, 1, -1, MAX_VALUE, MIN_VALUE  
- [ ] **Invalid inputs**: Test with negative lengths, invalid enums, corrupted data
- [ ] **Resource limits**: Test under memory pressure, file descriptor exhaustion
- [ ] **Concurrent access**: Test with multiple threads, interrupts, async operations
- [ ] **Performance boundaries**: Test with tiny inputs (1 element) and huge inputs (1M+ elements)
- [ ] **Error injection**: Test with simulated allocation failures, I/O errors, system call failures
- [ ] **Platform variations**: Test on different architectures, endianness, alignment requirements
- [ ] **Regression cases**: Test previously found bugs to prevent regressions
- [ ] **Fuzzing integration**: Use AFL++ to discover additional edge cases automatically

### **4. Test Organization & Categorization - MANDATORY STRUCTURE**
**MANDATORY**: ALL tests MUST be organized according to the categorization in `docs/TEST_CATEGORIZATION.md`. **NO EXCEPTIONS.**

#### **Required Test Directory Structure:**
```
tests/
├── basic/                  # Phase 1: Basic language features
│   ├── literals/          # literal.orus, boolean.orus
│   ├── operators/         # binary.orus, boolean_ops2.orus, complex_expr.orus
│   ├── variables/         # vars.orus, assignment.orus, mut_compound.orus
│   └── strings/           # string_concat.orus, multi_placeholder.orus, format_spec.orus
├── control_flow/          # Phase 2: Control flow (when implemented)
│   ├── conditions/        # if/else tests
│   ├── loops/            # while/for loop tests
│   └── functions/        # function definition and call tests
├── collections/           # Phase 3: Collections (when implemented)
│   ├── arrays/           # array tests
│   ├── maps/             # hashmap tests
│   └── iterators/        # iterator tests
├── advanced/              # Phase 4: Advanced features (when implemented)
│   ├── structs/          # struct and method tests
│   ├── generics/         # generic type tests
│   └── modules/          # module system tests
├── edge_cases/            # Edge case and boundary tests
│   ├── boundaries/       # boundary value tests
│   ├── errors/           # error condition tests
│   └── stress/           # stress tests (chain100.orus, etc.)
└── benchmarks/            # Performance benchmarks
    ├── micro/            # Micro-benchmarks
    └── macro/            # Macro-benchmarks
```

#### **Test Organization Requirements:**

1. **ALWAYS categorize new tests** according to `docs/TEST_CATEGORIZATION.md`
2. **ALWAYS update the Makefile** to include new test categories
3. **ALWAYS run tests by category** for targeted testing
4. **ALWAYS add edge case variants** for each new test category

```makefile
# ✅ REQUIRED: Makefile targets for each test category
.PHONY: test-all test-basic test-control-flow test-collections test-advanced test-edge-cases test-benchmarks

# Run all tests
test-all: test-basic test-edge-cases test-benchmarks
	@echo "All tests passed!"

# Basic language features (Phase 1)
test-basic: test-literals test-operators test-variables test-strings
	@echo "Basic tests passed!"

test-literals:
	@echo "Testing literals..."
	./orus tests/basic/literals/literal.orus
	./orus tests/basic/literals/boolean.orus

test-operators:
	@echo "Testing operators..."
	./orus tests/basic/operators/binary.orus
	./orus tests/basic/operators/boolean_ops2.orus
	./orus tests/basic/operators/complex_expr.orus

test-variables:
	@echo "Testing variables..."
	./orus tests/basic/variables/vars.orus
	./orus tests/basic/variables/assignment.orus
	./orus tests/basic/variables/mut_compound.orus

test-strings:
	@echo "Testing strings..."
	./orus tests/basic/strings/string_concat.orus
	./orus tests/basic/strings/multi_placeholder.orus
	./orus tests/basic/strings/format_spec.orus

# Control flow tests (Phase 2 - when implemented)
test-control-flow: test-conditions test-loops test-functions
	@echo "Control flow tests passed!"

test-conditions:
	@echo "Testing conditions..."
	# Add if/else tests when implemented

test-loops:
	@echo "Testing loops..."
	# Add while/for tests when implemented

test-functions:
	@echo "Testing functions..."
	# Add function tests when implemented

# Edge cases and stress tests
test-edge-cases: test-boundaries test-errors test-stress
	@echo "Edge case tests passed!"

test-boundaries:
	@echo "Testing boundary conditions..."
	# Add boundary value tests

test-errors:
	@echo "Testing error conditions..."
	# Add error handling tests

test-stress:
	@echo "Testing stress conditions..."
	./orus tests/edge_cases/stress/chain100.orus

# Performance benchmarks
test-benchmarks: test-micro test-macro
	@echo "Benchmark tests passed!"

test-micro:
	@echo "Running micro-benchmarks..."
	# Add micro-benchmark tests

test-macro:
	@echo "Running macro-benchmarks..."
	# Add macro-benchmark tests
```

#### **Test Creation Workflow - MANDATORY for Every New Test:**

1. **Determine category** using `docs/TEST_CATEGORIZATION.md`
2. **Create test in correct directory** following the structure above
3. **Add edge case variants** for the same test scenario
4. **Update Makefile** with new test targets
5. **Run category-specific tests** to verify integration
6. **Document test purpose** and expected behavior

```c
// ✅ REQUIRED: Example of adding a new test category
// When implementing if/else statements:

// 1. Create tests/control_flow/conditions/if_basic.orus
if true:
    print("condition works")

// 2. Create edge case variant: tests/control_flow/conditions/if_edge_cases.orus
if true:
    print("true branch")
else:
    print("false branch")

// Test with boundary conditions
let x = 0
if x == 0:
    print("zero")
elif x > 0:
    print("positive")
else:
    print("negative")

// 3. Update Makefile test-conditions target:
test-conditions:
	@echo "Testing conditions..."
	./orus tests/control_flow/conditions/if_basic.orus
	./orus tests/control_flow/conditions/if_edge_cases.orus
	./orus tests/control_flow/conditions/if_nested.orus
	./orus tests/control_flow/conditions/if_complex.orus
```

#### **Test Maintenance Requirements:**

- **ALWAYS move tests** to correct categories when reorganizing
- **ALWAYS update Makefile** when adding/moving tests
- **ALWAYS maintain edge case coverage** for each category
- **ALWAYS run category tests** before committing changes
- **ALWAYS document** test rationale and expected outputs

---

## 🚫 **Forbidden Practices**

### **NEVER Use These:**
1. **`malloc/free`** for frequent allocations → Use arena allocators
2. **Recursive algorithms** without tail-call optimization → Use iterative with explicit stack
3. **String concatenation** with `strcat` → Use rope data structures
4. **Linear search** in hot paths → Use hash tables or binary search
5. **Exception handling** in C code → Use error codes
6. **Floating-point** for performance counters → Use integer arithmetic
7. **`printf` family** in hot paths → Use specialized formatters
8. **Mutex locks** for atomic counters → Use atomic operations
9. **Virtual function calls** in tight loops → Use template specialization
10. **Debug builds** for performance measurements → Always use release builds

### **NEVER Compromise On:**
1. **Cache locality** - Always consider memory access patterns
2. **Branch prediction** - Structure code to be branch-predictor friendly
3. **SIMD utilization** - Process data in vector-friendly formats
4. **Compile-time optimization** - Prefer compile-time computation over runtime
5. **Memory allocation patterns** - Predictable allocation is faster allocation

---

## 🎯 **Implementation Checklist**

Before submitting ANY code, verify:

### **Performance Checklist:**
- [ ] Profiled with hardware performance counters
- [ ] Compared against best-in-class alternatives
- [ ] SIMD implementation for bulk operations
- [ ] Cache-friendly memory layout
- [ ] Branch-predictor friendly control flow
- [ ] No unnecessary allocations in hot paths
- [ ] Lock-free where possible

### **Quality Checklist:**
- [ ] Zero static analysis warnings
- [ ] **100% test coverage with comprehensive edge case testing for EVERY function**
- [ ] **ALL boundary conditions tested**: empty, null, min/max values, overflow cases
- [ ] **ALL error paths tested**: OOM, invalid input, system failures, concurrent access
- [ ] **Tests properly categorized** according to `docs/TEST_CATEGORIZATION.md`
- [ ] **Makefile updated** with new test targets for added test categories
- [ ] **Category-specific tests pass** (test-basic, test-control-flow, etc.)
- [ ] **Fuzz testing with AFL++ discovering additional edge cases**
- [ ] Memory safety verified with sanitizers on ALL edge cases
- [ ] Performance regression tests pass for both normal and degenerate inputs
- [ ] Documentation with complexity analysis and edge case behavior
- [ ] Benchmark results documented for edge cases and worst-case scenarios

### **Engineering Checklist:**
- [ ] Error handling for all failure modes
- [ ] Resource cleanup with RAII patterns
- [ ] Bounds checking for all array accesses
- [ ] Overflow checking for arithmetic operations
- [ ] Thread safety analysis completed
- [ ] API design reviewed for misuse resistance
- [ ] Performance telemetry integrated

---

## 📚 **Required Reading**

Before contributing to Orus, study these resources:

### **Performance Engineering:**
1. "Computer Systems: A Programmer's Perspective" - Bryant & O'Hallaron
2. "Software Optimization Guide" - Intel Corporation
3. "What Every Programmer Should Know About Memory" - Ulrich Drepper
4. "The Art of Computer Programming" - Donald Knuth (Volumes 1-3)

### **Systems Programming:**
1. "Advanced Programming in the UNIX Environment" - Stevens & Rago
2. "Linux Kernel Development" - Robert Love
3. "Understanding the Linux Kernel" - Bovet & Cesati

### **Compiler Design:**
1. "Engineering a Compiler" - Cooper & Torczon
2. "Advanced Compiler Design and Implementation" - Muchnick
3. "Modern Compiler Implementation in C" - Appel

---

## 📋 **Documentation & Roadmap Management**

### **MANDATORY: Always Update Documentation**
Every contribution MUST include documentation updates:

1. **Update `COMPILER_DESIGN.md`** for architectural changes:
   - Update phase completion status (Phase 1, 2, 3, 4, 5, 6)
   - Document multi-pass compiler implementation progress
   - Add new register allocation strategies and optimization decisions
   - Update VM-aware optimization strategies and bytecode generation

2. **Update `docs/MISSING.md`** with completed features:
   - Mark TODO items as ✅ COMPLETED when implemented
   - Add new TODO items for discovered requirements
   - Update progress estimates and implementation status
   - Document any architectural decisions or changes

2. **Update `docs/IMPLEMENTATION_GUIDE.md`** for new patterns:
   - Add high-performance C code examples for new features
   - Document optimization techniques used
   - Include performance benchmarks and analysis
   - Add best practices learned during implementation

3. **Update `docs/TEST_CATEGORIZATION.md`** for new tests:
   - Categorize all new tests according to the established structure
   - Update test coverage analysis for new features
   - Document missing tests and coverage gaps
   - Add edge case requirements for new test categories

4. **Update `Makefile`** for new test categories:
   - Add test targets for new test directories
   - Update category-specific test runners
   - Maintain test organization and automation
   - Ensure all tests are included in `test-all` target

5. **Follow Implementation Guidelines**:
   - **ALWAYS** consult `docs/IMPLEMENTATION_GUIDE.md` before coding
   - Use the provided high-performance patterns and architectures
   - Follow the zero-cost abstraction principles
   - Implement SIMD-optimized versions as shown in examples

```c
// ✅ REQUIRED: Update roadmap after implementing features
static void update_roadmap_progress(const char* feature_name, 
                                   ImplementationStatus status) {
    // Mark feature as completed in MISSING.md
    roadmap_mark_completed(feature_name, status);
    
    // Document performance characteristics
    document_performance_metrics(feature_name, get_benchmark_results());
    
    // Update implementation guide with new patterns
    if (is_new_pattern(feature_name)) {
        add_implementation_example(feature_name, get_code_example());
    }
}

// Usage after every major feature implementation
void complete_feature_implementation(Feature* feature) {
    // Implement the feature with high-performance code
    implement_feature_optimized(feature);
    
    // MANDATORY: Update documentation
    update_roadmap_progress(feature->name, IMPLEMENTATION_COMPLETE);
    
    // Add to implementation guide if it introduces new patterns
    if (feature->introduces_new_patterns) {
        add_to_implementation_guide(feature);
    }
}
```

### **Documentation Standards:**
- **Always reference `docs/LANGUAGE.md`** for canonical Orus syntax
- **Use only Orus code examples** in `MISSING.md` (NO C code)
- **Use only high-performance C code** in `IMPLEMENTATION_GUIDE.md`
- **Update progress percentages** and completion estimates
- **Document performance improvements** with before/after benchmarks

### **Roadmap Synchronization:**
- `docs/MISSING.md` = **WHAT** needs to be implemented (Orus language features)
- `docs/IMPLEMENTATION_GUIDE.md` = **HOW** to implement it (high-performance C code)
- `docs/LANGUAGE.md` = **SPECIFICATION** of Orus syntax and semantics

### **Required Documentation Flow:**
```
1. Read LANGUAGE.md → Understand Orus specifications
2. Check COMPILER_DESIGN.md → Understand current multi-pass compiler architecture
3. Check MISSING.md → See what needs implementation  
4. Study IMPLEMENTATION_GUIDE.md → Learn high-performance patterns
5. Implement feature → Following the multi-pass pipeline (optimization → bytecode emission)
6. Update COMPILER_DESIGN.md → Mark phase progress, document register allocation decisions
7. Update MISSING.md → Mark feature as completed
8. Update IMPLEMENTATION_GUIDE.md → Add new patterns if applicable
9. Update TEST_CATEGORIZATION.md → Categorize new tests
10. Update Makefile → Add test targets for new categories
11. Benchmark compiler → Performance characteristics of the multi-pass pipeline
```

### **Documentation Quality Requirements:**
- **Orus code examples** must compile and run correctly
- **C implementation code** must pass all static analysis tools
- **Performance claims** must be backed by benchmark data
- **Architecture decisions** must be justified with technical reasoning
- **TODO items** must have realistic time estimates and priorities

---

## 🏆 **Success Metrics**

Your contributions to Orus are successful when:

1. **Performance**: New code is faster than existing alternatives
2. **Reliability**: Zero crashes, memory leaks, or undefined behavior
3. **Maintainability**: Code is self-documenting with clear invariants
4. **Scalability**: Performance scales linearly with input size
5. **Portability**: Works optimally on x86-64, ARM64, and RISC-V

---

## ⚡ **Remember: Performance is NOT Optional**

Orus aims to be the **fastest** programming language implementation. Every line of code you write either:
- Makes Orus faster 🚀
- Makes Orus more reliable 🛡️
- Makes Orus easier to optimize 🔧

If your code doesn't meet these criteria, **it doesn't belong in Orus.**

**Think like a systems engineer building fighter jet software - every microsecond matters, every byte counts, every algorithm must be optimal.**

---

*"Premature optimization is the root of all evil" - NOT in Orus. In Orus, inadequate optimization is the root of all evil.*
