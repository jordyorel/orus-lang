/*
 * Orus Language Project
 * ---------------------------------------------------------------------------
 * File: include/vm/vm_arithmetic.h
 * Author: Jordy Orel KONDA
 * Copyright (c) 2025 Jordy Orel KONDA
 * License: MIT License (see LICENSE file in the project root)
 * Description: Declares arithmetic operation handlers for the Orus virtual machine.
 */

#ifndef ORUS_VM_ARITHMETIC_H
#define ORUS_VM_ARITHMETIC_H

#include "../../src/vm/core/vm_internal.h"
#include "vm/vm_comparison.h"
#include <math.h>

// These macros implement automatic overflow handling and type promotion
// for all arithmetic operations. They were extracted from the dispatch
// files to keep the codebase organized.

#define HANDLE_I32_OVERFLOW_ADD(a, b, dst_reg) \
    do { \
        int32_t result; \
        if (unlikely(__builtin_add_overflow(a, b, &result))) { \
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                       "Integer overflow"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } else { \
            vm_store_i32_typed_hot((dst_reg), result); \
        } \
    } while (0)

#define HANDLE_I32_OVERFLOW_SUB(a, b, dst_reg) \
    do { \
        int32_t result; \
        if (unlikely(__builtin_sub_overflow(a, b, &result))) { \
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                       "Integer overflow"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } else { \
            vm_store_i32_typed_hot((dst_reg), result); \
        } \
    } while (0)

#define HANDLE_I32_OVERFLOW_MUL(a, b, dst_reg) \
    do { \
        int32_t result; \
        if (unlikely(__builtin_mul_overflow(a, b, &result))) { \
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                       "Integer overflow"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } else { \
            vm_store_i32_typed_hot((dst_reg), result); \
        } \
    } while (0)

