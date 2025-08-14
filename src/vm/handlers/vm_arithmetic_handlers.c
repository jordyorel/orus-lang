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
    
    // Use frame-aware register access for consistency with recursive function calls
    Value left_val = vm_get_register_safe(left);
    Value right_val = vm_get_register_safe(right);
    
    // DEBUG: Print what values we're actually getting - REMOVED
    
    if (!IS_I32(left_val) || !IS_I32(right_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i32");
        return;
    }
    
    int32_t result = AS_I32(left_val) + AS_I32(right_val);
    // Debug output removed
    vm_set_register_safe(dst, I32_VAL(result));
}

void handle_sub_i32_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    
    // Use frame-aware register access for consistency with recursive function calls
    Value left_val = vm_get_register_safe(left);
    Value right_val = vm_get_register_safe(right);
    
    if (!IS_I32(left_val) || !IS_I32(right_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i32");
        return;
    }
    
    int32_t result = AS_I32(left_val) - AS_I32(right_val);
    vm_set_register_safe(dst, I32_VAL(result));
}

void handle_mul_i32_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    
    // Use frame-aware register access for consistency with recursive function calls
    Value left_val = vm_get_register_safe(left);
    Value right_val = vm_get_register_safe(right);
    
    if (!IS_I32(left_val) || !IS_I32(right_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i32");
        return;
    }
    
    int32_t result = AS_I32(left_val) * AS_I32(right_val);
    vm_set_register_safe(dst, I32_VAL(result));
}

void handle_div_i32_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    
    // Use frame-aware register access for consistency with recursive function calls
    Value left_val = vm_get_register_safe(left);
    Value right_val = vm_get_register_safe(right);
    
    if (!IS_I32(left_val) || !IS_I32(right_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i32");
        return;
    }
    
    int32_t divisor = AS_I32(right_val);
    if (divisor == 0) {
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero");
        return;
    }
    
    int32_t result = AS_I32(left_val) / divisor;
    vm_set_register_safe(dst, I32_VAL(result));
}

void handle_mod_i32_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    
    // Use frame-aware register access for consistency with recursive function calls
    Value left_val = vm_get_register_safe(left);
    Value right_val = vm_get_register_safe(right);
    
    if (!IS_I32(left_val) || !IS_I32(right_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i32");
        return;
    }
    
    int32_t divisor = AS_I32(right_val);
    if (divisor == 0) {
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero");
        return;
    }
    
    int32_t result = AS_I32(left_val) % divisor;
    vm_set_register_safe(dst, I32_VAL(result));
}

// ====== I64 Typed Arithmetic Handlers ======

void handle_add_i64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    
    // Use frame-aware register access for consistency with recursive function calls
    Value left_val = vm_get_register_safe(left);
    Value right_val = vm_get_register_safe(right);
    
    if (!IS_I64(left_val) || !IS_I64(right_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i64");
        return;
    }
    
    int64_t result = AS_I64(left_val) + AS_I64(right_val);
    vm_set_register_safe(dst, I64_VAL(result));
}

void handle_sub_i64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    
    // Use frame-aware register access for consistency with recursive function calls
    Value left_val = vm_get_register_safe(left);
    Value right_val = vm_get_register_safe(right);
    
    if (!IS_I64(left_val) || !IS_I64(right_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i64");
        return;
    }
    
    int64_t result = AS_I64(left_val) - AS_I64(right_val);
    vm_set_register_safe(dst, I64_VAL(result));
}

void handle_mul_i64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    
    // Use frame-aware register access for consistency with recursive function calls
    Value left_val = vm_get_register_safe(left);
    Value right_val = vm_get_register_safe(right);
    
    if (!IS_I64(left_val) || !IS_I64(right_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i64");
        return;
    }
    
    int64_t result = AS_I64(left_val) * AS_I64(right_val);
    vm_set_register_safe(dst, I64_VAL(result));
}

