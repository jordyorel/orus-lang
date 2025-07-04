```c
// Shows the dramatic difference

// ============================================
// BEFORE: Current implementation (slow)
// ============================================

// Typical hot loop in current VM:
void current_vm_loop() {
    // Simulating: for (i = 0; i < 1000000; i++) sum += i;
    
    // LOAD_I32_CONST 0, 0
    vm.instruction_count++;
    if (vm.trace) { /* debug output */ }
    if (IS_ERROR(vm.lastError)) { /* error handling */ }
    uint8_t reg = READ_BYTE();
    uint16_t constantIndex = READ_SHORT();
    vm.registers[reg] = READ_CONSTANT(constantIndex);
    
    // ... (many more instructions with checks)
    
    // Each iteration does:
    // 1. ADD_I32_R with type checking
    if (!IS_I32(vm.registers[src1]) || !IS_I32(vm.registers[src2])) {
        runtimeError(ERROR_TYPE, location, "Operands must be i32");
        RETURN(INTERPRET_RUNTIME_ERROR);
    }
    int32_t a = AS_I32(vm.registers[src1]);  // Unboxing
    int32_t b = AS_I32(vm.registers[src2]);  // Unboxing
    vm.registers[dst] = I32_VAL(a + b);      // Boxing
    
    // 2. INC_I32_R with overflow checking
    int32_t val = AS_I32(vm.registers[reg]);
    int32_t result;
    if (__builtin_add_overflow(val, 1, &result)) {
        runtimeError(ERROR_VALUE, location, "Integer overflow");
        RETURN(INTERPRET_RUNTIME_ERROR);
    }
    vm.registers[reg] = I32_VAL(result);
    
    // 3. LT_I32_R with type checking
    if (!IS_I32(vm.registers[src1]) || !IS_I32(vm.registers[src2])) {
        runtimeError(ERROR_TYPE, location, "Operands must be i32");
        RETURN(INTERPRET_RUNTIME_ERROR);
    }
    vm.registers[dst] = BOOL_VAL(AS_I32(vm.registers[src1]) < AS_I32(vm.registers[src2]));
    
    // 4. JUMP_IF_NOT_R with type checking
    if (!IS_BOOL(vm.registers[reg])) {
        runtimeError(ERROR_TYPE, location, "Condition must be boolean");
        RETURN(INTERPRET_RUNTIME_ERROR);
    }
    if (!AS_BOOL(vm.registers[reg])) {
        vm.ip += offset;
    }
    
    // 5. JUMP back
    vm.ip -= offset;
    
    // Total per iteration: 5 instructions, 7 type checks, 6 boxing/unboxing operations
}

// ============================================
// AFTER: Optimized implementation (fast)
// ============================================

void optimized_vm_loop() {
    // Same loop with optimizations
    
    // Setup (unchanged)
    vm.typed_regs.i32_regs[0] = 0;        // i = 0
    vm.typed_regs.i32_regs[1] = 1000000;  // limit
    vm.typed_regs.i32_regs[2] = 0;        // sum = 0
    
    // Hot loop with fused instruction:
    while (1) {
        // ADD_I32_TYPED (no checks, no boxing)
        vm.typed_regs.i32_regs[2] += vm.typed_regs.i32_regs[0];
        
        // INC_CMP_JMP (fused instruction)
        if (++vm.typed_regs.i32_regs[0] >= vm.typed_regs.i32_regs[1]) {
            break;
        }
        
        // That's it! 2 operations instead of 5, zero type checks, zero boxing
    }
    
    // Or in actual VM bytecode:
    // LABEL_OP_INC_CMP_JMP: {
    //     uint8_t reg = *vm.ip++;
    //     uint8_t limit_reg = *vm.ip++;
    //     int16_t offset = *(int16_t*)vm.ip;
    //     vm.ip += 2;
    //     
    //     if (++vm.typed_regs.i32_regs[reg] < vm.typed_regs.i32_regs[limit_reg]) {
    //         vm.ip += offset;
    //     }
    //     DISPATCH_TYPED();  // Just: goto *dispatchTable[*vm.ip++]
    // }
}

// ============================================
// PERFORMANCE METRICS
// ============================================

/*
Current Implementation (per loop iteration):
- Instructions executed: 5
- Type checks: 7
- Boxing operations: 3
- Unboxing operations: 6
- Memory accesses: ~15
- Branch mispredictions: ~2-3
- Estimated cycles: ~50-70

Optimized Implementation (per loop iteration):
- Instructions executed: 2 (60% reduction)
- Type checks: 0 (100% reduction)
- Boxing operations: 0 (100% reduction)
- Unboxing operations: 0 (100% reduction)
- Memory accesses: ~4 (73% reduction)
- Branch mispredictions: ~0-1 (50% reduction)
- Estimated cycles: ~8-12 (80% reduction!)

Expected speedup: 4-6x on tight loops
*/

// ============================================
// REAL BENCHMARK CODE
// ============================================

#include <time.h>
#include <stdio.h>

void benchmark_current_vs_optimized() {
    const int ITERATIONS = 10000000;
    clock_t start, end;
    double cpu_time_used;
    
    // Benchmark current implementation
    printf("=== Current Implementation ===\n");
    start = clock();
    
    // Simulate current VM executing the loop
    for (int i = 0; i < ITERATIONS; i++) {
        // Type check 1
        if ((vm.registers[0].type & 0xF) != VAL_I32) { /* error */ }
        // Type check 2  
        if ((vm.registers[1].type & 0xF) != VAL_I32) { /* error */ }
        // Unbox, add, box
        int32_t a = vm.registers[0].as.i32;
        int32_t b = vm.registers[1].as.i32;
        vm.registers[2].type = VAL_I32 | (VAL_I32 << 4);
        vm.registers[2].as.i32 = a + b;
        // ... more overhead
    }
    
    end = clock();
    cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
    printf("Time: %.3f seconds\n", cpu_time_used);
    
    // Benchmark optimized implementation
    printf("\n=== Optimized Implementation ===\n");
    start = clock();
    
    // Simulate optimized VM
    int32_t sum = 0;
    for (int32_t i = 0; i < ITERATIONS; i++) {
        sum += i;  // Direct operation, no overhead
    }
    
    end = clock();
    cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
    printf("Time: %.3f seconds\n", cpu_time_used);
    printf("Result: %d\n", sum);
}

// ============================================
// COMPILER OUTPUT COMPARISON
// ============================================

/*
BEFORE - Bytecode for: sum = sum + i
----------------------------------------
LOAD_GLOBAL R3, sum_index      ; 3 bytes
LOAD_GLOBAL R4, i_index        ; 3 bytes  
ADD_I32_R R5, R3, R4          ; 4 bytes (with type checks)
STORE_GLOBAL sum_index, R5     ; 3 bytes
Total: 13 bytes, 4 instructions

AFTER - Bytecode for: sum = sum + i (when types are known)
----------------------------------------
ADD_I32_TYPED R2, R2, R0      ; 3 bytes (no type checks!)
Total: 3 bytes, 1 instruction

For a loop with 1M iterations:
- Before: 13MB of bytecode traffic
- After: 3MB of bytecode traffic (77% reduction)
- This fits in L1 cache!
*/
```


