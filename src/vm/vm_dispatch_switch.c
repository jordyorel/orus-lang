#include "vm_dispatch.h"
#include "builtins.h"
#include <math.h>

// ✅ Auto-detect computed goto support
#ifndef USE_COMPUTED_GOTO
  #if defined(__GNUC__) || defined(__clang__)
    #define USE_COMPUTED_GOTO 1
  #else
    #define USE_COMPUTED_GOTO 0
  #endif
#endif

// These macros implement automatic i32 -> i64 promotion on overflow
// with zero-cost when overflow is not detected (hot path optimization)

#define HANDLE_I32_OVERFLOW_ADD(a, b, dst_reg) \
    do { \
        int32_t result; \
        if (unlikely(__builtin_add_overflow(a, b, &result))) { \
            /* Overflow detected: promote to i64 and continue */ \
            int64_t result64 = (int64_t)(a) + (int64_t)(b); \
            vm.registers[dst_reg] = I64_VAL(result64); \
        } else { \
            /* Hot path: no overflow, stay with i32 */ \
            vm.registers[dst_reg] = I32_VAL(result); \
        } \
    } while (0)

#define HANDLE_I32_OVERFLOW_SUB(a, b, dst_reg) \
    do { \
        int32_t result; \
        if (unlikely(__builtin_sub_overflow(a, b, &result))) { \
            /* Overflow detected: promote to i64 and continue */ \
            int64_t result64 = (int64_t)(a) - (int64_t)(b); \
            vm.registers[dst_reg] = I64_VAL(result64); \
        } else { \
            /* Hot path: no overflow, stay with i32 */ \
            vm.registers[dst_reg] = I32_VAL(result); \
        } \
    } while (0)

#define HANDLE_I32_OVERFLOW_MUL(a, b, dst_reg) \
    do { \
        int32_t result; \
        if (unlikely(__builtin_mul_overflow(a, b, &result))) { \
            /* Overflow detected: promote to i64 and continue */ \
            int64_t result64 = (int64_t)(a) * (int64_t)(b); \
            vm.registers[dst_reg] = I64_VAL(result64); \
        } else { \
            /* Hot path: no overflow, stay with i32 */ \
            vm.registers[dst_reg] = I32_VAL(result); \
        } \
    } while (0)

// Branch prediction hints for performance (following AGENTS.md performance-first principle)
#ifndef likely
#define likely(x)       __builtin_expect(!!(x), 1)
#endif
#ifndef unlikely
#define unlikely(x)     __builtin_expect(!!(x), 0)
#endif

// Mixed-type arithmetic: Intelligent type promotion for operations
// When i32 and i64 are mixed, promote result to i64
#define HANDLE_MIXED_ADD(val1, val2, dst_reg) \
    do { \
        if (IS_I32(val1) && IS_I32(val2)) { \
            HANDLE_I32_OVERFLOW_ADD(AS_I32(val1), AS_I32(val2), dst_reg); \
        } else if (IS_I64(val1) && IS_I64(val2)) { \
            /* Both i64: perform i64 arithmetic with overflow check */ \
            int64_t a = AS_I64(val1); \
            int64_t b = AS_I64(val2); \
            int64_t result; \
            if (unlikely(__builtin_add_overflow(a, b, &result))) { \
                runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                           "Integer overflow: result exceeds i64 range"); \
                RETURN(INTERPRET_RUNTIME_ERROR); \
            } \
            vm.registers[dst_reg] = I64_VAL(result); \
        } else { \
            /* Mixed types: promote to i64 */ \
            int64_t a = IS_I32(val1) ? (int64_t)AS_I32(val1) : AS_I64(val1); \
            int64_t b = IS_I32(val2) ? (int64_t)AS_I32(val2) : AS_I64(val2); \
            int64_t result; \
            if (unlikely(__builtin_add_overflow(a, b, &result))) { \
                runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                           "Integer overflow: result exceeds i64 range"); \
                RETURN(INTERPRET_RUNTIME_ERROR); \
            } \
            vm.registers[dst_reg] = I64_VAL(result); \
        } \
    } while (0)

#define HANDLE_MIXED_SUB(val1, val2, dst_reg) \
    do { \
        if (IS_I32(val1) && IS_I32(val2)) { \
            HANDLE_I32_OVERFLOW_SUB(AS_I32(val1), AS_I32(val2), dst_reg); \
        } else if (IS_I64(val1) && IS_I64(val2)) { \
            int64_t a = AS_I64(val1); \
            int64_t b = AS_I64(val2); \
            int64_t result; \
            if (unlikely(__builtin_sub_overflow(a, b, &result))) { \
                runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                           "Integer overflow: result exceeds i64 range"); \
                RETURN(INTERPRET_RUNTIME_ERROR); \
            } \
            vm.registers[dst_reg] = I64_VAL(result); \
        } else { \
            int64_t a = IS_I32(val1) ? (int64_t)AS_I32(val1) : AS_I64(val1); \
            int64_t b = IS_I32(val2) ? (int64_t)AS_I32(val2) : AS_I64(val2); \
            int64_t result; \
            if (unlikely(__builtin_sub_overflow(a, b, &result))) { \
                runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                           "Integer overflow: result exceeds i64 range"); \
                RETURN(INTERPRET_RUNTIME_ERROR); \
            } \
            vm.registers[dst_reg] = I64_VAL(result); \
        } \
    } while (0)

#define HANDLE_MIXED_MUL(val1, val2, dst_reg) \
    do { \
        if (IS_I32(val1) && IS_I32(val2)) { \
            HANDLE_I32_OVERFLOW_MUL(AS_I32(val1), AS_I32(val2), dst_reg); \
        } else if (IS_I64(val1) && IS_I64(val2)) { \
            int64_t a = AS_I64(val1); \
            int64_t b = AS_I64(val2); \
            int64_t result; \
            if (unlikely(__builtin_mul_overflow(a, b, &result))) { \
                runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                           "Integer overflow: result exceeds i64 range"); \
                RETURN(INTERPRET_RUNTIME_ERROR); \
            } \
            vm.registers[dst_reg] = I64_VAL(result); \
        } else { \
            int64_t a = IS_I32(val1) ? (int64_t)AS_I32(val1) : AS_I64(val1); \
            int64_t b = IS_I32(val2) ? (int64_t)AS_I32(val2) : AS_I64(val2); \
            int64_t result; \
            if (unlikely(__builtin_mul_overflow(a, b, &result))) { \
                runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                           "Integer overflow: result exceeds i64 range"); \
                RETURN(INTERPRET_RUNTIME_ERROR); \
            } \
            vm.registers[dst_reg] = I64_VAL(result); \
        } \
    } while (0)

#if !USE_COMPUTED_GOTO

InterpretResult vm_run_dispatch(void) {
    double start_time = get_time_vm();
    #define RETURN(val) \
        do { \
            vm.lastExecutionTime = get_time_vm() - start_time; \
            return (val); \
        } while (0)

        for (;;) {
            if (vm.trace) {
                // Debug trace
                printf("        ");
                for (int i = 0; i < 8; i++) {
                    printf("[ R%d: ", i);
                    printValue(vm.registers[i]);
                    printf(" ]");
                }
                printf("\n");

                disassembleInstruction(vm.chunk, (int)(vm.ip - vm.chunk->code));
            }

            vm.instruction_count++;

            uint8_t instruction = READ_BYTE();

            switch (instruction) {
                case OP_LOAD_CONST: {
                    uint8_t reg = READ_BYTE();
                    uint16_t constantIndex = READ_SHORT();
                    vm.registers[reg] = READ_CONSTANT(constantIndex);
                    break;
                }

                case OP_LOAD_NIL: {
                    uint8_t reg = READ_BYTE();
                    vm.registers[reg] = NIL_VAL;
                    break;
                }

                case OP_LOAD_TRUE: {
                    uint8_t reg = READ_BYTE();
                    vm.registers[reg] = BOOL_VAL(true);
                    break;
                }

                case OP_LOAD_FALSE: {
                    uint8_t reg = READ_BYTE();
                    vm.registers[reg] = BOOL_VAL(false);
                    break;
                }

                case OP_MOVE: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    vm.registers[dst] = vm.registers[src];
                    break;
                }

                case OP_LOAD_GLOBAL: {
                    uint8_t reg = READ_BYTE();
                    uint8_t globalIndex = READ_BYTE();
                    if (globalIndex >= vm.variableCount ||
                        vm.globalTypes[globalIndex] == NULL) {
                        runtimeError(ERROR_NAME, (SrcLocation){NULL, 0, 0},
                                    "Undefined variable");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }
                    vm.registers[reg] = vm.globals[globalIndex];
                    break;
                }

                case OP_STORE_GLOBAL: {
                    uint8_t globalIndex = READ_BYTE();
                    uint8_t reg = READ_BYTE();
                    vm.globals[globalIndex] = vm.registers[reg];
                    break;
                }

                // Arithmetic operations with intelligent overflow handling
                case OP_ADD_I32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    // Type validation with mixed-type support
                    if (!(IS_I32(vm.registers[src1]) || IS_I64(vm.registers[src1])) ||
                        !(IS_I32(vm.registers[src2]) || IS_I64(vm.registers[src2]))) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Operands must be integers (i32 or i64)");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }

#if USE_FAST_ARITH
                    // Fast path: assume i32, no overflow checking
                    int32_t a = AS_I32(vm.registers[src1]);
                    int32_t b = AS_I32(vm.registers[src2]);
                    vm.registers[dst] = I32_VAL(a + b);
#else
                    // Intelligent overflow handling with automatic promotion
                    HANDLE_MIXED_ADD(vm.registers[src1], vm.registers[src2], dst);
#endif
                    break;
                }

                case OP_SUB_I32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    // Type validation with mixed-type support
                    if (!(IS_I32(vm.registers[src1]) || IS_I64(vm.registers[src1])) ||
                        !(IS_I32(vm.registers[src2]) || IS_I64(vm.registers[src2]))) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Operands must be integers (i32 or i64)");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }

#if USE_FAST_ARITH
                    // Fast path: assume i32, no overflow checking
                    int32_t a = AS_I32(vm.registers[src1]);
                    int32_t b = AS_I32(vm.registers[src2]);
                    vm.registers[dst] = I32_VAL(a - b);
#else
                    // Intelligent overflow handling with automatic promotion
                    HANDLE_MIXED_SUB(vm.registers[src1], vm.registers[src2], dst);
#endif
                    break;
                }

                case OP_MUL_I32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    // Type validation with mixed-type support
                    if (!(IS_I32(vm.registers[src1]) || IS_I64(vm.registers[src1])) ||
                        !(IS_I32(vm.registers[src2]) || IS_I64(vm.registers[src2]))) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Operands must be integers (i32 or i64)");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }

