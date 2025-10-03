// Orus Language Project
// ---------------------------------------------------------------------------
// File: src/vm/runtime/vm_loop_fastpaths.c
// Author: Jordy Orel KONDA
// Copyright (c) 2025 Jordy Orel KONDA
// License: MIT License (see LICENSE file in the project root)
// Description: Implements optimized fast-path loops for hot opcode sequences.


#include "vm/vm_loop_fastpaths.h"
#include "vm/vm_comparison.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

static size_t vm_branch_cache_bucket_index(uint16_t loop_id, uint16_t reg) {
    uint32_t key = ((uint32_t)loop_id << 16) ^ (uint32_t)reg;
    key *= 2654435761u;
    return key % LOOP_BRANCH_CACHE_BUCKET_COUNT;
}

static LoopBranchCacheBucket* vm_branch_cache_bucket(uint16_t loop_id, uint16_t reg) {
    size_t index = vm_branch_cache_bucket_index(loop_id, reg);
    return &vm.branch_cache.buckets[index];
}

static LoopBranchCacheEntry* vm_branch_cache_lookup(uint16_t loop_id, uint16_t reg) {
    LoopBranchCacheBucket* bucket = vm_branch_cache_bucket(loop_id, reg);
    for (size_t i = 0; i < LOOP_BRANCH_CACHE_BUCKET_SIZE; ++i) {
        LoopBranchCacheEntry* entry = &bucket->slots[i];
        if (entry->valid && entry->loop_id == loop_id && entry->predicate_reg == reg) {
            return entry;
        }
    }
    return NULL;
}

static LoopBranchCacheEntry* vm_branch_cache_select_slot(uint16_t loop_id, uint16_t reg) {
    LoopBranchCacheBucket* bucket = vm_branch_cache_bucket(loop_id, reg);
    LoopBranchCacheEntry* free_slot = NULL;
    for (size_t i = 0; i < LOOP_BRANCH_CACHE_BUCKET_SIZE; ++i) {
        LoopBranchCacheEntry* entry = &bucket->slots[i];
        if (!entry->valid) {
            if (!free_slot) {
                free_slot = entry;
            }
            continue;
        }
        if (entry->loop_id == loop_id && entry->predicate_reg == reg) {
            return entry;
        }
    }
    if (free_slot) {
        return free_slot;
    }

    size_t slot_index = reg % LOOP_BRANCH_CACHE_BUCKET_SIZE;
    return &bucket->slots[slot_index];
}

void vm_branch_cache_reset(void) {
    memset(vm.branch_cache.buckets, 0, sizeof(vm.branch_cache.buckets));
    memset(vm.branch_cache.guard_generations, 0, sizeof(vm.branch_cache.guard_generations));
}

void vm_branch_cache_bump_generation(uint16_t reg) {
    if (reg >= REGISTER_COUNT) {
        return;
    }
    vm.branch_cache.guard_generations[reg]++;
}

bool vm_branch_cache_try_get(uint16_t loop_id, uint16_t reg, bool* out_value) {
    if (!out_value || !vm.config.enable_bool_branch_fastpath) {
        return false;
    }

    LoopBranchCacheEntry* entry = vm_branch_cache_lookup(loop_id, reg);
    if (!vm_typed_reg_in_range(reg) || vm.typed_regs.reg_types[reg] != REG_TYPE_BOOL) {
        if (entry) {
            entry->valid = false;
        }
        vm_trace_loop_event(LOOP_TRACE_BRANCH_CACHE_MISS);
        return false;
    }

    if (!entry || !entry->valid) {
        vm_trace_loop_event(LOOP_TRACE_BRANCH_CACHE_MISS);
        return false;
    }

    if (entry->guard_generation != vm.branch_cache.guard_generations[reg]) {
        entry->valid = false;
        vm_trace_loop_event(LOOP_TRACE_BRANCH_CACHE_MISS);
        return false;
    }

    *out_value = vm.typed_regs.bool_regs[reg];
    vm_trace_loop_event(LOOP_TRACE_TYPED_HIT);
    vm_trace_loop_event(LOOP_TRACE_BRANCH_FAST_HIT);
    vm_trace_loop_event(LOOP_TRACE_BRANCH_CACHE_HIT);
    return true;
}