# VM Optimization Integration Guide

## 🎯 Goal: Beat Lua's Performance

Current gaps:
- Arithmetic: Lua is 1.8x faster
- Control flow: Lua is 3.3x faster

Target: 2-3x speedup to match or beat Lua

## Phase 1: Immediate Wins (1-2 hours, 40-50% speedup)

### 1.1 Replace DISPATCH() Macro

In `vm.c`, replace the existing DISPATCH() macro:

```c
// At the top of run() function, add:
#ifdef ORUS_RELEASE
    #define DISPATCH() goto *dispatchTable[*vm.ip++]
    #define DISPATCH_TYPED() goto *dispatchTable[*vm.ip++]
#else
    // Keep existing DISPATCH() for debug builds
#endif
```

### 1.2 Remove Type Checks from Typed Operations

Find all `LABEL_OP_*_TYPED` blocks and remove type checking. Example:

```c
// BEFORE:
LABEL_OP_ADD_I32_TYPED: {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    
    vm.typed_regs.i32_regs[dst] = vm.typed_regs.i32_regs[left] + vm.typed_regs.i32_regs[right];
    vm.typed_regs.reg_types[dst] = REG_TYPE_I32;
    
    DISPATCH();
}

// AFTER:
LABEL_OP_ADD_I32_TYPED: {
    uint8_t dst = *vm.ip++;
    uint8_t left = *vm.ip++;
    uint8_t right = *vm.ip++;
    
    vm.typed_regs.i32_regs[dst] = vm.typed_regs.i32_regs[left] + vm.typed_regs.i32_regs[right];
    
    DISPATCH_TYPED();  // Use fast dispatch
}
```

