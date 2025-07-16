#ifndef ORUS_VM_TYPED_OPS_H
#define ORUS_VM_TYPED_OPS_H

#include "../../src/vm/core/vm_internal.h"
#include <math.h>

#define VM_TYPED_BIN_OP(array, op, type_enum) \
    do { \
        uint8_t dst = READ_BYTE(); \
        uint8_t left = READ_BYTE(); \
        uint8_t right = READ_BYTE(); \
        vm.typed_regs.array[dst] = vm.typed_regs.array[left] op vm.typed_regs.array[right]; \
        vm.typed_regs.reg_types[dst] = type_enum; \
    } while (0)

#define VM_TYPED_DIV_OP(array, type_enum) \
    do { \
        uint8_t dst = READ_BYTE(); \
        uint8_t left = READ_BYTE(); \
        uint8_t right = READ_BYTE(); \
        if (vm.typed_regs.array[right] == 0) { \
            runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero"); \
            return INTERPRET_RUNTIME_ERROR; \
        } \
        vm.typed_regs.array[dst] = vm.typed_regs.array[left] / vm.typed_regs.array[right]; \
        vm.typed_regs.reg_types[dst] = type_enum; \
    } while (0)

#define VM_TYPED_MOD_OP(array, type_enum, expr) \
    do { \
        uint8_t dst = READ_BYTE(); \
        uint8_t left = READ_BYTE(); \
        uint8_t right = READ_BYTE(); \
        if (vm.typed_regs.array[right] == 0) { \
            runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero"); \
            return INTERPRET_RUNTIME_ERROR; \
        } \
        vm.typed_regs.array[dst] = (expr); \
        vm.typed_regs.reg_types[dst] = type_enum; \
    } while (0)

#define VM_TYPED_CMP_OP(array, cmp) \
    do { \
        uint8_t dst = READ_BYTE(); \
        uint8_t left = READ_BYTE(); \
        uint8_t right = READ_BYTE(); \
        vm.typed_regs.bool_regs[dst] = vm.typed_regs.array[left] cmp vm.typed_regs.array[right]; \
        vm.typed_regs.reg_types[dst] = REG_TYPE_BOOL; \
    } while (0)

#define VM_TYPED_LOAD_CONST(array, field, type_enum) \
    do { \
        uint8_t reg = READ_BYTE(); \
        uint16_t constantIndex = READ_SHORT(); \
        vm.typed_regs.array[reg] = READ_CONSTANT(constantIndex).as.field; \
        vm.typed_regs.reg_types[reg] = type_enum; \
    } while (0)

#define VM_TYPED_MOVE(array, type_enum) \
    do { \
        uint8_t dst = READ_BYTE(); \
        uint8_t src = READ_BYTE(); \
        vm.typed_regs.array[dst] = vm.typed_regs.array[src]; \
        vm.typed_regs.reg_types[dst] = type_enum; \
    } while (0)

#endif // ORUS_VM_TYPED_OPS_H