void vm_branch_cache_store(uint16_t loop_id, uint16_t reg) {
    if (!vm_typed_reg_in_range(reg) || vm.typed_regs.reg_types[reg] != REG_TYPE_BOOL) {
        return;
    }

    LoopBranchCacheEntry* entry = vm_branch_cache_select_slot(loop_id, reg);
    if (!entry) {
        return;
    }

    entry->valid = true;
    entry->loop_id = loop_id;
    entry->predicate_reg = reg;
    entry->predicate_type = REG_TYPE_BOOL;
    entry->guard_generation = vm.branch_cache.guard_generations[reg];
}

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
    vm_branch_cache_bump_generation(reg);
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
        vm_branch_cache_bump_generation(reg);
        return VM_BOOL_BRANCH_RESULT_FAIL;
    }

    *out_value = AS_BOOL(condition);
    vm_cache_bool_typed(reg, *out_value);
    return VM_BOOL_BRANCH_RESULT_BOXED;
}

bool vm_exec_inc_i32_checked(uint16_t reg) {
    if (vm.config.disable_inc_typed_fastpath) {
        vm_trace_loop_event(LOOP_TRACE_TYPED_MISS);
        vm_trace_loop_event(LOOP_TRACE_INC_FAST_MISS);
        return false;
    }

    if (!vm_typed_reg_in_range(reg)) {
        vm_trace_loop_event(LOOP_TRACE_INC_TYPE_INSTABILITY);
        vm_trace_loop_event(LOOP_TRACE_TYPED_MISS);
        vm_trace_loop_event(LOOP_TRACE_INC_FAST_MISS);
        return false;
    }

    if (vm.typed_regs.reg_types[reg] != REG_TYPE_I32) {
        vm_branch_cache_bump_generation(reg);
        vm.typed_regs.reg_types[reg] = REG_TYPE_HEAP;
        vm.typed_regs.dirty[reg] = false;
        vm_trace_loop_event(LOOP_TRACE_INC_TYPE_INSTABILITY);
        vm_trace_loop_event(LOOP_TRACE_TYPED_MISS);
        vm_trace_loop_event(LOOP_TRACE_INC_FAST_MISS);
        return false;
    }

    int32_t current = vm.typed_regs.i32_regs[reg];
    int32_t next_value;
    if (__builtin_add_overflow(current, 1, &next_value)) {
        vm_branch_cache_bump_generation(reg);
        vm.typed_regs.reg_types[reg] = REG_TYPE_HEAP;
        vm.typed_regs.dirty[reg] = false;
        vm_trace_loop_event(LOOP_TRACE_INC_OVERFLOW_BAILOUT);
        vm_trace_loop_event(LOOP_TRACE_TYPED_MISS);
        vm_trace_loop_event(LOOP_TRACE_INC_FAST_MISS);
        return false;
    }

    vm_store_i32_typed_hot(reg, next_value);
    vm_trace_loop_event(LOOP_TRACE_TYPED_HIT);
    vm_trace_loop_event(LOOP_TRACE_INC_FAST_HIT);
    return true;
}

bool vm_exec_inc_i64_checked(uint16_t reg) {
    if (vm.config.disable_inc_typed_fastpath) {
        vm_trace_loop_event(LOOP_TRACE_TYPED_MISS);
        vm_trace_loop_event(LOOP_TRACE_INC_FAST_MISS);
        return false;
    }

    if (!vm_typed_reg_in_range(reg)) {
        vm_trace_loop_event(LOOP_TRACE_INC_TYPE_INSTABILITY);
        vm_trace_loop_event(LOOP_TRACE_TYPED_MISS);
        vm_trace_loop_event(LOOP_TRACE_INC_FAST_MISS);
        return false;
    }

    if (vm.typed_regs.reg_types[reg] != REG_TYPE_I64) {
        vm_branch_cache_bump_generation(reg);
        vm.typed_regs.reg_types[reg] = REG_TYPE_HEAP;
        vm.typed_regs.dirty[reg] = false;
        vm_trace_loop_event(LOOP_TRACE_INC_TYPE_INSTABILITY);
        vm_trace_loop_event(LOOP_TRACE_TYPED_MISS);
        vm_trace_loop_event(LOOP_TRACE_INC_FAST_MISS);
        return false;
    }

    int64_t current = vm.typed_regs.i64_regs[reg];
    int64_t next_value;
    if (__builtin_add_overflow(current, (int64_t)1, &next_value)) {
        vm_branch_cache_bump_generation(reg);
        vm.typed_regs.reg_types[reg] = REG_TYPE_HEAP;
        vm.typed_regs.dirty[reg] = false;
        vm_trace_loop_event(LOOP_TRACE_INC_OVERFLOW_BAILOUT);
        vm_trace_loop_event(LOOP_TRACE_TYPED_MISS);
        vm_trace_loop_event(LOOP_TRACE_INC_FAST_MISS);
        return false;
    }

    vm_store_i64_typed_hot(reg, next_value);
    vm_trace_loop_event(LOOP_TRACE_TYPED_HIT);
    vm_trace_loop_event(LOOP_TRACE_INC_FAST_HIT);
    return true;
}

