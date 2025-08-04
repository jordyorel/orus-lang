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
#include <math.h>

// ====== I32 Typed Arithmetic Handlers ======

void handle_add_i32_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    
    // Use standard register system for consistency with load/move operations
    if (!IS_I32(vm.registers[left]) || !IS_I32(vm.registers[right])) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i32");
        return;
    }
    
    int32_t result = AS_I32(vm.registers[left]) + AS_I32(vm.registers[right]);
    vm.registers[dst] = I32_VAL(result);
}

void handle_sub_i32_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    
    // Use standard register system for consistency with load/move operations
    if (!IS_I32(vm.registers[left]) || !IS_I32(vm.registers[right])) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i32");
        return;
    }
    
    int32_t result = AS_I32(vm.registers[left]) - AS_I32(vm.registers[right]);
    vm.registers[dst] = I32_VAL(result);
}

void handle_mul_i32_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    
    // Use standard register system for consistency with load/move operations
    if (!IS_I32(vm.registers[left]) || !IS_I32(vm.registers[right])) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i32");
        return;
    }
    
    int32_t result = AS_I32(vm.registers[left]) * AS_I32(vm.registers[right]);
    vm.registers[dst] = I32_VAL(result);
}

void handle_div_i32_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    
    // Use standard register system for consistency with load/move operations
    if (!IS_I32(vm.registers[left]) || !IS_I32(vm.registers[right])) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i32");
        return;
    }
    
    int32_t divisor = AS_I32(vm.registers[right]);
    if (divisor == 0) {
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero");
        return;
    }
    
    int32_t result = AS_I32(vm.registers[left]) / divisor;
    vm.registers[dst] = I32_VAL(result);
}

void handle_mod_i32_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    
    // Use standard register system for consistency with load/move operations
    if (!IS_I32(vm.registers[left]) || !IS_I32(vm.registers[right])) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i32");
        return;
    }
    
    int32_t divisor = AS_I32(vm.registers[right]);
    if (divisor == 0) {
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero");
        return;
    }
    
    int32_t result = AS_I32(vm.registers[left]) % divisor;
    vm.registers[dst] = I32_VAL(result);
}

// ====== I64 Typed Arithmetic Handlers ======

void handle_add_i64_typed(void) {
    printf("[DEBUG] handle_add_i64_typed called - THIS SHOULD NOT HAPPEN FOR I32 OPERATIONS!\n");
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    
    printf("[DEBUG] i64 handler: dst=R%d, left=R%d, right=R%d\n", dst, left, right);
    printf("[DEBUG] i64 handler: left_type=%d, right_type=%d\n", 
           vm.registers[left].type, vm.registers[right].type);
    printf("[DEBUG] i64 handler: Expected opcode 131, but got called instead of opcode 126\n");
    
    // Use standard register system for consistency with load/move operations
    if (!IS_I64(vm.registers[left]) || !IS_I64(vm.registers[right])) {
        printf("[DEBUG] i64 handler: Type check failed - as expected since we have i32 values!\n");
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i64");
        return;
    }
    
    int64_t result = AS_I64(vm.registers[left]) + AS_I64(vm.registers[right]);
    vm.registers[dst] = I64_VAL(result);
}

void handle_sub_i64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    
    // Use standard register system for consistency with load/move operations
    if (!IS_I64(vm.registers[left]) || !IS_I64(vm.registers[right])) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i64");
        return;
    }
    
    int64_t result = AS_I64(vm.registers[left]) - AS_I64(vm.registers[right]);
    vm.registers[dst] = I64_VAL(result);
}

void handle_mul_i64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    
    // Use standard register system for consistency with load/move operations
    if (!IS_I64(vm.registers[left]) || !IS_I64(vm.registers[right])) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i64");
        return;
    }
    
    int64_t result = AS_I64(vm.registers[left]) * AS_I64(vm.registers[right]);
    vm.registers[dst] = I64_VAL(result);
}

