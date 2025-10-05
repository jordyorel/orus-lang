// Orus Language Project
// ---------------------------------------------------------------------------
// File: src/vm/handlers/vm_arithmetic_handlers.c
// Author: Jordy Orel KONDA
// Copyright (c) 2023 Jordy Orel KONDA
// License: MIT License (see LICENSE file in the project root)
// Description: Implements arithmetic opcode handlers invoked by the VM dispatcher.



#include "vm/vm_opcode_handlers.h"
#include "vm/vm_dispatch.h"
#include "vm/vm_comparison.h"
#include <math.h>

static inline bool read_i32_operand(uint16_t reg, int32_t* out, bool* typed_out) {
    if (vm_try_read_i32_typed(reg, out)) {
        if (typed_out) {
            *typed_out = true;
        }
        return true;
    }

    Value boxed = vm_get_register_safe(reg);
    if (!IS_I32(boxed)) {
        return false;
    }

    int32_t value = AS_I32(boxed);
    vm_cache_i32_typed(reg, value);
    *out = value;
    if (typed_out) {
        *typed_out = false;
    }
    return true;
}

static inline bool read_i64_operand(uint16_t reg, int64_t* out) {
    if (vm_try_read_i64_typed(reg, out)) {
        return true;
    }

    Value boxed = vm_get_register_safe(reg);
    if (!IS_I64(boxed)) {
        return false;
    }

    int64_t value = AS_I64(boxed);
    vm_cache_i64_typed(reg, value);
    *out = value;
    return true;
}

static inline bool read_u32_operand(uint16_t reg, uint32_t* out) {
    if (vm_try_read_u32_typed(reg, out)) {
        return true;
    }

    Value boxed = vm_get_register_safe(reg);
    if (!IS_U32(boxed)) {
        return false;
    }

    uint32_t value = AS_U32(boxed);
    vm_cache_u32_typed(reg, value);
    *out = value;
    return true;
}

static inline bool read_u64_operand(uint16_t reg, uint64_t* out) {
    if (vm_try_read_u64_typed(reg, out)) {
        return true;
    }

    Value boxed = vm_get_register_safe(reg);
    if (!IS_U64(boxed)) {
        return false;
    }

    uint64_t value = AS_U64(boxed);
    vm_cache_u64_typed(reg, value);
    *out = value;
    return true;
}

static inline bool read_f64_operand(uint16_t reg, double* out) {
    if (vm_try_read_f64_typed(reg, out)) {
        return true;
    }

    Value boxed = vm_get_register_safe(reg);
    if (!IS_F64(boxed)) {
        return false;
    }

    double value = AS_F64(boxed);
    vm_cache_f64_typed(reg, value);
    *out = value;
    return true;
}

#define DEFINE_TYPED_ARITH_HANDLER(OP_NAME, TYPE_SUFFIX, CTYPE, READ_FN, STORE_FN, ZERO_GUARD, RESULT_EXPR) \
    void handle_##OP_NAME##_##TYPE_SUFFIX##_typed(void) { \
        uint8_t dst = READ_BYTE(); \
        uint8_t left = READ_BYTE(); \
        uint8_t right = READ_BYTE(); \
        CTYPE left_val; \
        if (!(READ_FN(left, &left_val))) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be " #TYPE_SUFFIX); \
            return; \
        } \
        CTYPE right_val; \
        if (!(READ_FN(right, &right_val))) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be " #TYPE_SUFFIX); \
            return; \
        } \
        ZERO_GUARD; \
        STORE_FN(dst, (RESULT_EXPR)); \
    }

