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

// ====== I32 Typed Arithmetic Handlers ======

void handle_add_i32_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();

    int32_t left_val;
    bool left_typed = vm_try_read_i32_typed(left, &left_val);
    if (!left_typed) {
        Value boxed = vm_get_register_safe(left);
        if (!IS_I32(boxed)) {
            vm_trace_loop_event(LOOP_TRACE_TYPE_MISMATCH);
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i32");
            return;
        }
        left_val = AS_I32(boxed);
        vm_cache_i32_typed(left, left_val);
    }

    int32_t right_val;
    bool right_typed = vm_try_read_i32_typed(right, &right_val);
    if (!right_typed) {
        Value boxed = vm_get_register_safe(right);
        if (!IS_I32(boxed)) {
            vm_trace_loop_event(LOOP_TRACE_TYPE_MISMATCH);
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i32");
            return;
        }
        right_val = AS_I32(boxed);
        vm_cache_i32_typed(right, right_val);
    }

    if (left_typed && right_typed) {
        vm_trace_loop_event(LOOP_TRACE_TYPED_HIT);
    } else {
        vm_trace_loop_event(LOOP_TRACE_TYPED_MISS);
    }

    vm_store_i32_register(dst, left_val + right_val);
}

void handle_sub_i32_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();

    int32_t left_val;
    bool left_typed = vm_try_read_i32_typed(left, &left_val);
    if (!left_typed) {
        Value boxed = vm_get_register_safe(left);
        if (!IS_I32(boxed)) {
            vm_trace_loop_event(LOOP_TRACE_TYPE_MISMATCH);
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i32");
            return;
        }
        left_val = AS_I32(boxed);
        vm_cache_i32_typed(left, left_val);
    }

    int32_t right_val;
    bool right_typed = vm_try_read_i32_typed(right, &right_val);
    if (!right_typed) {
        Value boxed = vm_get_register_safe(right);
        if (!IS_I32(boxed)) {
            vm_trace_loop_event(LOOP_TRACE_TYPE_MISMATCH);
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i32");
            return;
        }
        right_val = AS_I32(boxed);
        vm_cache_i32_typed(right, right_val);
    }

    if (left_typed && right_typed) {
        vm_trace_loop_event(LOOP_TRACE_TYPED_HIT);
    } else {
        vm_trace_loop_event(LOOP_TRACE_TYPED_MISS);
    }

    vm_store_i32_register(dst, left_val - right_val);
}

void handle_mul_i32_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();

    int32_t left_val;
    bool left_typed = vm_try_read_i32_typed(left, &left_val);
    if (!left_typed) {
        Value boxed = vm_get_register_safe(left);
        if (!IS_I32(boxed)) {
            vm_trace_loop_event(LOOP_TRACE_TYPE_MISMATCH);
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i32");
            return;
        }
        left_val = AS_I32(boxed);
        vm_cache_i32_typed(left, left_val);
    }

    int32_t right_val;
    bool right_typed = vm_try_read_i32_typed(right, &right_val);
    if (!right_typed) {
        Value boxed = vm_get_register_safe(right);
        if (!IS_I32(boxed)) {
            vm_trace_loop_event(LOOP_TRACE_TYPE_MISMATCH);
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i32");
            return;
        }
        right_val = AS_I32(boxed);
        vm_cache_i32_typed(right, right_val);
    }

    if (left_typed && right_typed) {
        vm_trace_loop_event(LOOP_TRACE_TYPED_HIT);
    } else {
        vm_trace_loop_event(LOOP_TRACE_TYPED_MISS);
    }

    vm_store_i32_register(dst, left_val * right_val);
}