void handle_div_i64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    
    // Use frame-aware register access for consistency with recursive function calls
    Value left_val = vm_get_register_safe(left);
    Value right_val = vm_get_register_safe(right);
    
    if (!IS_I64(left_val) || !IS_I64(right_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i64");
        return;
    }
    
    int64_t divisor = AS_I64(right_val);
    if (divisor == 0) {
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero");
        return;
    }
    
    int64_t result = AS_I64(left_val) / divisor;
    vm_set_register_safe(dst, I64_VAL(result));
}

void handle_mod_i64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    
    // Use frame-aware register access for consistency with recursive function calls
    Value left_val = vm_get_register_safe(left);
    Value right_val = vm_get_register_safe(right);
    
    if (!IS_I64(left_val) || !IS_I64(right_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i64");
        return;
    }
    
    int64_t divisor = AS_I64(right_val);
    if (divisor == 0) {
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero");
        return;
    }
    
    int64_t result = AS_I64(left_val) % divisor;
    vm_set_register_safe(dst, I64_VAL(result));
}

// ====== F64 Typed Arithmetic Handlers ======

void handle_add_f64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    
    // Use frame-aware register access for consistency with recursive function calls
    Value left_val = vm_get_register_safe(left);
    Value right_val = vm_get_register_safe(right);
    
    if (!IS_F64(left_val) || !IS_F64(right_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be f64");
        return;
    }
    
    double result = AS_F64(left_val) + AS_F64(right_val);
    vm_set_register_safe(dst, F64_VAL(result));
}

void handle_sub_f64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    
    // Use frame-aware register access for consistency with recursive function calls
    Value left_val = vm_get_register_safe(left);
    Value right_val = vm_get_register_safe(right);
    
    if (!IS_F64(left_val) || !IS_F64(right_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be f64");
        return;
    }
    
    double result = AS_F64(left_val) - AS_F64(right_val);
    vm_set_register_safe(dst, F64_VAL(result));
}

void handle_mul_f64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    
    // Use frame-aware register access for consistency with recursive function calls
    Value left_val = vm_get_register_safe(left);
    Value right_val = vm_get_register_safe(right);
    
    if (!IS_F64(left_val) || !IS_F64(right_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be f64");
        return;
    }
    
    double result = AS_F64(left_val) * AS_F64(right_val);
    vm_set_register_safe(dst, F64_VAL(result));
}

void handle_div_f64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    
    // Use frame-aware register access for consistency with recursive function calls
    Value left_val = vm_get_register_safe(left);
    Value right_val = vm_get_register_safe(right);
    
    if (!IS_F64(left_val) || !IS_F64(right_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be f64");
        return;
    }
    
    double divisor = AS_F64(right_val);
    if (divisor == 0.0) {
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero");
        return;
    }
    
    double result = AS_F64(left_val) / divisor;
    vm_set_register_safe(dst, F64_VAL(result));
}

void handle_mod_f64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    
    // Use frame-aware register access for consistency with recursive function calls
    Value left_val = vm_get_register_safe(left);
    Value right_val = vm_get_register_safe(right);
    
    if (!IS_F64(left_val) || !IS_F64(right_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be f64");
        return;
    }
    
    double divisor = AS_F64(right_val);
    if (divisor == 0.0) {
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero");
        return;
    }
    
    double result = fmod(AS_F64(left_val), divisor);
    vm_set_register_safe(dst, F64_VAL(result));
}

// ====== U32 Typed Arithmetic Handlers ======

void handle_add_u32_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    
    // Use frame-aware register access for consistency with recursive function calls
    Value left_val = vm_get_register_safe(left);
    Value right_val = vm_get_register_safe(right);
    
    if (!IS_U32(left_val) || !IS_U32(right_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u32");
        return;
    }
    
    uint32_t result = AS_U32(left_val) + AS_U32(right_val);
    vm_set_register_safe(dst, U32_VAL(result));
}

void handle_sub_u32_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    
    // Use frame-aware register access for consistency with recursive function calls
    Value left_val = vm_get_register_safe(left);
    Value right_val = vm_get_register_safe(right);
    
    if (!IS_U32(left_val) || !IS_U32(right_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u32");
        return;
    }
    
    uint32_t result = AS_U32(left_val) - AS_U32(right_val);
    vm_set_register_safe(dst, U32_VAL(result));
}