bool vm_exec_inc_u32_checked(uint16_t reg) {
    if (vm.config.disable_inc_typed_fastpath) {
        vm_trace_loop_event(LOOP_TRACE_TYPED_MISS);
        vm_trace_loop_event(LOOP_TRACE_INC_FAST_MISS);
        return false;
    }

    if (!vm_typed_reg_in_range(reg)) {
        vm_trace_loop_event(LOOP_TRACE_INC_TYPE_INSTABILITY);
        vm_trace_loop_event(LOOP_TRACE_TYPED_MISS);
        vm_trace_loop_event(LOOP_TRACE_INC_FAST_MISS);
        return false;
    }

    if (vm.typed_regs.reg_types[reg] != REG_TYPE_U32) {
        vm_branch_cache_bump_generation(reg);
        vm.typed_regs.reg_types[reg] = REG_TYPE_HEAP;
        vm.typed_regs.dirty[reg] = false;
        vm_trace_loop_event(LOOP_TRACE_INC_TYPE_INSTABILITY);
        vm_trace_loop_event(LOOP_TRACE_TYPED_MISS);
        vm_trace_loop_event(LOOP_TRACE_INC_FAST_MISS);
        return false;
    }

    uint32_t current = vm.typed_regs.u32_regs[reg];
    uint32_t next_value = current + (uint32_t)1;

    vm_store_u32_typed_hot(reg, next_value);
    vm_trace_loop_event(LOOP_TRACE_TYPED_HIT);
    vm_trace_loop_event(LOOP_TRACE_INC_FAST_HIT);
    return true;
}

bool vm_exec_inc_u64_checked(uint16_t reg) {
    if (vm.config.disable_inc_typed_fastpath) {
        vm_trace_loop_event(LOOP_TRACE_TYPED_MISS);
        vm_trace_loop_event(LOOP_TRACE_INC_FAST_MISS);
        return false;
    }

    if (!vm_typed_reg_in_range(reg)) {
        vm_trace_loop_event(LOOP_TRACE_INC_TYPE_INSTABILITY);
        vm_trace_loop_event(LOOP_TRACE_TYPED_MISS);
        vm_trace_loop_event(LOOP_TRACE_INC_FAST_MISS);
        return false;
    }

    if (vm.typed_regs.reg_types[reg] != REG_TYPE_U64) {
        vm_branch_cache_bump_generation(reg);
        vm.typed_regs.reg_types[reg] = REG_TYPE_HEAP;
        vm.typed_regs.dirty[reg] = false;
        vm_trace_loop_event(LOOP_TRACE_INC_TYPE_INSTABILITY);
        vm_trace_loop_event(LOOP_TRACE_TYPED_MISS);
        vm_trace_loop_event(LOOP_TRACE_INC_FAST_MISS);
        return false;
    }

    uint64_t current = vm.typed_regs.u64_regs[reg];
    uint64_t next_value = current + (uint64_t)1;

    vm_store_u64_typed_hot(reg, next_value);
    vm_trace_loop_event(LOOP_TRACE_TYPED_HIT);
    vm_trace_loop_event(LOOP_TRACE_INC_FAST_HIT);
    return true;
}

