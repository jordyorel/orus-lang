/*
 * Orus Language Project
 * ---------------------------------------------------------------------------
 * File: src/vm/handlers/vm_arithmetic_handlers.c
 * Author: Jordy Orel KONDA
 * Copyright (c) 2025 Jordy Orel KONDA
 * License: MIT License (see LICENSE file in the project root)
 * Description: Implements arithmetic opcode handlers invoked by the VM dispatcher.
 */

/*
 * File: src/vm/handlers/vm_arithmetic_handlers.c
 * High-performance arithmetic opcode handlers for the Orus VM
 * 
 * Design Philosophy:
 * - Static inline functions for zero-cost abstraction
 * - Preserve computed-goto dispatch performance
 * - Clean separation of opcode implementation from dispatch logic
 * - Maintain exact same behavior as original macros
 */

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

// ====== I32 Typed Arithmetic Handlers ======

void handle_add_i32_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();

    int32_t left_val;
    bool left_typed = false;
    if (!read_i32_operand(left, &left_val, &left_typed)) {
        vm_trace_loop_event(LOOP_TRACE_TYPE_MISMATCH);
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i32");
        return;
    }

    int32_t right_val;
    bool right_typed = false;
    if (!read_i32_operand(right, &right_val, &right_typed)) {
        vm_trace_loop_event(LOOP_TRACE_TYPE_MISMATCH);
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i32");
        return;
    }

    if (left_typed && right_typed) {
        vm_trace_loop_event(LOOP_TRACE_TYPED_HIT);
    } else {
        vm_trace_loop_event(LOOP_TRACE_TYPED_MISS);
    }

    vm_store_i32_typed_hot(dst, left_val + right_val);
}

void handle_sub_i32_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();

    int32_t left_val;
    bool left_typed = false;
    if (!read_i32_operand(left, &left_val, &left_typed)) {
        vm_trace_loop_event(LOOP_TRACE_TYPE_MISMATCH);
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i32");
        return;
    }

    int32_t right_val;
    bool right_typed = false;
    if (!read_i32_operand(right, &right_val, &right_typed)) {
        vm_trace_loop_event(LOOP_TRACE_TYPE_MISMATCH);
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i32");
        return;
    }

    if (left_typed && right_typed) {
        vm_trace_loop_event(LOOP_TRACE_TYPED_HIT);
    } else {
        vm_trace_loop_event(LOOP_TRACE_TYPED_MISS);
    }

    vm_store_i32_typed_hot(dst, left_val - right_val);
}

void handle_mul_i32_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();

    int32_t left_val;
    bool left_typed = false;
    if (!read_i32_operand(left, &left_val, &left_typed)) {
        vm_trace_loop_event(LOOP_TRACE_TYPE_MISMATCH);
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i32");
        return;
    }

    int32_t right_val;
    bool right_typed = false;
    if (!read_i32_operand(right, &right_val, &right_typed)) {
        vm_trace_loop_event(LOOP_TRACE_TYPE_MISMATCH);
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i32");
        return;
    }

    if (left_typed && right_typed) {
        vm_trace_loop_event(LOOP_TRACE_TYPED_HIT);
    } else {
        vm_trace_loop_event(LOOP_TRACE_TYPED_MISS);
    }

    vm_store_i32_typed_hot(dst, left_val * right_val);
}

void handle_div_i32_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();

    int32_t left_val;
    bool left_typed = false;
    if (!read_i32_operand(left, &left_val, &left_typed)) {
        vm_trace_loop_event(LOOP_TRACE_TYPE_MISMATCH);
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i32");
        return;
    }

    int32_t right_val;
    bool right_typed = false;
    if (!read_i32_operand(right, &right_val, &right_typed)) {
        vm_trace_loop_event(LOOP_TRACE_TYPE_MISMATCH);
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i32");
        return;
    }

    if (right_val == 0) {
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero");
        return;
    }

    if (left_typed && right_typed) {
        vm_trace_loop_event(LOOP_TRACE_TYPED_HIT);
    } else {
        vm_trace_loop_event(LOOP_TRACE_TYPED_MISS);
    }

    vm_store_i32_typed_hot(dst, left_val / right_val);
}

void handle_mod_i32_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();

    int32_t left_val;
    bool left_typed = false;
    if (!read_i32_operand(left, &left_val, &left_typed)) {
        vm_trace_loop_event(LOOP_TRACE_TYPE_MISMATCH);
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i32");
        return;
    }

    int32_t right_val;
    bool right_typed = false;
    if (!read_i32_operand(right, &right_val, &right_typed)) {
        vm_trace_loop_event(LOOP_TRACE_TYPE_MISMATCH);
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i32");
        return;
    }

    if (right_val == 0) {
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero");
        return;
    }

    if (left_typed && right_typed) {
        vm_trace_loop_event(LOOP_TRACE_TYPED_HIT);
    } else {
        vm_trace_loop_event(LOOP_TRACE_TYPED_MISS);
    }

    vm_store_i32_typed_hot(dst, left_val % right_val);
}

// ====== I64 Typed Arithmetic Handlers ======