#if USE_FAST_ARITH
                    // Fast path: assume i32, no overflow checking
                    int32_t a = AS_I32(vm.registers[src1]);
                    int32_t b = AS_I32(vm.registers[src2]);
                    vm.registers[dst] = I32_VAL(a * b);
#else
                    // Intelligent overflow handling with automatic promotion
                    HANDLE_MIXED_MUL(vm.registers[src1], vm.registers[src2], dst);
#endif
                    break;
                }

                case OP_DIV_I32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    // Type validation with mixed-type support
                    if (!(IS_I32(vm.registers[src1]) || IS_I64(vm.registers[src1])) ||
                        !(IS_I32(vm.registers[src2]) || IS_I64(vm.registers[src2]))) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Operands must be i32 or i64");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }

                    // Use mixed-type division handling
                    HANDLE_MIXED_DIV(vm.registers[src1], vm.registers[src2], dst);
                    break;
                }

                case OP_MOD_I32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    // Type validation with mixed-type support
                    if (!(IS_I32(vm.registers[src1]) || IS_I64(vm.registers[src1])) ||
                        !(IS_I32(vm.registers[src2]) || IS_I64(vm.registers[src2]))) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Operands must be i32 or i64");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }

                    // Use mixed-type modulo handling
                    HANDLE_MIXED_MOD(vm.registers[src1], vm.registers[src2], dst);
                    break;
                }

                case OP_INC_I32_R: {
                    uint8_t reg = READ_BYTE();
    #if USE_FAST_ARITH
                    vm.registers[reg] = I32_VAL(AS_I32(vm.registers[reg]) + 1);
    #else
                    int32_t val = AS_I32(vm.registers[reg]);
                    int32_t result;
                    if (__builtin_add_overflow(val, 1, &result)) {
                        runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                    "Integer overflow");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }
                    vm.registers[reg] = I32_VAL(result);
    #endif
                    break;
                }

                case OP_DEC_I32_R: {
                    uint8_t reg = READ_BYTE();
    #if USE_FAST_ARITH
                    vm.registers[reg] = I32_VAL(AS_I32(vm.registers[reg]) - 1);
    #else
                    int32_t val = AS_I32(vm.registers[reg]);
                    int32_t result;
                    if (__builtin_sub_overflow(val, 1, &result)) {
                        runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                    "Integer overflow");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }
                    vm.registers[reg] = I32_VAL(result);
    #endif
                    break;
                }

                // I64 arithmetic operations
                case OP_ADD_I64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (!IS_I64(vm.registers[src1]) ||
                        !IS_I64(vm.registers[src2])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Operands must be i64");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }

                    int64_t a = AS_I64(vm.registers[src1]);
                    int64_t b = AS_I64(vm.registers[src2]);
    #if USE_FAST_ARITH
                    vm.registers[dst] = I64_VAL(a + b);
    #else
                    int64_t result;
                    if (__builtin_add_overflow(a, b, &result)) {
                        runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                    "Integer overflow");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }
                    vm.registers[dst] = I64_VAL(result);
    #endif
                    break;
                }

                case OP_SUB_I64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (!IS_I64(vm.registers[src1]) ||
                        !IS_I64(vm.registers[src2])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Operands must be i64");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }

                    int64_t a = AS_I64(vm.registers[src1]);
                    int64_t b = AS_I64(vm.registers[src2]);
    #if USE_FAST_ARITH
                    vm.registers[dst] = I64_VAL(a - b);
    #else
                    int64_t result;
                    if (__builtin_sub_overflow(a, b, &result)) {
                        runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                    "Integer overflow");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }
                    vm.registers[dst] = I64_VAL(result);
    #endif
                    break;
                }

                case OP_MUL_I64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (!IS_I64(vm.registers[src1]) ||
                        !IS_I64(vm.registers[src2])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Operands must be i64");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }

                    int64_t a = AS_I64(vm.registers[src1]);
                    int64_t b = AS_I64(vm.registers[src2]);
    #if USE_FAST_ARITH
                    vm.registers[dst] = I64_VAL(a * b);
    #else
                    int64_t result;
                    if (__builtin_mul_overflow(a, b, &result)) {
                        runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                    "Integer overflow");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }
                    vm.registers[dst] = I64_VAL(result);
    #endif
                    break;
                }

                case OP_DIV_I64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (!IS_I64(vm.registers[src1]) ||
                        !IS_I64(vm.registers[src2])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Operands must be i64");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }

                    int64_t b = AS_I64(vm.registers[src2]);
                    if (b == 0) {
                        runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                    "Division by zero");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }

                    vm.registers[dst] = I64_VAL(AS_I64(vm.registers[src1]) / b);
                    break;
                }

                case OP_MOD_I64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (!IS_I64(vm.registers[src1]) ||
                        !IS_I64(vm.registers[src2])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Operands must be i64");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }

                    int64_t b = AS_I64(vm.registers[src2]);
                    if (b == 0) {
                        runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                    "Division by zero");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }

                    vm.registers[dst] = I64_VAL(AS_I64(vm.registers[src1]) % b);
                    break;
                }

                case OP_ADD_U32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (!IS_U32(vm.registers[src1]) || !IS_U32(vm.registers[src2])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Operands must be u32");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }

                    uint32_t a = AS_U32(vm.registers[src1]);
                    uint32_t b = AS_U32(vm.registers[src2]);
                    
                    // Check for overflow: if a + b < a, then overflow occurred
                    if (UINT32_MAX - a < b) {
                        runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                    "u32 addition overflow");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }

                    vm.registers[dst] = U32_VAL(a + b);
                    break;
                }

                case OP_SUB_U32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (!IS_U32(vm.registers[src1]) || !IS_U32(vm.registers[src2])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Operands must be u32");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }

                    uint32_t a = AS_U32(vm.registers[src1]);
                    uint32_t b = AS_U32(vm.registers[src2]);
                    
                    // Check for underflow: if a < b, then underflow would occur
                    if (a < b) {
                        runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                    "u32 subtraction underflow");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }

                    vm.registers[dst] = U32_VAL(a - b);
                    break;
                }

                case OP_MUL_U32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (!IS_U32(vm.registers[src1]) || !IS_U32(vm.registers[src2])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Operands must be u32");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }

                    uint32_t a = AS_U32(vm.registers[src1]);
                    uint32_t b = AS_U32(vm.registers[src2]);
                    
                    // Check for multiplication overflow: if a != 0 && result / a != b
                    if (a != 0 && b > UINT32_MAX / a) {
                        runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                    "u32 multiplication overflow");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }

                    vm.registers[dst] = U32_VAL(a * b);
                    break;
                }

                case OP_DIV_U32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (!IS_U32(vm.registers[src1]) || !IS_U32(vm.registers[src2])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Operands must be u32");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }

                    uint32_t b = AS_U32(vm.registers[src2]);
                    if (b == 0) {
                        runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                    "Division by zero");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }

                    vm.registers[dst] = U32_VAL(AS_U32(vm.registers[src1]) / b);
                    break;
                }

                case OP_MOD_U32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (!IS_U32(vm.registers[src1]) || !IS_U32(vm.registers[src2])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Operands must be u32");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }

                    uint32_t b = AS_U32(vm.registers[src2]);
                    if (b == 0) {
                        runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                    "Division by zero");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }

                    vm.registers[dst] = U32_VAL(AS_U32(vm.registers[src1]) % b);
                    break;
                }

                case OP_ADD_U64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (!IS_U64(vm.registers[src1]) || !IS_U64(vm.registers[src2])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Operands must be u64");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }

                    uint64_t a = AS_U64(vm.registers[src1]);
                    uint64_t b = AS_U64(vm.registers[src2]);
                    
                    // Check for overflow: if a + b < a, then overflow occurred
                    if (UINT64_MAX - a < b) {
                        runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                    "u64 addition overflow");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }

                    vm.registers[dst] = U64_VAL(a + b);
                    break;
                }

                case OP_SUB_U64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (!IS_U64(vm.registers[src1]) || !IS_U64(vm.registers[src2])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Operands must be u64");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }

                    uint64_t a = AS_U64(vm.registers[src1]);
                    uint64_t b = AS_U64(vm.registers[src2]);
                    
                    // Check for underflow: if a < b, then underflow would occur
                    if (a < b) {
                        runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                    "u64 subtraction underflow");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }

                    vm.registers[dst] = U64_VAL(a - b);
                    break;
                }

                case OP_MUL_U64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (!IS_U64(vm.registers[src1]) || !IS_U64(vm.registers[src2])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Operands must be u64");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }

                    uint64_t a = AS_U64(vm.registers[src1]);
                    uint64_t b = AS_U64(vm.registers[src2]);
                    
                    // Check for multiplication overflow: if a != 0 && result / a != b
                    if (a != 0 && b > UINT64_MAX / a) {
                        runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                    "u64 multiplication overflow");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }

                    vm.registers[dst] = U64_VAL(a * b);
                    break;
                }

                case OP_DIV_U64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (!IS_U64(vm.registers[src1]) || !IS_U64(vm.registers[src2])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Operands must be u64");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }

                    uint64_t b = AS_U64(vm.registers[src2]);
                    if (b == 0) {
                        runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                    "Division by zero");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }

                    vm.registers[dst] = U64_VAL(AS_U64(vm.registers[src1]) / b);
                    break;
                }

                case OP_MOD_U64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (!IS_U64(vm.registers[src1]) || !IS_U64(vm.registers[src2])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Operands must be u64");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }

                    uint64_t b = AS_U64(vm.registers[src2]);
                    if (b == 0) {
                        runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                    "Division by zero");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }

                    vm.registers[dst] = U64_VAL(AS_U64(vm.registers[src1]) % b);
                    break;
                }

                case OP_I32_TO_I64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    if (!IS_I32(vm.registers[src])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Source must be i32");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }
                    vm.registers[dst] = I64_VAL((int64_t)AS_I32(vm.registers[src]));
                    break;
                }

                case OP_I32_TO_U32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    if (!IS_I32(vm.registers[src])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Source must be i32");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }
                    vm.registers[dst] = U32_VAL((uint32_t)AS_I32(vm.registers[src]));
                    break;
                }

                case OP_U32_TO_I32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    if (!IS_U32(vm.registers[src])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Source must be u32");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }
                    vm.registers[dst] = I32_VAL((int32_t)AS_U32(vm.registers[src]));
                    break;
                }

                case OP_F64_TO_U32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    if (!IS_F64(vm.registers[src])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Source must be f64");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }
                    double val = AS_F64(vm.registers[src]);
                    if (val < 0.0 || val > (double)UINT32_MAX) {
                        runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                    "f64 value out of u32 range");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }
                    vm.registers[dst] = U32_VAL((uint32_t)val);
                    break;
                }

                case OP_U32_TO_F64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    if (!IS_U32(vm.registers[src])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Source must be u32");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }
                    vm.registers[dst] = F64_VAL((double)AS_U32(vm.registers[src]));
                    break;
                }

                case OP_I32_TO_U64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    if (!IS_I32(vm.registers[src])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Source must be i32");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }
                    int32_t val = AS_I32(vm.registers[src]);
                    if (val < 0) {
                        runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                    "Cannot convert negative i32 to u64");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }
                    vm.registers[dst] = U64_VAL((uint64_t)val);
                    break;
                }

                case OP_I64_TO_U64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    if (!IS_I64(vm.registers[src])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Source must be i64");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }
                    int64_t val = AS_I64(vm.registers[src]);
                    if (val < 0) {
                        runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                    "Cannot convert negative i64 to u64");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }
                    vm.registers[dst] = U64_VAL((uint64_t)val);
                    break;
                }

                case OP_U64_TO_I32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    if (!IS_U64(vm.registers[src])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Source must be u64");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }
                    uint64_t val = AS_U64(vm.registers[src]);
                    if (val > (uint64_t)INT32_MAX) {
                        runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                    "u64 value too large for i32");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }
                    vm.registers[dst] = I32_VAL((int32_t)val);
                    break;
                }

                case OP_U64_TO_I64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    if (!IS_U64(vm.registers[src])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Source must be u64");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }
                    uint64_t val = AS_U64(vm.registers[src]);
                    if (val > (uint64_t)INT64_MAX) {
                        runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                    "u64 value too large for i64");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }
                    vm.registers[dst] = I64_VAL((int64_t)val);
                    break;
                }

                case OP_U32_TO_U64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    if (!IS_U32(vm.registers[src])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Source must be u32");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }
                    vm.registers[dst] = U64_VAL((uint64_t)AS_U32(vm.registers[src]));
                    break;
                }

                case OP_U64_TO_U32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    if (!IS_U64(vm.registers[src])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Source must be u64");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }
                    uint64_t val = AS_U64(vm.registers[src]);
                    if (val > (uint64_t)UINT32_MAX) {
                        runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                    "u64 value too large for u32");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }
                    vm.registers[dst] = U32_VAL((uint32_t)val);
                    break;
                }

                case OP_F64_TO_U64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    if (!IS_F64(vm.registers[src])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Source must be f64");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }
                    double val = AS_F64(vm.registers[src]);
                    if (val < 0.0 || val > (double)UINT64_MAX) {
                        runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                    "f64 value out of u64 range");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }
                    vm.registers[dst] = U64_VAL((uint64_t)val);
                    break;
                }

                case OP_U64_TO_F64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    if (!IS_U64(vm.registers[src])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Source must be u64");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }
                    vm.registers[dst] = F64_VAL((double)AS_U64(vm.registers[src]));
                    break;
                }

                // F64 Arithmetic Operations
                case OP_ADD_F64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    if (!IS_F64(vm.registers[src1]) || !IS_F64(vm.registers[src2])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be f64");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    vm.registers[dst] = F64_VAL(AS_F64(vm.registers[src1]) + AS_F64(vm.registers[src2]));
                    break;
                }

                case OP_SUB_F64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    if (!IS_F64(vm.registers[src1]) || !IS_F64(vm.registers[src2])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be f64");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    vm.registers[dst] = F64_VAL(AS_F64(vm.registers[src1]) - AS_F64(vm.registers[src2]));
                    break;
                }

                case OP_MUL_F64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    if (!IS_F64(vm.registers[src1]) || !IS_F64(vm.registers[src2])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be f64");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    vm.registers[dst] = F64_VAL(AS_F64(vm.registers[src1]) * AS_F64(vm.registers[src2]));
                    break;
                }

                case OP_DIV_F64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    if (!IS_F64(vm.registers[src1]) || !IS_F64(vm.registers[src2])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be f64");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    double a = AS_F64(vm.registers[src1]);
                    double b = AS_F64(vm.registers[src2]);
                    
                    // IEEE 754 compliant: division by zero produces infinity, not error
                    double result = a / b;
                    
                    // The result may be infinity, -infinity, or NaN
                    // These are valid f64 values according to IEEE 754
                    vm.registers[dst] = F64_VAL(result);
                    break;
                }

                case OP_MOD_F64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    if (!IS_F64(vm.registers[src1]) || !IS_F64(vm.registers[src2])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be f64");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    double a = AS_F64(vm.registers[src1]);
                    double b = AS_F64(vm.registers[src2]);
                    
                    // IEEE 754 compliant: use fmod for floating point modulo
                    double result = fmod(a, b);
                    
                    // The result may be infinity, -infinity, or NaN
                    // These are valid f64 values according to IEEE 754
                    vm.registers[dst] = F64_VAL(result);
                    break;
                }

                // Bitwise Operations
                case OP_AND_I32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    if (!IS_I32(vm.registers[src1]) || !IS_I32(vm.registers[src2])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i32");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    vm.registers[dst] = I32_VAL(AS_I32(vm.registers[src1]) & AS_I32(vm.registers[src2]));
                    break;
                }

                case OP_OR_I32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    if (!IS_I32(vm.registers[src1]) || !IS_I32(vm.registers[src2])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i32");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    vm.registers[dst] = I32_VAL(AS_I32(vm.registers[src1]) | AS_I32(vm.registers[src2]));
                    break;
                }

                case OP_XOR_I32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    if (!IS_I32(vm.registers[src1]) || !IS_I32(vm.registers[src2])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i32");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    vm.registers[dst] = I32_VAL(AS_I32(vm.registers[src1]) ^ AS_I32(vm.registers[src2]));
                    break;
                }

                case OP_NOT_I32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    if (!IS_I32(vm.registers[src])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operand must be i32");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    vm.registers[dst] = I32_VAL(~AS_I32(vm.registers[src]));
                    break;
                }

                case OP_SHL_I32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    if (!IS_I32(vm.registers[src1]) || !IS_I32(vm.registers[src2])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i32");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    vm.registers[dst] = I32_VAL(AS_I32(vm.registers[src1]) << AS_I32(vm.registers[src2]));
                    break;
                }

                case OP_SHR_I32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    if (!IS_I32(vm.registers[src1]) || !IS_I32(vm.registers[src2])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i32");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    vm.registers[dst] = I32_VAL(AS_I32(vm.registers[src1]) >> AS_I32(vm.registers[src2]));
                    break;
                }

                // F64 Comparison Operations
                case OP_LT_F64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    if (!IS_F64(vm.registers[src1]) || !IS_F64(vm.registers[src2])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be f64");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    vm.registers[dst] = BOOL_VAL(AS_F64(vm.registers[src1]) < AS_F64(vm.registers[src2]));
                    break;
                }

                case OP_LE_F64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    if (!IS_F64(vm.registers[src1]) || !IS_F64(vm.registers[src2])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be f64");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    vm.registers[dst] = BOOL_VAL(AS_F64(vm.registers[src1]) <= AS_F64(vm.registers[src2]));
                    break;
                }

                case OP_GT_F64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    if (!IS_F64(vm.registers[src1]) || !IS_F64(vm.registers[src2])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be f64");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    vm.registers[dst] = BOOL_VAL(AS_F64(vm.registers[src1]) > AS_F64(vm.registers[src2]));
                    break;
                }

                case OP_GE_F64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    if (!IS_F64(vm.registers[src1]) || !IS_F64(vm.registers[src2])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be f64");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    vm.registers[dst] = BOOL_VAL(AS_F64(vm.registers[src1]) >= AS_F64(vm.registers[src2]));
                    break;
                }

                // F64 Type Conversion Operations
                case OP_I32_TO_F64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    if (!IS_I32(vm.registers[src])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Source must be i32");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    vm.registers[dst] = F64_VAL((double)AS_I32(vm.registers[src]));
                    break;
                }

                case OP_I64_TO_F64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    if (!IS_I64(vm.registers[src])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Source must be i64");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    vm.registers[dst] = F64_VAL((double)AS_I64(vm.registers[src]));
                    break;
                }

                case OP_F64_TO_I32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    if (!IS_F64(vm.registers[src])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Source must be f64");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    vm.registers[dst] = I32_VAL((int32_t)AS_F64(vm.registers[src]));
                    break;
                }

                case OP_F64_TO_I64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    if (!IS_F64(vm.registers[src])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Source must be f64");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    vm.registers[dst] = I64_VAL((int64_t)AS_F64(vm.registers[src]));
                    break;
                }

                // Comparison operations
                case OP_LT_I32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (!IS_I32(vm.registers[src1]) ||
                        !IS_I32(vm.registers[src2])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Operands must be i32");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }

                    vm.registers[dst] = BOOL_VAL(AS_I32(vm.registers[src1]) <
                                                AS_I32(vm.registers[src2]));
                    break;
                }

                case OP_LE_I32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (!IS_I32(vm.registers[src1]) || !IS_I32(vm.registers[src2])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Operands must be i32");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }

                    vm.registers[dst] = BOOL_VAL(AS_I32(vm.registers[src1]) <=
                                                AS_I32(vm.registers[src2]));
                    break;
                }

                case OP_GT_I32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (!IS_I32(vm.registers[src1]) || !IS_I32(vm.registers[src2])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Operands must be i32");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }

                    vm.registers[dst] = BOOL_VAL(AS_I32(vm.registers[src1]) >
                                                AS_I32(vm.registers[src2]));
                    break;
                }

                case OP_GE_I32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (!IS_I32(vm.registers[src1]) || !IS_I32(vm.registers[src2])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Operands must be i32");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }

                    vm.registers[dst] = BOOL_VAL(AS_I32(vm.registers[src1]) >=
                                                AS_I32(vm.registers[src2]));
                    break;
                }

                // I64 comparison operations
                case OP_LT_I64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (!IS_I64(vm.registers[src1]) ||
                        !IS_I64(vm.registers[src2])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Operands must be i64");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }

                    vm.registers[dst] = BOOL_VAL(AS_I64(vm.registers[src1]) <
                                                AS_I64(vm.registers[src2]));
                    break;
                }

                case OP_LE_I64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (!IS_I64(vm.registers[src1]) || !IS_I64(vm.registers[src2])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Operands must be i64");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }

                    vm.registers[dst] = BOOL_VAL(AS_I64(vm.registers[src1]) <=
                                                AS_I64(vm.registers[src2]));
                    break;
                }

                case OP_GT_I64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (!IS_I64(vm.registers[src1]) || !IS_I64(vm.registers[src2])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Operands must be i64");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }

                    vm.registers[dst] = BOOL_VAL(AS_I64(vm.registers[src1]) >
                                                AS_I64(vm.registers[src2]));
                    break;
                }

                case OP_GE_I64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (!IS_I64(vm.registers[src1]) || !IS_I64(vm.registers[src2])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Operands must be i64");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }


                    vm.registers[dst] = BOOL_VAL(AS_I64(vm.registers[src1]) >=
                                                AS_I64(vm.registers[src2]));
                    break;
                }

                case OP_LT_U32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    if (!IS_U32(vm.registers[src1]) || !IS_U32(vm.registers[src2])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Operands must be u32");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }
                    vm.registers[dst] = BOOL_VAL(AS_U32(vm.registers[src1]) < AS_U32(vm.registers[src2]));
                    break;
                }

                case OP_LE_U32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    if (!IS_U32(vm.registers[src1]) || !IS_U32(vm.registers[src2])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Operands must be u32");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }
                    vm.registers[dst] = BOOL_VAL(AS_U32(vm.registers[src1]) <= AS_U32(vm.registers[src2]));
                    break;
                }

                case OP_GT_U32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    if (!IS_U32(vm.registers[src1]) || !IS_U32(vm.registers[src2])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Operands must be u32");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }
                    vm.registers[dst] = BOOL_VAL(AS_U32(vm.registers[src1]) > AS_U32(vm.registers[src2]));
                    break;
                }

                case OP_GE_U32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    if (!IS_U32(vm.registers[src1]) || !IS_U32(vm.registers[src2])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Operands must be u32");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }
                    vm.registers[dst] = BOOL_VAL(AS_U32(vm.registers[src1]) >= AS_U32(vm.registers[src2]));
                    break;
                }

                case OP_LT_U64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    if (!IS_U64(vm.registers[src1]) || !IS_U64(vm.registers[src2])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Operands must be u64");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }
                    vm.registers[dst] = BOOL_VAL(AS_U64(vm.registers[src1]) < AS_U64(vm.registers[src2]));
                    break;
                }

                case OP_LE_U64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    if (!IS_U64(vm.registers[src1]) || !IS_U64(vm.registers[src2])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Operands must be u64");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }
                    vm.registers[dst] = BOOL_VAL(AS_U64(vm.registers[src1]) <= AS_U64(vm.registers[src2]));
                    break;
                }

                case OP_GT_U64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    if (!IS_U64(vm.registers[src1]) || !IS_U64(vm.registers[src2])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Operands must be u64");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }
                    vm.registers[dst] = BOOL_VAL(AS_U64(vm.registers[src1]) > AS_U64(vm.registers[src2]));
                    break;
                }

                case OP_GE_U64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    if (!IS_U64(vm.registers[src1]) || !IS_U64(vm.registers[src2])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Operands must be u64");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }
                    vm.registers[dst] = BOOL_VAL(AS_U64(vm.registers[src1]) >= AS_U64(vm.registers[src2]));
                    break;
                }

                case OP_EQ_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    vm.registers[dst] = BOOL_VAL(
                        valuesEqual(vm.registers[src1], vm.registers[src2]));
                    break;
                }

                case OP_NE_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    vm.registers[dst] = BOOL_VAL(
                        !valuesEqual(vm.registers[src1], vm.registers[src2]));
                    break;
                }

                case OP_AND_BOOL_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (!IS_BOOL(vm.registers[src1]) || !IS_BOOL(vm.registers[src2])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Operands must be bool");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }

                    vm.registers[dst] = BOOL_VAL(AS_BOOL(vm.registers[src1]) &&
                                                AS_BOOL(vm.registers[src2]));
                    break;
                }

                case OP_OR_BOOL_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (!IS_BOOL(vm.registers[src1]) || !IS_BOOL(vm.registers[src2])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Operands must be bool");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }

                    vm.registers[dst] = BOOL_VAL(AS_BOOL(vm.registers[src1]) ||
                                                AS_BOOL(vm.registers[src2]));
                    break;
                }

                case OP_NOT_BOOL_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();

                    if (!IS_BOOL(vm.registers[src])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Operand must be bool");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }

                    vm.registers[dst] = BOOL_VAL(!AS_BOOL(vm.registers[src]));
                    break;
                }

                case OP_CONCAT_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (!IS_STRING(vm.registers[src1]) || !IS_STRING(vm.registers[src2])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Operands must be string");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }

                    ObjString* a = AS_STRING(vm.registers[src1]);
                    ObjString* b = AS_STRING(vm.registers[src2]);
                    int newLen = a->length + b->length;
                    char* buf = malloc(newLen + 1);
                    memcpy(buf, a->chars, a->length);
                    memcpy(buf + a->length, b->chars, b->length);
                    buf[newLen] = '\0';
                    ObjString* res = allocateString(buf, newLen);
                    free(buf);
                    vm.registers[dst] = STRING_VAL(res);
                    break;
                }

                // Control flow
                case OP_JUMP: {
                    uint16_t offset = READ_SHORT();
                    vm.ip += offset;
                    break;
                }

                case OP_JUMP_IF_NOT_R: {
                    uint8_t reg = READ_BYTE();
                    uint16_t offset = READ_SHORT();

                    if (!IS_BOOL(vm.registers[reg])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Condition must be boolean");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }

                    if (!AS_BOOL(vm.registers[reg])) {
                        vm.ip += offset;
                    }
                    break;
                }

                case OP_LOOP: {
                    uint16_t offset = READ_SHORT();
                    vm.ip -= offset;
                    break;
                }

                // I/O operations
                case OP_PRINT_MULTI_R: {
                    uint8_t first = READ_BYTE();
                    uint8_t count = READ_BYTE();
                    uint8_t nl = READ_BYTE();

                    builtin_print(&vm.registers[first], count, nl != 0);
                    break;
                }

                case OP_PRINT_R: {
                    uint8_t reg = READ_BYTE();
                    builtin_print(&vm.registers[reg], 1, true);
                    break;
                }

                case OP_PRINT_NO_NL_R: {
                    uint8_t reg = READ_BYTE();
                    builtin_print(&vm.registers[reg], 1, false);
                    break;
                }

                // Function operations
                case OP_CALL_R: {
                    uint8_t funcReg = READ_BYTE();
                    uint8_t firstArgReg = READ_BYTE();
                    uint8_t argCount = READ_BYTE();
                    uint8_t resultReg = READ_BYTE();
                    
                    Value funcValue = vm.registers[funcReg];
                    
                    if (IS_I32(funcValue)) {
                        int functionIndex = AS_I32(funcValue);
                        
                        if (functionIndex < 0 || functionIndex >= vm.functionCount) {
                            vm.registers[resultReg] = NIL_VAL;
                            break;
                        }
                        
                        Function* function = &vm.functions[functionIndex];
                        
                        // Check arity
                        if (argCount != function->arity) {
                            vm.registers[resultReg] = NIL_VAL;
                            break;
                        }
                        
                        // Check if we have room for another call frame
                        if (vm.frameCount >= FRAMES_MAX) {
                            vm.registers[resultReg] = NIL_VAL;
                            break;
                        }
                        
                        // Create new call frame
                        CallFrame* frame = &vm.frames[vm.frameCount++];
                        frame->returnAddress = vm.ip;
                        frame->previousChunk = vm.chunk;
                        frame->baseRegister = resultReg;
                        frame->registerCount = argCount;
                        frame->functionIndex = functionIndex;
                        
                        // Copy arguments to function's parameter registers
                        for (int i = 0; i < argCount; i++) {
                            vm.registers[i] = vm.registers[firstArgReg + i];
                        }
                        
                        // Switch to function's chunk
                        vm.chunk = function->chunk;
                        vm.ip = function->chunk->code + function->start;
                        
                    } else {
                        vm.registers[resultReg] = NIL_VAL;
                    }
                    break;
                }
                case OP_TAIL_CALL_R: {
                    uint8_t funcReg = READ_BYTE();
                    uint8_t firstArgReg = READ_BYTE();
                    uint8_t argCount = READ_BYTE();
                    uint8_t resultReg = READ_BYTE();
                    
                    Value funcValue = vm.registers[funcReg];
                    
                    if (IS_I32(funcValue)) {
                        int functionIndex = AS_I32(funcValue);
                        
                        if (functionIndex < 0 || functionIndex >= vm.functionCount) {
                            vm.registers[resultReg] = NIL_VAL;
                            break;
                        }
                        
                        Function* function = &vm.functions[functionIndex];
                        
                        // Check arity
                        if (argCount != function->arity) {
                            vm.registers[resultReg] = NIL_VAL;
                            break;
                        }
                        
                        // For tail calls, we reuse the current frame instead of creating a new one
                        // This prevents stack growth in recursive calls
                        
                        // Copy arguments to function's parameter registers
                        // We need to be careful about overlapping registers
                        Value tempArgs[256];
                        for (int i = 0; i < argCount; i++) {
                            tempArgs[i] = vm.registers[firstArgReg + i];
                        }
                        
                        // Clear the parameter registers first
                        for (int i = 0; i < argCount; i++) {
                            vm.registers[i] = tempArgs[i];
                        }
                        
                        // Switch to function's chunk - reuse current frame
                        vm.chunk = function->chunk;
                        vm.ip = function->chunk->code + function->start;
                        
                    } else {
                        vm.registers[resultReg] = NIL_VAL;
                    }
                    break;
                }
                case OP_RETURN_R: {
                    uint8_t reg = READ_BYTE();
                    Value returnValue = vm.registers[reg];

                    if (vm.frameCount > 0) {
                        CallFrame* frame = &vm.frames[--vm.frameCount];
                        vm.chunk = frame->previousChunk;
                        vm.ip = frame->returnAddress;

                        // Store return value in the result register
                        vm.registers[frame->baseRegister] = returnValue;
                    } else {
                        // Top-level return
                        vm.lastExecutionTime = get_time_vm() - start_time;
                        RETURN(INTERPRET_OK);
                    }
                    break;
                }

                case OP_RETURN_VOID: {
                    if (vm.frameCount > 0) {
                        CallFrame* frame = &vm.frames[--vm.frameCount];
                        vm.chunk = frame->previousChunk;
                        vm.ip = frame->returnAddress;
                    } else {
                        vm.lastExecutionTime = get_time_vm() - start_time;
                        RETURN(INTERPRET_OK);
                    }
                    break;
                }

                case OP_CLOSURE_R: {
                    uint8_t dstReg = READ_BYTE();
                    uint8_t functionReg = READ_BYTE();
                    uint8_t upvalueCount = READ_BYTE();
                    
                    Value functionValue = vm.registers[functionReg];
                    if (!IS_FUNCTION(functionValue)) {
                        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL, 0, 0},
                                    "Expected function for closure creation");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }
                    
                    ObjFunction* function = AS_FUNCTION(functionValue);
                    ObjClosure* closure = allocateClosure(function);
                    
                    for (int i = 0; i < upvalueCount; i++) {
                        uint8_t isLocal = READ_BYTE();
                        uint8_t index = READ_BYTE();
                        
                        if (isLocal) {
                            closure->upvalues[i] = captureUpvalue(&vm.registers[index]);
                        } else {
                            ObjClosure* enclosing = AS_CLOSURE(vm.registers[0]); // Current closure
                            closure->upvalues[i] = enclosing->upvalues[index];
                        }
                    }
                    
                    vm.registers[dstReg] = CLOSURE_VAL(closure);
                    break;
                }

                case OP_GET_UPVALUE_R: {
                    uint8_t dstReg = READ_BYTE();
                    uint8_t upvalueIndex = READ_BYTE();
                    
                    ObjClosure* closure = AS_CLOSURE(vm.registers[0]); // Current closure
                    vm.registers[dstReg] = *closure->upvalues[upvalueIndex]->location;
                    break;
                }

                case OP_SET_UPVALUE_R: {
                    uint8_t upvalueIndex = READ_BYTE();
                    uint8_t valueReg = READ_BYTE();
                    
                    ObjClosure* closure = AS_CLOSURE(vm.registers[0]); // Current closure
                    *closure->upvalues[upvalueIndex]->location = vm.registers[valueReg];
                    break;
                }

                case OP_CLOSE_UPVALUE_R: {
                    uint8_t localReg = READ_BYTE();
                    closeUpvalues(&vm.registers[localReg]);
                    break;
                }

                // Short jump optimizations for performance  
                case OP_JUMP_SHORT: {
                    uint8_t offset = READ_BYTE();
                    vm.ip += offset;
                    break;
                }

                case OP_JUMP_BACK_SHORT: {
                    uint8_t offset = READ_BYTE();
                    vm.ip -= offset;
                    break;
                }

                case OP_JUMP_IF_NOT_SHORT: {
                    uint8_t reg = READ_BYTE();
                    uint8_t offset = READ_BYTE();
                    
                    if (!IS_BOOL(vm.registers[reg])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Condition must be boolean");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    
                    if (!AS_BOOL(vm.registers[reg])) {
                        vm.ip += offset;
                    }
                    break;
                }

                case OP_LOOP_SHORT: {
                    uint8_t offset = READ_BYTE();
                    vm.ip -= offset;
                    break;
                }

                // Typed arithmetic operations for maximum performance (bypass Value boxing)
                case OP_ADD_I32_TYPED: {
                    uint8_t dst = READ_BYTE();
                    uint8_t left = READ_BYTE();
                    uint8_t right = READ_BYTE();
                    
                    vm.typed_regs.i32_regs[dst] = vm.typed_regs.i32_regs[left] + vm.typed_regs.i32_regs[right];
                    vm.typed_regs.reg_types[dst] = REG_TYPE_I32;
                    
                    break;
                }

                case OP_SUB_I32_TYPED: {
                    uint8_t dst = READ_BYTE();
                    uint8_t left = READ_BYTE();
                    uint8_t right = READ_BYTE();
                    
                    vm.typed_regs.i32_regs[dst] = vm.typed_regs.i32_regs[left] - vm.typed_regs.i32_regs[right];
                    vm.typed_regs.reg_types[dst] = REG_TYPE_I32;
                    
                    break;
                }

                case OP_MUL_I32_TYPED: {
                    uint8_t dst = READ_BYTE();
                    uint8_t left = READ_BYTE();
                    uint8_t right = READ_BYTE();
                    
                    vm.typed_regs.i32_regs[dst] = vm.typed_regs.i32_regs[left] * vm.typed_regs.i32_regs[right];
                    vm.typed_regs.reg_types[dst] = REG_TYPE_I32;
                    
                    break;
                }

                case OP_DIV_I32_TYPED: {
                    uint8_t dst = READ_BYTE();
                    uint8_t left = READ_BYTE();
                    uint8_t right = READ_BYTE();
                    
                    if (vm.typed_regs.i32_regs[right] == 0) {
                        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL, 0, 0}, "Division by zero");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    
                    vm.typed_regs.i32_regs[dst] = vm.typed_regs.i32_regs[left] / vm.typed_regs.i32_regs[right];
                    vm.typed_regs.reg_types[dst] = REG_TYPE_I32;
                    
                    break;
                }

                case OP_MOD_I32_TYPED: {
                    uint8_t dst = READ_BYTE();
                    uint8_t left = READ_BYTE();
                    uint8_t right = READ_BYTE();
                    
                    if (vm.typed_regs.i32_regs[right] == 0) {
                        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL, 0, 0}, "Modulo by zero");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    
                    vm.typed_regs.i32_regs[dst] = vm.typed_regs.i32_regs[left] % vm.typed_regs.i32_regs[right];
                    vm.typed_regs.reg_types[dst] = REG_TYPE_I32;
                    
                    break;
                }

                // Additional typed operations (I64, F64, comparisons, loads, moves)
                case OP_ADD_I64_TYPED: {
                    uint8_t dst = READ_BYTE();
                    uint8_t left = READ_BYTE();
                    uint8_t right = READ_BYTE();
                    
                    vm.typed_regs.i64_regs[dst] = vm.typed_regs.i64_regs[left] + vm.typed_regs.i64_regs[right];
                    vm.typed_regs.reg_types[dst] = REG_TYPE_I64;
                    
                    break;
                }

                case OP_SUB_I64_TYPED: {
                    uint8_t dst = READ_BYTE();
                    uint8_t left = READ_BYTE();
                    uint8_t right = READ_BYTE();
                    
                    vm.typed_regs.i64_regs[dst] = vm.typed_regs.i64_regs[left] - vm.typed_regs.i64_regs[right];
                    vm.typed_regs.reg_types[dst] = REG_TYPE_I64;
                    
                    break;
                }

                case OP_MUL_I64_TYPED: {
                    uint8_t dst = READ_BYTE();
                    uint8_t left = READ_BYTE();
                    uint8_t right = READ_BYTE();
                    
                    vm.typed_regs.i64_regs[dst] = vm.typed_regs.i64_regs[left] * vm.typed_regs.i64_regs[right];
                    vm.typed_regs.reg_types[dst] = REG_TYPE_I64;
                    
                    break;
                }

                case OP_DIV_I64_TYPED: {
                    uint8_t dst = READ_BYTE();
                    uint8_t left = READ_BYTE();
                    uint8_t right = READ_BYTE();
                    
                    if (vm.typed_regs.i64_regs[right] == 0) {
                        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL, 0, 0}, "Division by zero");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    
                    vm.typed_regs.i64_regs[dst] = vm.typed_regs.i64_regs[left] / vm.typed_regs.i64_regs[right];
                    vm.typed_regs.reg_types[dst] = REG_TYPE_I64;
                    
                    break;
                }

                case OP_MOD_I64_TYPED: {
                    uint8_t dst = READ_BYTE();
                    uint8_t left = READ_BYTE();
                    uint8_t right = READ_BYTE();
                    
                    if (vm.typed_regs.i64_regs[right] == 0) {
                        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL, 0, 0}, "Division by zero");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    
                    vm.typed_regs.i64_regs[dst] = vm.typed_regs.i64_regs[left] % vm.typed_regs.i64_regs[right];
                    vm.typed_regs.reg_types[dst] = REG_TYPE_I64;
                    
                    break;
                }

                case OP_ADD_F64_TYPED: {
                    uint8_t dst = READ_BYTE();
                    uint8_t left = READ_BYTE();
                    uint8_t right = READ_BYTE();
                    
                    vm.typed_regs.f64_regs[dst] = vm.typed_regs.f64_regs[left] + vm.typed_regs.f64_regs[right];
                    vm.typed_regs.reg_types[dst] = REG_TYPE_F64;
                    
                    break;
                }

                case OP_SUB_F64_TYPED: {
                    uint8_t dst = READ_BYTE();
                    uint8_t left = READ_BYTE();
                    uint8_t right = READ_BYTE();
                    
                    vm.typed_regs.f64_regs[dst] = vm.typed_regs.f64_regs[left] - vm.typed_regs.f64_regs[right];
                    vm.typed_regs.reg_types[dst] = REG_TYPE_F64;
                    
                    break;
                }

                case OP_MUL_F64_TYPED: {
                    uint8_t dst = READ_BYTE();
                    uint8_t left = READ_BYTE();
                    uint8_t right = READ_BYTE();
                    
                    vm.typed_regs.f64_regs[dst] = vm.typed_regs.f64_regs[left] * vm.typed_regs.f64_regs[right];
                    vm.typed_regs.reg_types[dst] = REG_TYPE_F64;
                    
                    break;
                }

                case OP_DIV_F64_TYPED: {
                    uint8_t dst = READ_BYTE();
                    uint8_t left = READ_BYTE();
                    uint8_t right = READ_BYTE();
                    
                    vm.typed_regs.f64_regs[dst] = vm.typed_regs.f64_regs[left] / vm.typed_regs.f64_regs[right];
                    vm.typed_regs.reg_types[dst] = REG_TYPE_F64;
                    
                    break;
                }

                case OP_MOD_F64_TYPED: {
                    uint8_t dst = READ_BYTE();
                    uint8_t left = READ_BYTE();
                    uint8_t right = READ_BYTE();
                    
                    vm.typed_regs.f64_regs[dst] = fmod(vm.typed_regs.f64_regs[left], vm.typed_regs.f64_regs[right]);
                    vm.typed_regs.reg_types[dst] = REG_TYPE_F64;
                    
                    break;
                }

                // U32 Typed Operations
                case OP_ADD_U32_TYPED: {
                    uint8_t dst = READ_BYTE();
                    uint8_t left = READ_BYTE();
                    uint8_t right = READ_BYTE();
                    
                    vm.typed_regs.u32_regs[dst] = vm.typed_regs.u32_regs[left] + vm.typed_regs.u32_regs[right];
                    vm.typed_regs.reg_types[dst] = REG_TYPE_U32;
                    
                    break;
                }

                case OP_SUB_U32_TYPED: {
                    uint8_t dst = READ_BYTE();
                    uint8_t left = READ_BYTE();
                    uint8_t right = READ_BYTE();
                    
                    vm.typed_regs.u32_regs[dst] = vm.typed_regs.u32_regs[left] - vm.typed_regs.u32_regs[right];
                    vm.typed_regs.reg_types[dst] = REG_TYPE_U32;
                    
                    break;
                }

                case OP_MUL_U32_TYPED: {
                    uint8_t dst = READ_BYTE();
                    uint8_t left = READ_BYTE();
                    uint8_t right = READ_BYTE();
                    
                    vm.typed_regs.u32_regs[dst] = vm.typed_regs.u32_regs[left] * vm.typed_regs.u32_regs[right];
                    vm.typed_regs.reg_types[dst] = REG_TYPE_U32;
                    
                    break;
                }

                case OP_DIV_U32_TYPED: {
                    uint8_t dst = READ_BYTE();
                    uint8_t left = READ_BYTE();
                    uint8_t right = READ_BYTE();
                    
                    if (vm.typed_regs.u32_regs[right] == 0) {
                        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL, 0, 0}, "Division by zero");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    
                    vm.typed_regs.u32_regs[dst] = vm.typed_regs.u32_regs[left] / vm.typed_regs.u32_regs[right];
                    vm.typed_regs.reg_types[dst] = REG_TYPE_U32;
                    
                    break;
                }

                case OP_MOD_U32_TYPED: {
                    uint8_t dst = READ_BYTE();
                    uint8_t left = READ_BYTE();
                    uint8_t right = READ_BYTE();
                    
                    if (vm.typed_regs.u32_regs[right] == 0) {
                        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL, 0, 0}, "Division by zero");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    
                    vm.typed_regs.u32_regs[dst] = vm.typed_regs.u32_regs[left] % vm.typed_regs.u32_regs[right];
                    vm.typed_regs.reg_types[dst] = REG_TYPE_U32;
                    
                    break;
                }

                // U64 Typed Operations
                case OP_ADD_U64_TYPED: {
                    uint8_t dst = READ_BYTE();
                    uint8_t left = READ_BYTE();
                    uint8_t right = READ_BYTE();
                    
                    vm.typed_regs.u64_regs[dst] = vm.typed_regs.u64_regs[left] + vm.typed_regs.u64_regs[right];
                    vm.typed_regs.reg_types[dst] = REG_TYPE_U64;
                    
                    break;
                }

                case OP_SUB_U64_TYPED: {
                    uint8_t dst = READ_BYTE();
                    uint8_t left = READ_BYTE();
                    uint8_t right = READ_BYTE();
                    
                    vm.typed_regs.u64_regs[dst] = vm.typed_regs.u64_regs[left] - vm.typed_regs.u64_regs[right];
                    vm.typed_regs.reg_types[dst] = REG_TYPE_U64;
                    
                    break;
                }

                case OP_MUL_U64_TYPED: {
                    uint8_t dst = READ_BYTE();
                    uint8_t left = READ_BYTE();
                    uint8_t right = READ_BYTE();
                    
                    vm.typed_regs.u64_regs[dst] = vm.typed_regs.u64_regs[left] * vm.typed_regs.u64_regs[right];
                    vm.typed_regs.reg_types[dst] = REG_TYPE_U64;
                    
                    break;
                }

                case OP_DIV_U64_TYPED: {
                    uint8_t dst = READ_BYTE();
                    uint8_t left = READ_BYTE();
                    uint8_t right = READ_BYTE();
                    
                    if (vm.typed_regs.u64_regs[right] == 0) {
                        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL, 0, 0}, "Division by zero");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    
                    vm.typed_regs.u64_regs[dst] = vm.typed_regs.u64_regs[left] / vm.typed_regs.u64_regs[right];
                    vm.typed_regs.reg_types[dst] = REG_TYPE_U64;
                    
                    break;
                }

                case OP_MOD_U64_TYPED: {
                    uint8_t dst = READ_BYTE();
                    uint8_t left = READ_BYTE();
                    uint8_t right = READ_BYTE();
                    
                    if (vm.typed_regs.u64_regs[right] == 0) {
                        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL, 0, 0}, "Division by zero");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    
                    vm.typed_regs.u64_regs[dst] = vm.typed_regs.u64_regs[left] % vm.typed_regs.u64_regs[right];
                    vm.typed_regs.reg_types[dst] = REG_TYPE_U64;
                    
                    break;
                }

                // Mixed-Type Arithmetic Operations (I32 op F64)
                case OP_ADD_I32_F64: {
                    uint8_t dst = READ_BYTE();
                    uint8_t i32_reg = READ_BYTE();
                    uint8_t f64_reg = READ_BYTE();
                    
                    if (!IS_I32(vm.registers[i32_reg]) || !IS_F64(vm.registers[f64_reg])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Mixed-type operation requires i32 and f64 operands");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    
                    double result = (double)AS_I32(vm.registers[i32_reg]) + AS_F64(vm.registers[f64_reg]);
                    vm.registers[dst] = F64_VAL(result);
                    
                    break;
                }

                case OP_SUB_I32_F64: {
                    uint8_t dst = READ_BYTE();
                    uint8_t i32_reg = READ_BYTE();
                    uint8_t f64_reg = READ_BYTE();
                    
                    if (!IS_I32(vm.registers[i32_reg]) || !IS_F64(vm.registers[f64_reg])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Mixed-type operation requires i32 and f64 operands");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    
                    double result = (double)AS_I32(vm.registers[i32_reg]) - AS_F64(vm.registers[f64_reg]);
                    vm.registers[dst] = F64_VAL(result);
                    
                    break;
                }

                case OP_MUL_I32_F64: {
                    uint8_t dst = READ_BYTE();
                    uint8_t i32_reg = READ_BYTE();
                    uint8_t f64_reg = READ_BYTE();
                    
                    if (!IS_I32(vm.registers[i32_reg]) || !IS_F64(vm.registers[f64_reg])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Mixed-type operation requires i32 and f64 operands");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    
                    double result = (double)AS_I32(vm.registers[i32_reg]) * AS_F64(vm.registers[f64_reg]);
                    vm.registers[dst] = F64_VAL(result);
                    
                    break;
                }

                case OP_DIV_I32_F64: {
                    uint8_t dst = READ_BYTE();
                    uint8_t i32_reg = READ_BYTE();
                    uint8_t f64_reg = READ_BYTE();
                    
                    if (!IS_I32(vm.registers[i32_reg]) || !IS_F64(vm.registers[f64_reg])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Mixed-type operation requires i32 and f64 operands");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    
                    double result = (double)AS_I32(vm.registers[i32_reg]) / AS_F64(vm.registers[f64_reg]);
                    vm.registers[dst] = F64_VAL(result);
                    
                    break;
                }

                case OP_MOD_I32_F64: {
                    uint8_t dst = READ_BYTE();
                    uint8_t i32_reg = READ_BYTE();
                    uint8_t f64_reg = READ_BYTE();
                    
                    if (!IS_I32(vm.registers[i32_reg]) || !IS_F64(vm.registers[f64_reg])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Mixed-type operation requires i32 and f64 operands");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    
                    double result = fmod((double)AS_I32(vm.registers[i32_reg]), AS_F64(vm.registers[f64_reg]));
                    vm.registers[dst] = F64_VAL(result);
                    
                    break;
                }

                // Mixed-Type Arithmetic Operations (F64 op I32)
                case OP_ADD_F64_I32: {
                    uint8_t dst = READ_BYTE();
                    uint8_t f64_reg = READ_BYTE();
                    uint8_t i32_reg = READ_BYTE();
                    
                    if (!IS_F64(vm.registers[f64_reg]) || !IS_I32(vm.registers[i32_reg])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Mixed-type operation requires f64 and i32 operands");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    
                    double result = AS_F64(vm.registers[f64_reg]) + (double)AS_I32(vm.registers[i32_reg]);
                    vm.registers[dst] = F64_VAL(result);
                    
                    break;
                }

                case OP_SUB_F64_I32: {
                    uint8_t dst = READ_BYTE();
                    uint8_t f64_reg = READ_BYTE();
                    uint8_t i32_reg = READ_BYTE();
                    
                    if (!IS_F64(vm.registers[f64_reg]) || !IS_I32(vm.registers[i32_reg])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Mixed-type operation requires f64 and i32 operands");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    
                    double result = AS_F64(vm.registers[f64_reg]) - (double)AS_I32(vm.registers[i32_reg]);
                    vm.registers[dst] = F64_VAL(result);
                    
                    break;
                }

                case OP_MUL_F64_I32: {
                    uint8_t dst = READ_BYTE();
                    uint8_t f64_reg = READ_BYTE();
                    uint8_t i32_reg = READ_BYTE();
                    
                    if (!IS_F64(vm.registers[f64_reg]) || !IS_I32(vm.registers[i32_reg])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Mixed-type operation requires f64 and i32 operands");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    
                    double result = AS_F64(vm.registers[f64_reg]) * (double)AS_I32(vm.registers[i32_reg]);
                    vm.registers[dst] = F64_VAL(result);
                    
                    break;
                }

                case OP_DIV_F64_I32: {
                    uint8_t dst = READ_BYTE();
                    uint8_t f64_reg = READ_BYTE();
                    uint8_t i32_reg = READ_BYTE();
                    
                    if (!IS_F64(vm.registers[f64_reg]) || !IS_I32(vm.registers[i32_reg])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Mixed-type operation requires f64 and i32 operands");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    
                    double result = AS_F64(vm.registers[f64_reg]) / (double)AS_I32(vm.registers[i32_reg]);
                    vm.registers[dst] = F64_VAL(result);
                    
                    break;
                }

                case OP_MOD_F64_I32: {
                    uint8_t dst = READ_BYTE();
                    uint8_t f64_reg = READ_BYTE();
                    uint8_t i32_reg = READ_BYTE();
                    
                    if (!IS_F64(vm.registers[f64_reg]) || !IS_I32(vm.registers[i32_reg])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Mixed-type operation requires f64 and i32 operands");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    
                    double result = fmod(AS_F64(vm.registers[f64_reg]), (double)AS_I32(vm.registers[i32_reg]));
                    vm.registers[dst] = F64_VAL(result);
                    
                    break;
                }

                case OP_LT_I32_TYPED: {
                    uint8_t dst = READ_BYTE();
                    uint8_t left = READ_BYTE();
                    uint8_t right = READ_BYTE();
                    
                    vm.typed_regs.bool_regs[dst] = vm.typed_regs.i32_regs[left] < vm.typed_regs.i32_regs[right];
                    vm.typed_regs.reg_types[dst] = REG_TYPE_BOOL;
                    
                    break;
                }

                case OP_LE_I32_TYPED: {
                    uint8_t dst = READ_BYTE();
                    uint8_t left = READ_BYTE();
                    uint8_t right = READ_BYTE();
                    
                    vm.typed_regs.bool_regs[dst] = vm.typed_regs.i32_regs[left] <= vm.typed_regs.i32_regs[right];
                    vm.typed_regs.reg_types[dst] = REG_TYPE_BOOL;
                    
                    break;
                }

                case OP_GT_I32_TYPED: {
                    uint8_t dst = READ_BYTE();
                    uint8_t left = READ_BYTE();
                    uint8_t right = READ_BYTE();
                    
                    vm.typed_regs.bool_regs[dst] = vm.typed_regs.i32_regs[left] > vm.typed_regs.i32_regs[right];
                    vm.typed_regs.reg_types[dst] = REG_TYPE_BOOL;
                    
                    break;
                }

                case OP_GE_I32_TYPED: {
                    uint8_t dst = READ_BYTE();
                    uint8_t left = READ_BYTE();
                    uint8_t right = READ_BYTE();
                    
                    vm.typed_regs.bool_regs[dst] = vm.typed_regs.i32_regs[left] >= vm.typed_regs.i32_regs[right];
                    vm.typed_regs.reg_types[dst] = REG_TYPE_BOOL;
                    
                    break;
                }

                case OP_LOAD_I32_CONST: {
                    uint8_t reg = READ_BYTE();
                    uint16_t constantIndex = READ_SHORT();
                    int32_t value = READ_CONSTANT(constantIndex).as.i32;
                    
                    vm.typed_regs.i32_regs[reg] = value;
                    vm.typed_regs.reg_types[reg] = REG_TYPE_I32;
                    
                    break;
                }

                case OP_LOAD_I64_CONST: {
                    uint8_t reg = READ_BYTE();
                    uint16_t constantIndex = READ_SHORT();
                    int64_t value = READ_CONSTANT(constantIndex).as.i64;
                    
                    vm.typed_regs.i64_regs[reg] = value;
                    vm.typed_regs.reg_types[reg] = REG_TYPE_I64;
                    
                    break;
                }

                case OP_LOAD_F64_CONST: {
                    uint8_t reg = READ_BYTE();
                    uint16_t constantIndex = READ_SHORT();
                    double value = READ_CONSTANT(constantIndex).as.f64;
                    
                    vm.typed_regs.f64_regs[reg] = value;
                    vm.typed_regs.reg_types[reg] = REG_TYPE_F64;
                    
                    break;
                }

                case OP_MOVE_I32: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    
                    vm.typed_regs.i32_regs[dst] = vm.typed_regs.i32_regs[src];
                    vm.typed_regs.reg_types[dst] = REG_TYPE_I32;
                    
                    break;
                }

                case OP_MOVE_I64: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    
                    vm.typed_regs.i64_regs[dst] = vm.typed_regs.i64_regs[src];
                    vm.typed_regs.reg_types[dst] = REG_TYPE_I64;
                    
                    break;
                }

                case OP_MOVE_F64: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    
                    vm.typed_regs.f64_regs[dst] = vm.typed_regs.f64_regs[src];
                    vm.typed_regs.reg_types[dst] = REG_TYPE_F64;
                    
                    break;
                }

                case OP_TIME_STAMP: {
                    uint8_t dst = READ_BYTE();
                    
                    // Get high-precision timestamp in milliseconds
                    int32_t timestamp = builtin_time_stamp();
                    
                    // Store in typed register and regular register for compatibility
                    vm.typed_regs.i32_regs[dst] = timestamp;
                    vm.typed_regs.reg_types[dst] = REG_TYPE_I32;
                    vm.registers[dst] = I32_VAL(timestamp);

                    break;
                }

                // Fused instructions for optimized loops and arithmetic
                case OP_ADD_I32_IMM: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    int32_t imm = *(int32_t*)vm.ip;
                    vm.ip += 4;

                    if (!IS_I32(vm.registers[src])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Operand must be i32");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }

                    int32_t result = AS_I32(vm.registers[src]) + imm;
                    vm.registers[dst] = I32_VAL(result);
                    break;
                }

                case OP_SUB_I32_IMM: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    int32_t imm = *(int32_t*)vm.ip;
                    vm.ip += 4;

                    if (!IS_I32(vm.registers[src])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Operand must be i32");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }

                    int32_t result = AS_I32(vm.registers[src]) - imm;
                    vm.registers[dst] = I32_VAL(result);
                    break;
                }

                case OP_MUL_I32_IMM: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    int32_t imm = *(int32_t*)vm.ip;
                    vm.ip += 4;

                    if (!IS_I32(vm.registers[src])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Operand must be i32");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }

                    int32_t result = AS_I32(vm.registers[src]) * imm;
                    vm.registers[dst] = I32_VAL(result);
                    break;
                }

                case OP_CMP_I32_IMM: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    int32_t imm = *(int32_t*)vm.ip;
                    vm.ip += 4;

                    vm.typed_regs.bool_regs[dst] = vm.typed_regs.i32_regs[src] < imm;
                    vm.typed_regs.reg_types[dst] = REG_TYPE_BOOL;
                    break;
                }

                case OP_INC_CMP_JMP: {
                    uint8_t reg = READ_BYTE();
                    uint8_t limit_reg = READ_BYTE();
                    int16_t offset = READ_SHORT();

                    if (!IS_I32(vm.registers[reg]) || !IS_I32(vm.registers[limit_reg])) {
                        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                    "Operands must be i32");
                        RETURN(INTERPRET_RUNTIME_ERROR);
                    }

                    int32_t incremented = AS_I32(vm.registers[reg]) + 1;
                    vm.registers[reg] = I32_VAL(incremented);
                    if (incremented < AS_I32(vm.registers[limit_reg])) {
                        vm.ip += offset;
                    }
                    break;
                }

                case OP_DEC_CMP_JMP: {
                    uint8_t reg = READ_BYTE();
                    uint8_t zero_test = READ_BYTE();
                    int16_t offset = READ_SHORT();

                    if (--vm.typed_regs.i32_regs[reg] > vm.typed_regs.i32_regs[zero_test]) {
                        vm.ip += offset;
                    }
                    break;
                }

                case OP_MUL_ADD_I32: {
                    uint8_t dst = READ_BYTE();
                    uint8_t mul1 = READ_BYTE();
                    uint8_t mul2 = READ_BYTE();
                    uint8_t add = READ_BYTE();

                    vm.typed_regs.i32_regs[dst] =
                        vm.typed_regs.i32_regs[mul1] * vm.typed_regs.i32_regs[mul2] +
                        vm.typed_regs.i32_regs[add];
                    vm.typed_regs.reg_types[dst] = REG_TYPE_I32;
                    break;
                }

                case OP_HALT:
                    vm.lastExecutionTime = get_time_vm() - start_time;
                    RETURN(INTERPRET_OK);

                default:
                    runtimeError(ERROR_RUNTIME, (SrcLocation){NULL, 0, 0},
                                "Unknown opcode: %d", instruction);
                    vm.lastExecutionTime = get_time_vm() - start_time;
                    RETURN(INTERPRET_RUNTIME_ERROR);
            }

            if (IS_ERROR(vm.lastError)) {
                // Handle runtime errors
                if (vm.tryFrameCount > 0) {
                    // Exception handling
                    TryFrame frame = vm.tryFrames[--vm.tryFrameCount];
                    vm.ip = frame.handler;
                    vm.globals[frame.varIndex] = vm.lastError;
                    vm.lastError = NIL_VAL;
                } else {
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }
            }
        }
    #undef RETURN
}
#endif // !USE_COMPUTED_GOTO