void handle_div_i32_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();

    int32_t left_val;
    bool left_typed = vm_try_read_i32_typed(left, &left_val);
    if (!left_typed) {
        Value boxed = vm_get_register_safe(left);
        if (!IS_I32(boxed)) {
            vm_trace_loop_event(LOOP_TRACE_TYPE_MISMATCH);
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i32");
            return;
        }
        left_val = AS_I32(boxed);
        vm_cache_i32_typed(left, left_val);
    }

    int32_t right_val;
    bool right_typed = vm_try_read_i32_typed(right, &right_val);
    if (!right_typed) {
        Value boxed = vm_get_register_safe(right);
        if (!IS_I32(boxed)) {
            vm_trace_loop_event(LOOP_TRACE_TYPE_MISMATCH);
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i32");
            return;
        }
        right_val = AS_I32(boxed);
        vm_cache_i32_typed(right, right_val);
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

    vm_store_i32_register(dst, left_val / right_val);
}

void handle_mod_i32_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();

    int32_t left_val;
    bool left_typed = vm_try_read_i32_typed(left, &left_val);
    if (!left_typed) {
        Value boxed = vm_get_register_safe(left);
        if (!IS_I32(boxed)) {
            vm_trace_loop_event(LOOP_TRACE_TYPE_MISMATCH);
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i32");
            return;
        }
        left_val = AS_I32(boxed);
        vm_cache_i32_typed(left, left_val);
    }

    int32_t right_val;
    bool right_typed = vm_try_read_i32_typed(right, &right_val);
    if (!right_typed) {
        Value boxed = vm_get_register_safe(right);
        if (!IS_I32(boxed)) {
            vm_trace_loop_event(LOOP_TRACE_TYPE_MISMATCH);
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i32");
            return;
        }
        right_val = AS_I32(boxed);
        vm_cache_i32_typed(right, right_val);
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

    vm_store_i32_register(dst, left_val % right_val);
}

// ====== I64 Typed Arithmetic Handlers ======

void handle_add_i64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();

    int64_t left_val;
    if (!vm_try_read_i64_typed(left, &left_val)) {
        Value boxed = vm_get_register_safe(left);
        if (!IS_I64(boxed)) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i64");
            return;
        }
        left_val = AS_I64(boxed);
        vm_cache_i64_typed(left, left_val);
    }

    int64_t right_val;
    if (!vm_try_read_i64_typed(right, &right_val)) {
        Value boxed = vm_get_register_safe(right);
        if (!IS_I64(boxed)) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i64");
            return;
        }
        right_val = AS_I64(boxed);
        vm_cache_i64_typed(right, right_val);
    }

    vm_store_i64_register(dst, left_val + right_val);
}

void handle_sub_i64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();

    int64_t left_val;
    if (!vm_try_read_i64_typed(left, &left_val)) {
        Value boxed = vm_get_register_safe(left);
        if (!IS_I64(boxed)) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i64");
            return;
        }
        left_val = AS_I64(boxed);
        vm_cache_i64_typed(left, left_val);
    }

    int64_t right_val;
    if (!vm_try_read_i64_typed(right, &right_val)) {
        Value boxed = vm_get_register_safe(right);
        if (!IS_I64(boxed)) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i64");
            return;
        }
        right_val = AS_I64(boxed);
        vm_cache_i64_typed(right, right_val);
    }

    vm_store_i64_register(dst, left_val - right_val);
}

void handle_mul_i64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();

    int64_t left_val;
    if (!vm_try_read_i64_typed(left, &left_val)) {
        Value boxed = vm_get_register_safe(left);
        if (!IS_I64(boxed)) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i64");
            return;
        }
        left_val = AS_I64(boxed);
        vm_cache_i64_typed(left, left_val);
    }

    int64_t right_val;
    if (!vm_try_read_i64_typed(right, &right_val)) {
        Value boxed = vm_get_register_safe(right);
        if (!IS_I64(boxed)) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i64");
            return;
        }
        right_val = AS_I64(boxed);
        vm_cache_i64_typed(right, right_val);
    }

    vm_store_i64_register(dst, left_val * right_val);
}

void handle_div_i64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();

    int64_t left_val;
    if (!vm_try_read_i64_typed(left, &left_val)) {
        Value boxed = vm_get_register_safe(left);
        if (!IS_I64(boxed)) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i64");
            return;
        }
        left_val = AS_I64(boxed);
        vm_cache_i64_typed(left, left_val);
    }

    int64_t right_val;
    if (!vm_try_read_i64_typed(right, &right_val)) {
        Value boxed = vm_get_register_safe(right);
        if (!IS_I64(boxed)) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i64");
            return;
        }
        right_val = AS_I64(boxed);
        vm_cache_i64_typed(right, right_val);
    }

    if (right_val == 0) {
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero");
        return;
    }

    vm_store_i64_register(dst, left_val / right_val);
}