void handle_add_i64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();

    int64_t left_val;
    if (!read_i64_operand(left, &left_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i64");
        return;
    }

    int64_t right_val;
    if (!read_i64_operand(right, &right_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i64");
        return;
    }

    store_i64_register(dst, left_val + right_val);
}

void handle_sub_i64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();

    int64_t left_val;
    if (!read_i64_operand(left, &left_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i64");
        return;
    }

    int64_t right_val;
    if (!read_i64_operand(right, &right_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i64");
        return;
    }

    store_i64_register(dst, left_val - right_val);
}

void handle_mul_i64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();

    int64_t left_val;
    if (!read_i64_operand(left, &left_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i64");
        return;
    }

    int64_t right_val;
    if (!read_i64_operand(right, &right_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i64");
        return;
    }

    store_i64_register(dst, left_val * right_val);
}

void handle_div_i64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();

    int64_t left_val;
    if (!read_i64_operand(left, &left_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i64");
        return;
    }

    int64_t right_val;
    if (!read_i64_operand(right, &right_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i64");
        return;
    }

    if (right_val == 0) {
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero");
        return;
    }

    store_i64_register(dst, left_val / right_val);
}

void handle_mod_i64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();

    int64_t left_val;
    if (!read_i64_operand(left, &left_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i64");
        return;
    }

    int64_t right_val;
    if (!read_i64_operand(right, &right_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i64");
        return;
    }

    if (right_val == 0) {
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero");
        return;
    }

    store_i64_register(dst, left_val % right_val);
}

// ====== F64 Typed Arithmetic Handlers ======

void handle_add_f64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();

    double left_val;
    if (!read_f64_operand(left, &left_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be f64");
        return;
    }

    double right_val;
    if (!read_f64_operand(right, &right_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be f64");
        return;
    }

    store_f64_register(dst, left_val + right_val);
}

void handle_sub_f64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();

    double left_val;
    if (!read_f64_operand(left, &left_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be f64");
        return;
    }

    double right_val;
    if (!read_f64_operand(right, &right_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be f64");
        return;
    }

    store_f64_register(dst, left_val - right_val);
}

void handle_mul_f64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();

    double left_val;
    if (!read_f64_operand(left, &left_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be f64");
        return;
    }

    double right_val;
    if (!read_f64_operand(right, &right_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be f64");
        return;
    }

    store_f64_register(dst, left_val * right_val);
}

void handle_div_f64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();

    double left_val;
    if (!read_f64_operand(left, &left_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be f64");
        return;
    }

    double right_val;
    if (!read_f64_operand(right, &right_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be f64");
        return;
    }

    if (right_val == 0.0) {
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero");
        return;
    }

    store_f64_register(dst, left_val / right_val);
}

void handle_mod_f64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();

    double left_val;
    if (!read_f64_operand(left, &left_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be f64");
        return;
    }

    double right_val;
    if (!read_f64_operand(right, &right_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be f64");
        return;
    }

    if (right_val == 0.0) {
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero");
        return;
    }

    store_f64_register(dst, fmod(left_val, right_val));
}

// ====== U32 Typed Arithmetic Handlers ======

void handle_add_u32_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();

    uint32_t left_val;
    if (!read_u32_operand(left, &left_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u32");
        return;
    }

    uint32_t right_val;
    if (!read_u32_operand(right, &right_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u32");
        return;
    }

    store_u32_register(dst, left_val + right_val);
}

void handle_sub_u32_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();

    uint32_t left_val;
    if (!read_u32_operand(left, &left_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u32");
        return;
    }

    uint32_t right_val;
    if (!read_u32_operand(right, &right_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u32");
        return;
    }

    store_u32_register(dst, left_val - right_val);
}

void handle_mul_u32_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();

    uint32_t left_val;
    if (!read_u32_operand(left, &left_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u32");
        return;
    }

    uint32_t right_val;
    if (!read_u32_operand(right, &right_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u32");
        return;
    }

    store_u32_register(dst, left_val * right_val);
}

void handle_div_u32_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();

    uint32_t left_val;
    if (!read_u32_operand(left, &left_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u32");
        return;
    }

    uint32_t right_val;
    if (!read_u32_operand(right, &right_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u32");
        return;
    }

    if (right_val == 0) {
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero");
        return;
    }

    store_u32_register(dst, left_val / right_val);
}

void handle_mod_u32_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();

    uint32_t left_val;
    if (!read_u32_operand(left, &left_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u32");
        return;
    }

    uint32_t right_val;
    if (!read_u32_operand(right, &right_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u32");
        return;
    }

    if (right_val == 0) {
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero");
        return;
    }

    store_u32_register(dst, left_val % right_val);
}

// ====== U64 Typed Arithmetic Handlers ======

void handle_add_u64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();

    uint64_t left_val;
    if (!read_u64_operand(left, &left_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u64");
        return;
    }

    uint64_t right_val;
    if (!read_u64_operand(right, &right_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u64");
        return;
    }

    store_u64_register(dst, left_val + right_val);
}

void handle_sub_u64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();

    uint64_t left_val;
    if (!read_u64_operand(left, &left_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u64");
        return;
    }

    uint64_t right_val;
    if (!read_u64_operand(right, &right_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u64");
        return;
    }

    store_u64_register(dst, left_val - right_val);
}

void handle_mul_u64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();

    uint64_t left_val;
    if (!read_u64_operand(left, &left_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u64");
        return;
    }

    uint64_t right_val;
    if (!read_u64_operand(right, &right_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u64");
        return;
    }

    store_u64_register(dst, left_val * right_val);
}

void handle_div_u64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();

    uint64_t left_val;
    if (!read_u64_operand(left, &left_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u64");
        return;
    }

    uint64_t right_val;
    if (!read_u64_operand(right, &right_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u64");
        return;
    }

    if (right_val == 0) {
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero");
        return;
    }

    store_u64_register(dst, left_val / right_val);
}

void handle_mod_u64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();

    uint64_t left_val;
    if (!read_u64_operand(left, &left_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u64");
        return;
    }

    uint64_t right_val;
    if (!read_u64_operand(right, &right_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u64");
        return;
    }

    if (right_val == 0) {
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero");
        return;
    }

    store_u64_register(dst, left_val % right_val);
}
