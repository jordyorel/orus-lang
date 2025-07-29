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

static inline void handle_add_i32_typed(void) __attribute__((unused)); 
static inline void handle_add_i32_typed(void) {
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

static inline void handle_sub_i32_typed(void) {
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

static inline void handle_mul_i32_typed(void) {
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

static inline void handle_div_i32_typed(void) {
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

static inline void handle_mod_i32_typed(void) {
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

static inline void handle_add_i64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    vm.typed_regs.i64_regs[dst] = vm.typed_regs.i64_regs[left] + vm.typed_regs.i64_regs[right];
    vm.typed_regs.reg_types[dst] = REG_TYPE_I64;
}

static inline void handle_sub_i64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    vm.typed_regs.i64_regs[dst] = vm.typed_regs.i64_regs[left] - vm.typed_regs.i64_regs[right];
    vm.typed_regs.reg_types[dst] = REG_TYPE_I64;
}

static inline void handle_mul_i64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    vm.typed_regs.i64_regs[dst] = vm.typed_regs.i64_regs[left] * vm.typed_regs.i64_regs[right];
    vm.typed_regs.reg_types[dst] = REG_TYPE_I64;
}

static inline void handle_div_i64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    if (vm.typed_regs.i64_regs[right] == 0) {
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero");
        return;
    }
    vm.typed_regs.i64_regs[dst] = vm.typed_regs.i64_regs[left] / vm.typed_regs.i64_regs[right];
    vm.typed_regs.reg_types[dst] = REG_TYPE_I64;
}

static inline void handle_mod_i64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    if (vm.typed_regs.i64_regs[right] == 0) {
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero");
        return;
    }
    vm.typed_regs.i64_regs[dst] = vm.typed_regs.i64_regs[left] % vm.typed_regs.i64_regs[right];
    vm.typed_regs.reg_types[dst] = REG_TYPE_I64;
}

// ====== F64 Typed Arithmetic Handlers ======

static inline void handle_add_f64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    vm.typed_regs.f64_regs[dst] = vm.typed_regs.f64_regs[left] + vm.typed_regs.f64_regs[right];
    vm.typed_regs.reg_types[dst] = REG_TYPE_F64;
}

static inline void handle_sub_f64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    vm.typed_regs.f64_regs[dst] = vm.typed_regs.f64_regs[left] - vm.typed_regs.f64_regs[right];
    vm.typed_regs.reg_types[dst] = REG_TYPE_F64;
}

static inline void handle_mul_f64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    vm.typed_regs.f64_regs[dst] = vm.typed_regs.f64_regs[left] * vm.typed_regs.f64_regs[right];
    vm.typed_regs.reg_types[dst] = REG_TYPE_F64;
}

static inline void handle_div_f64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    if (vm.typed_regs.f64_regs[right] == 0.0) {
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero");
        return;
    }
    vm.typed_regs.f64_regs[dst] = vm.typed_regs.f64_regs[left] / vm.typed_regs.f64_regs[right];
    vm.typed_regs.reg_types[dst] = REG_TYPE_F64;
}

static inline void handle_mod_f64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    if (vm.typed_regs.f64_regs[right] == 0.0) {
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero");
        return;
    }
    vm.typed_regs.f64_regs[dst] = fmod(vm.typed_regs.f64_regs[left], vm.typed_regs.f64_regs[right]);
    vm.typed_regs.reg_types[dst] = REG_TYPE_F64;
}

// ====== U32 Typed Arithmetic Handlers ======

static inline void handle_add_u32_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    vm.typed_regs.u32_regs[dst] = vm.typed_regs.u32_regs[left] + vm.typed_regs.u32_regs[right];
    vm.typed_regs.reg_types[dst] = REG_TYPE_U32;
}

static inline void handle_sub_u32_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    vm.typed_regs.u32_regs[dst] = vm.typed_regs.u32_regs[left] - vm.typed_regs.u32_regs[right];
    vm.typed_regs.reg_types[dst] = REG_TYPE_U32;
}

static inline void handle_mul_u32_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    vm.typed_regs.u32_regs[dst] = vm.typed_regs.u32_regs[left] * vm.typed_regs.u32_regs[right];
    vm.typed_regs.reg_types[dst] = REG_TYPE_U32;
}

static inline void handle_div_u32_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    if (vm.typed_regs.u32_regs[right] == 0) {
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero");
        return;
    }
    vm.typed_regs.u32_regs[dst] = vm.typed_regs.u32_regs[left] / vm.typed_regs.u32_regs[right];
    vm.typed_regs.reg_types[dst] = REG_TYPE_U32;
}

static inline void handle_mod_u32_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    if (vm.typed_regs.u32_regs[right] == 0) {
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero");
        return;
    }
    vm.typed_regs.u32_regs[dst] = vm.typed_regs.u32_regs[left] % vm.typed_regs.u32_regs[right];
    vm.typed_regs.reg_types[dst] = REG_TYPE_U32;
}

// ====== U64 Typed Arithmetic Handlers ======

static inline void handle_add_u64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    vm.typed_regs.u64_regs[dst] = vm.typed_regs.u64_regs[left] + vm.typed_regs.u64_regs[right];
    vm.typed_regs.reg_types[dst] = REG_TYPE_U64;
}

static inline void handle_sub_u64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    vm.typed_regs.u64_regs[dst] = vm.typed_regs.u64_regs[left] - vm.typed_regs.u64_regs[right];
    vm.typed_regs.reg_types[dst] = REG_TYPE_U64;
}

static inline void handle_mul_u64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    vm.typed_regs.u64_regs[dst] = vm.typed_regs.u64_regs[left] * vm.typed_regs.u64_regs[right];
    vm.typed_regs.reg_types[dst] = REG_TYPE_U64;
}

static inline void handle_div_u64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    if (vm.typed_regs.u64_regs[right] == 0) {
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero");
        return;
    }
    vm.typed_regs.u64_regs[dst] = vm.typed_regs.u64_regs[left] / vm.typed_regs.u64_regs[right];
    vm.typed_regs.reg_types[dst] = REG_TYPE_U64;
}

static inline void handle_mod_u64_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    if (vm.typed_regs.u64_regs[right] == 0) {
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero");
        return;
    }
    vm.typed_regs.u64_regs[dst] = vm.typed_regs.u64_regs[left] % vm.typed_regs.u64_regs[right];
    vm.typed_regs.reg_types[dst] = REG_TYPE_U64;
}