void handle_mod_i64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();

    int64_t left_val;
    if (!vm_try_read_i64_typed(left, &left_val)) {
        Value boxed = vm_get_register_safe(left);
        if (!IS_I64(boxed)) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i64");
            return;
        }
        left_val = AS_I64(boxed);
        vm_cache_i64_typed(left, left_val);
    }

    int64_t right_val;
    if (!vm_try_read_i64_typed(right, &right_val)) {
        Value boxed = vm_get_register_safe(right);
        if (!IS_I64(boxed)) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i64");
            return;
        }
        right_val = AS_I64(boxed);
        vm_cache_i64_typed(right, right_val);
    }

    if (right_val == 0) {
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero");
        return;
    }

    vm_store_i64_register(dst, left_val % right_val);
}

// ====== F64 Typed Arithmetic Handlers ======

void handle_add_f64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();

    double left_val;
    if (!vm_try_read_f64_typed(left, &left_val)) {
        Value boxed = vm_get_register_safe(left);
        if (!IS_F64(boxed)) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be f64");
            return;
        }
        left_val = AS_F64(boxed);
        vm_cache_f64_typed(left, left_val);
    }

    double right_val;
    if (!vm_try_read_f64_typed(right, &right_val)) {
        Value boxed = vm_get_register_safe(right);
        if (!IS_F64(boxed)) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be f64");
            return;
        }
        right_val = AS_F64(boxed);
        vm_cache_f64_typed(right, right_val);
    }

    vm_store_f64_register(dst, left_val + right_val);
}

void handle_sub_f64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();

    double left_val;
    if (!vm_try_read_f64_typed(left, &left_val)) {
        Value boxed = vm_get_register_safe(left);
        if (!IS_F64(boxed)) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be f64");
            return;
        }
        left_val = AS_F64(boxed);
        vm_cache_f64_typed(left, left_val);
    }

    double right_val;
    if (!vm_try_read_f64_typed(right, &right_val)) {
        Value boxed = vm_get_register_safe(right);
        if (!IS_F64(boxed)) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be f64");
            return;
        }
        right_val = AS_F64(boxed);
        vm_cache_f64_typed(right, right_val);
    }

    vm_store_f64_register(dst, left_val - right_val);
}

void handle_mul_f64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();

    double left_val;
    if (!vm_try_read_f64_typed(left, &left_val)) {
        Value boxed = vm_get_register_safe(left);
        if (!IS_F64(boxed)) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be f64");
            return;
        }
        left_val = AS_F64(boxed);
        vm_cache_f64_typed(left, left_val);
    }

    double right_val;
    if (!vm_try_read_f64_typed(right, &right_val)) {
        Value boxed = vm_get_register_safe(right);
        if (!IS_F64(boxed)) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be f64");
            return;
        }
        right_val = AS_F64(boxed);
        vm_cache_f64_typed(right, right_val);
    }

    vm_store_f64_register(dst, left_val * right_val);
}

void handle_div_f64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();

    double left_val;
    if (!vm_try_read_f64_typed(left, &left_val)) {
        Value boxed = vm_get_register_safe(left);
        if (!IS_F64(boxed)) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be f64");
            return;
        }
        left_val = AS_F64(boxed);
        vm_cache_f64_typed(left, left_val);
    }

    double right_val;
    if (!vm_try_read_f64_typed(right, &right_val)) {
        Value boxed = vm_get_register_safe(right);
        if (!IS_F64(boxed)) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be f64");
            return;
        }
        right_val = AS_F64(boxed);
        vm_cache_f64_typed(right, right_val);
    }

    if (right_val == 0.0) {
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero");
        return;
    }

    vm_store_f64_register(dst, left_val / right_val);
}

