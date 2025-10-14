// Orus Language Project
// ---------------------------------------------------------------------------
// File: include/vm/vm_typed_ops.h
// Author: Jordy Orel KONDA
// Copyright (c) 2025 Jordy Orel KONDA
// License: MIT License (see LICENSE file in the project root)
// Description: Declares typed operation helpers bridging static and dynamic semantics.


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

#define VM_TYPED_DIV_OP(array, zero_value, store_fn) \
    do { \
        uint8_t dst = READ_BYTE(); \
        uint8_t left = READ_BYTE(); \
        uint8_t right = READ_BYTE(); \
        if (vm.typed_regs.array[right] == (zero_value)) { \
            runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero"); \
        } else { \
            store_fn(dst, vm.typed_regs.array[left] / vm.typed_regs.array[right]); \
        } \
    } while (0)

#define VM_TYPED_MOD_OP(array, zero_value, store_fn, expr) \
    do { \
        uint8_t dst = READ_BYTE(); \
        uint8_t left = READ_BYTE(); \
        uint8_t right = READ_BYTE(); \
        if (vm.typed_regs.array[right] == (zero_value)) { \
            runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero"); \
        } else { \
            store_fn(dst, (expr)); \
        } \
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

#define VM_TYPED_ADD_I32() VM_TYPED_BIN_OP(i32_regs, +, vm_store_i32_typed_hot)
#define VM_TYPED_SUB_I32() VM_TYPED_BIN_OP(i32_regs, -, vm_store_i32_typed_hot)
#define VM_TYPED_MUL_I32() VM_TYPED_BIN_OP(i32_regs, *, vm_store_i32_typed_hot)
#define VM_TYPED_DIV_I32() VM_TYPED_DIV_OP(i32_regs, 0, vm_store_i32_typed_hot)
#define VM_TYPED_MOD_I32() \
    VM_TYPED_MOD_OP(i32_regs, 0, vm_store_i32_typed_hot, \
                    vm.typed_regs.i32_regs[left] % vm.typed_regs.i32_regs[right])

#define VM_TYPED_ADD_I64() VM_TYPED_BIN_OP(i64_regs, +, vm_store_i64_typed_hot)
#define VM_TYPED_SUB_I64() VM_TYPED_BIN_OP(i64_regs, -, vm_store_i64_typed_hot)
#define VM_TYPED_MUL_I64() VM_TYPED_BIN_OP(i64_regs, *, vm_store_i64_typed_hot)
#define VM_TYPED_DIV_I64() VM_TYPED_DIV_OP(i64_regs, 0, vm_store_i64_typed_hot)
#define VM_TYPED_MOD_I64() \
    VM_TYPED_MOD_OP(i64_regs, 0, vm_store_i64_typed_hot, \
                    vm.typed_regs.i64_regs[left] % vm.typed_regs.i64_regs[right])

#define VM_TYPED_ADD_U32() VM_TYPED_BIN_OP(u32_regs, +, vm_store_u32_typed_hot)
#define VM_TYPED_SUB_U32() VM_TYPED_BIN_OP(u32_regs, -, vm_store_u32_typed_hot)
#define VM_TYPED_MUL_U32() VM_TYPED_BIN_OP(u32_regs, *, vm_store_u32_typed_hot)
#define VM_TYPED_DIV_U32() VM_TYPED_DIV_OP(u32_regs, 0, vm_store_u32_typed_hot)
#define VM_TYPED_MOD_U32() \
    VM_TYPED_MOD_OP(u32_regs, 0, vm_store_u32_typed_hot, \
                    vm.typed_regs.u32_regs[left] % vm.typed_regs.u32_regs[right])

#define VM_TYPED_ADD_U64() VM_TYPED_BIN_OP(u64_regs, +, vm_store_u64_typed_hot)
#define VM_TYPED_SUB_U64() VM_TYPED_BIN_OP(u64_regs, -, vm_store_u64_typed_hot)
#define VM_TYPED_MUL_U64() VM_TYPED_BIN_OP(u64_regs, *, vm_store_u64_typed_hot)
#define VM_TYPED_DIV_U64() VM_TYPED_DIV_OP(u64_regs, 0, vm_store_u64_typed_hot)
#define VM_TYPED_MOD_U64() \
    VM_TYPED_MOD_OP(u64_regs, 0, vm_store_u64_typed_hot, \
                    vm.typed_regs.u64_regs[left] % vm.typed_regs.u64_regs[right])

#define VM_TYPED_ADD_F64() VM_TYPED_BIN_OP(f64_regs, +, vm_store_f64_typed_hot)
#define VM_TYPED_SUB_F64() VM_TYPED_BIN_OP(f64_regs, -, vm_store_f64_typed_hot)
#define VM_TYPED_MUL_F64() VM_TYPED_BIN_OP(f64_regs, *, vm_store_f64_typed_hot)
#define VM_TYPED_DIV_F64() VM_TYPED_DIV_OP(f64_regs, 0.0, vm_store_f64_typed_hot)
#define VM_TYPED_MOD_F64() \
    VM_TYPED_MOD_OP(f64_regs, 0.0, vm_store_f64_typed_hot, \
                    fmod(vm.typed_regs.f64_regs[left], vm.typed_regs.f64_regs[right]))

#endif // ORUS_VM_TYPED_OPS_H