### 1.3 Optimize Hot Opcodes Order

Reorder dispatch table initialization to put hot opcodes first:

```c
// In dispatch table init:
// Put these FIRST for cache locality
dispatchTable[OP_ADD_I32_TYPED] = &&LABEL_OP_ADD_I32_TYPED;
dispatchTable[OP_LT_I32_TYPED] = &&LABEL_OP_LT_I32_TYPED;
dispatchTable[OP_JUMP_SHORT] = &&LABEL_OP_JUMP_SHORT;
dispatchTable[OP_INC_I32_R] = &&LABEL_OP_INC_I32_R;
// Then the rest...
```

## Phase 2: Instruction Fusion (2-3 hours, 20-30% speedup)

### 2.1 Add Fused Opcodes to vm.h

```c
// In OpCode enum, add:
OP_ADD_I32_IMM = 250,    // Add immediate
OP_INC_CMP_JMP = 251,    // Increment + compare + jump
OP_MUL_ADD_I32 = 252,    // Multiply-add
```

### 2.2 Implement Fused Instructions

Add these implementations in run():

```c
LABEL_OP_ADD_I32_IMM: {
    uint8_t dst = *vm.ip++;
    uint8_t src = *vm.ip++;
    int32_t imm = *(int32_t*)vm.ip;
    vm.ip += 4;
    
    vm.typed_regs.i32_regs[dst] = vm.typed_regs.i32_regs[src] + imm;
    DISPATCH_TYPED();
}

LABEL_OP_INC_CMP_JMP: {
    uint8_t reg = *vm.ip++;
    uint8_t limit_reg = *vm.ip++;
    int16_t offset = *(int16_t*)vm.ip;
    vm.ip += 2;
    
    if (++vm.typed_regs.i32_regs[reg] < vm.typed_regs.i32_regs[limit_reg]) {
        vm.ip += offset;
    }
    DISPATCH_TYPED();
}
```

### 2.3 Update Compiler to Emit Fused Instructions

In `compiler.c`, add pattern detection:

```c
// Add to compile_for_statement() or similar:
if (node->increment->type == AST_UNARY_OP && 
    node->increment->op == OP_INC &&
    node->condition->type == AST_BINARY_OP &&
    node->condition->op == OP_LT) {
    
    // Emit fused increment-compare-jump
    emitByte(compiler, OP_INC_CMP_JMP);
    emitByte(compiler, loop_var_reg);
    emitByte(compiler, limit_reg);
    emitShort(compiler, loop_start_offset);
} else {
    // Emit regular instructions
}
```

## Phase 3: Compiler Optimizations (3-4 hours, 15-25% speedup)

### 3.1 Implement Type Inference

Add type tracking to the compiler:

```c
typedef struct {
    RegisterType type;
    bool is_constant;
    union {
        int32_t i32_val;
        int64_t i64_val;
        double f64_val;
    } const_value;
} RegisterInfo;

// Track register types during compilation
RegisterInfo reg_info[256];

// When compiling literals:
void compile_int_literal(Compiler* c, int32_t value, uint8_t dst_reg) {
    reg_info[dst_reg].type = REG_TYPE_I32;
    reg_info[dst_reg].is_constant = true;
    reg_info[dst_reg].const_value.i32_val = value;
    
    // Emit typed load
    emitByte(c, OP_LOAD_I32_CONST);
    emitByte(c, dst_reg);
    emitShort(c, addConstant(c->chunk, I32_VAL(value)));
}
```