bool vm_exec_monotonic_inc_cmp_i32(uint16_t counter_reg, uint16_t limit_reg,
                                   bool* out_should_continue) {
    if (vm.config.disable_inc_typed_fastpath) {
        vm_trace_loop_event(LOOP_TRACE_TYPED_MISS);
        vm_trace_loop_event(LOOP_TRACE_INC_FAST_MISS);
        return false;
    }

    if (!vm_typed_reg_in_range(counter_reg) || !vm_typed_reg_in_range(limit_reg)) {
        vm_trace_loop_event(LOOP_TRACE_INC_TYPE_INSTABILITY);
        vm_trace_loop_event(LOOP_TRACE_TYPED_MISS);
        vm_trace_loop_event(LOOP_TRACE_INC_FAST_MISS);
        return false;
    }

    if (vm.typed_regs.reg_types[counter_reg] != REG_TYPE_I32 ||
        vm.typed_regs.reg_types[limit_reg] != REG_TYPE_I32) {
        if (vm.typed_regs.reg_types[counter_reg] != REG_TYPE_I32) {
            vm_branch_cache_bump_generation(counter_reg);
            vm.typed_regs.reg_types[counter_reg] = REG_TYPE_HEAP;
            vm_trace_loop_event(LOOP_TRACE_INC_TYPE_INSTABILITY);
        }
        vm_trace_loop_event(LOOP_TRACE_TYPED_MISS);
        vm_trace_loop_event(LOOP_TRACE_INC_FAST_MISS);
        return false;
    }

    int32_t current = vm.typed_regs.i32_regs[counter_reg];
    if (current == INT32_MAX) {
        vm_branch_cache_bump_generation(counter_reg);
        vm.typed_regs.reg_types[counter_reg] = REG_TYPE_HEAP;
        vm_trace_loop_event(LOOP_TRACE_INC_OVERFLOW_BAILOUT);
        vm_trace_loop_event(LOOP_TRACE_TYPED_MISS);
        vm_trace_loop_event(LOOP_TRACE_INC_FAST_MISS);
        return false;
    }

    int32_t next_value = current + 1;
    store_i32_register(counter_reg, next_value);

    int32_t limit_value = vm.typed_regs.i32_regs[limit_reg];
    if (out_should_continue) {
        *out_should_continue = next_value < limit_value;
    }

    vm_trace_loop_event(LOOP_TRACE_TYPED_HIT);
    vm_trace_loop_event(LOOP_TRACE_INC_FAST_HIT);
    return true;
}

bool vm_exec_monotonic_inc_cmp_i64(uint16_t counter_reg, uint16_t limit_reg,
                                   bool* out_should_continue) {
    if (vm.config.disable_inc_typed_fastpath) {
        vm_trace_loop_event(LOOP_TRACE_TYPED_MISS);
        vm_trace_loop_event(LOOP_TRACE_INC_FAST_MISS);
        return false;
    }

    if (!vm_typed_reg_in_range(counter_reg) || !vm_typed_reg_in_range(limit_reg)) {
        vm_trace_loop_event(LOOP_TRACE_INC_TYPE_INSTABILITY);
        vm_trace_loop_event(LOOP_TRACE_TYPED_MISS);
        vm_trace_loop_event(LOOP_TRACE_INC_FAST_MISS);
        return false;
    }

    if (vm.typed_regs.reg_types[counter_reg] != REG_TYPE_I64 ||
        vm.typed_regs.reg_types[limit_reg] != REG_TYPE_I64) {
        if (vm.typed_regs.reg_types[counter_reg] != REG_TYPE_I64) {
            vm_branch_cache_bump_generation(counter_reg);
            vm.typed_regs.reg_types[counter_reg] = REG_TYPE_HEAP;
            vm_trace_loop_event(LOOP_TRACE_INC_TYPE_INSTABILITY);
        }
        vm_trace_loop_event(LOOP_TRACE_TYPED_MISS);
        vm_trace_loop_event(LOOP_TRACE_INC_FAST_MISS);
        return false;
    }

    int64_t current = vm.typed_regs.i64_regs[counter_reg];
    if (current == INT64_MAX) {
        vm_branch_cache_bump_generation(counter_reg);
        vm.typed_regs.reg_types[counter_reg] = REG_TYPE_HEAP;
        vm_trace_loop_event(LOOP_TRACE_INC_OVERFLOW_BAILOUT);
        vm_trace_loop_event(LOOP_TRACE_TYPED_MISS);
        vm_trace_loop_event(LOOP_TRACE_INC_FAST_MISS);
        return false;
    }

    int64_t next_value = current + 1;
    store_i64_register(counter_reg, next_value);

    int64_t limit_value = vm.typed_regs.i64_regs[limit_reg];
    if (out_should_continue) {
        *out_should_continue = next_value < limit_value;
    }

    vm_trace_loop_event(LOOP_TRACE_TYPED_HIT);
    vm_trace_loop_event(LOOP_TRACE_INC_FAST_HIT);
    return true;
}

