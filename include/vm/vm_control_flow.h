// Orus Language Project

#ifndef ORUS_VM_CONTROL_FLOW_H
#define ORUS_VM_CONTROL_FLOW_H

#include "vm/vm_comparison.h"

static inline bool CF_JUMP(uint16_t offset) {
    // If VM is shutting down or chunk is invalid, ignore jump
    if (vm.isShuttingDown || !vm.chunk || !vm.chunk->code) {
        return true;  // Silently ignore jump during cleanup
    }

    int32_t signed_offset = (int16_t)offset;
    uint8_t* new_ip = vm.ip + signed_offset;
    if (new_ip < vm.chunk->code || new_ip >= vm.chunk->code + vm.chunk->count) {
        // Also ignore if we're already outside bounds (cleanup phase)
        if (vm.ip < vm.chunk->code || vm.ip >= vm.chunk->code + vm.chunk->count) {
            return true;  // Silently ignore - we're in cleanup
        }

        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Jump out of bounds");
        return false;
    }
    vm.ip = new_ip;
    return true;
}

static inline bool CF_JUMP_BACK(uint16_t offset) {
    // If VM is shutting down or chunk is invalid, ignore jump
    if (vm.isShuttingDown || !vm.chunk || !vm.chunk->code) {
        return true;  // Silently ignore jump during cleanup
    }

    uint8_t* new_ip = vm.ip - offset;
    if (new_ip < vm.chunk->code || new_ip >= vm.chunk->code + vm.chunk->count) {
        // Also ignore if we're already outside bounds (cleanup phase)
        if (vm.ip < vm.chunk->code || vm.ip >= vm.chunk->code + vm.chunk->count) {
            return true;  // Silently ignore - we're in cleanup
        }

        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Jump back out of bounds");
        return false;
    }
    vm.ip = new_ip;
    return true;
}

static inline bool CF_JUMP_IF_NOT(uint8_t reg, uint16_t offset) {
    // If VM is shutting down or chunk is invalid, ignore jump
    if (vm.isShuttingDown || !vm.chunk || !vm.chunk->code) {
        return true;  // Silently ignore jump during cleanup
    }

    Value condition = vm_get_register_safe(reg);
    if (!IS_BOOL(condition)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Condition must be boolean");
        return false;
    }

    if (!AS_BOOL(condition)) {
        if (!CF_JUMP(offset)) {
            return false;
        }
    }

    return true;
}

static inline bool CF_JUMP_IF(uint8_t reg, uint16_t offset) {
    // If VM is shutting down or chunk is invalid, ignore jump
    if (vm.isShuttingDown || !vm.chunk || !vm.chunk->code) {
        return true;  // Silently ignore jump during cleanup
    }

    Value condition = vm_get_register_safe(reg);
    if (!IS_BOOL(condition)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Condition must be boolean");
        return false;
    }

    if (AS_BOOL(condition)) {
        if (!CF_JUMP(offset)) {
            return false;
        }
    }

    return true;
}

static inline bool CF_BRANCH_TYPED(uint16_t loop_id, uint8_t reg, uint16_t offset) {
    (void)loop_id;

    if (vm.isShuttingDown || !vm.chunk || !vm.chunk->code) {
        return true;
    }

    Value condition = vm_get_register_safe(reg);
    if (!IS_BOOL(condition)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Condition must be boolean");
        return false;
    }

    if (!AS_BOOL(condition)) {
        if (!CF_JUMP(offset)) {
            return false;
        }
    }

    return true;
}

static inline bool CF_JUMP_IF_NOT_I32_TYPED(uint8_t left_reg, uint8_t right_reg, uint16_t offset) {
    if (vm.isShuttingDown || !vm.chunk || !vm.chunk->code) {
        return true;
    }

    int32_t left_i32 = 0;
    int32_t right_i32 = 0;
    bool left_typed = vm_try_read_i32_typed(left_reg, &left_i32);
    bool right_typed = vm_try_read_i32_typed(right_reg, &right_i32);

    if (!left_typed || !right_typed) {
        Value left_value = vm_get_register_safe(left_reg);
        Value right_value = vm_get_register_safe(right_reg);

        if (!IS_I32(left_value) || !IS_I32(right_value)) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i32");
            return false;
        }

        left_i32 = AS_I32(left_value);
        right_i32 = AS_I32(right_value);
        vm_cache_i32_typed(left_reg, left_i32);
        vm_cache_i32_typed(right_reg, right_i32);
    }

    if (!(left_i32 < right_i32)) {
        if (!CF_JUMP(offset)) {
            return false;
        }
    }
    return true;
}

#define CF_LOOP(offset) CF_JUMP_BACK(offset)

#define CF_JUMP_SHORT(offset) CF_JUMP((uint16_t)(offset))
#define CF_JUMP_BACK_SHORT(offset) CF_JUMP_BACK((uint16_t)(offset))
#define CF_JUMP_IF_NOT_SHORT(reg, offset) CF_JUMP_IF_NOT(reg, (uint16_t)(offset))
#define CF_JUMP_IF_SHORT(reg, offset) CF_JUMP_IF(reg, (uint16_t)(offset))
#define CF_LOOP_SHORT(offset) CF_LOOP((uint16_t)(offset))

#endif // ORUS_VM_CONTROL_FLOW_H
