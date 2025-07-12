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
        /* Simple f64 promotion following Lua's design */ \
        if (IS_F64(val1) || IS_F64(val2)) { \
            double a = IS_F64(val1) ? AS_F64(val1) : \
                      (IS_I64(val1) ? (double)AS_I64(val1) : (double)AS_I32(val1)); \
            double b = IS_F64(val2) ? AS_F64(val2) : \
                      (IS_I64(val2) ? (double)AS_I64(val2) : (double)AS_I32(val2)); \
            vm.registers[dst_reg] = F64_VAL(a + b); \
        } else if (IS_I32(val1) && IS_I32(val2)) { \
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
            /* Mixed integer types: promote to i64 */ \
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
        /* Simple f64 promotion following Lua's design */ \
        if (IS_F64(val1) || IS_F64(val2)) { \
            double a = IS_F64(val1) ? AS_F64(val1) : \
                      (IS_I64(val1) ? (double)AS_I64(val1) : (double)AS_I32(val1)); \
            double b = IS_F64(val2) ? AS_F64(val2) : \
                      (IS_I64(val2) ? (double)AS_I64(val2) : (double)AS_I32(val2)); \
            vm.registers[dst_reg] = F64_VAL(a - b); \
        } else if (IS_I32(val1) && IS_I32(val2)) { \
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
        /* Simple f64 promotion following Lua's design */ \
        if (IS_F64(val1) || IS_F64(val2)) { \
            double a = IS_F64(val1) ? AS_F64(val1) : \
                      (IS_I64(val1) ? (double)AS_I64(val1) : (double)AS_I32(val1)); \
            double b = IS_F64(val2) ? AS_F64(val2) : \
                      (IS_I64(val2) ? (double)AS_I64(val2) : (double)AS_I32(val2)); \
            vm.registers[dst_reg] = F64_VAL(a * b); \
        } else if (IS_I32(val1) && IS_I32(val2)) { \
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

// Simplified mixed-type division and modulo handling
#define HANDLE_MIXED_DIV(val1, val2, dst_reg) \
    do { \
        /* Simple f64 promotion following Lua's design */ \
        if (IS_F64(val1) || IS_F64(val2)) { \
            double a = IS_F64(val1) ? AS_F64(val1) : \
                      (IS_I64(val1) ? (double)AS_I64(val1) : (double)AS_I32(val1)); \
            double b = IS_F64(val2) ? AS_F64(val2) : \
                      (IS_I64(val2) ? (double)AS_I64(val2) : (double)AS_I32(val2)); \
            if (b == 0.0) { \
                runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, "Division by zero"); \
                RETURN(INTERPRET_RUNTIME_ERROR); \
            } \
            vm.registers[dst_reg] = F64_VAL(a / b); \
        } else if (IS_I32(val1) && IS_I32(val2)) { \
            int32_t a = AS_I32(val1); \
            int32_t b = AS_I32(val2); \
            if (b == 0) { \
                runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, "Division by zero"); \
                RETURN(INTERPRET_RUNTIME_ERROR); \
            } \
            if (a == INT32_MIN && b == -1) { \
                /* Overflow: promote to i64 */ \
                vm.registers[dst_reg] = I64_VAL((int64_t)INT32_MAX + 1); \
            } else { \
                vm.registers[dst_reg] = I32_VAL(a / b); \
            } \
        } else { \
            /* Mixed integer types: promote to i64 */ \
            int64_t a = IS_I32(val1) ? (int64_t)AS_I32(val1) : AS_I64(val1); \
            int64_t b = IS_I32(val2) ? (int64_t)AS_I32(val2) : AS_I64(val2); \
            if (b == 0) { \
                runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, "Division by zero"); \
                RETURN(INTERPRET_RUNTIME_ERROR); \
            } \
            vm.registers[dst_reg] = I64_VAL(a / b); \
        } \
    } while (0)

#define HANDLE_MIXED_MOD(val1, val2, dst_reg) \
    do { \
        /* Simple f64 promotion following Lua's design */ \
        if (IS_F64(val1) || IS_F64(val2)) { \
            double a = IS_F64(val1) ? AS_F64(val1) : \
                      (IS_I64(val1) ? (double)AS_I64(val1) : (double)AS_I32(val1)); \
            double b = IS_F64(val2) ? AS_F64(val2) : \
                      (IS_I64(val2) ? (double)AS_I64(val2) : (double)AS_I32(val2)); \
            if (b == 0.0) { \
                runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, "Division by zero"); \
                RETURN(INTERPRET_RUNTIME_ERROR); \
            } \
            vm.registers[dst_reg] = F64_VAL(fmod(a, b)); \
        } else if (IS_I32(val1) && IS_I32(val2)) { \
            int32_t a = AS_I32(val1); \
            int32_t b = AS_I32(val2); \
            if (b == 0) { \
                runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, "Division by zero"); \
                RETURN(INTERPRET_RUNTIME_ERROR); \
            } \
            if (a == INT32_MIN && b == -1) { \
                /* Modulo overflow case: result is 0 */ \
                vm.registers[dst_reg] = I32_VAL(0); \
            } else { \
                vm.registers[dst_reg] = I32_VAL(a % b); \
            } \
        } else { \
            /* Mixed integer types: promote to i64 */ \
            int64_t a = IS_I32(val1) ? (int64_t)AS_I32(val1) : AS_I64(val1); \
            int64_t b = IS_I32(val2) ? (int64_t)AS_I32(val2) : AS_I64(val2); \
            if (b == 0) { \
                runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, "Division by zero"); \
                RETURN(INTERPRET_RUNTIME_ERROR); \
            } \
            vm.registers[dst_reg] = I64_VAL(a % b); \
        } \
    } while (0)

#if USE_COMPUTED_GOTO