void handle_mul_u32_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    
    // Use frame-aware register access for consistency with recursive function calls
    Value left_val = vm_get_register_safe(left);
    Value right_val = vm_get_register_safe(right);
    
    if (!IS_U32(left_val) || !IS_U32(right_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u32");
        return;
    }
    
    uint32_t result = AS_U32(left_val) * AS_U32(right_val);
    vm_set_register_safe(dst, U32_VAL(result));
}

void handle_div_u32_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    
    // Use frame-aware register access for consistency with recursive function calls
    Value left_val = vm_get_register_safe(left);
    Value right_val = vm_get_register_safe(right);
    
    if (!IS_U32(left_val) || !IS_U32(right_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u32");
        return;
    }
    
    uint32_t right_val_u32 = AS_U32(right_val);
    if (right_val_u32 == 0) {
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero");
        return;
    }
    
    uint32_t result = AS_U32(left_val) / right_val_u32;
    vm_set_register_safe(dst, U32_VAL(result));
}

void handle_mod_u32_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    
    // Use frame-aware register access for consistency with recursive function calls
    Value left_val = vm_get_register_safe(left);
    Value right_val = vm_get_register_safe(right);
    
    if (!IS_U32(left_val) || !IS_U32(right_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u32");
        return;
    }
    
    uint32_t right_val_u32 = AS_U32(right_val);
    if (right_val_u32 == 0) {
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero");
        return;
    }
    
    uint32_t result = AS_U32(left_val) % right_val_u32;
    vm_set_register_safe(dst, U32_VAL(result));
}

// ====== U64 Typed Arithmetic Handlers ======

void handle_add_u64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    
    // Use frame-aware register access for consistency with recursive function calls
    Value left_val = vm_get_register_safe(left);
    Value right_val = vm_get_register_safe(right);
    
    if (!IS_U64(left_val) || !IS_U64(right_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u64");
        return;
    }
    
    uint64_t result = AS_U64(left_val) + AS_U64(right_val);
    vm_set_register_safe(dst, U64_VAL(result));
}

void handle_sub_u64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    
    // Use frame-aware register access for consistency with recursive function calls
    Value left_val = vm_get_register_safe(left);
    Value right_val = vm_get_register_safe(right);
    
    if (!IS_U64(left_val) || !IS_U64(right_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u64");
        return;
    }
    
    uint64_t result = AS_U64(left_val) - AS_U64(right_val);
    vm_set_register_safe(dst, U64_VAL(result));
}

void handle_mul_u64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    
    // Use frame-aware register access for consistency with recursive function calls
    Value left_val = vm_get_register_safe(left);
    Value right_val = vm_get_register_safe(right);
    
    if (!IS_U64(left_val) || !IS_U64(right_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u64");
        return;
    }
    
    uint64_t result = AS_U64(left_val) * AS_U64(right_val);
    vm_set_register_safe(dst, U64_VAL(result));
}

void handle_div_u64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    
    // Use frame-aware register access for consistency with recursive function calls
    Value left_val = vm_get_register_safe(left);
    Value right_val = vm_get_register_safe(right);
    
    if (!IS_U64(left_val) || !IS_U64(right_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u64");
        return;
    }
    
    uint64_t right_val_u64 = AS_U64(right_val);
    if (right_val_u64 == 0) {
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero");
        return;
    }
    
    uint64_t result = AS_U64(left_val) / right_val_u64;
    vm_set_register_safe(dst, U64_VAL(result));
}

void handle_mod_u64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    
    // Use frame-aware register access for consistency with recursive function calls
    Value left_val = vm_get_register_safe(left);
    Value right_val = vm_get_register_safe(right);
    
    if (!IS_U64(left_val) || !IS_U64(right_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u64");
        return;
    }
    
    uint64_t right_val_u64 = AS_U64(right_val);
    if (right_val_u64 == 0) {
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero");
        return;
    }
    
    uint64_t result = AS_U64(left_val) % right_val_u64;
    vm_set_register_safe(dst, U64_VAL(result));
}