void handle_div_i64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    
    // Use standard register system for consistency with load/move operations
    if (!IS_I64(vm.registers[left]) || !IS_I64(vm.registers[right])) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i64");
        return;
    }
    
    int64_t divisor = AS_I64(vm.registers[right]);
    if (divisor == 0) {
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero");
        return;
    }
    
    int64_t result = AS_I64(vm.registers[left]) / divisor;
    vm.registers[dst] = I64_VAL(result);
}

void handle_mod_i64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    
    // Use standard register system for consistency with load/move operations
    if (!IS_I64(vm.registers[left]) || !IS_I64(vm.registers[right])) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i64");
        return;
    }
    
    int64_t divisor = AS_I64(vm.registers[right]);
    if (divisor == 0) {
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero");
        return;
    }
    
    int64_t result = AS_I64(vm.registers[left]) % divisor;
    vm.registers[dst] = I64_VAL(result);
}

// ====== F64 Typed Arithmetic Handlers ======

void handle_add_f64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    
    // Use standard register system for consistency with load/move operations
    if (!IS_F64(vm.registers[left]) || !IS_F64(vm.registers[right])) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be f64");
        return;
    }
    
    double result = AS_F64(vm.registers[left]) + AS_F64(vm.registers[right]);
    vm.registers[dst] = F64_VAL(result);
}

void handle_sub_f64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    
    // Use standard register system for consistency with load/move operations
    if (!IS_F64(vm.registers[left]) || !IS_F64(vm.registers[right])) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be f64");
        return;
    }
    
    double result = AS_F64(vm.registers[left]) - AS_F64(vm.registers[right]);
    vm.registers[dst] = F64_VAL(result);
}

void handle_mul_f64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    
    // Use standard register system for consistency with load/move operations
    if (!IS_F64(vm.registers[left]) || !IS_F64(vm.registers[right])) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be f64");
        return;
    }
    
    double result = AS_F64(vm.registers[left]) * AS_F64(vm.registers[right]);
    vm.registers[dst] = F64_VAL(result);
}

void handle_div_f64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    
    // Use standard register system for consistency with load/move operations
    if (!IS_F64(vm.registers[left]) || !IS_F64(vm.registers[right])) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be f64");
        return;
    }
    
    double divisor = AS_F64(vm.registers[right]);
    if (divisor == 0.0) {
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero");
        return;
    }
    
    double result = AS_F64(vm.registers[left]) / divisor;
    vm.registers[dst] = F64_VAL(result);
}

void handle_mod_f64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    
    // Use standard register system for consistency with load/move operations
    if (!IS_F64(vm.registers[left]) || !IS_F64(vm.registers[right])) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be f64");
        return;
    }
    
    double divisor = AS_F64(vm.registers[right]);
    if (divisor == 0.0) {
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero");
        return;
    }
    
    double result = fmod(AS_F64(vm.registers[left]), divisor);
    vm.registers[dst] = F64_VAL(result);
}

// ====== U32 Typed Arithmetic Handlers ======

void handle_add_u32_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    
    // Use standard register system for consistency with load/move operations
    if (!IS_U32(vm.registers[left]) || !IS_U32(vm.registers[right])) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u32");
        return;
    }
    
    uint32_t result = AS_U32(vm.registers[left]) + AS_U32(vm.registers[right]);
    vm.registers[dst] = U32_VAL(result);
}

void handle_sub_u32_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    
    // Use standard register system for consistency with load/move operations
    if (!IS_U32(vm.registers[left]) || !IS_U32(vm.registers[right])) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u32");
        return;
    }
    
    uint32_t result = AS_U32(vm.registers[left]) - AS_U32(vm.registers[right]);
    vm.registers[dst] = U32_VAL(result);
}