void handle_mod_f64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();

    double left_val;
    if (!vm_try_read_f64_typed(left, &left_val)) {
        Value boxed = vm_get_register_safe(left);
        if (!IS_F64(boxed)) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be f64");
            return;
        }
        left_val = AS_F64(boxed);
        vm_cache_f64_typed(left, left_val);
    }

    double right_val;
    if (!vm_try_read_f64_typed(right, &right_val)) {
        Value boxed = vm_get_register_safe(right);
        if (!IS_F64(boxed)) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be f64");
            return;
        }
        right_val = AS_F64(boxed);
        vm_cache_f64_typed(right, right_val);
    }

    if (right_val == 0.0) {
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero");
        return;
    }

    vm_store_f64_register(dst, fmod(left_val, right_val));
}

// ====== U32 Typed Arithmetic Handlers ======

void handle_add_u32_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();

    uint32_t left_val;
    if (!vm_try_read_u32_typed(left, &left_val)) {
        Value boxed = vm_get_register_safe(left);
        if (!IS_U32(boxed)) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u32");
            return;
        }
        left_val = AS_U32(boxed);
        vm_cache_u32_typed(left, left_val);
    }

    uint32_t right_val;
    if (!vm_try_read_u32_typed(right, &right_val)) {
        Value boxed = vm_get_register_safe(right);
        if (!IS_U32(boxed)) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u32");
            return;
        }
        right_val = AS_U32(boxed);
        vm_cache_u32_typed(right, right_val);
    }

    vm_store_u32_register(dst, left_val + right_val);
}

void handle_sub_u32_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();

    uint32_t left_val;
    if (!vm_try_read_u32_typed(left, &left_val)) {
        Value boxed = vm_get_register_safe(left);
        if (!IS_U32(boxed)) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u32");
            return;
        }
        left_val = AS_U32(boxed);
        vm_cache_u32_typed(left, left_val);
    }

    uint32_t right_val;
    if (!vm_try_read_u32_typed(right, &right_val)) {
        Value boxed = vm_get_register_safe(right);
        if (!IS_U32(boxed)) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u32");
            return;
        }
        right_val = AS_U32(boxed);
        vm_cache_u32_typed(right, right_val);
    }

    vm_store_u32_register(dst, left_val - right_val);
}

void handle_mul_u32_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();

    uint32_t left_val;
    if (!vm_try_read_u32_typed(left, &left_val)) {
        Value boxed = vm_get_register_safe(left);
        if (!IS_U32(boxed)) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u32");
            return;
        }
        left_val = AS_U32(boxed);
        vm_cache_u32_typed(left, left_val);
    }

    uint32_t right_val;
    if (!vm_try_read_u32_typed(right, &right_val)) {
        Value boxed = vm_get_register_safe(right);
        if (!IS_U32(boxed)) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u32");
            return;
        }
        right_val = AS_U32(boxed);
        vm_cache_u32_typed(right, right_val);
    }

    vm_store_u32_register(dst, left_val * right_val);
}

void handle_div_u32_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();

    uint32_t left_val;
    if (!vm_try_read_u32_typed(left, &left_val)) {
        Value boxed = vm_get_register_safe(left);
        if (!IS_U32(boxed)) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u32");
            return;
        }
        left_val = AS_U32(boxed);
        vm_cache_u32_typed(left, left_val);
    }

    uint32_t right_val;
    if (!vm_try_read_u32_typed(right, &right_val)) {
        Value boxed = vm_get_register_safe(right);
        if (!IS_U32(boxed)) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u32");
            return;
        }
        right_val = AS_U32(boxed);
        vm_cache_u32_typed(right, right_val);
    }

    if (right_val == 0) {
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero");
        return;
    }

    vm_store_u32_register(dst, left_val / right_val);
}

void handle_mod_u32_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();

    uint32_t left_val;
    if (!vm_try_read_u32_typed(left, &left_val)) {
        Value boxed = vm_get_register_safe(left);
        if (!IS_U32(boxed)) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u32");
            return;
        }
        left_val = AS_U32(boxed);
        vm_cache_u32_typed(left, left_val);
    }

    uint32_t right_val;
    if (!vm_try_read_u32_typed(right, &right_val)) {
        Value boxed = vm_get_register_safe(right);
        if (!IS_U32(boxed)) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u32");
            return;
        }
        right_val = AS_U32(boxed);
        vm_cache_u32_typed(right, right_val);
    }

    if (right_val == 0) {
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero");
        return;
    }

    vm_store_u32_register(dst, left_val % right_val);
}

