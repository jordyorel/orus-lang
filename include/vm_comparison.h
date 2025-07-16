#ifndef ORUS_VM_COMPARISON_H
#define ORUS_VM_COMPARISON_H

#include "../src/vm/core/vm_internal.h"

// Equality comparisons
#define CMP_EQ(dst, a, b) \
    do { \
        vm.registers[(dst)] = BOOL_VAL(valuesEqual(vm.registers[(a)], vm.registers[(b)])); \
    } while (0)

#define CMP_NE(dst, a, b) \
    do { \
        vm.registers[(dst)] = BOOL_VAL(!valuesEqual(vm.registers[(a)], vm.registers[(b)])); \
    } while (0)

// Signed 32-bit comparisons
#define CMP_I32_LT(dst, a, b) \
    do { \
        if (!IS_I32(vm.registers[(a)]) || !IS_I32(vm.registers[(b)])) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i32"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm.registers[(dst)] = BOOL_VAL(AS_I32(vm.registers[(a)]) < AS_I32(vm.registers[(b)])); \
    } while (0)

#define CMP_I32_LE(dst, a, b) \
    do { \
        if (!IS_I32(vm.registers[(a)]) || !IS_I32(vm.registers[(b)])) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i32"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm.registers[(dst)] = BOOL_VAL(AS_I32(vm.registers[(a)]) <= AS_I32(vm.registers[(b)])); \
    } while (0)

#define CMP_I32_GT(dst, a, b) \
    do { \
        if (!IS_I32(vm.registers[(a)]) || !IS_I32(vm.registers[(b)])) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i32"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm.registers[(dst)] = BOOL_VAL(AS_I32(vm.registers[(a)]) > AS_I32(vm.registers[(b)])); \
    } while (0)

#define CMP_I32_GE(dst, a, b) \
    do { \
        if (!IS_I32(vm.registers[(a)]) || !IS_I32(vm.registers[(b)])) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i32"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm.registers[(dst)] = BOOL_VAL(AS_I32(vm.registers[(a)]) >= AS_I32(vm.registers[(b)])); \
    } while (0)

// Signed 64-bit comparisons
#define CMP_I64_LT(dst, a, b) \
    do { \
        if (!IS_I64(vm.registers[(a)]) || !IS_I64(vm.registers[(b)])) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i64"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm.registers[(dst)] = BOOL_VAL(AS_I64(vm.registers[(a)]) < AS_I64(vm.registers[(b)])); \
    } while (0)

#define CMP_I64_LE(dst, a, b) \
    do { \
        if (!IS_I64(vm.registers[(a)]) || !IS_I64(vm.registers[(b)])) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i64"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm.registers[(dst)] = BOOL_VAL(AS_I64(vm.registers[(a)]) <= AS_I64(vm.registers[(b)])); \
    } while (0)

#define CMP_I64_GT(dst, a, b) \
    do { \
        if (!IS_I64(vm.registers[(a)]) || !IS_I64(vm.registers[(b)])) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i64"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm.registers[(dst)] = BOOL_VAL(AS_I64(vm.registers[(a)]) > AS_I64(vm.registers[(b)])); \
    } while (0)

#define CMP_I64_GE(dst, a, b) \
    do { \
        if (!IS_I64(vm.registers[(a)]) || !IS_I64(vm.registers[(b)])) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i64"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm.registers[(dst)] = BOOL_VAL(AS_I64(vm.registers[(a)]) >= AS_I64(vm.registers[(b)])); \
    } while (0)

// Unsigned 32-bit comparisons
#define CMP_U32_LT(dst, a, b) \
    do { \
        if (!IS_U32(vm.registers[(a)]) || !IS_U32(vm.registers[(b)])) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u32"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm.registers[(dst)] = BOOL_VAL(AS_U32(vm.registers[(a)]) < AS_U32(vm.registers[(b)])); \
    } while (0)

#define CMP_U32_LE(dst, a, b) \
    do { \
        if (!IS_U32(vm.registers[(a)]) || !IS_U32(vm.registers[(b)])) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u32"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm.registers[(dst)] = BOOL_VAL(AS_U32(vm.registers[(a)]) <= AS_U32(vm.registers[(b)])); \
    } while (0)

#define CMP_U32_GT(dst, a, b) \
    do { \
        if (!IS_U32(vm.registers[(a)]) || !IS_U32(vm.registers[(b)])) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u32"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm.registers[(dst)] = BOOL_VAL(AS_U32(vm.registers[(a)]) > AS_U32(vm.registers[(b)])); \
    } while (0)

#define CMP_U32_GE(dst, a, b) \
    do { \
        if (!IS_U32(vm.registers[(a)]) || !IS_U32(vm.registers[(b)])) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u32"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm.registers[(dst)] = BOOL_VAL(AS_U32(vm.registers[(a)]) >= AS_U32(vm.registers[(b)])); \
    } while (0)

// Unsigned 64-bit comparisons
#define CMP_U64_LT(dst, a, b) \
    do { \
        if (!IS_U64(vm.registers[(a)]) || !IS_U64(vm.registers[(b)])) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u64"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm.registers[(dst)] = BOOL_VAL(AS_U64(vm.registers[(a)]) < AS_U64(vm.registers[(b)])); \
    } while (0)

#define CMP_U64_LE(dst, a, b) \
    do { \
        if (!IS_U64(vm.registers[(a)]) || !IS_U64(vm.registers[(b)])) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u64"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm.registers[(dst)] = BOOL_VAL(AS_U64(vm.registers[(a)]) <= AS_U64(vm.registers[(b)])); \
    } while (0)

#define CMP_U64_GT(dst, a, b) \
    do { \
        if (!IS_U64(vm.registers[(a)]) || !IS_U64(vm.registers[(b)])) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u64"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm.registers[(dst)] = BOOL_VAL(AS_U64(vm.registers[(a)]) > AS_U64(vm.registers[(b)])); \
    } while (0)

#define CMP_U64_GE(dst, a, b) \
    do { \
        if (!IS_U64(vm.registers[(a)]) || !IS_U64(vm.registers[(b)])) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u64"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm.registers[(dst)] = BOOL_VAL(AS_U64(vm.registers[(a)]) >= AS_U64(vm.registers[(b)])); \
    } while (0)

// Double comparisons
#define CMP_F64_LT(dst, a, b) \
    do { \
        if (!IS_F64(vm.registers[(a)]) || !IS_F64(vm.registers[(b)])) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be f64"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm.registers[(dst)] = BOOL_VAL(AS_F64(vm.registers[(a)]) < AS_F64(vm.registers[(b)])); \
    } while (0)

#define CMP_F64_LE(dst, a, b) \
    do { \
        if (!IS_F64(vm.registers[(a)]) || !IS_F64(vm.registers[(b)])) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be f64"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm.registers[(dst)] = BOOL_VAL(AS_F64(vm.registers[(a)]) <= AS_F64(vm.registers[(b)])); \
    } while (0)

#define CMP_F64_GT(dst, a, b) \
    do { \
        if (!IS_F64(vm.registers[(a)]) || !IS_F64(vm.registers[(b)])) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be f64"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm.registers[(dst)] = BOOL_VAL(AS_F64(vm.registers[(a)]) > AS_F64(vm.registers[(b)])); \
    } while (0)

#define CMP_F64_GE(dst, a, b) \
    do { \
        if (!IS_F64(vm.registers[(a)]) || !IS_F64(vm.registers[(b)])) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be f64"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm.registers[(dst)] = BOOL_VAL(AS_F64(vm.registers[(a)]) >= AS_F64(vm.registers[(b)])); \
    } while (0)

#endif // ORUS_VM_COMPARISON_H