void handle_mul_u32_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    
    // Use standard register system for consistency with load/move operations
    if (!IS_U32(vm.registers[left]) || !IS_U32(vm.registers[right])) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u32");
        return;
    }
    
    uint32_t result = AS_U32(vm.registers[left]) * AS_U32(vm.registers[right]);
    vm.registers[dst] = U32_VAL(result);
}

void handle_div_u32_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    
    // Use standard register system for consistency with load/move operations
    if (!IS_U32(vm.registers[left]) || !IS_U32(vm.registers[right])) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u32");
        return;
    }
    
    uint32_t right_val = AS_U32(vm.registers[right]);
    if (right_val == 0) {
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero");
        return;
    }
    
    uint32_t result = AS_U32(vm.registers[left]) / right_val;
    vm.registers[dst] = U32_VAL(result);
}

void handle_mod_u32_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    
    // Use standard register system for consistency with load/move operations
    if (!IS_U32(vm.registers[left]) || !IS_U32(vm.registers[right])) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u32");
        return;
    }
    
    uint32_t right_val = AS_U32(vm.registers[right]);
    if (right_val == 0) {
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero");
        return;
    }
    
    uint32_t result = AS_U32(vm.registers[left]) % right_val;
    vm.registers[dst] = U32_VAL(result);
}

// ====== U64 Typed Arithmetic Handlers ======

void handle_add_u64_typed(void) {
    printf("[DEBUG] handle_add_u64_typed called - THIS SHOULD NOT HAPPEN FOR I32 OPERATIONS!\n");
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    
    printf("[DEBUG] u64 handler: dst=R%d, left=R%d, right=R%d\n", dst, left, right);
    printf("[DEBUG] u64 handler: left_type=%d, right_type=%d\n", 
           vm.registers[left].type, vm.registers[right].type);
    
    // Use standard register system for consistency with load/move operations
    if (!IS_U64(vm.registers[left]) || !IS_U64(vm.registers[right])) {
        printf("[DEBUG] u64 handler: Type check failed - as expected since we have i32 values!\n");
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u64");
        return;
    }
    
    uint64_t result = AS_U64(vm.registers[left]) + AS_U64(vm.registers[right]);
    vm.registers[dst] = U64_VAL(result);
}

void handle_sub_u64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    
    // Use standard register system for consistency with load/move operations
    if (!IS_U64(vm.registers[left]) || !IS_U64(vm.registers[right])) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u64");
        return;
    }
    
    uint64_t result = AS_U64(vm.registers[left]) - AS_U64(vm.registers[right]);
    vm.registers[dst] = U64_VAL(result);
}

void handle_mul_u64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    
    // Use standard register system for consistency with load/move operations
    if (!IS_U64(vm.registers[left]) || !IS_U64(vm.registers[right])) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u64");
        return;
    }
    
    uint64_t result = AS_U64(vm.registers[left]) * AS_U64(vm.registers[right]);
    vm.registers[dst] = U64_VAL(result);
}

void handle_div_u64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    
    // Use standard register system for consistency with load/move operations
    if (!IS_U64(vm.registers[left]) || !IS_U64(vm.registers[right])) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u64");
        return;
    }
    
    uint64_t right_val = AS_U64(vm.registers[right]);
    if (right_val == 0) {
        printf("[DEBUG] u64 division by zero: right_val = %llu\n", (unsigned long long)right_val);
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero");
        return;
    }
    
    uint64_t result = AS_U64(vm.registers[left]) / right_val;
    vm.registers[dst] = U64_VAL(result);
}

void handle_mod_u64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    
    // Use standard register system for consistency with load/move operations
    if (!IS_U64(vm.registers[left]) || !IS_U64(vm.registers[right])) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u64");
        return;
    }
    
    uint64_t right_val = AS_U64(vm.registers[right]);
    if (right_val == 0) {
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero");
        return;
    }
    
    uint64_t result = AS_U64(vm.registers[left]) % right_val;
    vm.registers[dst] = U64_VAL(result);
}