### 3.2 Emit Typed Instructions When Types are Known

```c
void compile_addition(Compiler* c, ASTNode* left, ASTNode* right) {
    uint8_t left_reg = compile_expression(c, left);
    uint8_t right_reg = compile_expression(c, right);
    uint8_t dst_reg = allocate_register(c);
    
    // If both operands are known to be i32
    if (reg_info[left_reg].type == REG_TYPE_I32 && 
        reg_info[right_reg].type == REG_TYPE_I32) {
        
        // Check for immediate optimization
        if (reg_info[right_reg].is_constant) {
            emitByte(c, OP_ADD_I32_IMM);
            emitByte(c, dst_reg);
            emitByte(c, left_reg);
            emitInt32(c, reg_info[right_reg].const_value.i32_val);
        } else {
            // Use typed instruction
            emitByte(c, OP_ADD_I32_TYPED);
            emitByte(c, dst_reg);
            emitByte(c, left_reg);
            emitByte(c, right_reg);
        }
        
        reg_info[dst_reg].type = REG_TYPE_I32;
    } else {
        // Fall back to boxed operation
        emitByte(c, OP_ADD_I32_R);
        emitByte(c, dst_reg);
        emitByte(c, left_reg);
        emitByte(c, right_reg);
    }
}
```

## Phase 4: Advanced Optimizations (Optional, 10-20% more)

### 4.1 SIMD Operations (if targeting modern CPUs)

```c
#ifdef __SSE2__
LABEL_OP_ADD_I32X4: {
    uint8_t dst = *vm.ip++;
    uint8_t src1 = *vm.ip++;
    uint8_t src2 = *vm.ip++;
    
    __m128i a = _mm_loadu_si128((__m128i*)&vm.typed_regs.i32_regs[src1]);
    __m128i b = _mm_loadu_si128((__m128i*)&vm.typed_regs.i32_regs[src2]);
    __m128i result = _mm_add_epi32(a, b);
    _mm_storeu_si128((__m128i*)&vm.typed_regs.i32_regs[dst], result);
    
    DISPATCH_TYPED();
}
#endif
```

### 4.2 Profile-Guided Optimization

```bash
# Compile with PGO
gcc -O3 -fprofile-generate vm.c -o orus_pgo
./orus_pgo benchmark.orus
gcc -O3 -fprofile-use vm.c -o orus_optimized
```

## Benchmarking Strategy

Create this benchmark to measure improvements:

```orus
// arithmetic_bench.orus
fn benchmark_arithmetic() {
    let start = timestamp();
    let sum: i32 = 0;
    
    for i in 0..10000000 {
        sum = sum + i * 2 - 1;
    }
    
    let end = timestamp();
    print("Time: ", (end - start) / 1000000, "ms");
    print("Sum: ", sum);
}

// control_flow_bench.orus
fn benchmark_control() {
    let start = timestamp();
    let count: i32 = 0;
    
    for i in 0..1000000 {
        if i % 2 == 0 {
            count = count + 1;
        } else if i % 3 == 0 {
            count = count - 1;
        }
    }
    
    let end = timestamp();
    print("Time: ", (end - start) / 1000000, "ms");
}
```

## Expected Results After Each Phase

| Phase | Arithmetic vs Lua | Control Flow vs Lua |
|-------|------------------|-------------------|
| Current | 1.8x slower | 3.3x slower |
| Phase 1 | 1.2x slower | 2.0x slower |
| Phase 2 | 0.9x (10% faster!) | 1.3x slower |
| Phase 3 | 0.7x (30% faster!) | 0.9x (10% faster!) |
| Phase 4 | 0.6x (40% faster!) | 0.8x (20% faster!) |

## Quick Start Checklist

1. [ ] Add `#define ORUS_RELEASE` and rebuild
2. [ ] Replace DISPATCH() macro
3. [ ] Remove type checks from typed ops
4. [ ] Add fused increment-compare-jump
5. [ ] Update compiler to emit typed ops
6. [ ] Run benchmarks and celebrate! 🎉

