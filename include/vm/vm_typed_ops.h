#ifndef ORUS_VM_TYPED_OPS_H
#define ORUS_VM_TYPED_OPS_H

#include "../../src/vm/core/vm_internal.h"
#include "vm/vm_comparison.h"
#include <math.h>

#define VM_TYPED_BIN_OP(array, op, store_fn) \
    do { \
        uint8_t dst = READ_BYTE(); \
        uint8_t left = READ_BYTE(); \
        uint8_t right = READ_BYTE(); \
        store_fn(dst, vm.typed_regs.array[left] op vm.typed_regs.array[right]); \
    } while (0)

#define VM_TYPED_DIV_OP(array, store_fn) \
    do { \
        uint8_t dst = READ_BYTE(); \
        uint8_t left = READ_BYTE(); \
        uint8_t right = READ_BYTE(); \
        if (vm.typed_regs.array[right] == 0) { \
            runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero"); \
            return INTERPRET_RUNTIME_ERROR; \
        } \
        store_fn(dst, vm.typed_regs.array[left] / vm.typed_regs.array[right]); \
    } while (0)

#define VM_TYPED_MOD_OP(array, store_fn, expr) \
    do { \
        uint8_t dst = READ_BYTE(); \
        uint8_t left = READ_BYTE(); \
        uint8_t right = READ_BYTE(); \
        if (vm.typed_regs.array[right] == 0) { \
            runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero"); \
            return INTERPRET_RUNTIME_ERROR; \
        } \
        store_fn(dst, (expr)); \
    } while (0)

#define VM_TYPED_CMP_OP(array, cmp) \
    do { \
        uint8_t dst = READ_BYTE(); \
        uint8_t left = READ_BYTE(); \
        uint8_t right = READ_BYTE(); \
        bool result = vm.typed_regs.array[left] cmp vm.typed_regs.array[right]; \
        vm_store_bool_register(dst, result); \
    } while (0)

#define VM_TYPED_LOAD_CONST(array, field, store_fn) \
    do { \
        uint8_t reg = READ_BYTE(); \
        uint16_t constantIndex = READ_SHORT(); \
        store_fn(reg, READ_CONSTANT(constantIndex).as.field); \
    } while (0)

#define VM_TYPED_MOVE(array, store_fn) \
    do { \
        uint8_t dst = READ_BYTE(); \
        uint8_t src = READ_BYTE(); \
        store_fn(dst, vm.typed_regs.array[src]); \
    } while (0)

#endif // ORUS_VM_TYPED_OPS_H