// ====== U64 Typed Arithmetic Handlers ======

void handle_add_u64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();

    uint64_t left_val;
    if (!vm_try_read_u64_typed(left, &left_val)) {
        Value boxed = vm_get_register_safe(left);
        if (!IS_U64(boxed)) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u64");
            return;
        }
        left_val = AS_U64(boxed);
        vm_cache_u64_typed(left, left_val);
    }

    uint64_t right_val;
    if (!vm_try_read_u64_typed(right, &right_val)) {
        Value boxed = vm_get_register_safe(right);
        if (!IS_U64(boxed)) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u64");
            return;
        }
        right_val = AS_U64(boxed);
        vm_cache_u64_typed(right, right_val);
    }

    vm_store_u64_register(dst, left_val + right_val);
}

void handle_sub_u64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();

    uint64_t left_val;
    if (!vm_try_read_u64_typed(left, &left_val)) {
        Value boxed = vm_get_register_safe(left);
        if (!IS_U64(boxed)) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u64");
            return;
        }
        left_val = AS_U64(boxed);
        vm_cache_u64_typed(left, left_val);
    }

    uint64_t right_val;
    if (!vm_try_read_u64_typed(right, &right_val)) {
        Value boxed = vm_get_register_safe(right);
        if (!IS_U64(boxed)) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u64");
            return;
        }
        right_val = AS_U64(boxed);
        vm_cache_u64_typed(right, right_val);
    }

    vm_store_u64_register(dst, left_val - right_val);
}

void handle_mul_u64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();

    uint64_t left_val;
    if (!vm_try_read_u64_typed(left, &left_val)) {
        Value boxed = vm_get_register_safe(left);
        if (!IS_U64(boxed)) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u64");
            return;
        }
        left_val = AS_U64(boxed);
        vm_cache_u64_typed(left, left_val);
    }

    uint64_t right_val;
    if (!vm_try_read_u64_typed(right, &right_val)) {
        Value boxed = vm_get_register_safe(right);
        if (!IS_U64(boxed)) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u64");
            return;
        }
        right_val = AS_U64(boxed);
        vm_cache_u64_typed(right, right_val);
    }

    vm_store_u64_register(dst, left_val * right_val);
}

void handle_div_u64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();

    uint64_t left_val;
    if (!vm_try_read_u64_typed(left, &left_val)) {
        Value boxed = vm_get_register_safe(left);
        if (!IS_U64(boxed)) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u64");
            return;
        }
        left_val = AS_U64(boxed);
        vm_cache_u64_typed(left, left_val);
    }

    uint64_t right_val;
    if (!vm_try_read_u64_typed(right, &right_val)) {
        Value boxed = vm_get_register_safe(right);
        if (!IS_U64(boxed)) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u64");
            return;
        }
        right_val = AS_U64(boxed);
        vm_cache_u64_typed(right, right_val);
    }

    if (right_val == 0) {
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero");
        return;
    }

    vm_store_u64_register(dst, left_val / right_val);
}

void handle_mod_u64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();

    uint64_t left_val;
    if (!vm_try_read_u64_typed(left, &left_val)) {
        Value boxed = vm_get_register_safe(left);
        if (!IS_U64(boxed)) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u64");
            return;
        }
        left_val = AS_U64(boxed);
        vm_cache_u64_typed(left, left_val);
    }

    uint64_t right_val;
    if (!vm_try_read_u64_typed(right, &right_val)) {
        Value boxed = vm_get_register_safe(right);
        if (!IS_U64(boxed)) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u64");
            return;
        }
        right_val = AS_U64(boxed);
        vm_cache_u64_typed(right, right_val);
    }

    if (right_val == 0) {
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero");
        return;
    }

    vm_store_u64_register(dst, left_val % right_val);
}