InterpretResult vm_run_dispatch(void) {

    double start_time = get_time_vm();
    #define RETURN(val) \
        do { \
            vm.lastExecutionTime = get_time_vm() - start_time; \
            return (val); \
        } while (0)

    // Initialize dispatch table with label addresses - this only runs ONCE per process
    static bool global_dispatch_initialized = false;
    if (!global_dispatch_initialized) {
        // Phase 1.3 Optimization: Hot opcodes first for better cache locality
        // Most frequently used typed operations (hot path)
        vm_dispatch_table[OP_ADD_I32_TYPED] = &&LABEL_OP_ADD_I32_TYPED;
        vm_dispatch_table[OP_SUB_I32_TYPED] = &&LABEL_OP_SUB_I32_TYPED;
        vm_dispatch_table[OP_MUL_I32_TYPED] = &&LABEL_OP_MUL_I32_TYPED;
        vm_dispatch_table[OP_LT_I32_TYPED] = &&LABEL_OP_LT_I32_TYPED;
        vm_dispatch_table[OP_LE_I32_TYPED] = &&LABEL_OP_LE_I32_TYPED;
        vm_dispatch_table[OP_GT_I32_TYPED] = &&LABEL_OP_GT_I32_TYPED;
        vm_dispatch_table[OP_GE_I32_TYPED] = &&LABEL_OP_GE_I32_TYPED;
        
        // Short jumps for tight loops
        vm_dispatch_table[OP_JUMP_SHORT] = &&LABEL_OP_JUMP_SHORT;
        vm_dispatch_table[OP_JUMP_BACK_SHORT] = &&LABEL_OP_JUMP_BACK_SHORT;
        vm_dispatch_table[OP_JUMP_IF_NOT_SHORT] = &&LABEL_OP_JUMP_IF_NOT_SHORT;
        vm_dispatch_table[OP_LOOP_SHORT] = &&LABEL_OP_LOOP_SHORT;
        
        // Loop-critical operations
        vm_dispatch_table[OP_INC_I32_R] = &&LABEL_OP_INC_I32_R;
        
        // Remaining typed operations
        vm_dispatch_table[OP_DIV_I32_TYPED] = &&LABEL_OP_DIV_I32_TYPED;
        vm_dispatch_table[OP_MOD_I32_TYPED] = &&LABEL_OP_MOD_I32_TYPED;
        vm_dispatch_table[OP_ADD_I64_TYPED] = &&LABEL_OP_ADD_I64_TYPED;
        vm_dispatch_table[OP_SUB_I64_TYPED] = &&LABEL_OP_SUB_I64_TYPED;
        vm_dispatch_table[OP_MUL_I64_TYPED] = &&LABEL_OP_MUL_I64_TYPED;
        vm_dispatch_table[OP_DIV_I64_TYPED] = &&LABEL_OP_DIV_I64_TYPED;
        vm_dispatch_table[OP_MOD_I64_TYPED] = &&LABEL_OP_MOD_I64_TYPED;
        vm_dispatch_table[OP_ADD_F64_TYPED] = &&LABEL_OP_ADD_F64_TYPED;
        vm_dispatch_table[OP_SUB_F64_TYPED] = &&LABEL_OP_SUB_F64_TYPED;
        vm_dispatch_table[OP_MUL_F64_TYPED] = &&LABEL_OP_MUL_F64_TYPED;
        vm_dispatch_table[OP_DIV_F64_TYPED] = &&LABEL_OP_DIV_F64_TYPED;
        vm_dispatch_table[OP_MOD_F64_TYPED] = &&LABEL_OP_MOD_F64_TYPED;
        
        // U32 and U64 typed operations
        vm_dispatch_table[OP_ADD_U32_TYPED] = &&LABEL_OP_ADD_U32_TYPED;
        vm_dispatch_table[OP_SUB_U32_TYPED] = &&LABEL_OP_SUB_U32_TYPED;
        vm_dispatch_table[OP_MUL_U32_TYPED] = &&LABEL_OP_MUL_U32_TYPED;
        vm_dispatch_table[OP_DIV_U32_TYPED] = &&LABEL_OP_DIV_U32_TYPED;
        vm_dispatch_table[OP_MOD_U32_TYPED] = &&LABEL_OP_MOD_U32_TYPED;
        
        vm_dispatch_table[OP_ADD_U64_TYPED] = &&LABEL_OP_ADD_U64_TYPED;
        vm_dispatch_table[OP_SUB_U64_TYPED] = &&LABEL_OP_SUB_U64_TYPED;
        vm_dispatch_table[OP_MUL_U64_TYPED] = &&LABEL_OP_MUL_U64_TYPED;
        vm_dispatch_table[OP_DIV_U64_TYPED] = &&LABEL_OP_DIV_U64_TYPED;
        vm_dispatch_table[OP_MOD_U64_TYPED] = &&LABEL_OP_MOD_U64_TYPED;
        
        // Mixed-type arithmetic operations
        vm_dispatch_table[OP_ADD_I32_F64] = &&LABEL_OP_ADD_I32_F64;
        vm_dispatch_table[OP_SUB_I32_F64] = &&LABEL_OP_SUB_I32_F64;
        vm_dispatch_table[OP_MUL_I32_F64] = &&LABEL_OP_MUL_I32_F64;
        vm_dispatch_table[OP_DIV_I32_F64] = &&LABEL_OP_DIV_I32_F64;
        vm_dispatch_table[OP_MOD_I32_F64] = &&LABEL_OP_MOD_I32_F64;
        
        vm_dispatch_table[OP_ADD_F64_I32] = &&LABEL_OP_ADD_F64_I32;
        vm_dispatch_table[OP_SUB_F64_I32] = &&LABEL_OP_SUB_F64_I32;
        vm_dispatch_table[OP_MUL_F64_I32] = &&LABEL_OP_MUL_F64_I32;
        vm_dispatch_table[OP_DIV_F64_I32] = &&LABEL_OP_DIV_F64_I32;
        vm_dispatch_table[OP_MOD_F64_I32] = &&LABEL_OP_MOD_F64_I32;
        
        // Constant loading (also hot)
        vm_dispatch_table[OP_LOAD_I32_CONST] = &&LABEL_OP_LOAD_I32_CONST;
        vm_dispatch_table[OP_LOAD_I64_CONST] = &&LABEL_OP_LOAD_I64_CONST;
        vm_dispatch_table[OP_LOAD_F64_CONST] = &&LABEL_OP_LOAD_F64_CONST;
        
        // Standard operations (less hot)
        vm_dispatch_table[OP_LOAD_CONST] = &&LABEL_OP_LOAD_CONST;
        vm_dispatch_table[OP_LOAD_NIL] = &&LABEL_OP_LOAD_NIL;
        vm_dispatch_table[OP_LOAD_TRUE] = &&LABEL_OP_LOAD_TRUE;
        vm_dispatch_table[OP_LOAD_FALSE] = &&LABEL_OP_LOAD_FALSE;
        vm_dispatch_table[OP_MOVE] = &&LABEL_OP_MOVE;
        vm_dispatch_table[OP_LOAD_GLOBAL] = &&LABEL_OP_LOAD_GLOBAL;
        vm_dispatch_table[OP_STORE_GLOBAL] = &&LABEL_OP_STORE_GLOBAL;
        vm_dispatch_table[OP_ADD_I32_R] = &&LABEL_OP_ADD_I32_R;
        vm_dispatch_table[OP_SUB_I32_R] = &&LABEL_OP_SUB_I32_R;
        vm_dispatch_table[OP_MUL_I32_R] = &&LABEL_OP_MUL_I32_R;
        vm_dispatch_table[OP_DIV_I32_R] = &&LABEL_OP_DIV_I32_R;
        vm_dispatch_table[OP_MOD_I32_R] = &&LABEL_OP_MOD_I32_R;
        vm_dispatch_table[OP_INC_I32_R] = &&LABEL_OP_INC_I32_R;
        vm_dispatch_table[OP_DEC_I32_R] = &&LABEL_OP_DEC_I32_R;
        vm_dispatch_table[OP_ADD_I64_R] = &&LABEL_OP_ADD_I64_R;
        vm_dispatch_table[OP_SUB_I64_R] = &&LABEL_OP_SUB_I64_R;
        vm_dispatch_table[OP_MUL_I64_R] = &&LABEL_OP_MUL_I64_R;
        vm_dispatch_table[OP_DIV_I64_R] = &&LABEL_OP_DIV_I64_R;
        vm_dispatch_table[OP_MOD_I64_R] = &&LABEL_OP_MOD_I64_R;
        vm_dispatch_table[OP_ADD_U32_R] = &&LABEL_OP_ADD_U32_R;
        vm_dispatch_table[OP_SUB_U32_R] = &&LABEL_OP_SUB_U32_R;
        vm_dispatch_table[OP_MUL_U32_R] = &&LABEL_OP_MUL_U32_R;
        vm_dispatch_table[OP_DIV_U32_R] = &&LABEL_OP_DIV_U32_R;
        vm_dispatch_table[OP_MOD_U32_R] = &&LABEL_OP_MOD_U32_R;
        vm_dispatch_table[OP_ADD_U64_R] = &&LABEL_OP_ADD_U64_R;
        vm_dispatch_table[OP_SUB_U64_R] = &&LABEL_OP_SUB_U64_R;
        vm_dispatch_table[OP_MUL_U64_R] = &&LABEL_OP_MUL_U64_R;
        vm_dispatch_table[OP_DIV_U64_R] = &&LABEL_OP_DIV_U64_R;
        vm_dispatch_table[OP_MOD_U64_R] = &&LABEL_OP_MOD_U64_R;
        vm_dispatch_table[OP_I32_TO_I64_R] = &&LABEL_OP_I32_TO_I64_R;
        vm_dispatch_table[OP_I32_TO_U32_R] = &&LABEL_OP_I32_TO_U32_R;
        vm_dispatch_table[OP_U32_TO_I32_R] = &&LABEL_OP_U32_TO_I32_R;
        vm_dispatch_table[OP_ADD_F64_R] = &&LABEL_OP_ADD_F64_R;
        vm_dispatch_table[OP_SUB_F64_R] = &&LABEL_OP_SUB_F64_R;
        vm_dispatch_table[OP_MUL_F64_R] = &&LABEL_OP_MUL_F64_R;
        vm_dispatch_table[OP_DIV_F64_R] = &&LABEL_OP_DIV_F64_R;
        vm_dispatch_table[OP_MOD_F64_R] = &&LABEL_OP_MOD_F64_R;

        // Bitwise operations
        vm_dispatch_table[OP_AND_I32_R] = &&LABEL_OP_AND_I32_R;
        vm_dispatch_table[OP_OR_I32_R] = &&LABEL_OP_OR_I32_R;
        vm_dispatch_table[OP_XOR_I32_R] = &&LABEL_OP_XOR_I32_R;
        vm_dispatch_table[OP_NOT_I32_R] = &&LABEL_OP_NOT_I32_R;
        vm_dispatch_table[OP_SHL_I32_R] = &&LABEL_OP_SHL_I32_R;
        vm_dispatch_table[OP_SHR_I32_R] = &&LABEL_OP_SHR_I32_R;

        vm_dispatch_table[OP_LT_F64_R] = &&LABEL_OP_LT_F64_R;
        vm_dispatch_table[OP_LE_F64_R] = &&LABEL_OP_LE_F64_R;
        vm_dispatch_table[OP_GT_F64_R] = &&LABEL_OP_GT_F64_R;
        vm_dispatch_table[OP_GE_F64_R] = &&LABEL_OP_GE_F64_R;
        vm_dispatch_table[OP_I32_TO_F64_R] = &&LABEL_OP_I32_TO_F64_R;
        vm_dispatch_table[OP_I64_TO_F64_R] = &&LABEL_OP_I64_TO_F64_R;
        vm_dispatch_table[OP_F64_TO_I32_R] = &&LABEL_OP_F64_TO_I32_R;
        vm_dispatch_table[OP_F64_TO_I64_R] = &&LABEL_OP_F64_TO_I64_R;
        vm_dispatch_table[OP_LT_I32_R] = &&LABEL_OP_LT_I32_R;
        vm_dispatch_table[OP_LE_I32_R] = &&LABEL_OP_LE_I32_R;
        vm_dispatch_table[OP_GT_I32_R] = &&LABEL_OP_GT_I32_R;
        vm_dispatch_table[OP_GE_I32_R] = &&LABEL_OP_GE_I32_R;
        vm_dispatch_table[OP_LT_I64_R] = &&LABEL_OP_LT_I64_R;
        vm_dispatch_table[OP_LE_I64_R] = &&LABEL_OP_LE_I64_R;
        vm_dispatch_table[OP_GT_I64_R] = &&LABEL_OP_GT_I64_R;
        vm_dispatch_table[OP_GE_I64_R] = &&LABEL_OP_GE_I64_R;
        vm_dispatch_table[OP_LT_U32_R] = &&LABEL_OP_LT_U32_R;
        vm_dispatch_table[OP_LE_U32_R] = &&LABEL_OP_LE_U32_R;
        vm_dispatch_table[OP_GT_U32_R] = &&LABEL_OP_GT_U32_R;
        vm_dispatch_table[OP_GE_U32_R] = &&LABEL_OP_GE_U32_R;
        vm_dispatch_table[OP_LT_U64_R] = &&LABEL_OP_LT_U64_R;
        vm_dispatch_table[OP_LE_U64_R] = &&LABEL_OP_LE_U64_R;
        vm_dispatch_table[OP_GT_U64_R] = &&LABEL_OP_GT_U64_R;
        vm_dispatch_table[OP_GE_U64_R] = &&LABEL_OP_GE_U64_R;
        vm_dispatch_table[OP_EQ_R] = &&LABEL_OP_EQ_R;
        vm_dispatch_table[OP_NE_R] = &&LABEL_OP_NE_R;
        vm_dispatch_table[OP_AND_BOOL_R] = &&LABEL_OP_AND_BOOL_R;
        vm_dispatch_table[OP_OR_BOOL_R] = &&LABEL_OP_OR_BOOL_R;
        vm_dispatch_table[OP_NOT_BOOL_R] = &&LABEL_OP_NOT_BOOL_R;
        vm_dispatch_table[OP_CONCAT_R] = &&LABEL_OP_CONCAT_R;
        vm_dispatch_table[OP_JUMP] = &&LABEL_OP_JUMP;
        vm_dispatch_table[OP_JUMP_IF_NOT_R] = &&LABEL_OP_JUMP_IF_NOT_R;
        vm_dispatch_table[OP_LOOP] = &&LABEL_OP_LOOP;
        vm_dispatch_table[OP_GET_ITER_R] = &&LABEL_OP_GET_ITER_R;
        vm_dispatch_table[OP_ITER_NEXT_R] = &&LABEL_OP_ITER_NEXT_R;
        vm_dispatch_table[OP_PRINT_MULTI_R] = &&LABEL_OP_PRINT_MULTI_R;
        vm_dispatch_table[OP_PRINT_R] = &&LABEL_OP_PRINT_R;
        vm_dispatch_table[OP_PRINT_NO_NL_R] = &&LABEL_OP_PRINT_NO_NL_R;
        vm_dispatch_table[OP_CALL_R] = &&LABEL_OP_CALL_R;
        vm_dispatch_table[OP_TAIL_CALL_R] = &&LABEL_OP_TAIL_CALL_R;
        vm_dispatch_table[OP_RETURN_R] = &&LABEL_OP_RETURN_R;
        vm_dispatch_table[OP_RETURN_VOID] = &&LABEL_OP_RETURN_VOID;
        
        // Closure operations
        vm_dispatch_table[OP_CLOSURE_R] = &&LABEL_OP_CLOSURE_R;
        vm_dispatch_table[OP_GET_UPVALUE_R] = &&LABEL_OP_GET_UPVALUE_R;
        vm_dispatch_table[OP_SET_UPVALUE_R] = &&LABEL_OP_SET_UPVALUE_R;
        vm_dispatch_table[OP_CLOSE_UPVALUE_R] = &&LABEL_OP_CLOSE_UPVALUE_R;
        
        // Note: Hot opcodes already assigned above for optimal cache locality
        
        vm_dispatch_table[OP_MOVE_I32] = &&LABEL_OP_MOVE_I32;
        vm_dispatch_table[OP_MOVE_I64] = &&LABEL_OP_MOVE_I64;
        vm_dispatch_table[OP_MOVE_F64] = &&LABEL_OP_MOVE_F64;
        
        // Phase 2.2: Fused instruction dispatch entries
        vm_dispatch_table[OP_ADD_I32_IMM] = &&LABEL_OP_ADD_I32_IMM;
        vm_dispatch_table[OP_SUB_I32_IMM] = &&LABEL_OP_SUB_I32_IMM;
        vm_dispatch_table[OP_MUL_I32_IMM] = &&LABEL_OP_MUL_I32_IMM;
        vm_dispatch_table[OP_CMP_I32_IMM] = &&LABEL_OP_CMP_I32_IMM;
        vm_dispatch_table[OP_INC_CMP_JMP] = &&LABEL_OP_INC_CMP_JMP;
        vm_dispatch_table[OP_DEC_CMP_JMP] = &&LABEL_OP_DEC_CMP_JMP;
        vm_dispatch_table[OP_MUL_ADD_I32] = &&LABEL_OP_MUL_ADD_I32;
        
        // Built-in functions
        vm_dispatch_table[OP_TIME_STAMP] = &&LABEL_OP_TIME_STAMP;
        
        vm_dispatch_table[OP_HALT] = &&LABEL_OP_HALT;
        
        // Mark dispatch table as initialized to prevent re-initialization
        global_dispatch_initialized = true;
    }

    uint8_t instruction;

    // Phase 1.1 Optimization: Fast DISPATCH macro for production builds
    #ifdef ORUS_DEBUG
        // Debug build: Keep full error checking and tracing
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
                /* Update line tracking for error reporting */ \
                int instruction_offset = (int)(vm.ip - vm.chunk->code); \
                if (vm.chunk && instruction_offset >= 0 && instruction_offset < vm.chunk->count) { \
                    vm.currentLine = vm.chunk->lines[instruction_offset]; \
                    vm.currentColumn = vm.chunk->columns[instruction_offset]; \
                } \
                if (vm.trace) { \
                    printf("        "); \
                    for (int i = 0; i < 8; i++) { \
                        printf("[ R%d: ", i); \
                        printValue(vm.registers[i]); \
                        printf(" ]"); \
                    } \
                    printf("\\n"); \
                    disassembleInstruction(vm.chunk, (int)(vm.ip - vm.chunk->code)); \
                } \
                vm.instruction_count++; \
                instruction = READ_BYTE(); \
                if (instruction > OP_HALT || vm_dispatch_table[instruction] == NULL) { \
                    goto LABEL_UNKNOWN; \
                } \
                goto *vm_dispatch_table[instruction]; \
            } while (0)
        
        // Same as normal dispatch for debug builds
        #define DISPATCH_TYPED() DISPATCH()
    #else
    // Production build: Ultra-fast dispatch with line tracking for error reporting
    #define DISPATCH() do { \
        int instruction_offset = (int)(vm.ip - vm.chunk->code); \
        if (vm.chunk && instruction_offset >= 0 && instruction_offset < vm.chunk->count) { \
            vm.currentLine = vm.chunk->lines[instruction_offset]; \
            vm.currentColumn = vm.chunk->columns[instruction_offset]; \
        } \
        goto *vm_dispatch_table[*vm.ip++]; \
    } while(0)
    
    // Even faster for typed operations - no error checking needed but still track lines
    #define DISPATCH_TYPED() do { \
        int instruction_offset = (int)(vm.ip - vm.chunk->code); \
        if (vm.chunk && instruction_offset >= 0 && instruction_offset < vm.chunk->count) { \
            vm.currentLine = vm.chunk->lines[instruction_offset]; \
            vm.currentColumn = vm.chunk->columns[instruction_offset]; \
        } \
        goto *vm_dispatch_table[*vm.ip++]; \
    } while(0)
    #endif
        DISPATCH();

    LABEL_OP_LOAD_CONST: {
            uint8_t reg = READ_BYTE();
            uint16_t constantIndex = READ_SHORT();
            vm.registers[reg] = READ_CONSTANT(constantIndex);
            DISPATCH();
        }

    LABEL_OP_LOAD_NIL: {
            uint8_t reg = READ_BYTE();
            vm.registers[reg] = NIL_VAL;
            DISPATCH();
        }

    LABEL_OP_LOAD_TRUE: {
            uint8_t reg = READ_BYTE();
            vm.registers[reg] = BOOL_VAL(true);
            DISPATCH();
        }

    LABEL_OP_LOAD_FALSE: {
            uint8_t reg = READ_BYTE();
            vm.registers[reg] = BOOL_VAL(false);
            DISPATCH();
        }

    LABEL_OP_MOVE: {
            uint8_t dst = READ_BYTE();
            uint8_t src = READ_BYTE();
            vm.registers[dst] = vm.registers[src];
            DISPATCH();
        }

    LABEL_OP_LOAD_GLOBAL: {
            uint8_t reg = READ_BYTE();
            uint8_t globalIndex = READ_BYTE();
            if (globalIndex >= vm.variableCount || vm.globalTypes[globalIndex] == NULL) {
                runtimeError(ERROR_NAME, (SrcLocation){NULL, 0, 0}, "Undefined variable");
                RETURN(INTERPRET_RUNTIME_ERROR);
            }
            vm.registers[reg] = vm.globals[globalIndex];
            DISPATCH();
        }

    LABEL_OP_STORE_GLOBAL: {
            uint8_t globalIndex = READ_BYTE();
            uint8_t reg = READ_BYTE();
            vm.globals[globalIndex] = vm.registers[reg];
            DISPATCH();
        }

    LABEL_OP_ADD_I32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            
            // Simple type validation - allow numeric types following Lua's design
            if (!(IS_I32(vm.registers[src1]) || IS_I64(vm.registers[src1]) || IS_F64(vm.registers[src1])) ||
                !(IS_I32(vm.registers[src2]) || IS_I64(vm.registers[src2]) || IS_F64(vm.registers[src2]))) {
                runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                            "Operands must be numeric (i32, i64, or f64)");
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
            DISPATCH();
        }

    LABEL_OP_SUB_I32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            
            // Simple type validation - allow numeric types following Lua's design
            if (!(IS_I32(vm.registers[src1]) || IS_I64(vm.registers[src1]) || IS_F64(vm.registers[src1])) ||
                !(IS_I32(vm.registers[src2]) || IS_I64(vm.registers[src2]) || IS_F64(vm.registers[src2]))) {
                runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                            "Operands must be numeric (i32, i64, or f64)");
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
            DISPATCH();
        }

    LABEL_OP_MUL_I32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            
            // Simple type validation - allow numeric types following Lua's design
            if (!(IS_I32(vm.registers[src1]) || IS_I64(vm.registers[src1]) || IS_F64(vm.registers[src1])) ||
                !(IS_I32(vm.registers[src2]) || IS_I64(vm.registers[src2]) || IS_F64(vm.registers[src2]))) {
                runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                            "Operands must be numeric (i32, i64, or f64)");
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
            DISPATCH();
        }

    LABEL_OP_DIV_I32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            
            // Simple type validation - allow numeric types following Lua's design
            if (!(IS_I32(vm.registers[src1]) || IS_I64(vm.registers[src1]) || IS_F64(vm.registers[src1])) ||
                !(IS_I32(vm.registers[src2]) || IS_I64(vm.registers[src2]) || IS_F64(vm.registers[src2]))) {
                runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be numeric (i32, i64, or f64)");
                RETURN(INTERPRET_RUNTIME_ERROR);
            }
            // Use mixed-type division handling
            HANDLE_MIXED_DIV(vm.registers[src1], vm.registers[src2], dst);
            DISPATCH();
        }

    LABEL_OP_MOD_I32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            
            // Simple type validation - allow numeric types following Lua's design
            if (!(IS_I32(vm.registers[src1]) || IS_I64(vm.registers[src1]) || IS_F64(vm.registers[src1])) ||
                !(IS_I32(vm.registers[src2]) || IS_I64(vm.registers[src2]) || IS_F64(vm.registers[src2]))) {
                runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be numeric (i32, i64, or f64)");
                RETURN(INTERPRET_RUNTIME_ERROR);
            }
            // Use mixed-type modulo handling
            HANDLE_MIXED_MOD(vm.registers[src1], vm.registers[src2], dst);
            DISPATCH();
        }

    LABEL_OP_INC_I32_R: {
            uint8_t reg = READ_BYTE();
    #if USE_FAST_ARITH
            vm.registers[reg] = I32_VAL(AS_I32(vm.registers[reg]) + 1);
    #else
            int32_t val = AS_I32(vm.registers[reg]);
            int32_t result;
            if (__builtin_add_overflow(val, 1, &result)) {
                runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, "Integer overflow");
                RETURN(INTERPRET_RUNTIME_ERROR);
            }
            vm.registers[reg] = I32_VAL(result);
    #endif
            DISPATCH();
        }

    LABEL_OP_DEC_I32_R: {
            uint8_t reg = READ_BYTE();
    #if USE_FAST_ARITH
            vm.registers[reg] = I32_VAL(AS_I32(vm.registers[reg]) - 1);
    #else
            int32_t val = AS_I32(vm.registers[reg]);
            int32_t result;
            if (__builtin_sub_overflow(val, 1, &result)) {
                runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, "Integer overflow");
                RETURN(INTERPRET_RUNTIME_ERROR);
            }
            vm.registers[reg] = I32_VAL(result);
    #endif
            DISPATCH();
        }

    LABEL_OP_ADD_I64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            if (!IS_I64(vm.registers[src1]) || !IS_I64(vm.registers[src2])) {
                runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i64");
                RETURN(INTERPRET_RUNTIME_ERROR);
            }
            int64_t a = AS_I64(vm.registers[src1]);
            int64_t b = AS_I64(vm.registers[src2]);
    #if USE_FAST_ARITH
            vm.registers[dst] = I64_VAL(a + b);
    #else
            int64_t result;
            if (__builtin_add_overflow(a, b, &result)) {
                runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, "Integer overflow");
                RETURN(INTERPRET_RUNTIME_ERROR);
            }
            vm.registers[dst] = I64_VAL(result);
    #endif
            DISPATCH();
        }

    LABEL_OP_SUB_I64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            if (!IS_I64(vm.registers[src1]) || !IS_I64(vm.registers[src2])) {
                runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i64");
                RETURN(INTERPRET_RUNTIME_ERROR);
            }
            int64_t a = AS_I64(vm.registers[src1]);
            int64_t b = AS_I64(vm.registers[src2]);
    #if USE_FAST_ARITH
            vm.registers[dst] = I64_VAL(a - b);
    #else
            int64_t result;
            if (__builtin_sub_overflow(a, b, &result)) {
                runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, "Integer overflow");
                RETURN(INTERPRET_RUNTIME_ERROR);
            }
            vm.registers[dst] = I64_VAL(result);
    #endif
            DISPATCH();
        }

    LABEL_OP_MUL_I64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            if (!IS_I64(vm.registers[src1]) || !IS_I64(vm.registers[src2])) {
                runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i64");
                RETURN(INTERPRET_RUNTIME_ERROR);
            }
            int64_t a = AS_I64(vm.registers[src1]);
            int64_t b = AS_I64(vm.registers[src2]);
    #if USE_FAST_ARITH
            vm.registers[dst] = I64_VAL(a * b);
    #else
            int64_t result;
            if (__builtin_mul_overflow(a, b, &result)) {
                runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, "Integer overflow");
                RETURN(INTERPRET_RUNTIME_ERROR);
            }
            vm.registers[dst] = I64_VAL(result);
    #endif
            DISPATCH();
        }

    LABEL_OP_DIV_I64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            if (!IS_I64(vm.registers[src1]) || !IS_I64(vm.registers[src2])) {
                runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i64");
                RETURN(INTERPRET_RUNTIME_ERROR);
            }
            int64_t b = AS_I64(vm.registers[src2]);
            if (b == 0) {
                runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, "Division by zero");
                RETURN(INTERPRET_RUNTIME_ERROR);
            }
            vm.registers[dst] = I64_VAL(AS_I64(vm.registers[src1]) / b);
            DISPATCH();
        }

    LABEL_OP_MOD_I64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            if (!IS_I64(vm.registers[src1]) || !IS_I64(vm.registers[src2])) {
                runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i64");
                RETURN(INTERPRET_RUNTIME_ERROR);
            }
            int64_t b = AS_I64(vm.registers[src2]);
            if (b == 0) {
                runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, "Division by zero");
                RETURN(INTERPRET_RUNTIME_ERROR);
            }
            vm.registers[dst] = I64_VAL(AS_I64(vm.registers[src1]) % b);
            DISPATCH();
        }

    LABEL_OP_ADD_U32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            if (!IS_U32(vm.registers[src1]) || !IS_U32(vm.registers[src2])) {
                runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be u32");
                RETURN(INTERPRET_RUNTIME_ERROR);
            }
            vm.registers[dst] = U32_VAL(AS_U32(vm.registers[src1]) + AS_U32(vm.registers[src2]));
            DISPATCH();
        }

    LABEL_OP_SUB_U32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            if (!IS_U32(vm.registers[src1]) || !IS_U32(vm.registers[src2])) {
                runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be u32");
                RETURN(INTERPRET_RUNTIME_ERROR);
            }
            vm.registers[dst] = U32_VAL(AS_U32(vm.registers[src1]) - AS_U32(vm.registers[src2]));
            DISPATCH();
        }

    LABEL_OP_MUL_U32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            if (!IS_U32(vm.registers[src1]) || !IS_U32(vm.registers[src2])) {
                runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be u32");
                RETURN(INTERPRET_RUNTIME_ERROR);
            }
            vm.registers[dst] = U32_VAL(AS_U32(vm.registers[src1]) * AS_U32(vm.registers[src2]));
            DISPATCH();
        }

    LABEL_OP_DIV_U32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            if (!IS_U32(vm.registers[src1]) || !IS_U32(vm.registers[src2])) {
                runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be u32");
                RETURN(INTERPRET_RUNTIME_ERROR);
            }
            uint32_t b = AS_U32(vm.registers[src2]);
            if (b == 0) {
                runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, "Division by zero");
                RETURN(INTERPRET_RUNTIME_ERROR);
            }
            vm.registers[dst] = U32_VAL(AS_U32(vm.registers[src1]) / b);
            DISPATCH();
        }

    LABEL_OP_MOD_U32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            if (!IS_U32(vm.registers[src1]) || !IS_U32(vm.registers[src2])) {
                runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be u32");
                RETURN(INTERPRET_RUNTIME_ERROR);
            }
            uint32_t b = AS_U32(vm.registers[src2]);
            if (b == 0) {
                runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, "Division by zero");
                RETURN(INTERPRET_RUNTIME_ERROR);
            }
            vm.registers[dst] = U32_VAL(AS_U32(vm.registers[src1]) % b);
            DISPATCH();
        }

    LABEL_OP_ADD_U64_R: {
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
            DISPATCH();
        }

    LABEL_OP_SUB_U64_R: {
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
            DISPATCH();
        }

    LABEL_OP_MUL_U64_R: {
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
            DISPATCH();
        }

    LABEL_OP_DIV_U64_R: {
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
            DISPATCH();
        }

    LABEL_OP_MOD_U64_R: {
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
            DISPATCH();
        }

    LABEL_OP_I32_TO_I64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src = READ_BYTE();
            if (!IS_I32(vm.registers[src])) {
                runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Source must be i32");
                RETURN(INTERPRET_RUNTIME_ERROR);
            }
            vm.registers[dst] = I64_VAL((int64_t)AS_I32(vm.registers[src]));
            DISPATCH();
        }

    LABEL_OP_I32_TO_U32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src = READ_BYTE();
            if (!IS_I32(vm.registers[src])) {
                runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Source must be i32");
                RETURN(INTERPRET_RUNTIME_ERROR);
            }
            vm.registers[dst] = U32_VAL((uint32_t)AS_I32(vm.registers[src]));
            DISPATCH();
        }

    LABEL_OP_U32_TO_I32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src = READ_BYTE();
            if (!IS_U32(vm.registers[src])) {
                runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Source must be u32");
                RETURN(INTERPRET_RUNTIME_ERROR);
            }
            vm.registers[dst] = I32_VAL((int32_t)AS_U32(vm.registers[src]));
            DISPATCH();
        }

    // F64 Arithmetic Operations
    LABEL_OP_ADD_F64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            if (!IS_F64(vm.registers[src1]) || !IS_F64(vm.registers[src2])) {
                runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be f64");
                RETURN(INTERPRET_RUNTIME_ERROR);
            }
            vm.registers[dst] = F64_VAL(AS_F64(vm.registers[src1]) + AS_F64(vm.registers[src2]));
            DISPATCH();
        }

    LABEL_OP_SUB_F64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            if (!IS_F64(vm.registers[src1]) || !IS_F64(vm.registers[src2])) {
                runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be f64");
                RETURN(INTERPRET_RUNTIME_ERROR);
            }
            vm.registers[dst] = F64_VAL(AS_F64(vm.registers[src1]) - AS_F64(vm.registers[src2]));
            DISPATCH();
        }

    LABEL_OP_MUL_F64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            if (!IS_F64(vm.registers[src1]) || !IS_F64(vm.registers[src2])) {
                runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be f64");
                RETURN(INTERPRET_RUNTIME_ERROR);
            }
            vm.registers[dst] = F64_VAL(AS_F64(vm.registers[src1]) * AS_F64(vm.registers[src2]));
            DISPATCH();
        }

    LABEL_OP_DIV_F64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            if (!IS_F64(vm.registers[src1]) || !IS_F64(vm.registers[src2])) {
                runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be f64");
                RETURN(INTERPRET_RUNTIME_ERROR);
            }
            double a = AS_F64(vm.registers[src1]);
            double b = AS_F64(vm.registers[src2]);
            
            // IEEE 754 compliant: division by zero produces infinity, not error
            double result = a / b;
            
            // The result may be infinity, -infinity, or NaN
            // These are valid f64 values according to IEEE 754
            vm.registers[dst] = F64_VAL(result);
            DISPATCH();
        }

    LABEL_OP_MOD_F64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            if (!IS_F64(vm.registers[src1]) || !IS_F64(vm.registers[src2])) {
                runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be f64");
                RETURN(INTERPRET_RUNTIME_ERROR);
            }
            double a = AS_F64(vm.registers[src1]);
            double b = AS_F64(vm.registers[src2]);
            
            // IEEE 754 compliant: use fmod for floating point modulo
            double result = fmod(a, b);
            
            // The result may be infinity, -infinity, or NaN
            // These are valid f64 values according to IEEE 754
            vm.registers[dst] = F64_VAL(result);
            DISPATCH();
        }

    // Bitwise Operations
    LABEL_OP_AND_I32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            if (!IS_I32(vm.registers[src1]) || !IS_I32(vm.registers[src2])) {
                runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i32");
                RETURN(INTERPRET_RUNTIME_ERROR);
            }
            vm.registers[dst] = I32_VAL(AS_I32(vm.registers[src1]) & AS_I32(vm.registers[src2]));
            DISPATCH();
        }

    LABEL_OP_OR_I32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            if (!IS_I32(vm.registers[src1]) || !IS_I32(vm.registers[src2])) {
                runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i32");
                RETURN(INTERPRET_RUNTIME_ERROR);
            }
            vm.registers[dst] = I32_VAL(AS_I32(vm.registers[src1]) | AS_I32(vm.registers[src2]));
            DISPATCH();
        }

    LABEL_OP_XOR_I32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            if (!IS_I32(vm.registers[src1]) || !IS_I32(vm.registers[src2])) {
                runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i32");
                RETURN(INTERPRET_RUNTIME_ERROR);
            }
            vm.registers[dst] = I32_VAL(AS_I32(vm.registers[src1]) ^ AS_I32(vm.registers[src2]));
            DISPATCH();
        }

    LABEL_OP_NOT_I32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src = READ_BYTE();
            if (!IS_I32(vm.registers[src])) {
                runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operand must be i32");
                RETURN(INTERPRET_RUNTIME_ERROR);
            }
            vm.registers[dst] = I32_VAL(~AS_I32(vm.registers[src]));
            DISPATCH();
        }

    LABEL_OP_SHL_I32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            if (!IS_I32(vm.registers[src1]) || !IS_I32(vm.registers[src2])) {
                runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i32");
                RETURN(INTERPRET_RUNTIME_ERROR);
            }
            vm.registers[dst] = I32_VAL(AS_I32(vm.registers[src1]) << AS_I32(vm.registers[src2]));
            DISPATCH();
        }

    LABEL_OP_SHR_I32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            if (!IS_I32(vm.registers[src1]) || !IS_I32(vm.registers[src2])) {
                runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i32");
                RETURN(INTERPRET_RUNTIME_ERROR);
            }
            vm.registers[dst] = I32_VAL(AS_I32(vm.registers[src1]) >> AS_I32(vm.registers[src2]));
            DISPATCH();
        }

    // F64 Comparison Operations
    LABEL_OP_LT_F64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            if (!IS_F64(vm.registers[src1]) || !IS_F64(vm.registers[src2])) {
                runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be f64");
                RETURN(INTERPRET_RUNTIME_ERROR);
            }
            vm.registers[dst] = BOOL_VAL(AS_F64(vm.registers[src1]) < AS_F64(vm.registers[src2]));
            DISPATCH();
        }

    LABEL_OP_LE_F64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            if (!IS_F64(vm.registers[src1]) || !IS_F64(vm.registers[src2])) {
                runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be f64");
                RETURN(INTERPRET_RUNTIME_ERROR);
            }
            vm.registers[dst] = BOOL_VAL(AS_F64(vm.registers[src1]) <= AS_F64(vm.registers[src2]));
            DISPATCH();
        }

    LABEL_OP_GT_F64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            if (!IS_F64(vm.registers[src1]) || !IS_F64(vm.registers[src2])) {
                runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be f64");
                RETURN(INTERPRET_RUNTIME_ERROR);
            }
            vm.registers[dst] = BOOL_VAL(AS_F64(vm.registers[src1]) > AS_F64(vm.registers[src2]));
            DISPATCH();
        }

    LABEL_OP_GE_F64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            if (!IS_F64(vm.registers[src1]) || !IS_F64(vm.registers[src2])) {
                runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be f64");
                RETURN(INTERPRET_RUNTIME_ERROR);
            }
            vm.registers[dst] = BOOL_VAL(AS_F64(vm.registers[src1]) >= AS_F64(vm.registers[src2]));
            DISPATCH();
        }

    // F64 Type Conversion Operations
    LABEL_OP_I32_TO_F64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src = READ_BYTE();
            if (!IS_I32(vm.registers[src])) {
                runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Source must be i32");
                RETURN(INTERPRET_RUNTIME_ERROR);
            }
            vm.registers[dst] = F64_VAL((double)AS_I32(vm.registers[src]));
            DISPATCH();
        }

    LABEL_OP_I64_TO_F64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src = READ_BYTE();
            if (!IS_I64(vm.registers[src])) {
                runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Source must be i64");
                RETURN(INTERPRET_RUNTIME_ERROR);
            }
            vm.registers[dst] = F64_VAL((double)AS_I64(vm.registers[src]));
            DISPATCH();
        }

    LABEL_OP_F64_TO_I32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src = READ_BYTE();
            if (!IS_F64(vm.registers[src])) {
                runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Source must be f64");
                RETURN(INTERPRET_RUNTIME_ERROR);
            }
            vm.registers[dst] = I32_VAL((int32_t)AS_F64(vm.registers[src]));
            DISPATCH();
        }

    LABEL_OP_F64_TO_I64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src = READ_BYTE();
            if (!IS_F64(vm.registers[src])) {
                runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Source must be f64");
                RETURN(INTERPRET_RUNTIME_ERROR);
            }
            vm.registers[dst] = I64_VAL((int64_t)AS_F64(vm.registers[src]));
            DISPATCH();
        }

    LABEL_OP_LT_I32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            if (!IS_I32(vm.registers[src1]) || !IS_I32(vm.registers[src2])) {
                runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i32");
                RETURN(INTERPRET_RUNTIME_ERROR);
            }
            vm.registers[dst] = BOOL_VAL(AS_I32(vm.registers[src1]) < AS_I32(vm.registers[src2]));
            DISPATCH();
        }

    LABEL_OP_EQ_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        vm.registers[dst] = BOOL_VAL(valuesEqual(vm.registers[src1], vm.registers[src2]));
        DISPATCH();
    }

    LABEL_OP_NE_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        vm.registers[dst] = BOOL_VAL(!valuesEqual(vm.registers[src1], vm.registers[src2]));
        DISPATCH();
    }

    LABEL_OP_LE_I32_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        if (!IS_I32(vm.registers[src1]) || !IS_I32(vm.registers[src2])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i32");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = BOOL_VAL(AS_I32(vm.registers[src1]) <= AS_I32(vm.registers[src2]));
        DISPATCH();
    }

    LABEL_OP_GT_I32_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        if (!IS_I32(vm.registers[src1]) || !IS_I32(vm.registers[src2])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i32");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = BOOL_VAL(AS_I32(vm.registers[src1]) > AS_I32(vm.registers[src2]));
        DISPATCH();
    }

    LABEL_OP_GE_I32_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        if (!IS_I32(vm.registers[src1]) || !IS_I32(vm.registers[src2])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i32");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = BOOL_VAL(AS_I32(vm.registers[src1]) >= AS_I32(vm.registers[src2]));
        DISPATCH();
    }

    LABEL_OP_LT_I64_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        if (!IS_I64(vm.registers[src1]) || !IS_I64(vm.registers[src2])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i64");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = BOOL_VAL(AS_I64(vm.registers[src1]) < AS_I64(vm.registers[src2]));
        DISPATCH();
    }

    LABEL_OP_LE_I64_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        if (!IS_I64(vm.registers[src1]) || !IS_I64(vm.registers[src2])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i64");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = BOOL_VAL(AS_I64(vm.registers[src1]) <= AS_I64(vm.registers[src2]));
        DISPATCH();
    }

    LABEL_OP_GT_I64_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        if (!IS_I64(vm.registers[src1]) || !IS_I64(vm.registers[src2])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i64");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = BOOL_VAL(AS_I64(vm.registers[src1]) > AS_I64(vm.registers[src2]));
        DISPATCH();
    }

    LABEL_OP_GE_I64_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        if (!IS_I64(vm.registers[src1]) || !IS_I64(vm.registers[src2])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i64");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
            vm.registers[dst] = BOOL_VAL(AS_I64(vm.registers[src1]) >= AS_I64(vm.registers[src2]));
            DISPATCH();
        }

    LABEL_OP_LT_U32_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        if (!IS_U32(vm.registers[src1]) || !IS_U32(vm.registers[src2])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be u32");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = BOOL_VAL(AS_U32(vm.registers[src1]) < AS_U32(vm.registers[src2]));
        DISPATCH();
    }

    LABEL_OP_LE_U32_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        if (!IS_U32(vm.registers[src1]) || !IS_U32(vm.registers[src2])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be u32");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = BOOL_VAL(AS_U32(vm.registers[src1]) <= AS_U32(vm.registers[src2]));
        DISPATCH();
    }

    LABEL_OP_GT_U32_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        if (!IS_U32(vm.registers[src1]) || !IS_U32(vm.registers[src2])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be u32");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = BOOL_VAL(AS_U32(vm.registers[src1]) > AS_U32(vm.registers[src2]));
        DISPATCH();
    }

    LABEL_OP_GE_U32_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        if (!IS_U32(vm.registers[src1]) || !IS_U32(vm.registers[src2])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be u32");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = BOOL_VAL(AS_U32(vm.registers[src1]) >= AS_U32(vm.registers[src2]));
        DISPATCH();
    }

    LABEL_OP_LT_U64_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        if (!IS_U64(vm.registers[src1]) || !IS_U64(vm.registers[src2])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be u64");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = BOOL_VAL(AS_U64(vm.registers[src1]) < AS_U64(vm.registers[src2]));
        DISPATCH();
    }

    LABEL_OP_LE_U64_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        if (!IS_U64(vm.registers[src1]) || !IS_U64(vm.registers[src2])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be u64");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = BOOL_VAL(AS_U64(vm.registers[src1]) <= AS_U64(vm.registers[src2]));
        DISPATCH();
    }

    LABEL_OP_GT_U64_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        if (!IS_U64(vm.registers[src1]) || !IS_U64(vm.registers[src2])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be u64");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = BOOL_VAL(AS_U64(vm.registers[src1]) > AS_U64(vm.registers[src2]));
        DISPATCH();
    }

    LABEL_OP_GE_U64_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        if (!IS_U64(vm.registers[src1]) || !IS_U64(vm.registers[src2])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be u64");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = BOOL_VAL(AS_U64(vm.registers[src1]) >= AS_U64(vm.registers[src2]));
        DISPATCH();
    }

    LABEL_OP_AND_BOOL_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        if (!IS_BOOL(vm.registers[src1]) || !IS_BOOL(vm.registers[src2])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be bool");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = BOOL_VAL(AS_BOOL(vm.registers[src1]) && AS_BOOL(vm.registers[src2]));
        DISPATCH();
    }

    LABEL_OP_OR_BOOL_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        if (!IS_BOOL(vm.registers[src1]) || !IS_BOOL(vm.registers[src2])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be bool");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = BOOL_VAL(AS_BOOL(vm.registers[src1]) || AS_BOOL(vm.registers[src2]));
        DISPATCH();
    }

    LABEL_OP_NOT_BOOL_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src = READ_BYTE();
        if (!IS_BOOL(vm.registers[src])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operand must be bool");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = BOOL_VAL(!AS_BOOL(vm.registers[src]));
        DISPATCH();
    }

    LABEL_OP_CONCAT_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        if (!IS_STRING(vm.registers[src1]) || !IS_STRING(vm.registers[src2])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be string");
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
        DISPATCH();
    }

    LABEL_OP_JUMP: {
            uint16_t offset = READ_SHORT();
            vm.ip += offset;
            DISPATCH();
        }

    LABEL_OP_JUMP_IF_NOT_R: {
            uint8_t reg = READ_BYTE();
            uint16_t offset = READ_SHORT();
            if (!IS_BOOL(vm.registers[reg])) {
                runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Condition must be boolean");
                RETURN(INTERPRET_RUNTIME_ERROR);
            }
            if (!AS_BOOL(vm.registers[reg])) {
                vm.ip += offset;
            }
            DISPATCH();
        }

    LABEL_OP_LOOP: {
        uint16_t offset = READ_SHORT();
        vm.ip -= offset;
        DISPATCH();
    }

    LABEL_OP_GET_ITER_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src = READ_BYTE();
        Value v = vm.registers[src];
        if (!IS_RANGE_ITERATOR(v)) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Value not iterable");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = v;
        DISPATCH();
    }

    LABEL_OP_ITER_NEXT_R: {
        uint8_t dst = READ_BYTE();
        uint8_t iterReg = READ_BYTE();
        uint8_t hasReg = READ_BYTE();
        if (!IS_RANGE_ITERATOR(vm.registers[iterReg])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Invalid iterator");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        ObjRangeIterator* it = AS_RANGE_ITERATOR(vm.registers[iterReg]);
        if (it->current >= it->end) {
            vm.registers[hasReg] = BOOL_VAL(false);
        } else {
            vm.registers[dst] = I64_VAL(it->current);
            it->current++;
            vm.registers[hasReg] = BOOL_VAL(true);
        }
        DISPATCH();
    }

    LABEL_OP_PRINT_MULTI_R: {
            uint8_t first = READ_BYTE();
            uint8_t count = READ_BYTE();
            uint8_t nl = READ_BYTE();
            builtin_print(&vm.registers[first], count, nl != 0);
            DISPATCH();
        }

    LABEL_OP_PRINT_R: {
            uint8_t reg = READ_BYTE();
            builtin_print(&vm.registers[reg], 1, true);
            DISPATCH();
        }

    LABEL_OP_PRINT_NO_NL_R: {
            uint8_t reg = READ_BYTE();
            builtin_print(&vm.registers[reg], 1, false);
            DISPATCH();
        }

    LABEL_OP_CALL_R: {
            uint8_t funcReg = READ_BYTE();
            uint8_t firstArgReg = READ_BYTE();
            uint8_t argCount = READ_BYTE();
            uint8_t resultReg = READ_BYTE();
            
            Value funcValue = vm.registers[funcReg];
            
            if (IS_I32(funcValue)) {
                int functionIndex = AS_I32(funcValue);
                
                if (functionIndex < 0 || functionIndex >= vm.functionCount) {
                    vm.registers[resultReg] = NIL_VAL;
                    DISPATCH();
                }
                
                Function* function = &vm.functions[functionIndex];
                
                // Check arity
                if (argCount != function->arity) {
                    vm.registers[resultReg] = NIL_VAL;
                    DISPATCH();
                }
                
                // Check if we have room for another call frame
                if (vm.frameCount >= FRAMES_MAX) {
                    vm.registers[resultReg] = NIL_VAL;
                    DISPATCH();
                }
                
                // Create new call frame
                CallFrame* frame = &vm.frames[vm.frameCount++];
                frame->returnAddress = vm.ip;
                frame->previousChunk = vm.chunk;
                frame->baseRegister = resultReg;
                frame->registerCount = argCount;
                frame->functionIndex = functionIndex;
                
                // Save registers that will be overwritten by parameters
                Value savedRegisters[256];
                for (int i = 0; i < argCount; i++) {
                    savedRegisters[i] = vm.registers[i];
                }
                
                // Copy arguments to function's parameter registers
                for (int i = 0; i < argCount; i++) {
                    vm.registers[i] = vm.registers[firstArgReg + i];
                }
                
                // Store saved registers in call frame for restoration
                frame->savedRegisterCount = argCount;
                for (int i = 0; i < argCount; i++) {
                    frame->savedRegisters[i] = savedRegisters[i];
                }
                
                // Switch to function's chunk
                vm.chunk = function->chunk;
                vm.ip = function->chunk->code + function->start;
                
            } else {
                vm.registers[resultReg] = NIL_VAL;
            }
            
            DISPATCH();
        }

    LABEL_OP_TAIL_CALL_R: {
            uint8_t funcReg = READ_BYTE();
            uint8_t firstArgReg = READ_BYTE();
            uint8_t argCount = READ_BYTE();
            uint8_t resultReg = READ_BYTE();
            
            Value funcValue = vm.registers[funcReg];
            
            if (IS_I32(funcValue)) {
                int functionIndex = AS_I32(funcValue);
                
                if (functionIndex < 0 || functionIndex >= vm.functionCount) {
                    vm.registers[resultReg] = NIL_VAL;
                    DISPATCH();
                }
                
                Function* function = &vm.functions[functionIndex];
                
                // Check arity
                if (argCount != function->arity) {
                    vm.registers[resultReg] = NIL_VAL;
                    DISPATCH();
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
            
            DISPATCH();
        }

    LABEL_OP_RETURN_R: {
            uint8_t reg = READ_BYTE();
            Value returnValue = vm.registers[reg];
            if (vm.frameCount > 0) {
                CallFrame* frame = &vm.frames[--vm.frameCount];
                
                // Restore saved registers
                for (int i = 0; i < frame->savedRegisterCount; i++) {
                    vm.registers[i] = frame->savedRegisters[i];
                }
                
                vm.chunk = frame->previousChunk;
                vm.ip = frame->returnAddress;
                vm.registers[frame->baseRegister] = returnValue;
            } else {
                vm.lastExecutionTime = get_time_vm() - start_time;
                RETURN(INTERPRET_OK);
            }
            DISPATCH();
        }

    LABEL_OP_RETURN_VOID: {
            if (vm.frameCount > 0) {
                CallFrame* frame = &vm.frames[--vm.frameCount];
                vm.chunk = frame->previousChunk;
                vm.ip = frame->returnAddress;
            } else {
                vm.lastExecutionTime = get_time_vm() - start_time;
                RETURN(INTERPRET_OK);
            }
            DISPATCH();
        }

    // Short jump optimizations for performance
    LABEL_OP_JUMP_SHORT: {
        uint8_t offset = READ_BYTE();
        vm.ip += offset;
        DISPATCH();
    }

    LABEL_OP_JUMP_BACK_SHORT: {
        uint8_t offset = READ_BYTE();
        vm.ip -= offset;
        DISPATCH();
    }

    LABEL_OP_JUMP_IF_NOT_SHORT: {
        uint8_t reg = READ_BYTE();
        uint8_t offset = READ_BYTE();
        
        if (!IS_BOOL(vm.registers[reg])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Condition must be boolean");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        
        if (!AS_BOOL(vm.registers[reg])) {
            vm.ip += offset;
        }
        DISPATCH();
    }

    LABEL_OP_LOOP_SHORT: {
        uint8_t offset = READ_BYTE();
        vm.ip -= offset;
        DISPATCH();
    }

    // Typed arithmetic operations for maximum performance (bypass Value boxing)
    LABEL_OP_ADD_I32_TYPED: {
        uint8_t dst = *vm.ip++;
        uint8_t left = *vm.ip++;
        uint8_t right = *vm.ip++;
        
        vm.typed_regs.i32_regs[dst] = vm.typed_regs.i32_regs[left] + vm.typed_regs.i32_regs[right];
        
        DISPATCH_TYPED();
    }

    LABEL_OP_SUB_I32_TYPED: {
        uint8_t dst = *vm.ip++;
        uint8_t left = *vm.ip++;
        uint8_t right = *vm.ip++;
        
        vm.typed_regs.i32_regs[dst] = vm.typed_regs.i32_regs[left] - vm.typed_regs.i32_regs[right];
        
        DISPATCH_TYPED();
    }

    LABEL_OP_MUL_I32_TYPED: {
        uint8_t dst = *vm.ip++;
        uint8_t left = *vm.ip++;
        uint8_t right = *vm.ip++;
        
        vm.typed_regs.i32_regs[dst] = vm.typed_regs.i32_regs[left] * vm.typed_regs.i32_regs[right];
        
        DISPATCH_TYPED();
    }

    LABEL_OP_DIV_I32_TYPED: {
        uint8_t dst = *vm.ip++;
        uint8_t left = *vm.ip++;
        uint8_t right = *vm.ip++;
        
        // Keep zero check for safety, but remove type checks
        if (vm.typed_regs.i32_regs[right] == 0) {
            runtimeError(ERROR_RUNTIME, (SrcLocation){NULL, 0, 0}, "Division by zero");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        
        vm.typed_regs.i32_regs[dst] = vm.typed_regs.i32_regs[left] / vm.typed_regs.i32_regs[right];
        
        DISPATCH_TYPED();
    }

    LABEL_OP_MOD_I32_TYPED: {
        uint8_t dst = *vm.ip++;
        uint8_t left = *vm.ip++;
        uint8_t right = *vm.ip++;
        
        if (vm.typed_regs.i32_regs[right] == 0) {
            runtimeError(ERROR_RUNTIME, (SrcLocation){NULL, 0, 0}, "Modulo by zero");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        
        vm.typed_regs.i32_regs[dst] = vm.typed_regs.i32_regs[left] % vm.typed_regs.i32_regs[right];
        
        DISPATCH_TYPED();
    }

    LABEL_OP_ADD_I64_TYPED: {
        uint8_t dst = *vm.ip++;
        uint8_t left = *vm.ip++;
        uint8_t right = *vm.ip++;
        
        vm.typed_regs.i64_regs[dst] = vm.typed_regs.i64_regs[left] + vm.typed_regs.i64_regs[right];
        
        DISPATCH_TYPED();
    }

    LABEL_OP_SUB_I64_TYPED: {
        uint8_t dst = *vm.ip++;
        uint8_t left = *vm.ip++;
        uint8_t right = *vm.ip++;
        
        vm.typed_regs.i64_regs[dst] = vm.typed_regs.i64_regs[left] - vm.typed_regs.i64_regs[right];
        
        DISPATCH_TYPED();
    }

    LABEL_OP_MUL_I64_TYPED: {
        uint8_t dst = *vm.ip++;
        uint8_t left = *vm.ip++;
        uint8_t right = *vm.ip++;
        
        vm.typed_regs.i64_regs[dst] = vm.typed_regs.i64_regs[left] * vm.typed_regs.i64_regs[right];
        
        DISPATCH_TYPED();
    }

    LABEL_OP_DIV_I64_TYPED: {
        uint8_t dst = *vm.ip++;
        uint8_t left = *vm.ip++;
        uint8_t right = *vm.ip++;
        
        if (vm.typed_regs.i64_regs[right] == 0) {
            runtimeError(ERROR_RUNTIME, (SrcLocation){NULL, 0, 0}, "Division by zero");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        
        vm.typed_regs.i64_regs[dst] = vm.typed_regs.i64_regs[left] / vm.typed_regs.i64_regs[right];
        
        DISPATCH_TYPED();
    }

    LABEL_OP_MOD_I64_TYPED: {
        uint8_t dst = *vm.ip++;
        uint8_t left = *vm.ip++;
        uint8_t right = *vm.ip++;
        
        if (vm.typed_regs.i64_regs[right] == 0) {
            runtimeError(ERROR_RUNTIME, (SrcLocation){NULL, 0, 0}, "Division by zero");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        
        vm.typed_regs.i64_regs[dst] = vm.typed_regs.i64_regs[left] % vm.typed_regs.i64_regs[right];
        
        DISPATCH_TYPED();
    }

    LABEL_OP_ADD_F64_TYPED: {
        uint8_t dst = *vm.ip++;
        uint8_t left = *vm.ip++;
        uint8_t right = *vm.ip++;
        
        vm.typed_regs.f64_regs[dst] = vm.typed_regs.f64_regs[left] + vm.typed_regs.f64_regs[right];
        
        DISPATCH_TYPED();
    }

    LABEL_OP_SUB_F64_TYPED: {
        uint8_t dst = *vm.ip++;
        uint8_t left = *vm.ip++;
        uint8_t right = *vm.ip++;
        
        vm.typed_regs.f64_regs[dst] = vm.typed_regs.f64_regs[left] - vm.typed_regs.f64_regs[right];
        
        DISPATCH_TYPED();
    }

    LABEL_OP_MUL_F64_TYPED: {
        uint8_t dst = *vm.ip++;
        uint8_t left = *vm.ip++;
        uint8_t right = *vm.ip++;
        
        vm.typed_regs.f64_regs[dst] = vm.typed_regs.f64_regs[left] * vm.typed_regs.f64_regs[right];
        
        DISPATCH_TYPED();
    }

    LABEL_OP_DIV_F64_TYPED: {
        uint8_t dst = *vm.ip++;
        uint8_t left = *vm.ip++;
        uint8_t right = *vm.ip++;
        
        vm.typed_regs.f64_regs[dst] = vm.typed_regs.f64_regs[left] / vm.typed_regs.f64_regs[right];
        
        DISPATCH_TYPED();
    }

    LABEL_OP_MOD_F64_TYPED: {
        uint8_t dst = *vm.ip++;
        uint8_t left = *vm.ip++;
        uint8_t right = *vm.ip++;
        
        vm.typed_regs.f64_regs[dst] = fmod(vm.typed_regs.f64_regs[left], vm.typed_regs.f64_regs[right]);
        
        DISPATCH_TYPED();
    }

    LABEL_OP_LT_I32_TYPED: {
        uint8_t dst = *vm.ip++;
        uint8_t left = *vm.ip++;
        uint8_t right = *vm.ip++;
        
        vm.typed_regs.bool_regs[dst] = vm.typed_regs.i32_regs[left] < vm.typed_regs.i32_regs[right];
        
        DISPATCH_TYPED();
    }

    LABEL_OP_LE_I32_TYPED: {
        uint8_t dst = *vm.ip++;
        uint8_t left = *vm.ip++;
        uint8_t right = *vm.ip++;
        
        vm.typed_regs.bool_regs[dst] = vm.typed_regs.i32_regs[left] <= vm.typed_regs.i32_regs[right];
        
        DISPATCH_TYPED();
    }

    LABEL_OP_GT_I32_TYPED: {
        uint8_t dst = *vm.ip++;
        uint8_t left = *vm.ip++;
        uint8_t right = *vm.ip++;
        
        vm.typed_regs.bool_regs[dst] = vm.typed_regs.i32_regs[left] > vm.typed_regs.i32_regs[right];
        
        DISPATCH_TYPED();
    }

    LABEL_OP_GE_I32_TYPED: {
        uint8_t dst = *vm.ip++;
        uint8_t left = *vm.ip++;
        uint8_t right = *vm.ip++;
        
        vm.typed_regs.bool_regs[dst] = vm.typed_regs.i32_regs[left] >= vm.typed_regs.i32_regs[right];
        
        DISPATCH_TYPED();
    }

    LABEL_OP_LOAD_I32_CONST: {
        uint8_t reg = READ_BYTE();
        uint16_t constantIndex = READ_SHORT();
        int32_t value = READ_CONSTANT(constantIndex).as.i32;
        
        vm.typed_regs.i32_regs[reg] = value;
        vm.typed_regs.reg_types[reg] = REG_TYPE_I32;
        
        DISPATCH();
    }

    LABEL_OP_LOAD_I64_CONST: {
        uint8_t reg = READ_BYTE();
        uint16_t constantIndex = READ_SHORT();
        int64_t value = READ_CONSTANT(constantIndex).as.i64;
        
        vm.typed_regs.i64_regs[reg] = value;
        vm.typed_regs.reg_types[reg] = REG_TYPE_I64;
        
        DISPATCH();
    }

    LABEL_OP_LOAD_F64_CONST: {
        uint8_t reg = READ_BYTE();
        uint16_t constantIndex = READ_SHORT();
        double value = READ_CONSTANT(constantIndex).as.f64;
        
        vm.typed_regs.f64_regs[reg] = value;
        vm.typed_regs.reg_types[reg] = REG_TYPE_F64;
        
        DISPATCH();
    }

    LABEL_OP_MOVE_I32: {
        uint8_t dst = READ_BYTE();
        uint8_t src = READ_BYTE();
        
        vm.typed_regs.i32_regs[dst] = vm.typed_regs.i32_regs[src];
        vm.typed_regs.reg_types[dst] = REG_TYPE_I32;
        
        DISPATCH();
    }

    LABEL_OP_MOVE_I64: {
        uint8_t dst = READ_BYTE();
        uint8_t src = READ_BYTE();
        
        vm.typed_regs.i64_regs[dst] = vm.typed_regs.i64_regs[src];
        vm.typed_regs.reg_types[dst] = REG_TYPE_I64;
        
        DISPATCH();
    }

    LABEL_OP_MOVE_F64: {
        uint8_t dst = READ_BYTE();
        uint8_t src = READ_BYTE();
        
        vm.typed_regs.f64_regs[dst] = vm.typed_regs.f64_regs[src];
        vm.typed_regs.reg_types[dst] = REG_TYPE_F64;
        
        DISPATCH();
    }

    // U32 Typed Operations
    LABEL_OP_ADD_U32_TYPED: {
        uint8_t dst = *vm.ip++;
        uint8_t left = *vm.ip++;
        uint8_t right = *vm.ip++;
        
        vm.typed_regs.u32_regs[dst] = vm.typed_regs.u32_regs[left] + vm.typed_regs.u32_regs[right];
        vm.typed_regs.reg_types[dst] = REG_TYPE_U32;
        
        DISPATCH_TYPED();
    }

    LABEL_OP_SUB_U32_TYPED: {
        uint8_t dst = *vm.ip++;
        uint8_t left = *vm.ip++;
        uint8_t right = *vm.ip++;
        
        vm.typed_regs.u32_regs[dst] = vm.typed_regs.u32_regs[left] - vm.typed_regs.u32_regs[right];
        vm.typed_regs.reg_types[dst] = REG_TYPE_U32;
        
        DISPATCH_TYPED();
    }

    LABEL_OP_MUL_U32_TYPED: {
        uint8_t dst = *vm.ip++;
        uint8_t left = *vm.ip++;
        uint8_t right = *vm.ip++;
        
        vm.typed_regs.u32_regs[dst] = vm.typed_regs.u32_regs[left] * vm.typed_regs.u32_regs[right];
        vm.typed_regs.reg_types[dst] = REG_TYPE_U32;
        
        DISPATCH_TYPED();
    }

    LABEL_OP_DIV_U32_TYPED: {
        uint8_t dst = *vm.ip++;
        uint8_t left = *vm.ip++;
        uint8_t right = *vm.ip++;
        
        if (vm.typed_regs.u32_regs[right] == 0) {
            runtimeError(ERROR_RUNTIME, (SrcLocation){NULL, 0, 0}, "Division by zero");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        
        vm.typed_regs.u32_regs[dst] = vm.typed_regs.u32_regs[left] / vm.typed_regs.u32_regs[right];
        vm.typed_regs.reg_types[dst] = REG_TYPE_U32;
        
        DISPATCH_TYPED();
    }

    LABEL_OP_MOD_U32_TYPED: {
        uint8_t dst = *vm.ip++;
        uint8_t left = *vm.ip++;
        uint8_t right = *vm.ip++;
        
        if (vm.typed_regs.u32_regs[right] == 0) {
            runtimeError(ERROR_RUNTIME, (SrcLocation){NULL, 0, 0}, "Division by zero");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        
        vm.typed_regs.u32_regs[dst] = vm.typed_regs.u32_regs[left] % vm.typed_regs.u32_regs[right];
        vm.typed_regs.reg_types[dst] = REG_TYPE_U32;
        
        DISPATCH_TYPED();
    }

    // U64 Typed Operations
    LABEL_OP_ADD_U64_TYPED: {
        uint8_t dst = *vm.ip++;
        uint8_t left = *vm.ip++;
        uint8_t right = *vm.ip++;
        
        vm.typed_regs.u64_regs[dst] = vm.typed_regs.u64_regs[left] + vm.typed_regs.u64_regs[right];
        vm.typed_regs.reg_types[dst] = REG_TYPE_U64;
        
        DISPATCH_TYPED();
    }

    LABEL_OP_SUB_U64_TYPED: {
        uint8_t dst = *vm.ip++;
        uint8_t left = *vm.ip++;
        uint8_t right = *vm.ip++;
        
        vm.typed_regs.u64_regs[dst] = vm.typed_regs.u64_regs[left] - vm.typed_regs.u64_regs[right];
        vm.typed_regs.reg_types[dst] = REG_TYPE_U64;
        
        DISPATCH_TYPED();
    }

    LABEL_OP_MUL_U64_TYPED: {
        uint8_t dst = *vm.ip++;
        uint8_t left = *vm.ip++;
        uint8_t right = *vm.ip++;
        
        vm.typed_regs.u64_regs[dst] = vm.typed_regs.u64_regs[left] * vm.typed_regs.u64_regs[right];
        vm.typed_regs.reg_types[dst] = REG_TYPE_U64;
        
        DISPATCH_TYPED();
    }

    LABEL_OP_DIV_U64_TYPED: {
        uint8_t dst = *vm.ip++;
        uint8_t left = *vm.ip++;
        uint8_t right = *vm.ip++;
        
        if (vm.typed_regs.u64_regs[right] == 0) {
            runtimeError(ERROR_RUNTIME, (SrcLocation){NULL, 0, 0}, "Division by zero");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        
        vm.typed_regs.u64_regs[dst] = vm.typed_regs.u64_regs[left] / vm.typed_regs.u64_regs[right];
        vm.typed_regs.reg_types[dst] = REG_TYPE_U64;
        
        DISPATCH_TYPED();
    }

    LABEL_OP_MOD_U64_TYPED: {
        uint8_t dst = *vm.ip++;
        uint8_t left = *vm.ip++;
        uint8_t right = *vm.ip++;
        
        if (vm.typed_regs.u64_regs[right] == 0) {
            runtimeError(ERROR_RUNTIME, (SrcLocation){NULL, 0, 0}, "Division by zero");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        
        vm.typed_regs.u64_regs[dst] = vm.typed_regs.u64_regs[left] % vm.typed_regs.u64_regs[right];
        vm.typed_regs.reg_types[dst] = REG_TYPE_U64;
        
        DISPATCH_TYPED();
    }

    // Mixed-Type Arithmetic Operations (I32 op F64)
    LABEL_OP_ADD_I32_F64: {
        uint8_t dst = READ_BYTE();
        uint8_t i32_reg = READ_BYTE();
        uint8_t f64_reg = READ_BYTE();
        
        if (!IS_I32(vm.registers[i32_reg]) || !IS_F64(vm.registers[f64_reg])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Mixed-type operation requires i32 and f64 operands");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        
        double result = (double)AS_I32(vm.registers[i32_reg]) + AS_F64(vm.registers[f64_reg]);
        vm.registers[dst] = F64_VAL(result);
        
        DISPATCH();
    }

    LABEL_OP_SUB_I32_F64: {
        uint8_t dst = READ_BYTE();
        uint8_t i32_reg = READ_BYTE();
        uint8_t f64_reg = READ_BYTE();
        
        if (!IS_I32(vm.registers[i32_reg]) || !IS_F64(vm.registers[f64_reg])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Mixed-type operation requires i32 and f64 operands");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        
        double result = (double)AS_I32(vm.registers[i32_reg]) - AS_F64(vm.registers[f64_reg]);
        vm.registers[dst] = F64_VAL(result);
        
        DISPATCH();
    }

    LABEL_OP_MUL_I32_F64: {
        uint8_t dst = READ_BYTE();
        uint8_t i32_reg = READ_BYTE();
        uint8_t f64_reg = READ_BYTE();
        
        if (!IS_I32(vm.registers[i32_reg]) || !IS_F64(vm.registers[f64_reg])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Mixed-type operation requires i32 and f64 operands");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        
        double result = (double)AS_I32(vm.registers[i32_reg]) * AS_F64(vm.registers[f64_reg]);
        vm.registers[dst] = F64_VAL(result);
        
        DISPATCH();
    }

    LABEL_OP_DIV_I32_F64: {
        uint8_t dst = READ_BYTE();
        uint8_t i32_reg = READ_BYTE();
        uint8_t f64_reg = READ_BYTE();
        
        if (!IS_I32(vm.registers[i32_reg]) || !IS_F64(vm.registers[f64_reg])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Mixed-type operation requires i32 and f64 operands");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        
        double result = (double)AS_I32(vm.registers[i32_reg]) / AS_F64(vm.registers[f64_reg]);
        vm.registers[dst] = F64_VAL(result);
        
        DISPATCH();
    }

    LABEL_OP_MOD_I32_F64: {
        uint8_t dst = READ_BYTE();
        uint8_t i32_reg = READ_BYTE();
        uint8_t f64_reg = READ_BYTE();
        
        if (!IS_I32(vm.registers[i32_reg]) || !IS_F64(vm.registers[f64_reg])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Mixed-type operation requires i32 and f64 operands");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        
        double result = fmod((double)AS_I32(vm.registers[i32_reg]), AS_F64(vm.registers[f64_reg]));
        vm.registers[dst] = F64_VAL(result);
        
        DISPATCH();
    }

    // Mixed-Type Arithmetic Operations (F64 op I32)
    LABEL_OP_ADD_F64_I32: {
        uint8_t dst = READ_BYTE();
        uint8_t f64_reg = READ_BYTE();
        uint8_t i32_reg = READ_BYTE();
        
        if (!IS_F64(vm.registers[f64_reg]) || !IS_I32(vm.registers[i32_reg])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Mixed-type operation requires f64 and i32 operands");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        
        double result = AS_F64(vm.registers[f64_reg]) + (double)AS_I32(vm.registers[i32_reg]);
        vm.registers[dst] = F64_VAL(result);
        
        DISPATCH();
    }

    LABEL_OP_SUB_F64_I32: {
        uint8_t dst = READ_BYTE();
        uint8_t f64_reg = READ_BYTE();
        uint8_t i32_reg = READ_BYTE();
        
        if (!IS_F64(vm.registers[f64_reg]) || !IS_I32(vm.registers[i32_reg])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Mixed-type operation requires f64 and i32 operands");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        
        double result = AS_F64(vm.registers[f64_reg]) - (double)AS_I32(vm.registers[i32_reg]);
        vm.registers[dst] = F64_VAL(result);
        
        DISPATCH();
    }

    LABEL_OP_MUL_F64_I32: {
        uint8_t dst = READ_BYTE();
        uint8_t f64_reg = READ_BYTE();
        uint8_t i32_reg = READ_BYTE();
        
        if (!IS_F64(vm.registers[f64_reg]) || !IS_I32(vm.registers[i32_reg])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Mixed-type operation requires f64 and i32 operands");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        
        double result = AS_F64(vm.registers[f64_reg]) * (double)AS_I32(vm.registers[i32_reg]);
        vm.registers[dst] = F64_VAL(result);
        
        DISPATCH();
    }

    LABEL_OP_DIV_F64_I32: {
        uint8_t dst = READ_BYTE();
        uint8_t f64_reg = READ_BYTE();
        uint8_t i32_reg = READ_BYTE();
        
        if (!IS_F64(vm.registers[f64_reg]) || !IS_I32(vm.registers[i32_reg])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Mixed-type operation requires f64 and i32 operands");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        
        double result = AS_F64(vm.registers[f64_reg]) / (double)AS_I32(vm.registers[i32_reg]);
        vm.registers[dst] = F64_VAL(result);
        
        DISPATCH();
    }

    LABEL_OP_MOD_F64_I32: {
        uint8_t dst = READ_BYTE();
        uint8_t f64_reg = READ_BYTE();
        uint8_t i32_reg = READ_BYTE();
        
        if (!IS_F64(vm.registers[f64_reg]) || !IS_I32(vm.registers[i32_reg])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Mixed-type operation requires f64 and i32 operands");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        
        double result = fmod(AS_F64(vm.registers[f64_reg]), (double)AS_I32(vm.registers[i32_reg]));
        vm.registers[dst] = F64_VAL(result);
        
        DISPATCH();
    }

    LABEL_OP_TIME_STAMP: {
        uint8_t dst = READ_BYTE();
        
        // Get high-precision timestamp in milliseconds
        int32_t timestamp = builtin_time_stamp();
        
        // Store in both typed register and regular register for compatibility
        vm.typed_regs.i32_regs[dst] = timestamp;
        vm.typed_regs.reg_types[dst] = REG_TYPE_I32;
        vm.registers[dst] = I32_VAL(timestamp);
        
        DISPATCH();
    }

    // Phase 2.2: Fused instruction implementations for better performance
    LABEL_OP_ADD_I32_IMM: {
        uint8_t dst = *vm.ip++;
        uint8_t src = *vm.ip++;
        int32_t imm = *(int32_t*)vm.ip;
        vm.ip += 4;
        
        // Compiler ensures this is only emitted for i32 operations, so trust it
        if (!IS_I32(vm.registers[src])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operand must be i32");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        
        int32_t result = AS_I32(vm.registers[src]) + imm;
        vm.registers[dst] = I32_VAL(result);
        
        DISPATCH_TYPED();
    }

    LABEL_OP_SUB_I32_IMM: {
        uint8_t dst = *vm.ip++;
        uint8_t src = *vm.ip++;
        int32_t imm = *(int32_t*)vm.ip;
        vm.ip += 4;
        
        // Compiler ensures this is only emitted for i32 operations, so trust it
        if (!IS_I32(vm.registers[src])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operand must be i32");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        
        int32_t result = AS_I32(vm.registers[src]) - imm;
        vm.registers[dst] = I32_VAL(result);
        
        DISPATCH_TYPED();
    }

    LABEL_OP_MUL_I32_IMM: {
        uint8_t dst = *vm.ip++;
        uint8_t src = *vm.ip++;
        int32_t imm = *(int32_t*)vm.ip;
        vm.ip += 4;
        
        // Compiler ensures this is only emitted for i32 operations, so trust it
        if (!IS_I32(vm.registers[src])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operand must be i32");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        
        int32_t result = AS_I32(vm.registers[src]) * imm;
        vm.registers[dst] = I32_VAL(result);
        
        DISPATCH_TYPED();
    }

    LABEL_OP_CMP_I32_IMM: {
        uint8_t dst = *vm.ip++;
        uint8_t src = *vm.ip++;
        int32_t imm = *(int32_t*)vm.ip;
        vm.ip += 4;
        
        vm.typed_regs.bool_regs[dst] = vm.typed_regs.i32_regs[src] < imm;
        
        DISPATCH_TYPED();
    }

    LABEL_OP_INC_CMP_JMP: {
        uint8_t reg = *vm.ip++;
        uint8_t limit_reg = *vm.ip++;
        int16_t offset = *(int16_t*)vm.ip;
        vm.ip += 2;
        
        // Compiler ensures this is only emitted for i32 operations, so trust it
        if (!IS_I32(vm.registers[reg]) || !IS_I32(vm.registers[limit_reg])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i32");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        
        int32_t incremented = AS_I32(vm.registers[reg]) + 1;
        vm.registers[reg] = I32_VAL(incremented);
        if (incremented < AS_I32(vm.registers[limit_reg])) {
            vm.ip += offset;
        }
        
        DISPATCH_TYPED();
    }

    LABEL_OP_DEC_CMP_JMP: {
        uint8_t reg = *vm.ip++;
        uint8_t zero_test = *vm.ip++;
        int16_t offset = *(int16_t*)vm.ip;
        vm.ip += 2;
        
        // Fused decrement + compare + conditional jump
        if (--vm.typed_regs.i32_regs[reg] > vm.typed_regs.i32_regs[zero_test]) {
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

    LABEL_OP_CLOSURE_R: {
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
        DISPATCH();
    }

    LABEL_OP_GET_UPVALUE_R: {
        uint8_t dstReg = READ_BYTE();
        uint8_t upvalueIndex = READ_BYTE();
        
        ObjClosure* closure = AS_CLOSURE(vm.registers[0]); // Current closure
        vm.registers[dstReg] = *closure->upvalues[upvalueIndex]->location;
        DISPATCH();
    }

    LABEL_OP_SET_UPVALUE_R: {
        uint8_t upvalueIndex = READ_BYTE();
        uint8_t valueReg = READ_BYTE();
        
        ObjClosure* closure = AS_CLOSURE(vm.registers[0]); // Current closure
        *closure->upvalues[upvalueIndex]->location = vm.registers[valueReg];
        DISPATCH();
    }

    LABEL_OP_CLOSE_UPVALUE_R: {
        uint8_t localReg = READ_BYTE();
        closeUpvalues(&vm.registers[localReg]);
        DISPATCH();
    }

    LABEL_OP_HALT:
        vm.lastExecutionTime = get_time_vm() - start_time;
        RETURN(INTERPRET_OK);

    LABEL_UNKNOWN: __attribute__((unused))
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL, 0, 0},
                    "Unknown opcode: %d", instruction);
        RETURN(INTERPRET_RUNTIME_ERROR);

    #undef RETURN
}
#endif // USE_COMPUTED_GOTO

// Extended overflow handling for all arithmetic operations and types

// Division macros with zero-check
#define HANDLE_I32_OVERFLOW_DIV(a, b, dst_reg) \
    do { \
        if (unlikely(b == 0)) { \
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                       "Division by zero"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        if (unlikely(a == INT32_MIN && b == -1)) { \
            /* Division overflow: promote to i64 */ \
            vm.registers[dst_reg] = I64_VAL((int64_t)INT32_MAX + 1); \
        } else { \
            vm.registers[dst_reg] = I32_VAL(a / b); \
        } \
    } while (0)

#define HANDLE_I32_OVERFLOW_MOD(a, b, dst_reg) \
    do { \
        if (unlikely(b == 0)) { \
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                       "Division by zero"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        if (unlikely(a == INT32_MIN && b == -1)) { \
            /* Modulo overflow case: result is 0 */ \
            vm.registers[dst_reg] = I32_VAL(0); \
        } else { \
            vm.registers[dst_reg] = I32_VAL(a % b); \
        } \
    } while (0)

// Unsigned 32-bit overflow handling
#define HANDLE_U32_OVERFLOW_ADD(a, b, dst_reg) \
    do { \
        uint32_t result; \
        if (unlikely(__builtin_add_overflow(a, b, &result))) { \
            /* Overflow detected: promote to u64 and continue */ \
            uint64_t result64 = (uint64_t)(a) + (uint64_t)(b); \
            vm.registers[dst_reg] = U64_VAL(result64); \
        } else { \
            vm.registers[dst_reg] = U32_VAL(result); \
        } \
    } while (0)

#define HANDLE_U32_OVERFLOW_SUB(a, b, dst_reg) \
    do { \
        uint32_t result; \
        if (unlikely(__builtin_sub_overflow(a, b, &result))) { \
            /* Underflow detected: error or wrap behavior */ \
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                       "Unsigned integer underflow"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } else { \
            vm.registers[dst_reg] = U32_VAL(result); \
        } \
    } while (0)

#define HANDLE_U32_OVERFLOW_MUL(a, b, dst_reg) \
    do { \
        uint32_t result; \
        if (unlikely(__builtin_mul_overflow(a, b, &result))) { \
            /* Overflow detected: promote to u64 and continue */ \
            uint64_t result64 = (uint64_t)(a) * (uint64_t)(b); \
            vm.registers[dst_reg] = U64_VAL(result64); \
        } else { \
            vm.registers[dst_reg] = U32_VAL(result); \
        } \
    } while (0)

#define HANDLE_U32_OVERFLOW_DIV(a, b, dst_reg) \
    do { \
        if (unlikely(b == 0)) { \
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                       "Division by zero"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm.registers[dst_reg] = U32_VAL(a / b); \
    } while (0)

#define HANDLE_U32_OVERFLOW_MOD(a, b, dst_reg) \
    do { \
        if (unlikely(b == 0)) { \
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                       "Division by zero"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm.registers[dst_reg] = U32_VAL(a % b); \
    } while (0)

// 64-bit overflow handling (already implemented in mixed macros but standalone versions)
#define HANDLE_I64_OVERFLOW_ADD(a, b, dst_reg) \
    do { \
        int64_t result; \
        if (unlikely(__builtin_add_overflow(a, b, &result))) { \
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                       "Integer overflow: result exceeds i64 range"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm.registers[dst_reg] = I64_VAL(result); \
    } while (0)

#define HANDLE_I64_OVERFLOW_SUB(a, b, dst_reg) \
    do { \
        int64_t result; \
        if (unlikely(__builtin_sub_overflow(a, b, &result))) { \
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                       "Integer overflow: result exceeds i64 range"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm.registers[dst_reg] = I64_VAL(result); \
    } while (0)

#define HANDLE_I64_OVERFLOW_MUL(a, b, dst_reg) \
    do { \
        int64_t result; \
        if (unlikely(__builtin_mul_overflow(a, b, &result))) { \
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                       "Integer overflow: result exceeds i64 range"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm.registers[dst_reg] = I64_VAL(result); \
    } while (0)

#define HANDLE_I64_OVERFLOW_DIV(a, b, dst_reg) \
    do { \
        if (unlikely(b == 0)) { \
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                       "Division by zero"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        if (unlikely(a == INT64_MIN && b == -1)) { \
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                       "Integer overflow: result exceeds i64 range"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm.registers[dst_reg] = I64_VAL(a / b); \
    } while (0)

#define HANDLE_I64_OVERFLOW_MOD(a, b, dst_reg) \
    do { \
        if (unlikely(b == 0)) { \
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                       "Division by zero"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        if (unlikely(a == INT64_MIN && b == -1)) { \
            vm.registers[dst_reg] = I64_VAL(0); \
        } else { \
            vm.registers[dst_reg] = I64_VAL(a % b); \
        } \
    } while (0)

// Unsigned 64-bit overflow handling
#define HANDLE_U64_OVERFLOW_ADD(a, b, dst_reg) \
    do { \
        uint64_t result; \
        if (unlikely(__builtin_add_overflow(a, b, &result))) { \
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                       "Unsigned integer overflow: result exceeds u64 range"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm.registers[dst_reg] = U64_VAL(result); \
    } while (0)

#define HANDLE_U64_OVERFLOW_SUB(a, b, dst_reg) \
    do { \
        uint64_t result; \
        if (unlikely(__builtin_sub_overflow(a, b, &result))) { \
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                       "Unsigned integer underflow"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm.registers[dst_reg] = U64_VAL(result); \
    } while (0)

#define HANDLE_U64_OVERFLOW_MUL(a, b, dst_reg) \
    do { \
        uint64_t result; \
        if (unlikely(__builtin_mul_overflow(a, b, &result))) { \
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                       "Unsigned integer overflow: result exceeds u64 range"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm.registers[dst_reg] = U64_VAL(result); \
    } while (0)

#define HANDLE_U64_OVERFLOW_DIV(a, b, dst_reg) \
    do { \
        if (unlikely(b == 0)) { \
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                       "Division by zero"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm.registers[dst_reg] = U64_VAL(a / b); \
    } while (0)

#define HANDLE_U64_OVERFLOW_MOD(a, b, dst_reg) \
    do { \
        if (unlikely(b == 0)) { \
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                       "Division by zero"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm.registers[dst_reg] = U64_VAL(a % b); \
    } while (0)

// Floating-point overflow handling
#define HANDLE_F64_OVERFLOW_ADD(a, b, dst_reg) \
    do { \
        double result = a + b; \
        if (unlikely(!isfinite(result))) { \
            if (isnan(result)) { \
                runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                           "Floating-point operation resulted in NaN"); \
            } else { \
                runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                           "Floating-point overflow: result is infinite"); \
            } \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm.registers[dst_reg] = F64_VAL(result); \
    } while (0)

#define HANDLE_F64_OVERFLOW_SUB(a, b, dst_reg) \
    do { \
        double result = a - b; \
        if (unlikely(!isfinite(result))) { \
            if (isnan(result)) { \
                runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                           "Floating-point operation resulted in NaN"); \
            } else { \
                runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                           "Floating-point overflow: result is infinite"); \
            } \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm.registers[dst_reg] = F64_VAL(result); \
    } while (0)

#define HANDLE_F64_OVERFLOW_MUL(a, b, dst_reg) \
    do { \
        double result = a * b; \
        if (unlikely(!isfinite(result))) { \
            if (isnan(result)) { \
                runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                           "Floating-point operation resulted in NaN"); \
            } else { \
                runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                           "Floating-point overflow: result is infinite"); \
            } \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm.registers[dst_reg] = F64_VAL(result); \
    } while (0)

#define HANDLE_F64_OVERFLOW_DIV(a, b, dst_reg) \
    do { \
        if (unlikely(b == 0.0)) { \
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                       "Division by zero"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        double result = a / b; \
        if (unlikely(!isfinite(result))) { \
            if (isnan(result)) { \
                runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                           "Floating-point operation resulted in NaN"); \
            } else { \
                runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                           "Floating-point overflow: result is infinite"); \
            } \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm.registers[dst_reg] = F64_VAL(result); \
    } while (0)

// Extended mixed-type arithmetic supporting all numeric types with intelligent promotion
// Priority: u32 < i32 < u64 < i64 < f64

// Enhanced mixed-type ADD with full type support
#define HANDLE_MIXED_ADD_ENHANCED(val1, val2, dst_reg) \
    do { \
        if (IS_F64(val1) || IS_F64(val2)) { \
            /* Float arithmetic: promote both to f64 */ \
            double a = IS_F64(val1) ? AS_F64(val1) : \
                      IS_I32(val1) ? (double)AS_I32(val1) : \
                      IS_I64(val1) ? (double)AS_I64(val1) : \
                      IS_U32(val1) ? (double)AS_U32(val1) : (double)AS_U64(val1); \
            double b = IS_F64(val2) ? AS_F64(val2) : \
                      IS_I32(val2) ? (double)AS_I32(val2) : \
                      IS_I64(val2) ? (double)AS_I64(val2) : \
                      IS_U32(val2) ? (double)AS_U32(val2) : (double)AS_U64(val2); \
            HANDLE_F64_OVERFLOW_ADD(a, b, dst_reg); \
        } else if (IS_I32(val1) && IS_I32(val2)) { \
            HANDLE_I32_OVERFLOW_ADD(AS_I32(val1), AS_I32(val2), dst_reg); \
        } else if (IS_I64(val1) && IS_I64(val2)) { \
            HANDLE_I64_OVERFLOW_ADD(AS_I64(val1), AS_I64(val2), dst_reg); \
        } else if (IS_U32(val1) && IS_U32(val2)) { \
            HANDLE_U32_OVERFLOW_ADD(AS_U32(val1), AS_U32(val2), dst_reg); \
        } else if (IS_U64(val1) && IS_U64(val2)) { \
            HANDLE_U64_OVERFLOW_ADD(AS_U64(val1), AS_U64(val2), dst_reg); \
        } else { \
            /* Mixed integer types: promote to largest signed type */ \
            int64_t a = IS_I32(val1) ? (int64_t)AS_I32(val1) : \
                       IS_I64(val1) ? AS_I64(val1) : \
                       IS_U32(val1) ? (int64_t)AS_U32(val1) : (int64_t)AS_U64(val1); \
            int64_t b = IS_I32(val2) ? (int64_t)AS_I32(val2) : \
                       IS_I64(val2) ? AS_I64(val2) : \
                       IS_U32(val2) ? (int64_t)AS_U32(val2) : (int64_t)AS_U64(val2); \
            HANDLE_I64_OVERFLOW_ADD(a, b, dst_reg); \
        } \
    } while (0)

// Enhanced mixed-type SUB with full type support
#define HANDLE_MIXED_SUB_ENHANCED(val1, val2, dst_reg) \
    do { \
        if (IS_F64(val1) || IS_F64(val2)) { \
            /* Float arithmetic: promote both to f64 */ \
            double a = IS_F64(val1) ? AS_F64(val1) : \
                      IS_I32(val1) ? (double)AS_I32(val1) : \
                      IS_I64(val1) ? (double)AS_I64(val1) : \
                      IS_U32(val1) ? (double)AS_U32(val1) : (double)AS_U64(val1); \
            double b = IS_F64(val2) ? AS_F64(val2) : \
                      IS_I32(val2) ? (double)AS_I32(val2) : \
                      IS_I64(val2) ? (double)AS_I64(val2) : \
                      IS_U32(val2) ? (double)AS_U32(val2) : (double)AS_U64(val2); \
            HANDLE_F64_OVERFLOW_SUB(a, b, dst_reg); \
        } else if (IS_I32(val1) && IS_I32(val2)) { \
            HANDLE_I32_OVERFLOW_SUB(AS_I32(val1), AS_I32(val2), dst_reg); \
        } else if (IS_I64(val1) && IS_I64(val2)) { \
            HANDLE_I64_OVERFLOW_SUB(AS_I64(val1), AS_I64(val2), dst_reg); \
        } else if (IS_U32(val1) && IS_U32(val2)) { \
            HANDLE_U32_OVERFLOW_SUB(AS_U32(val1), AS_U32(val2), dst_reg); \
        } else if (IS_U64(val1) && IS_U64(val2)) { \
            HANDLE_U64_OVERFLOW_SUB(AS_U64(val1), AS_U64(val2), dst_reg); \
        } else { \
            /* Mixed integer types: promote to largest signed type */ \
            int64_t a = IS_I32(val1) ? (int64_t)AS_I32(val1) : \
                       IS_I64(val1) ? AS_I64(val1) : \
                       IS_U32(val1) ? (int64_t)AS_U32(val1) : (int64_t)AS_U64(val1); \
            int64_t b = IS_I32(val2) ? (int64_t)AS_I32(val2) : \
                       IS_I64(val2) ? AS_I64(val2) : \
                       IS_U32(val2) ? (int64_t)AS_U32(val2) : (int64_t)AS_U64(val2); \
            HANDLE_I64_OVERFLOW_SUB(a, b, dst_reg); \
        } \
    } while (0)

// Enhanced mixed-type MUL with full type support
#define HANDLE_MIXED_MUL_ENHANCED(val1, val2, dst_reg) \
    do { \
        if (IS_F64(val1) || IS_F64(val2)) { \
            /* Float arithmetic: promote both to f64 */ \
            double a = IS_F64(val1) ? AS_F64(val1) : \
                      IS_I32(val1) ? (double)AS_I32(val1) : \
                      IS_I64(val1) ? (double)AS_I64(val1) : \
                      IS_U32(val1) ? (double)AS_U32(val1) : (double)AS_U64(val1); \
            double b = IS_F64(val2) ? AS_F64(val2) : \
                      IS_I32(val2) ? (double)AS_I32(val2) : \
                      IS_I64(val2) ? (double)AS_I64(val2) : \
                      IS_U32(val2) ? (double)AS_U32(val2) : (double)AS_U64(val2); \
            HANDLE_F64_OVERFLOW_MUL(a, b, dst_reg); \
        } else if (IS_I32(val1) && IS_I32(val2)) { \
            HANDLE_I32_OVERFLOW_MUL(AS_I32(val1), AS_I32(val2), dst_reg); \
        } else if (IS_I64(val1) && IS_I64(val2)) { \
            HANDLE_I64_OVERFLOW_MUL(AS_I64(val1), AS_I64(val2), dst_reg); \
        } else if (IS_U32(val1) && IS_U32(val2)) { \
            HANDLE_U32_OVERFLOW_MUL(AS_U32(val1), AS_U32(val2), dst_reg); \
        } else if (IS_U64(val1) && IS_U64(val2)) { \
            HANDLE_U64_OVERFLOW_MUL(AS_U64(val1), AS_U64(val2), dst_reg); \
        } else { \
            /* Mixed integer types: promote to largest signed type */ \
            int64_t a = IS_I32(val1) ? (int64_t)AS_I32(val1) : \
                       IS_I64(val1) ? AS_I64(val1) : \
                       IS_U32(val1) ? (int64_t)AS_U32(val1) : (int64_t)AS_U64(val1); \
            int64_t b = IS_I32(val2) ? (int64_t)AS_I32(val2) : \
                       IS_I64(val2) ? AS_I64(val2) : \
                       IS_U32(val2) ? (int64_t)AS_U32(val2) : (int64_t)AS_U64(val2); \
            HANDLE_I64_OVERFLOW_MUL(a, b, dst_reg); \
        } \
    } while (0)