#define HANDLE_I32_OVERFLOW_DIV(a, b, dst_reg) \
    do { \
        if (unlikely(b == 0)) { \
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                       "Division by zero"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        if (unlikely(a == INT32_MIN && b == -1)) { \
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                       "Integer overflow"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } else { \
            vm_store_i32_typed_hot((dst_reg), a / b); \
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
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                       "Integer overflow"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } else { \
            vm_store_i32_typed_hot((dst_reg), a % b); \
        } \
    } while (0)

#define HANDLE_U32_OVERFLOW_ADD(a, b, dst_reg) \
    do { \
        uint32_t result; \
        if (unlikely(__builtin_add_overflow(a, b, &result))) { \
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                       "Unsigned integer overflow"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } else { \
            vm_store_u32_typed_hot((dst_reg), result); \
        } \
    } while (0)

#define HANDLE_U32_OVERFLOW_SUB(a, b, dst_reg) \
    do { \
        uint32_t result; \
        if (unlikely(__builtin_sub_overflow(a, b, &result))) { \
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                       "Unsigned integer underflow"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } else { \
            vm_store_u32_typed_hot((dst_reg), result); \
        } \
    } while (0)

#define HANDLE_U32_OVERFLOW_MUL(a, b, dst_reg) \
    do { \
        uint32_t result; \
        if (unlikely(__builtin_mul_overflow(a, b, &result))) { \
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                       "Unsigned integer overflow"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } else { \
            vm_store_u32_typed_hot((dst_reg), result); \
        } \
    } while (0)

#define HANDLE_U32_OVERFLOW_DIV(a, b, dst_reg) \
    do { \
        if (unlikely(b == 0)) { \
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                       "Division by zero"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm_store_u32_typed_hot((dst_reg), a / b); \
    } while (0)

#define HANDLE_U32_OVERFLOW_MOD(a, b, dst_reg) \
    do { \
        if (unlikely(b == 0)) { \
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                       "Division by zero"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm_store_u32_typed_hot((dst_reg), a % b); \
    } while (0)

#define HANDLE_I64_OVERFLOW_ADD(a, b, dst_reg) \
    do { \
        int64_t result; \
        if (unlikely(__builtin_add_overflow(a, b, &result))) { \
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                       "Integer overflow: result exceeds i64 range"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm_store_i64_typed_hot((dst_reg), result); \
    } while (0)

#define HANDLE_I64_OVERFLOW_SUB(a, b, dst_reg) \
    do { \
        int64_t result; \
        if (unlikely(__builtin_sub_overflow(a, b, &result))) { \
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                       "Integer overflow: result exceeds i64 range"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm_store_i64_typed_hot((dst_reg), result); \
    } while (0)

#define HANDLE_I64_OVERFLOW_MUL(a, b, dst_reg) \
    do { \
        int64_t result; \
        if (unlikely(__builtin_mul_overflow(a, b, &result))) { \
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                       "Integer overflow: result exceeds i64 range"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm_store_i64_typed_hot((dst_reg), result); \
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
        vm_store_i64_typed_hot((dst_reg), a / b); \
    } while (0)

#define HANDLE_I64_OVERFLOW_MOD(a, b, dst_reg) \
    do { \
        if (unlikely(b == 0)) { \
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                       "Division by zero"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        if (unlikely(a == INT64_MIN && b == -1)) { \
            vm_store_i64_typed_hot((dst_reg), 0); \
        } else { \
            vm_store_i64_typed_hot((dst_reg), a % b); \
        } \
    } while (0)

#define HANDLE_U64_OVERFLOW_ADD(a, b, dst_reg) \
    do { \
        uint64_t result; \
        if (unlikely(__builtin_add_overflow(a, b, &result))) { \
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                       "Unsigned integer overflow: result exceeds u64 range"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm_store_u64_typed_hot((dst_reg), result); \
    } while (0)

#define HANDLE_U64_OVERFLOW_SUB(a, b, dst_reg) \
    do { \
        uint64_t result; \
        if (unlikely(__builtin_sub_overflow(a, b, &result))) { \
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                       "Unsigned integer underflow"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm_store_u64_typed_hot((dst_reg), result); \
    } while (0)

#define HANDLE_U64_OVERFLOW_MUL(a, b, dst_reg) \
    do { \
        uint64_t result; \
        if (unlikely(__builtin_mul_overflow(a, b, &result))) { \
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                       "Unsigned integer overflow: result exceeds u64 range"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm_store_u64_typed_hot((dst_reg), result); \
    } while (0)

#define HANDLE_U64_OVERFLOW_DIV(a, b, dst_reg) \
    do { \
        if (unlikely(b == 0)) { \
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                       "Division by zero"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm_store_u64_typed_hot((dst_reg), a / b); \
    } while (0)

#define HANDLE_U64_OVERFLOW_MOD(a, b, dst_reg) \
    do { \
        if (unlikely(b == 0)) { \
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                       "Division by zero"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm_store_u64_typed_hot((dst_reg), a % b); \
    } while (0)

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
        store_f64_register((dst_reg), result); \
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
        store_f64_register((dst_reg), result); \
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
        store_f64_register((dst_reg), result); \
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
        store_f64_register((dst_reg), result); \
    } while (0)

#define HANDLE_MIXED_ADD(val1, val2, dst_reg) \
    do { \
        if (IS_I32(val1) && IS_I32(val2)) { \
            HANDLE_I32_OVERFLOW_ADD(AS_I32(val1), AS_I32(val2), dst_reg); \
        } else if (IS_I64(val1) && IS_I64(val2)) { \
            int64_t a = AS_I64(val1); \
            int64_t b = AS_I64(val2); \
            int64_t result; \
            if (unlikely(__builtin_add_overflow(a, b, &result))) { \
                runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                           "Integer overflow: result exceeds i64 range"); \
                RETURN(INTERPRET_RUNTIME_ERROR); \
            } \
            vm_store_i64_typed_hot((dst_reg), result); \
        } else if (IS_U32(val1) && IS_U32(val2)) { \
            uint32_t a = AS_U32(val1); \
            uint32_t b = AS_U32(val2); \
            uint32_t result; \
            if (unlikely(__builtin_add_overflow(a, b, &result))) { \
                uint64_t result64 = (uint64_t)a + (uint64_t)b; \
                vm_store_u64_typed_hot((dst_reg), result64); \
            } else { \
                vm_store_u32_typed_hot((dst_reg), result); \
            } \
        } else if (IS_U64(val1) && IS_U64(val2)) { \
            uint64_t a = AS_U64(val1); \
            uint64_t b = AS_U64(val2); \
            uint64_t result; \
            if (unlikely(__builtin_add_overflow(a, b, &result))) { \
                runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                           "Integer overflow: result exceeds u64 range"); \
                RETURN(INTERPRET_RUNTIME_ERROR); \
            } \
            vm_store_u64_typed_hot((dst_reg), result); \
        } else if (IS_F64(val1) && IS_F64(val2)) { \
            double a = AS_F64(val1); \
            double b = AS_F64(val2); \
            store_f64_register((dst_reg), a + b); \
        } else if ((IS_I32(val1) || IS_I64(val1)) && (IS_I32(val2) || IS_I64(val2))) { \
            int64_t a = IS_I64(val1) ? AS_I64(val1) : (int64_t)AS_I32(val1); \
            int64_t b = IS_I64(val2) ? AS_I64(val2) : (int64_t)AS_I32(val2); \
            int64_t result; \
            if (unlikely(__builtin_add_overflow(a, b, &result))) { \
                runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                           "Integer overflow: result exceeds i64 range"); \
                RETURN(INTERPRET_RUNTIME_ERROR); \
            } \
            vm_store_i64_typed_hot((dst_reg), result); \
        } else if ((IS_U32(val1) || IS_U64(val1)) && (IS_U32(val2) || IS_U64(val2))) { \
            uint64_t a = IS_U64(val1) ? AS_U64(val1) : (uint64_t)AS_U32(val1); \
            uint64_t b = IS_U64(val2) ? AS_U64(val2) : (uint64_t)AS_U32(val2); \
            uint64_t result; \
            if (unlikely(__builtin_add_overflow(a, b, &result))) { \
                runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                           "Integer overflow: result exceeds u64 range"); \
                RETURN(INTERPRET_RUNTIME_ERROR); \
            } \
            vm_store_u64_typed_hot((dst_reg), result); \
        } else { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, \
                        "Type mismatch: Cannot mix signed/unsigned integers or integers/floats. Use 'as' to convert explicitly."); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
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
            vm_store_i64_typed_hot((dst_reg), result); \
        } else if (IS_U32(val1) && IS_U32(val2)) { \
            uint32_t a = AS_U32(val1); \
            uint32_t b = AS_U32(val2); \
            uint32_t result; \
            if (unlikely(__builtin_sub_overflow(a, b, &result))) { \
                runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                           "Integer underflow: result exceeds u32 range"); \
                RETURN(INTERPRET_RUNTIME_ERROR); \
            } \
            vm_store_u32_typed_hot((dst_reg), result); \
        } else if (IS_U64(val1) && IS_U64(val2)) { \
            uint64_t a = AS_U64(val1); \
            uint64_t b = AS_U64(val2); \
            uint64_t result; \
            if (unlikely(__builtin_sub_overflow(a, b, &result))) { \
                runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                           "Integer underflow: result exceeds u64 range"); \
                RETURN(INTERPRET_RUNTIME_ERROR); \
            } \
            vm_store_u64_typed_hot((dst_reg), result); \
        } else if (IS_F64(val1) && IS_F64(val2)) { \
            double a = AS_F64(val1); \
            double b = AS_F64(val2); \
            store_f64_register((dst_reg), a - b); \
        } else if ((IS_I32(val1) || IS_I64(val1)) && (IS_I32(val2) || IS_I64(val2))) { \
            int64_t a = IS_I64(val1) ? AS_I64(val1) : (int64_t)AS_I32(val1); \
            int64_t b = IS_I64(val2) ? AS_I64(val2) : (int64_t)AS_I32(val2); \
            int64_t result; \
            if (unlikely(__builtin_sub_overflow(a, b, &result))) { \
                runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                           "Integer overflow: result exceeds i64 range"); \
                RETURN(INTERPRET_RUNTIME_ERROR); \
            } \
            vm_store_i64_typed_hot((dst_reg), result); \
        } else if ((IS_U32(val1) || IS_U64(val1)) && (IS_U32(val2) || IS_U64(val2))) { \
            uint64_t a = IS_U64(val1) ? AS_U64(val1) : (uint64_t)AS_U32(val1); \
            uint64_t b = IS_U64(val2) ? AS_U64(val2) : (uint64_t)AS_U32(val2); \
            uint64_t result; \
            if (unlikely(__builtin_sub_overflow(a, b, &result))) { \
                runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                           "Integer underflow: result exceeds u64 range"); \
                RETURN(INTERPRET_RUNTIME_ERROR); \
            } \
            vm_store_u64_typed_hot((dst_reg), result); \
        } else { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, \
                        "Type mismatch: Cannot mix signed/unsigned integers or integers/floats. Use 'as' to convert explicitly."); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
    } while (0)

#define HANDLE_MIXED_MUL(val1, val2, dst_reg) \
    do { \
        if (IS_F64(val1) || IS_F64(val2)) { \
            double a = IS_F64(val1) ? AS_F64(val1) : \
                      (IS_I64(val1) ? (double)AS_I64(val1) : (double)AS_I32(val1)); \
            double b = IS_F64(val2) ? AS_F64(val2) : \
                      (IS_I64(val2) ? (double)AS_I64(val2) : (double)AS_I32(val2)); \
            store_f64_register((dst_reg), a * b); \
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
            vm_store_i64_typed_hot((dst_reg), result); \
        } else { \
            int64_t a = IS_I32(val1) ? (int64_t)AS_I32(val1) : AS_I64(val1); \
            int64_t b = IS_I32(val2) ? (int64_t)AS_I32(val2) : AS_I64(val2); \
            int64_t result; \
            if (unlikely(__builtin_mul_overflow(a, b, &result))) { \
                runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, \
                           "Integer overflow: result exceeds i64 range"); \
                RETURN(INTERPRET_RUNTIME_ERROR); \
            } \
            vm_store_i64_typed_hot((dst_reg), result); \
        } \
    } while (0)

#define HANDLE_MIXED_DIV(val1, val2, dst_reg) \
    do { \
        if (IS_F64(val1) || IS_F64(val2)) { \
            double a = IS_F64(val1) ? AS_F64(val1) : \
                      (IS_I64(val1) ? (double)AS_I64(val1) : (double)AS_I32(val1)); \
            double b = IS_F64(val2) ? AS_F64(val2) : \
                      (IS_I64(val2) ? (double)AS_I64(val2) : (double)AS_I32(val2)); \
            if (b == 0.0) { \
                runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, "Division by zero"); \
                RETURN(INTERPRET_RUNTIME_ERROR); \
            } \
            store_f64_register((dst_reg), a / b); \
        } else if (IS_I32(val1) && IS_I32(val2)) { \
            int32_t a = AS_I32(val1); \
            int32_t b = AS_I32(val2); \
            if (b == 0) { \
                runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, "Division by zero"); \
                RETURN(INTERPRET_RUNTIME_ERROR); \
            } \
            if (a == INT32_MIN && b == -1) { \
                vm_store_i64_typed_hot((dst_reg), (int64_t)INT32_MAX + 1); \
            } else { \
                vm_store_i32_typed_hot((dst_reg), a / b); \
            } \
        } else { \
            int64_t a = IS_I32(val1) ? (int64_t)AS_I32(val1) : AS_I64(val1); \
            int64_t b = IS_I32(val2) ? (int64_t)AS_I32(val2) : AS_I64(val2); \
            if (b == 0) { \
                runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, "Division by zero"); \
                RETURN(INTERPRET_RUNTIME_ERROR); \
            } \
            vm_store_i64_typed_hot((dst_reg), a / b); \
        } \
    } while (0)

