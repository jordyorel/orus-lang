#ifndef ORUS_VM_CONTROL_FLOW_H
#define ORUS_VM_CONTROL_FLOW_H

#include "../../src/vm/core/vm_internal.h"

// Forward declarations for safe register access helpers implemented in the
// computed-goto dispatch unit. These provide bounds-checked access to the
// unified register file without exposing the underlying representation here.
Value vm_get_register_safe(uint16_t id);
void vm_set_register_safe(uint16_t id, Value value);

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

#define CF_LOOP(offset) CF_JUMP_BACK(offset)

#define CF_JUMP_SHORT(offset) CF_JUMP((uint16_t)(offset))
#define CF_JUMP_BACK_SHORT(offset) CF_JUMP_BACK((uint16_t)(offset))
#define CF_JUMP_IF_NOT_SHORT(reg, offset) CF_JUMP_IF_NOT(reg, (uint16_t)(offset))
#define CF_LOOP_SHORT(offset) CF_LOOP((uint16_t)(offset))

#endif // ORUS_VM_CONTROL_FLOW_H