#define NO_EXTRA_GUARD do { } while (0)
#define GUARD_DIV_ZERO_INT do { \
    if ((right_val) == 0) { \
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero"); \
        return; \
    } \
} while (0)
#define GUARD_DIV_ZERO_F64 do { \
    if ((right_val) == 0.0) { \
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero"); \
        return; \
    } \
} while (0)

#define READ_I32_OPERAND(reg, out_ptr) read_i32_operand((reg), (out_ptr), NULL)
#define READ_I64_OPERAND(reg, out_ptr) read_i64_operand((reg), (out_ptr))
#define READ_U32_OPERAND(reg, out_ptr) read_u32_operand((reg), (out_ptr))
#define READ_U64_OPERAND(reg, out_ptr) read_u64_operand((reg), (out_ptr))
#define READ_F64_OPERAND(reg, out_ptr) read_f64_operand((reg), (out_ptr))

#define STORE_I32_RESULT(dst_reg, value) vm_store_i32_typed_hot((dst_reg), (value))
#define STORE_I64_RESULT(dst_reg, value) store_i64_register((dst_reg), (value))
#define STORE_U32_RESULT(dst_reg, value) store_u32_register((dst_reg), (value))
#define STORE_U64_RESULT(dst_reg, value) store_u64_register((dst_reg), (value))
#define STORE_F64_RESULT(dst_reg, value) store_f64_register((dst_reg), (value))

// ====== I32 Typed Arithmetic Handlers ======

DEFINE_TYPED_ARITH_HANDLER(add, i32, int32_t, READ_I32_OPERAND, STORE_I32_RESULT, NO_EXTRA_GUARD, left_val + right_val)
DEFINE_TYPED_ARITH_HANDLER(sub, i32, int32_t, READ_I32_OPERAND, STORE_I32_RESULT, NO_EXTRA_GUARD, left_val - right_val)
DEFINE_TYPED_ARITH_HANDLER(mul, i32, int32_t, READ_I32_OPERAND, STORE_I32_RESULT, NO_EXTRA_GUARD, left_val * right_val)
DEFINE_TYPED_ARITH_HANDLER(div, i32, int32_t, READ_I32_OPERAND, STORE_I32_RESULT, GUARD_DIV_ZERO_INT, left_val / right_val)
DEFINE_TYPED_ARITH_HANDLER(mod, i32, int32_t, READ_I32_OPERAND, STORE_I32_RESULT, GUARD_DIV_ZERO_INT, left_val % right_val)

// ====== I64 Typed Arithmetic Handlers ======

DEFINE_TYPED_ARITH_HANDLER(add, i64, int64_t, READ_I64_OPERAND, STORE_I64_RESULT, NO_EXTRA_GUARD, left_val + right_val)
DEFINE_TYPED_ARITH_HANDLER(sub, i64, int64_t, READ_I64_OPERAND, STORE_I64_RESULT, NO_EXTRA_GUARD, left_val - right_val)
DEFINE_TYPED_ARITH_HANDLER(mul, i64, int64_t, READ_I64_OPERAND, STORE_I64_RESULT, NO_EXTRA_GUARD, left_val * right_val)
DEFINE_TYPED_ARITH_HANDLER(div, i64, int64_t, READ_I64_OPERAND, STORE_I64_RESULT, GUARD_DIV_ZERO_INT, left_val / right_val)
DEFINE_TYPED_ARITH_HANDLER(mod, i64, int64_t, READ_I64_OPERAND, STORE_I64_RESULT, GUARD_DIV_ZERO_INT, left_val % right_val)

// ====== F64 Typed Arithmetic Handlers ======

DEFINE_TYPED_ARITH_HANDLER(add, f64, double, READ_F64_OPERAND, STORE_F64_RESULT, NO_EXTRA_GUARD, left_val + right_val)
DEFINE_TYPED_ARITH_HANDLER(sub, f64, double, READ_F64_OPERAND, STORE_F64_RESULT, NO_EXTRA_GUARD, left_val - right_val)
DEFINE_TYPED_ARITH_HANDLER(mul, f64, double, READ_F64_OPERAND, STORE_F64_RESULT, NO_EXTRA_GUARD, left_val * right_val)
DEFINE_TYPED_ARITH_HANDLER(div, f64, double, READ_F64_OPERAND, STORE_F64_RESULT, GUARD_DIV_ZERO_F64, left_val / right_val)
DEFINE_TYPED_ARITH_HANDLER(mod, f64, double, READ_F64_OPERAND, STORE_F64_RESULT, GUARD_DIV_ZERO_F64, fmod(left_val, right_val))

// ====== U32 Typed Arithmetic Handlers ======

DEFINE_TYPED_ARITH_HANDLER(add, u32, uint32_t, READ_U32_OPERAND, STORE_U32_RESULT, NO_EXTRA_GUARD, left_val + right_val)
DEFINE_TYPED_ARITH_HANDLER(sub, u32, uint32_t, READ_U32_OPERAND, STORE_U32_RESULT, NO_EXTRA_GUARD, left_val - right_val)
DEFINE_TYPED_ARITH_HANDLER(mul, u32, uint32_t, READ_U32_OPERAND, STORE_U32_RESULT, NO_EXTRA_GUARD, left_val * right_val)
DEFINE_TYPED_ARITH_HANDLER(div, u32, uint32_t, READ_U32_OPERAND, STORE_U32_RESULT, GUARD_DIV_ZERO_INT, left_val / right_val)
DEFINE_TYPED_ARITH_HANDLER(mod, u32, uint32_t, READ_U32_OPERAND, STORE_U32_RESULT, GUARD_DIV_ZERO_INT, left_val % right_val)

// ====== U64 Typed Arithmetic Handlers ======

DEFINE_TYPED_ARITH_HANDLER(add, u64, uint64_t, READ_U64_OPERAND, STORE_U64_RESULT, NO_EXTRA_GUARD, left_val + right_val)
DEFINE_TYPED_ARITH_HANDLER(sub, u64, uint64_t, READ_U64_OPERAND, STORE_U64_RESULT, NO_EXTRA_GUARD, left_val - right_val)
DEFINE_TYPED_ARITH_HANDLER(mul, u64, uint64_t, READ_U64_OPERAND, STORE_U64_RESULT, NO_EXTRA_GUARD, left_val * right_val)
DEFINE_TYPED_ARITH_HANDLER(div, u64, uint64_t, READ_U64_OPERAND, STORE_U64_RESULT, GUARD_DIV_ZERO_INT, left_val / right_val)
DEFINE_TYPED_ARITH_HANDLER(mod, u64, uint64_t, READ_U64_OPERAND, STORE_U64_RESULT, GUARD_DIV_ZERO_INT, left_val % right_val)
