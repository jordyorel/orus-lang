#ifndef ORUS_VM_CONTROL_FLOW_H
#define ORUS_VM_CONTROL_FLOW_H

#include "../../src/vm/core/vm_internal.h"

static inline void CF_JUMP(uint16_t offset) {
    uint8_t* new_ip = vm.ip + offset;
    if (new_ip < vm.chunk->code || new_ip >= vm.chunk->code + vm.chunk->count) {
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Jump out of bounds");
        return;
    }
    vm.ip = new_ip;
}

static inline void CF_JUMP_BACK(uint16_t offset) {
    uint8_t* new_ip = vm.ip - offset;
    if (new_ip < vm.chunk->code || new_ip >= vm.chunk->code + vm.chunk->count) {
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Jump back out of bounds");
        return;
    }
    vm.ip = new_ip;
}

static inline bool CF_JUMP_IF_NOT(uint8_t reg, uint16_t offset) {
    if (!IS_BOOL(vm.registers[reg])) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Condition must be boolean");
        return false;
    }
    if (!AS_BOOL(vm.registers[reg])) {
        uint8_t* new_ip = vm.ip + offset;
        if (new_ip < vm.chunk->code || new_ip >= vm.chunk->code + vm.chunk->count) {
            runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Conditional jump out of bounds");
            return false;
        }
        vm.ip = new_ip;
    }
    return true;
}

#define CF_LOOP(offset) CF_JUMP_BACK(offset)

#define CF_JUMP_SHORT(offset) CF_JUMP((uint16_t)(offset))
#define CF_JUMP_BACK_SHORT(offset) CF_JUMP_BACK((uint16_t)(offset))
#define CF_JUMP_IF_NOT_SHORT(reg, offset) CF_JUMP_IF_NOT(reg, (uint16_t)(offset))
#define CF_LOOP_SHORT(offset) CF_LOOP((uint16_t)(offset))

#endif // ORUS_VM_CONTROL_FLOW_H
