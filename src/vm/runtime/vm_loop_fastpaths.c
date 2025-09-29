/*
 * Orus Language Project
 * ---------------------------------------------------------------------------
 * File: src/vm/runtime/vm_loop_fastpaths.c
 * Author: Jordy Orel KONDA
 * Copyright (c) 2025 Jordy Orel KONDA
 * License: MIT License (see LICENSE file in the project root)
 * Description: Implements optimized fast-path loops for hot opcode sequences.
 */

#include "vm/vm_loop_fastpaths.h"
#include "vm/vm_comparison.h"

#include <limits.h>

bool vm_try_branch_bool_fast_hot(uint16_t reg, bool* out_value) {
    if (!out_value) {
        return false;
    }

    if (!vm.config.enable_bool_branch_fastpath || !vm_typed_reg_in_range(reg)) {
        vm_trace_loop_event(LOOP_TRACE_BRANCH_FAST_MISS);
        return false;
    }

    if (vm.typed_regs.reg_types[reg] == REG_TYPE_BOOL) {
        *out_value = vm.typed_regs.bool_regs[reg];
        vm_trace_loop_event(LOOP_TRACE_BRANCH_FAST_HIT);
        if (vm.config.enable_licm_typed_metadata) {
            vm_trace_loop_event(LOOP_TRACE_LICM_GUARD_FUSION);
        }
        return true;
    }

    vm_trace_loop_event(LOOP_TRACE_BRANCH_FAST_MISS);
    if (vm.config.enable_licm_typed_metadata) {
        vm_trace_loop_event(LOOP_TRACE_LICM_GUARD_DEMOTION);
    }
    return false;
}

VMBoolBranchResult vm_try_branch_bool_fast_cold(uint16_t reg, bool* out_value) {
    if (!out_value) {
        return VM_BOOL_BRANCH_RESULT_FAIL;
    }

    bool fast_value = false;
    if (vm_try_branch_bool_fast_hot(reg, &fast_value)) {
        *out_value = fast_value;
        return VM_BOOL_BRANCH_RESULT_TYPED;
    }

    Value condition = vm_get_register_safe(reg);
    if (!IS_BOOL(condition)) {
        vm_trace_loop_event(LOOP_TRACE_TYPE_MISMATCH);
        if (vm.config.enable_licm_typed_metadata) {
            vm_trace_loop_event(LOOP_TRACE_LICM_GUARD_DEMOTION);
        }
        return VM_BOOL_BRANCH_RESULT_FAIL;
    }

    *out_value = AS_BOOL(condition);
    return VM_BOOL_BRANCH_RESULT_BOXED;
}

bool vm_exec_inc_i32_checked(uint16_t reg) {
    if (vm.config.disable_inc_typed_fastpath) {
        vm_trace_loop_event(LOOP_TRACE_TYPED_MISS);
        return false;
    }

    if (!vm_typed_reg_in_range(reg)) {
        vm_trace_loop_event(LOOP_TRACE_INC_TYPE_INSTABILITY);
        vm_trace_loop_event(LOOP_TRACE_TYPED_MISS);
        return false;
    }

    if (vm.typed_regs.reg_types[reg] != REG_TYPE_I32) {
        vm.typed_regs.reg_types[reg] = REG_TYPE_HEAP;
        vm.typed_regs.dirty[reg] = false;
        vm_trace_loop_event(LOOP_TRACE_INC_TYPE_INSTABILITY);
        vm_trace_loop_event(LOOP_TRACE_TYPED_MISS);
        return false;
    }

    int32_t current = vm.typed_regs.i32_regs[reg];
    int32_t next_value;
    if (__builtin_add_overflow(current, 1, &next_value)) {
        vm.typed_regs.reg_types[reg] = REG_TYPE_HEAP;
        vm.typed_regs.dirty[reg] = false;
        vm_trace_loop_event(LOOP_TRACE_INC_OVERFLOW_BAILOUT);
        vm_trace_loop_event(LOOP_TRACE_TYPED_MISS);
        return false;
    }

    vm_store_i32_typed_hot(reg, next_value);
    vm_trace_loop_event(LOOP_TRACE_TYPED_HIT);
    return true;
}

bool vm_exec_monotonic_inc_cmp_i32(uint16_t counter_reg, uint16_t limit_reg,
                                   bool* out_should_continue) {
    if (vm.config.disable_inc_typed_fastpath) {
        vm_trace_loop_event(LOOP_TRACE_TYPED_MISS);
        return false;
    }

    if (!vm_typed_reg_in_range(counter_reg) || !vm_typed_reg_in_range(limit_reg)) {
        vm_trace_loop_event(LOOP_TRACE_INC_TYPE_INSTABILITY);
        vm_trace_loop_event(LOOP_TRACE_TYPED_MISS);
        return false;
    }

    if (vm.typed_regs.reg_types[counter_reg] != REG_TYPE_I32 ||
        vm.typed_regs.reg_types[limit_reg] != REG_TYPE_I32) {
        if (vm.typed_regs.reg_types[counter_reg] != REG_TYPE_I32) {
            vm.typed_regs.reg_types[counter_reg] = REG_TYPE_HEAP;
            vm_trace_loop_event(LOOP_TRACE_INC_TYPE_INSTABILITY);
        }
        vm_trace_loop_event(LOOP_TRACE_TYPED_MISS);
        return false;
    }

    int32_t current = vm.typed_regs.i32_regs[counter_reg];
    if (current == INT32_MAX) {
        vm.typed_regs.reg_types[counter_reg] = REG_TYPE_HEAP;
        vm_trace_loop_event(LOOP_TRACE_INC_OVERFLOW_BAILOUT);
        vm_trace_loop_event(LOOP_TRACE_TYPED_MISS);
        return false;
    }

    int32_t next_value = current + 1;
    store_i32_register(counter_reg, next_value);

    int32_t limit_value = vm.typed_regs.i32_regs[limit_reg];
    if (out_should_continue) {
        *out_should_continue = next_value < limit_value;
    }

    vm_trace_loop_event(LOOP_TRACE_TYPED_HIT);
    return true;
}

bool vm_typed_iterator_next(uint16_t reg, Value* out_value) {
    if (!vm_typed_iterator_is_active(reg) || !out_value) {
        return false;
    }

    TypedIteratorDescriptor* descriptor = &vm.typed_iterators[reg];
    switch (descriptor->kind) {
        case TYPED_ITER_RANGE_I64: {
            int64_t current = descriptor->data.range_i64.current;
            int64_t end = descriptor->data.range_i64.end;
            int64_t step = descriptor->data.range_i64.step;

            if (step == 0) {
                vm_typed_iterator_invalidate(reg);
                return false;
            }

            bool done = (step > 0) ? (current >= end) : (current <= end);
            if (done) {
                vm_typed_iterator_invalidate(reg);
                return false;
            }
            *out_value = I64_VAL(current);
            descriptor->data.range_i64.current = current + step;
            vm_trace_loop_event(LOOP_TRACE_TYPED_HIT);
            return true;
        }
        case TYPED_ITER_ARRAY_SLICE: {
            ObjArray* array = descriptor->data.array.array;
            uint32_t index = descriptor->data.array.index;
            if (!array || index >= (uint32_t)array->length) {
                vm_typed_iterator_invalidate(reg);
                return false;
            }
            *out_value = array->elements[index];
            descriptor->data.array.index = index + 1;
            vm_trace_loop_event(LOOP_TRACE_TYPED_HIT);
            return true;
        }
        case TYPED_ITER_NONE:
        default:
            break;
    }

    return false;
}