bool vm_exec_monotonic_inc_cmp_u32(uint16_t counter_reg, uint16_t limit_reg,
                                   bool* out_should_continue) {
    if (vm.config.disable_inc_typed_fastpath) {
        vm_trace_loop_event(LOOP_TRACE_TYPED_MISS);
        vm_trace_loop_event(LOOP_TRACE_INC_FAST_MISS);
        return false;
    }

    if (!vm_typed_reg_in_range(counter_reg) || !vm_typed_reg_in_range(limit_reg)) {
        vm_trace_loop_event(LOOP_TRACE_INC_TYPE_INSTABILITY);
        vm_trace_loop_event(LOOP_TRACE_TYPED_MISS);
        vm_trace_loop_event(LOOP_TRACE_INC_FAST_MISS);
        return false;
    }

    if (vm.typed_regs.reg_types[counter_reg] != REG_TYPE_U32 ||
        vm.typed_regs.reg_types[limit_reg] != REG_TYPE_U32) {
        if (vm.typed_regs.reg_types[counter_reg] != REG_TYPE_U32) {
            vm_branch_cache_bump_generation(counter_reg);
            vm.typed_regs.reg_types[counter_reg] = REG_TYPE_HEAP;
            vm_trace_loop_event(LOOP_TRACE_INC_TYPE_INSTABILITY);
        }
        vm_trace_loop_event(LOOP_TRACE_TYPED_MISS);
        vm_trace_loop_event(LOOP_TRACE_INC_FAST_MISS);
        return false;
    }

    uint32_t current = vm.typed_regs.u32_regs[counter_reg];
    if (current == UINT32_MAX) {
        vm_branch_cache_bump_generation(counter_reg);
        vm.typed_regs.reg_types[counter_reg] = REG_TYPE_HEAP;
        vm_trace_loop_event(LOOP_TRACE_INC_OVERFLOW_BAILOUT);
        vm_trace_loop_event(LOOP_TRACE_TYPED_MISS);
        vm_trace_loop_event(LOOP_TRACE_INC_FAST_MISS);
        return false;
    }

    uint32_t next_value = current + 1u;
    store_u32_register(counter_reg, next_value);

    uint32_t limit_value = vm.typed_regs.u32_regs[limit_reg];
    if (out_should_continue) {
        *out_should_continue = next_value < limit_value;
    }

    vm_trace_loop_event(LOOP_TRACE_TYPED_HIT);
    vm_trace_loop_event(LOOP_TRACE_INC_FAST_HIT);
    return true;
}

bool vm_exec_monotonic_inc_cmp_u64(uint16_t counter_reg, uint16_t limit_reg,
                                   bool* out_should_continue) {
    if (vm.config.disable_inc_typed_fastpath) {
        vm_trace_loop_event(LOOP_TRACE_TYPED_MISS);
        vm_trace_loop_event(LOOP_TRACE_INC_FAST_MISS);
        return false;
    }

    if (!vm_typed_reg_in_range(counter_reg) || !vm_typed_reg_in_range(limit_reg)) {
        vm_trace_loop_event(LOOP_TRACE_INC_TYPE_INSTABILITY);
        vm_trace_loop_event(LOOP_TRACE_TYPED_MISS);
        vm_trace_loop_event(LOOP_TRACE_INC_FAST_MISS);
        return false;
    }

    if (vm.typed_regs.reg_types[counter_reg] != REG_TYPE_U64 ||
        vm.typed_regs.reg_types[limit_reg] != REG_TYPE_U64) {
        if (vm.typed_regs.reg_types[counter_reg] != REG_TYPE_U64) {
            vm_branch_cache_bump_generation(counter_reg);
            vm.typed_regs.reg_types[counter_reg] = REG_TYPE_HEAP;
            vm_trace_loop_event(LOOP_TRACE_INC_TYPE_INSTABILITY);
        }
        vm_trace_loop_event(LOOP_TRACE_TYPED_MISS);
        vm_trace_loop_event(LOOP_TRACE_INC_FAST_MISS);
        return false;
    }

    uint64_t current = vm.typed_regs.u64_regs[counter_reg];
    if (current == UINT64_MAX) {
        vm_branch_cache_bump_generation(counter_reg);
        vm.typed_regs.reg_types[counter_reg] = REG_TYPE_HEAP;
        vm_trace_loop_event(LOOP_TRACE_INC_OVERFLOW_BAILOUT);
        vm_trace_loop_event(LOOP_TRACE_TYPED_MISS);
        vm_trace_loop_event(LOOP_TRACE_INC_FAST_MISS);
        return false;
    }

    uint64_t next_value = current + 1u;
    store_u64_register(counter_reg, next_value);

    uint64_t limit_value = vm.typed_regs.u64_regs[limit_reg];
    if (out_should_continue) {
        *out_should_continue = next_value < limit_value;
    }

    vm_trace_loop_event(LOOP_TRACE_TYPED_HIT);
    vm_trace_loop_event(LOOP_TRACE_INC_FAST_HIT);
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