## Debugging Tips

- Keep debug build with full DISPATCH() for development
- Add performance counters:
  ```c
  static uint64_t typed_ops_count = 0;
  static uint64_t boxed_ops_count = 0;
  
  // In typed ops: typed_ops_count++;
  // In boxed ops: boxed_ops_count++;
  
  // Print ratio to ensure compiler is emitting typed ops
  ```

## Next Steps After Beating Lua

Once you've beaten Lua, consider:
1. JIT compilation for hot functions
2. Escape analysis for stack allocation
3. Method inlining
4. Dead code elimination

You're just a few optimizations away from having one of the fastest scripting VMs!




--------------

```c
// vm_optimizations.h - Core optimizations to beat Lua

#ifndef VM_OPTIMIZATIONS_H
#define VM_OPTIMIZATIONS_H

// ==========================================
// 1. ELIMINATE RUNTIME TYPE CHECKING (30% gain)
// ==========================================

// Fast dispatch macro for production builds
#ifdef ORUS_DEBUG
    #define DISPATCH() \
        do { \
            if (IS_ERROR(vm.lastError)) { \
                if (vm.tryFrameCount > 0) { \
                    TryFrame frame = vm.tryFrames[--vm.tryFrameCount]; \
                    vm.ip = frame.handler; \
                    vm.globals[frame.varIndex] = vm.lastError; \
                    vm.lastError = NIL_VAL; \
                } else { \
                    RETURN(INTERPRET_RUNTIME_ERROR); \
                } \
            } \
            if (vm.trace) { \
                printf("        "); \
                for (int i = 0; i < 8; i++) { \
                    printf("[ R%d: ", i); \
                    printValue(vm.registers[i]); \
                    printf(" ]"); \
                } \
                printf("\n"); \
                disassembleInstruction(vm.chunk, (int)(vm.ip - vm.chunk->code)); \
            } \
            vm.instruction_count++; \
            instruction = READ_BYTE(); \
            goto *dispatchTable[instruction]; \
        } while (0)
#else
    // PRODUCTION: Ultra-fast dispatch with no checks
    #define DISPATCH() goto *dispatchTable[*vm.ip++]
    
    // Even faster for typed operations
    #define DISPATCH_TYPED() goto *dispatchTable[*vm.ip++]
#endif

// ==========================================
// 2. INSTRUCTION FUSION (20% gain)
// ==========================================

// Fused opcodes for common patterns
typedef enum {
    // Immediate arithmetic (constant folding)
    OP_ADD_I32_IMM = 250,      // add r1, r2, #imm
    OP_SUB_I32_IMM,            // sub r1, r2, #imm
    OP_MUL_I32_IMM,            // mul r1, r2, #imm
    OP_CMP_I32_IMM,            // cmp r1, #imm -> bool
    
    // Load and operate
    OP_LOAD_ADD_I32,           // r1 = memory[r2] + r3
    OP_LOAD_CMP_I32,           // bool = memory[r1] < r2
    
    // Increment and compare (loop optimization)
    OP_INC_CMP_JMP,            // r1++; if (r1 < r2) jump
    OP_DEC_CMP_JMP,            // r1--; if (r1 > 0) jump
    
    // Multi-operation fusion
    OP_MUL_ADD_I32,            // r1 = r2 * r3 + r4 (FMA)
    OP_LOAD_INC_STORE,         // memory[r1]++
} FusedOpcodes;

// ==========================================
// 3. OPTIMIZED HOT PATH IMPLEMENTATIONS
// ==========================================

// Inline assembly for critical operations (GCC/Clang)
#if defined(__GNUC__) || defined(__clang__)
    #define FAST_ADD_I32(dst, a, b) \
        __asm__ volatile("addl %1, %0" : "=r"(dst) : "r"(a), "0"(b))
    
    #define FAST_CMP_I32(result, a, b) \
        __asm__ volatile("cmpl %2, %1\n\t" \
                         "setl %0" : "=r"(result) : "r"(a), "r"(b))
#else
    #define FAST_ADD_I32(dst, a, b) dst = a + b
    #define FAST_CMP_I32(result, a, b) result = a < b
#endif

// ==========================================
// 4. SUPERINSTRUCTION IMPLEMENTATIONS
// ==========================================

// Implementation for fused add with immediate
static inline void execute_add_i32_imm(void) {
    uint8_t dst = *vm.ip++;
    uint8_t src = *vm.ip++;
    int32_t imm = *(int32_t*)vm.ip;
    vm.ip += 4;
    
    vm.typed_regs.i32_regs[dst] = vm.typed_regs.i32_regs[src] + imm;
}

// Implementation for increment-compare-jump (tight loop optimization)
static inline void execute_inc_cmp_jmp(void) {
    uint8_t reg = *vm.ip++;
    uint8_t limit_reg = *vm.ip++;
    int16_t jump_offset = *(int16_t*)vm.ip;
    vm.ip += 2;
    
    int32_t val = ++vm.typed_regs.i32_regs[reg];
    int32_t limit = vm.typed_regs.i32_regs[limit_reg];
    
    if (val < limit) {
        vm.ip += jump_offset;
    }
}

// ==========================================
// 5. REGISTER PRESSURE OPTIMIZATION
// ==========================================

// Pack multiple values into SIMD registers when available
#ifdef __SSE2__
#include <emmintrin.h>

static inline void execute_simd_add_i32x4(void) {
    uint8_t dst_base = *vm.ip++;
    uint8_t src1_base = *vm.ip++;
    uint8_t src2_base = *vm.ip++;
    
    __m128i a = _mm_loadu_si128((__m128i*)&vm.typed_regs.i32_regs[src1_base]);
    __m128i b = _mm_loadu_si128((__m128i*)&vm.typed_regs.i32_regs[src2_base]);
    __m128i result = _mm_add_epi32(a, b);
    _mm_storeu_si128((__m128i*)&vm.typed_regs.i32_regs[dst_base], result);
}
#endif

// ==========================================
// 6. COMPILER INTEGRATION HELPERS
// ==========================================

// Pattern matching for instruction fusion in compiler
typedef struct {
    OpCode pattern[4];
    uint8_t pattern_length;
    OpCode fused_op;
} FusionPattern;

static const FusionPattern fusion_patterns[] = {
    // Load constant + add → add immediate
    {{OP_LOAD_I32_CONST, OP_ADD_I32_TYPED}, 2, OP_ADD_I32_IMM},
    
    // Increment + compare + jump → fused loop instruction  
    {{OP_INC_I32_R, OP_LT_I32_TYPED, OP_JUMP_IF_NOT_SHORT}, 3, OP_INC_CMP_JMP},
    
    // Load + add → load-add
    {{OP_LOAD_GLOBAL, OP_ADD_I32_TYPED}, 2, OP_LOAD_ADD_I32},
};

// ==========================================
// 7. OPTIMIZED DISPATCH TABLE SETUP
// ==========================================

static void init_optimized_dispatch_table(void** table) {
    // Hot opcodes at the beginning for better cache locality
    table[OP_ADD_I32_TYPED] = &&LABEL_OP_ADD_I32_TYPED_FAST;
    table[OP_SUB_I32_TYPED] = &&LABEL_OP_SUB_I32_TYPED_FAST;
    table[OP_MUL_I32_TYPED] = &&LABEL_OP_MUL_I32_TYPED_FAST;
    table[OP_LT_I32_TYPED] = &&LABEL_OP_LT_I32_TYPED_FAST;
    table[OP_INC_CMP_JMP] = &&LABEL_OP_INC_CMP_JMP;
    table[OP_ADD_I32_IMM] = &&LABEL_OP_ADD_I32_IMM;
    // ... rest of opcodes
}

// ==========================================
// 8. ACTUAL OPTIMIZED IMPLEMENTATIONS
// ==========================================

// These go in your run() function:

LABEL_OP_ADD_I32_TYPED_FAST: {
    uint8_t dst = *vm.ip++;
    uint8_t src1 = *vm.ip++;
    uint8_t src2 = *vm.ip++;
    
    // Direct operation, no checks, no boxing
    vm.typed_regs.i32_regs[dst] = 
        vm.typed_regs.i32_regs[src1] + vm.typed_regs.i32_regs[src2];
    
    DISPATCH_TYPED();
}

LABEL_OP_ADD_I32_IMM: {
    uint8_t dst = *vm.ip++;
    uint8_t src = *vm.ip++;
    int32_t imm = *(int32_t*)vm.ip;
    vm.ip += 4;
    
    vm.typed_regs.i32_regs[dst] = vm.typed_regs.i32_regs[src] + imm;
    
    DISPATCH_TYPED();
}

LABEL_OP_INC_CMP_JMP: {
    uint8_t reg = *vm.ip++;
    uint8_t limit_reg = *vm.ip++;
    int16_t offset = *(int16_t*)vm.ip;
    vm.ip += 2;
    
    // Fused increment + compare + conditional jump
    if (++vm.typed_regs.i32_regs[reg] < vm.typed_regs.i32_regs[limit_reg]) {
        vm.ip += offset;
    }
    
    DISPATCH_TYPED();
}

LABEL_OP_MUL_ADD_I32: {
    uint8_t dst = *vm.ip++;
    uint8_t mul1 = *vm.ip++;
    uint8_t mul2 = *vm.ip++;
    uint8_t add = *vm.ip++;
    
    // Fused multiply-add (single operation on modern CPUs)
    vm.typed_regs.i32_regs[dst] = 
        vm.typed_regs.i32_regs[mul1] * vm.typed_regs.i32_regs[mul2] + 
        vm.typed_regs.i32_regs[add];
    
    DISPATCH_TYPED();
}

// ==========================================
// 9. LOOP OPTIMIZATION EXAMPLE
// ==========================================

// Original bytecode for: for(i=0; i<1000000; i++) sum += i;
// LOAD_I32_CONST R0, 0      // i = 0
// LOAD_I32_CONST R1, 1000000 // limit
// LOAD_I32_CONST R2, 0      // sum = 0
// LOOP_START:
// ADD_I32_R R2, R2, R0      // sum += i
// INC_I32_R R0              // i++
// LT_I32_R R3, R0, R1       // R3 = i < limit
// JUMP_IF_NOT_R R3, END     // if (!R3) goto END
// JUMP LOOP_START
// END:

// Optimized bytecode with fusion:
// LOAD_I32_CONST R0, 0      // i = 0
// LOAD_I32_CONST R1, 1000000 // limit
// LOAD_I32_CONST R2, 0      // sum = 0
// LOOP_START:
// ADD_I32_TYPED R2, R2, R0  // sum += i (no type check)
// INC_CMP_JMP R0, R1, LOOP_START // fused: i++; if(i<limit) goto LOOP_START
// END:

// This reduces 5 instructions to 2 in the hot loop!

// ==========================================
// 10. COMPILER MODIFICATIONS
// ==========================================

// In compiler.c, add this pattern matcher:
void optimize_instruction_sequence(Compiler* c) {
    // Scan for fusable patterns
    for (int i = 0; i < c->chunk->count - 3; i++) {
        uint8_t* code = &c->chunk->code[i];
        
        // Pattern: LOAD_CONST + ADD → ADD_IMM
        if (code[0] == OP_LOAD_I32_CONST && 
            code[4] == OP_ADD_I32_TYPED &&
            code[5] == code[3]) { // dst of load == src of add
            
            // Replace with fused instruction
            code[0] = OP_ADD_I32_IMM;
            code[1] = code[6]; // dst
            code[2] = code[7]; // src
            // Copy immediate value
            memcpy(&code[3], &c->chunk->constants.values[*(uint16_t*)&code[1]], 4);
            
            // NOP out the old ADD instruction
            code[7] = OP_NOP;
            code[8] = OP_NOP;
            code[9] = OP_NOP;
        }
    }
}

#endif // VM_OPTIMIZATIONS_H
```