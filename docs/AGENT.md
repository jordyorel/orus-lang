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

### **⚡ Critical Implementation Priorities**
Focus on these features in order for a functional Orus language:
1. **Print Function** (Phase 1.2) - Essential output with `print()` and string interpolation
2. **Main Function** (Phase 2.4) - Program entry point with `fn main:` syntax
3. **Variable Assignments** (Phase 1.4) - `x = value` operations  
4. **Control Flow** (Phase 2.1-2.2) - `if/else` and `while/for` loops
5. **Function Calls** (Phase 3.1) - Function definitions and invocations
6. **Arrays** (Phase 4.1) - Dynamic collections with indexing

---

## 🚀 **Core Performance Principles**

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

### **2. Benchmark Targets**
- **Compilation Speed**: >100,000 lines of code per second
- **Memory Usage**: <10MB for 1M lines of code compilation
- **Startup Time**: <5ms cold start, <1ms warm start
- **Runtime Performance**: 10x faster than Python, 2x faster than JavaScript

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

### **3. Testing Standards**
- **100% line coverage** for all new code
- **Property-based testing** with AFL++ fuzzing
- **Performance regression tests** for every algorithm
- **Stress testing** with realistic workloads

```c
// ✅ REQUIRED: Comprehensive test coverage
TEST(constraint_checking_simd_correctness) {
    // Test with various constraint combinations
    for (size_t count = 1; count <= 1024; count *= 2) {
        ConstraintFlags* types = generate_random_type_flags(count);
        ConstraintFlags* constraints = generate_random_constraints(count);
        
        // Test SIMD vs scalar implementations for correctness
        bool scalar_result = check_constraints_scalar(types, constraints, count);
        bool simd_result = check_constraints_simd(types, constraints, count);
        
        ASSERT_EQ(scalar_result, simd_result);
        
        // Performance requirement: SIMD must be at least 4x faster
        uint64_t scalar_cycles = benchmark_scalar(types, constraints, count);
        uint64_t simd_cycles = benchmark_simd(types, constraints, count);
        
        ASSERT_LT(simd_cycles * 4, scalar_cycles);
    }
}
```

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
- [ ] 100% test coverage with property-based tests
- [ ] Fuzz testing with AFL++
- [ ] Memory safety verified with sanitizers
- [ ] Performance regression tests pass
- [ ] Documentation with complexity analysis
- [ ] Benchmark results documented

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

1. **Update `docs/MISSING.md`** with completed features:
   - Mark TODO items as ✅ COMPLETED when implemented
   - Add new TODO items for discovered requirements
   - Update progress estimates and implementation status
   - Document any architectural decisions or changes

2. **Update `docs/IMPLEMENTATION_GUIDE.md`** for new patterns:
   - Add high-performance C code examples for new features
   - Document optimization techniques used
   - Include performance benchmarks and analysis
   - Add best practices learned during implementation

3. **Follow Implementation Guidelines**:
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
2. Check MISSING.md → See what needs implementation
3. Study IMPLEMENTATION_GUIDE.md → Learn high-performance patterns
4. Implement feature → Using best practices from guide
5. Update MISSING.md → Mark feature as completed
6. Update IMPLEMENTATION_GUIDE.md → Add new patterns if applicable
7. Benchmark and document → Performance characteristics
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
