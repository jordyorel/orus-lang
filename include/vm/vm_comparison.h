#ifndef ORUS_VM_COMPARISON_H
#define ORUS_VM_COMPARISON_H

#include "../../src/vm/core/vm_internal.h"

// Forward declarations for frame-aware register access
extern inline Value vm_get_register_safe(uint16_t id);
extern inline void vm_set_register_safe(uint16_t id, Value value);

// Equality comparisons
#define CMP_EQ(dst, a, b) \
    do { \
        vm_set_register_safe((dst), BOOL_VAL(valuesEqual(vm_get_register_safe(a), vm_get_register_safe(b)))); \
    } while (0)

#define CMP_NE(dst, a, b) \
    do { \
        vm_set_register_safe((dst), BOOL_VAL(!valuesEqual(vm_get_register_safe(a), vm_get_register_safe(b)))); \
    } while (0)

// Signed 32-bit comparisons
#define CMP_I32_LT(dst, a, b) \
    do { \
        Value val_a = vm_get_register_safe(a); \
        Value val_b = vm_get_register_safe(b); \
        if (!IS_I32(val_a) || !IS_I32(val_b)) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i32"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm_set_register_safe((dst), BOOL_VAL(AS_I32(val_a) < AS_I32(val_b))); \
    } while (0)

#define CMP_I32_LE(dst, a, b) \
    do { \
        Value val_a = vm_get_register_safe(a); \
        Value val_b = vm_get_register_safe(b); \
        if (!IS_I32(val_a) || !IS_I32(val_b)) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i32"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm_set_register_safe((dst), BOOL_VAL(AS_I32(val_a) <= AS_I32(val_b))); \
    } while (0)

#define CMP_I32_GT(dst, a, b) \
    do { \
        Value val_a = vm_get_register_safe(a); \
        Value val_b = vm_get_register_safe(b); \
        if (!IS_I32(val_a) || !IS_I32(val_b)) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i32"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm_set_register_safe((dst), BOOL_VAL(AS_I32(val_a) > AS_I32(val_b))); \
    } while (0)

#define CMP_I32_GE(dst, a, b) \
    do { \
        Value val_a = vm_get_register_safe(a); \
        Value val_b = vm_get_register_safe(b); \
        if (!IS_I32(val_a) || !IS_I32(val_b)) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i32"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm_set_register_safe((dst), BOOL_VAL(AS_I32(val_a) >= AS_I32(val_b))); \
    } while (0)

// Signed 64-bit comparisons
#define CMP_I64_LT(dst, a, b) \
    do { \
        Value val_a = vm_get_register_safe(a); \
        Value val_b = vm_get_register_safe(b); \
        if (!IS_I64(val_a) || !IS_I64(val_b)) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i64"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm_set_register_safe((dst), BOOL_VAL(AS_I64(val_a) < AS_I64(val_b))); \
    } while (0)

#define CMP_I64_LE(dst, a, b) \
    do { \
        Value val_a = vm_get_register_safe(a); \
        Value val_b = vm_get_register_safe(b); \
        if (!IS_I64(val_a) || !IS_I64(val_b)) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i64"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm_set_register_safe((dst), BOOL_VAL(AS_I64(val_a) <= AS_I64(val_b))); \
    } while (0)

#define CMP_I64_GT(dst, a, b) \
    do { \
        Value val_a = vm_get_register_safe(a); \
        Value val_b = vm_get_register_safe(b); \
        if (!IS_I64(val_a) || !IS_I64(val_b)) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i64"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm_set_register_safe((dst), BOOL_VAL(AS_I64(val_a) > AS_I64(val_b))); \
    } while (0)

#define CMP_I64_GE(dst, a, b) \
    do { \
        Value val_a = vm_get_register_safe(a); \
        Value val_b = vm_get_register_safe(b); \
        if (!IS_I64(val_a) || !IS_I64(val_b)) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i64"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm_set_register_safe((dst), BOOL_VAL(AS_I64(val_a) >= AS_I64(val_b))); \
    } while (0)

// Unsigned 32-bit comparisons
#define CMP_U32_LT(dst, a, b) \
    do { \
        Value val_a = vm_get_register_safe(a); \
        Value val_b = vm_get_register_safe(b); \
        if (!IS_U32(val_a) || !IS_U32(val_b)) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u32"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm_set_register_safe((dst), BOOL_VAL(AS_U32(val_a) < AS_U32(val_b))); \
    } while (0)

#define CMP_U32_LE(dst, a, b) \
    do { \
        Value val_a = vm_get_register_safe(a); \
        Value val_b = vm_get_register_safe(b); \
        if (!IS_U32(val_a) || !IS_U32(val_b)) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u32"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm_set_register_safe((dst), BOOL_VAL(AS_U32(val_a) <= AS_U32(val_b))); \
    } while (0)

#define CMP_U32_GT(dst, a, b) \
    do { \
        Value val_a = vm_get_register_safe(a); \
        Value val_b = vm_get_register_safe(b); \
        if (!IS_U32(val_a) || !IS_U32(val_b)) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u32"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm_set_register_safe((dst), BOOL_VAL(AS_U32(val_a) > AS_U32(val_b))); \
    } while (0)

#define CMP_U32_GE(dst, a, b) \
    do { \
        Value val_a = vm_get_register_safe(a); \
        Value val_b = vm_get_register_safe(b); \
        if (!IS_U32(val_a) || !IS_U32(val_b)) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u32"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm_set_register_safe((dst), BOOL_VAL(AS_U32(val_a) >= AS_U32(val_b))); \
    } while (0)

// Unsigned 64-bit comparisons
#define CMP_U64_LT(dst, a, b) \
    do { \
        Value val_a = vm_get_register_safe(a); \
        Value val_b = vm_get_register_safe(b); \
        if (!IS_U64(val_a) || !IS_U64(val_b)) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u64"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm_set_register_safe((dst), BOOL_VAL(AS_U64(val_a) < AS_U64(val_b))); \
    } while (0)

#define CMP_U64_LE(dst, a, b) \
    do { \
        Value val_a = vm_get_register_safe(a); \
        Value val_b = vm_get_register_safe(b); \
        if (!IS_U64(val_a) || !IS_U64(val_b)) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u64"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm_set_register_safe((dst), BOOL_VAL(AS_U64(val_a) <= AS_U64(val_b))); \
    } while (0)

#define CMP_U64_GT(dst, a, b) \
    do { \
        Value val_a = vm_get_register_safe(a); \
        Value val_b = vm_get_register_safe(b); \
        if (!IS_U64(val_a) || !IS_U64(val_b)) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u64"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm_set_register_safe((dst), BOOL_VAL(AS_U64(val_a) > AS_U64(val_b))); \
    } while (0)

#define CMP_U64_GE(dst, a, b) \
    do { \
        Value val_a = vm_get_register_safe(a); \
        Value val_b = vm_get_register_safe(b); \
        if (!IS_U64(val_a) || !IS_U64(val_b)) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u64"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm_set_register_safe((dst), BOOL_VAL(AS_U64(val_a) >= AS_U64(val_b))); \
    } while (0)

// Double comparisons
#define CMP_F64_LT(dst, a, b) \
    do { \
        Value val_a = vm_get_register_safe(a); \
        Value val_b = vm_get_register_safe(b); \
        if (!IS_F64(val_a) || !IS_F64(val_b)) { \
            fprintf(stderr, "[F64_LT_ERROR_TRACE] CMP_F64_LT triggered: dst=%d, a=%d, b=%d\n", (dst), (a), (b)); \
            fprintf(stderr, "[F64_LT_ERROR_TRACE] Register[%d] type: %d, Register[%d] type: %d\n", (a), val_a.type, (b), val_b.type); \
            fflush(stderr); \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be f64"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm_set_register_safe((dst), BOOL_VAL(AS_F64(val_a) < AS_F64(val_b))); \
    } while (0)

#define CMP_F64_LE(dst, a, b) \
    do { \
        Value val_a = vm_get_register_safe(a); \
        Value val_b = vm_get_register_safe(b); \
        if (!IS_F64(val_a) || !IS_F64(val_b)) { \
            fprintf(stderr, "[F64_LE_ERROR_TRACE] CMP_F64_LE triggered: dst=%d, a=%d, b=%d\n", (dst), (a), (b)); \
            fprintf(stderr, "[F64_LE_ERROR_TRACE] Register[%d] type: %d, Register[%d] type: %d\n", (a), val_a.type, (b), val_b.type); \
            fflush(stderr); \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be f64"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm_set_register_safe((dst), BOOL_VAL(AS_F64(val_a) <= AS_F64(val_b))); \
    } while (0)

#define CMP_F64_GT(dst, a, b) \
    do { \
        Value val_a = vm_get_register_safe(a); \
        Value val_b = vm_get_register_safe(b); \
        if (!IS_F64(val_a) || !IS_F64(val_b)) { \
            fprintf(stderr, "[F64_GT_ERROR_TRACE] CMP_F64_GT triggered: dst=%d, a=%d, b=%d\n", (dst), (a), (b)); \
            fprintf(stderr, "[F64_GT_ERROR_TRACE] Register[%d] type: %d, Register[%d] type: %d\n", (a), val_a.type, (b), val_b.type); \
            fflush(stderr); \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be f64"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm_set_register_safe((dst), BOOL_VAL(AS_F64(val_a) > AS_F64(val_b))); \
    } while (0)

#define CMP_F64_GE(dst, a, b) \
    do { \
        Value val_a = vm_get_register_safe(a); \
        Value val_b = vm_get_register_safe(b); \
        if (!IS_F64(val_a) || !IS_F64(val_b)) { \
            fprintf(stderr, "[F64_GE_ERROR_TRACE] CMP_F64_GE triggered: dst=%d, a=%d, b=%d\n", (dst), (a), (b)); \
            fprintf(stderr, "[F64_GE_ERROR_TRACE] Register[%d] type: %d, Register[%d] type: %d\n", (a), val_a.type, (b), val_b.type); \
            fflush(stderr); \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be f64"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm_set_register_safe((dst), BOOL_VAL(AS_F64(val_a) >= AS_F64(val_b))); \
    } while (0)

#endif // ORUS_VM_COMPARISON_H
