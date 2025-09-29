/*
 * Orus Language Project
 * ---------------------------------------------------------------------------
 * File: include/vm/vm_loop_fastpaths.h
 * Author: Jordy Orel KONDA
 * Copyright (c) 2025 Jordy Orel KONDA
 * License: MIT License (see LICENSE file in the project root)
 * Description: Declares fast-path specializations used by tight VM dispatch loops.
 */

#ifndef ORUS_VM_LOOP_FASTPATHS_H
#define ORUS_VM_LOOP_FASTPATHS_H

#include <stdbool.h>
#include <stdint.h>

#include "vm/vm.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VM_BOOL_BRANCH_RESULT_FAIL = 0,
    VM_BOOL_BRANCH_RESULT_BOXED = 1,
    VM_BOOL_BRANCH_RESULT_TYPED = 2,
} VMBoolBranchResult;

static inline void vm_typed_iterator_invalidate(uint16_t reg) {
    if (reg >= REGISTER_COUNT) {
        return;
    }
    vm.typed_iterators[reg].kind = TYPED_ITER_NONE;
    vm.typed_iterators[reg].data.range_i64.current = 0;
    vm.typed_iterators[reg].data.range_i64.end = 0;
    vm.typed_iterators[reg].data.range_i64.step = 1;
    vm.typed_iterators[reg].data.array.array = NULL;
    vm.typed_iterators[reg].data.array.index = 0;
}

static inline bool vm_typed_iterator_is_active(uint16_t reg) {
    return reg < REGISTER_COUNT && vm.typed_iterators[reg].kind != TYPED_ITER_NONE;
}

static inline void vm_typed_iterator_bind_range(uint16_t reg, int64_t start, int64_t end,
                                                int64_t step) {
    if (reg >= REGISTER_COUNT) {
        return;
    }
    vm.typed_iterators[reg].kind = TYPED_ITER_RANGE_I64;
    vm.typed_iterators[reg].data.range_i64.current = start;
    vm.typed_iterators[reg].data.range_i64.end = end;
    vm.typed_iterators[reg].data.range_i64.step = step;
}

static inline bool vm_typed_iterator_bind_array(uint16_t reg, ObjArray* array) {
    if (reg >= REGISTER_COUNT || !array) {
        return false;
    }
    vm.typed_iterators[reg].kind = TYPED_ITER_ARRAY_SLICE;
    vm.typed_iterators[reg].data.array.array = array;
    vm.typed_iterators[reg].data.array.index = 0;
    return true;
}

bool vm_try_branch_bool_fast_hot(uint16_t reg, bool* out_value);
VMBoolBranchResult vm_try_branch_bool_fast_cold(uint16_t reg, bool* out_value);
bool vm_exec_inc_i32_checked(uint16_t reg);
bool vm_exec_monotonic_inc_cmp_i32(uint16_t counter_reg, uint16_t limit_reg, bool* out_should_continue);
bool vm_typed_iterator_next(uint16_t reg, Value* out_value);

#ifdef __cplusplus
}
#endif

#endif // ORUS_VM_LOOP_FASTPATHS_H
