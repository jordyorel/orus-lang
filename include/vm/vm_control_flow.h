#ifndef ORUS_VM_CONTROL_FLOW_H
#define ORUS_VM_CONTROL_FLOW_H

#include "../../src/vm/core/vm_internal.h"

#define CF_JUMP(offset) \
    do { \
        vm.ip += (offset); \
    } while (0)

#define CF_JUMP_BACK(offset) \
    do { \
        vm.ip -= (offset); \
    } while (0)

#define CF_JUMP_IF_NOT(reg, offset) \
    do { \
        if (!IS_BOOL(vm.registers[(reg)])) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Condition must be boolean"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        if (!AS_BOOL(vm.registers[(reg)])) { \
            vm.ip += (offset); \
        } \
    } while (0)

#define CF_LOOP(offset) CF_JUMP_BACK(offset)

#define CF_JUMP_SHORT(offset) CF_JUMP((uint16_t)(offset))
#define CF_JUMP_BACK_SHORT(offset) CF_JUMP_BACK((uint16_t)(offset))
#define CF_JUMP_IF_NOT_SHORT(reg, offset) CF_JUMP_IF_NOT(reg, (uint16_t)(offset))
#define CF_LOOP_SHORT(offset) CF_LOOP((uint16_t)(offset))

#endif // ORUS_VM_CONTROL_FLOW_H