#define HANDLE_MIXED_MOD(val1, val2, dst_reg) \
    do { \
        if (IS_F64(val1) || IS_F64(val2)) { \
            double a = IS_F64(val1) ? AS_F64(val1) : \
                      (IS_I64(val1) ? (double)AS_I64(val1) : (double)AS_I32(val1)); \
            double b = IS_F64(val2) ? AS_F64(val2) : \
                      (IS_I64(val2) ? (double)AS_I64(val2) : (double)AS_I32(val2)); \
            if (b == 0.0) { \
                runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, "Division by zero"); \
                RETURN(INTERPRET_RUNTIME_ERROR); \
            } \
            store_f64_register((dst_reg), fmod(a, b)); \
        } else if (IS_I32(val1) && IS_I32(val2)) { \
            int32_t a = AS_I32(val1); \
            int32_t b = AS_I32(val2); \
            if (b == 0) { \
                runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, "Division by zero"); \
                RETURN(INTERPRET_RUNTIME_ERROR); \
            } \
            if (a == INT32_MIN && b == -1) { \
                vm_store_i32_typed_hot((dst_reg), 0); \
            } else { \
                vm_store_i32_typed_hot((dst_reg), a % b); \
            } \
        } else { \
            int64_t a = IS_I32(val1) ? (int64_t)AS_I32(val1) : AS_I64(val1); \
            int64_t b = IS_I32(val2) ? (int64_t)AS_I32(val2) : AS_I64(val2); \
            if (b == 0) { \
                runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, "Division by zero"); \
                RETURN(INTERPRET_RUNTIME_ERROR); \
            } \
            vm_store_i64_typed_hot((dst_reg), a % b); \
        } \
    } while (0)

#endif // ORUS_VM_ARITHMETIC_H
