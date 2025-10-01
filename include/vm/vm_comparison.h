/*
 * Orus Language Project
 * ---------------------------------------------------------------------------
 * File: include/vm/vm_comparison.h
 * Author: Jordy Orel KONDA
 * Copyright (c) 2025 Jordy Orel KONDA
 * License: MIT License (see LICENSE file in the project root)
 * Description: Declares comparison operation helpers for the Orus virtual machine.
 */

#ifndef ORUS_VM_COMPARISON_H
#define ORUS_VM_COMPARISON_H

#include "../../src/vm/core/vm_internal.h"
#include "vm/register_file.h"
#include "vm/vm_loop_fastpaths.h"

#define VM_TYPED_REGISTER_LIMIT \
    ((uint16_t)(sizeof(((TypedRegisters*)0)->i32_regs) / sizeof(int32_t)))

// Frame-aware register access helpers shared across dispatch implementations

static inline uint16_t vm_typed_register_capacity(void) {
    return (uint16_t)(sizeof(vm.typed_regs.i32_regs) / sizeof(vm.typed_regs.i32_regs[0]));
}

static inline bool vm_typed_reg_in_range(uint16_t id) {
    return id < VM_TYPED_REGISTER_LIMIT;
}

static inline RegisterType vm_register_type_from_value(Value value) {
    switch (value.type) {
        case VAL_I32:
            return REG_TYPE_I32;
        case VAL_I64:
            return REG_TYPE_I64;
        case VAL_U32:
            return REG_TYPE_U32;
        case VAL_U64:
            return REG_TYPE_U64;
        case VAL_F64:
            return REG_TYPE_F64;
        case VAL_BOOL:
            return REG_TYPE_BOOL;
        default:
            return REG_TYPE_NONE;
    }
}

static inline void vm_clear_typed_register_slot(uint16_t id, uint8_t reg_type) {
    switch (reg_type) {
        case REG_TYPE_I32:
            vm.typed_regs.i32_regs[id] = 0;
            break;
        case REG_TYPE_I64:
            vm.typed_regs.i64_regs[id] = 0;
            break;
        case REG_TYPE_U32:
            vm.typed_regs.u32_regs[id] = 0;
            break;
        case REG_TYPE_U64:
            vm.typed_regs.u64_regs[id] = 0;
            break;
        case REG_TYPE_F64:
            vm.typed_regs.f64_regs[id] = 0.0;
            break;
        case REG_TYPE_BOOL:
            vm.typed_regs.bool_regs[id] = false;
            break;
        default:
            break;
    }
    vm.typed_regs.dirty[id] = false;
}

static inline void vm_update_typed_register(uint16_t id, Value value) {
    uint16_t capacity = vm_typed_register_capacity();
    if (id >= capacity) {
        return;
    }

    RegisterType new_type = vm_register_type_from_value(value);
    if (new_type == REG_TYPE_NONE) {
        uint8_t old_type = vm.typed_regs.reg_types[id];
        if (old_type != REG_TYPE_NONE && old_type != REG_TYPE_HEAP) {
            vm_branch_cache_bump_generation(id);
            vm_clear_typed_register_slot(id, old_type);
        }
        vm.typed_regs.reg_types[id] = REG_TYPE_HEAP;
        vm.typed_regs.dirty[id] = false;
        return;
    }

    uint8_t old_type = vm.typed_regs.reg_types[id];
    if (old_type != new_type) {
        vm_branch_cache_bump_generation(id);
        vm_clear_typed_register_slot(id, old_type);
    }

    switch (new_type) {
        case REG_TYPE_I32:
            vm.typed_regs.i32_regs[id] = AS_I32(value);
            break;
        case REG_TYPE_I64:
            vm.typed_regs.i64_regs[id] = AS_I64(value);
            break;
        case REG_TYPE_U32:
            vm.typed_regs.u32_regs[id] = AS_U32(value);
            break;
        case REG_TYPE_U64:
            vm.typed_regs.u64_regs[id] = AS_U64(value);
            break;
        case REG_TYPE_F64:
            vm.typed_regs.f64_regs[id] = AS_F64(value);
            break;
        case REG_TYPE_BOOL:
            vm.typed_regs.bool_regs[id] = AS_BOOL(value);
            break;
        default:
            return;
    }

    vm.typed_regs.reg_types[id] = (uint8_t)new_type;
    vm.typed_regs.dirty[id] = false;
}

static inline Value vm_get_register_safe(uint16_t id) {
    if (id < 256) {
        if (vm_typed_reg_in_range(id) && vm.typed_regs.dirty[id]) {
            Value boxed = vm.registers[id];
            switch (vm.typed_regs.reg_types[id]) {
                case REG_TYPE_I32:
                    boxed = I32_VAL(vm.typed_regs.i32_regs[id]);
                    break;
                case REG_TYPE_I64:
                    boxed = I64_VAL(vm.typed_regs.i64_regs[id]);
                    break;
                case REG_TYPE_U32:
                    boxed = U32_VAL(vm.typed_regs.u32_regs[id]);
                    break;
                case REG_TYPE_U64:
                    boxed = U64_VAL(vm.typed_regs.u64_regs[id]);
                    break;
                case REG_TYPE_F64:
                    boxed = F64_VAL(vm.typed_regs.f64_regs[id]);
                    break;
                case REG_TYPE_BOOL:
                    boxed = BOOL_VAL(vm.typed_regs.bool_regs[id]);
                    break;
                default:
                    vm.typed_regs.dirty[id] = false;
                    return vm.registers[id];
            }

            vm.registers[id] = boxed;
            vm.typed_regs.dirty[id] = false;
            return boxed;
        }

        return vm.registers[id];
    }

    Value* reg_ptr = get_register(&vm.register_file, id);
    return reg_ptr ? *reg_ptr : BOOL_VAL(false);
}

static inline void vm_set_register_safe(uint16_t id, Value value) {
    if (id < 256) {
        vm_typed_iterator_invalidate(id);
        vm.registers[id] = value;
        vm_update_typed_register(id, value);
        vm.typed_regs.dirty[id] = false;
        return;
    }

    vm_typed_iterator_invalidate(id);
    set_register(&vm.register_file, id, value);
}

static inline Value vm_peek_register(uint16_t id) {
    if (id < REGISTER_COUNT) {
        return vm.registers[id];
    }
    return vm_get_register_safe(id);
}

static inline bool vm_try_read_i32_typed(uint16_t id, int32_t* out) {
    if (!vm_typed_reg_in_range(id) || vm.typed_regs.reg_types[id] != REG_TYPE_I32) {
        return false;
    }

    if (vm.typed_regs.dirty[id]) {
        *out = vm.typed_regs.i32_regs[id];
        return true;
    }

    Value current = vm_peek_register(id);
    if (!IS_I32(current)) {
        vm.typed_regs.reg_types[id] = REG_TYPE_NONE;
        vm.typed_regs.dirty[id] = false;
        return false;
    }

    int32_t cached = vm.typed_regs.i32_regs[id];
    int32_t value = AS_I32(current);
    if (cached != value) {
        vm.typed_regs.i32_regs[id] = value;
        cached = value;
    }

    *out = cached;
    return true;
}

static inline bool vm_try_read_i64_typed(uint16_t id, int64_t* out) {
    if (!vm_typed_reg_in_range(id) || vm.typed_regs.reg_types[id] != REG_TYPE_I64) {
        return false;
    }

    if (vm.typed_regs.dirty[id]) {
        *out = vm.typed_regs.i64_regs[id];
        return true;
    }

    Value current = vm_peek_register(id);
    if (!IS_I64(current)) {
        vm.typed_regs.reg_types[id] = REG_TYPE_NONE;
        vm.typed_regs.dirty[id] = false;
        return false;
    }

    int64_t cached = vm.typed_regs.i64_regs[id];
    int64_t value = AS_I64(current);
    if (cached != value) {
        vm.typed_regs.i64_regs[id] = value;
        cached = value;
    }

    *out = cached;
    return true;
}

static inline bool vm_try_read_u32_typed(uint16_t id, uint32_t* out) {
    if (!vm_typed_reg_in_range(id) || vm.typed_regs.reg_types[id] != REG_TYPE_U32) {
        return false;
    }

    if (vm.typed_regs.dirty[id]) {
        *out = vm.typed_regs.u32_regs[id];
        return true;
    }

    Value current = vm_peek_register(id);
    if (!IS_U32(current)) {
        vm.typed_regs.reg_types[id] = REG_TYPE_NONE;
        vm.typed_regs.dirty[id] = false;
        return false;
    }

    uint32_t cached = vm.typed_regs.u32_regs[id];
    uint32_t value = AS_U32(current);
    if (cached != value) {
        vm.typed_regs.u32_regs[id] = value;
        cached = value;
    }

    *out = cached;
    return true;
}

static inline bool vm_try_read_u64_typed(uint16_t id, uint64_t* out) {
    if (!vm_typed_reg_in_range(id) || vm.typed_regs.reg_types[id] != REG_TYPE_U64) {
        return false;
    }

    if (vm.typed_regs.dirty[id]) {
        *out = vm.typed_regs.u64_regs[id];
        return true;
    }

    Value current = vm_peek_register(id);
    if (!IS_U64(current)) {
        vm.typed_regs.reg_types[id] = REG_TYPE_NONE;
        vm.typed_regs.dirty[id] = false;
        return false;
    }

    uint64_t cached = vm.typed_regs.u64_regs[id];
    uint64_t value = AS_U64(current);
    if (cached != value) {
        vm.typed_regs.u64_regs[id] = value;
        cached = value;
    }

    *out = cached;
    return true;
}

static inline bool vm_try_read_f64_typed(uint16_t id, double* out) {
    if (!vm_typed_reg_in_range(id) || vm.typed_regs.reg_types[id] != REG_TYPE_F64) {
        return false;
    }

    if (vm.typed_regs.dirty[id]) {
        *out = vm.typed_regs.f64_regs[id];
        return true;
    }

    Value current = vm_peek_register(id);
    if (!IS_F64(current)) {
        vm.typed_regs.reg_types[id] = REG_TYPE_NONE;
        vm.typed_regs.dirty[id] = false;
        return false;
    }

    double cached = vm.typed_regs.f64_regs[id];
    double value = AS_F64(current);
    if (cached != value) {
        vm.typed_regs.f64_regs[id] = value;
        cached = value;
    }

    *out = cached;
    return true;
}

static inline bool vm_try_read_bool_typed(uint16_t id, bool* out) {
    if (!vm_typed_reg_in_range(id) || vm.typed_regs.reg_types[id] != REG_TYPE_BOOL) {
        return false;
    }

    if (vm.typed_regs.dirty[id]) {
        *out = vm.typed_regs.bool_regs[id];
        return true;
    }

    Value current = vm_peek_register(id);
    if (!IS_BOOL(current)) {
        vm.typed_regs.reg_types[id] = REG_TYPE_NONE;
        vm.typed_regs.dirty[id] = false;
        return false;
    }

    bool cached = vm.typed_regs.bool_regs[id];
    bool value = AS_BOOL(current);
    if (cached != value) {
        vm.typed_regs.bool_regs[id] = value;
        cached = value;
    }

    *out = cached;
    return true;
}

static inline void vm_cache_i32_typed(uint16_t id, int32_t value) {
    if (vm_typed_reg_in_range(id)) {
        vm.typed_regs.i32_regs[id] = value;
        vm.typed_regs.reg_types[id] = REG_TYPE_I32;
        vm.typed_regs.dirty[id] = false;
    }
}

static inline void vm_cache_i64_typed(uint16_t id, int64_t value) {
    if (vm_typed_reg_in_range(id)) {
        vm.typed_regs.i64_regs[id] = value;
        vm.typed_regs.reg_types[id] = REG_TYPE_I64;
        vm.typed_regs.dirty[id] = false;
    }
}

static inline void vm_cache_u32_typed(uint16_t id, uint32_t value) {
    if (vm_typed_reg_in_range(id)) {
        vm.typed_regs.u32_regs[id] = value;
        vm.typed_regs.reg_types[id] = REG_TYPE_U32;
        vm.typed_regs.dirty[id] = false;
    }
}

static inline void vm_cache_u64_typed(uint16_t id, uint64_t value) {
    if (vm_typed_reg_in_range(id)) {
        vm.typed_regs.u64_regs[id] = value;
        vm.typed_regs.reg_types[id] = REG_TYPE_U64;
        vm.typed_regs.dirty[id] = false;
    }
}

static inline void vm_cache_f64_typed(uint16_t id, double value) {
    if (vm_typed_reg_in_range(id)) {
        vm.typed_regs.f64_regs[id] = value;
        vm.typed_regs.reg_types[id] = REG_TYPE_F64;
        vm.typed_regs.dirty[id] = false;
    }
}

static inline void vm_cache_bool_typed(uint16_t id, bool value) {
    if (vm_typed_reg_in_range(id)) {
        if (vm.typed_regs.reg_types[id] != REG_TYPE_BOOL) {
            vm_branch_cache_bump_generation(id);
        }
        vm.typed_regs.bool_regs[id] = value;
        vm.typed_regs.reg_types[id] = REG_TYPE_BOOL;
        vm.typed_regs.dirty[id] = false;
    }
}

static inline bool vm_value_is_truthy(Value value) {
    if (IS_BOOL(value)) {
        return AS_BOOL(value);
    }
    if (IS_I32(value)) {
        return AS_I32(value) != 0;
    }
    if (IS_I64(value)) {
        return AS_I64(value) != 0;
    }
    if (IS_U32(value)) {
        return AS_U32(value) != 0;
    }
    if (IS_U64(value)) {
        return AS_U64(value) != 0;
    }
    if (IS_F64(value)) {
        return AS_F64(value) != 0.0;
    }
    return true;
}

static inline bool vm_register_is_truthy(uint16_t id) {
    return vm_value_is_truthy(vm_get_register_safe(id));
}

static inline bool vm_register_has_open_upvalue(uint16_t id) {
    if (id >= REGISTER_COUNT) {
        return false;
    }

    Value* target = &vm.registers[id];
    ObjUpvalue* upvalue = vm.openUpvalues;
    while (upvalue != NULL && upvalue->location > target) {
        upvalue = upvalue->next;
    }

    return upvalue != NULL && upvalue->location == target;
}

static inline bool vm_mark_typed_register_dirty(uint16_t id, RegisterType new_type) {
    if (!vm_typed_reg_in_range(id)) {
        return false;
    }

    vm_typed_iterator_invalidate(id);

    uint8_t previous_type = vm.typed_regs.reg_types[id];
    bool has_upvalue = id < REGISTER_COUNT && vm_register_has_open_upvalue(id);
    if (previous_type == new_type && !has_upvalue) {
        vm.typed_regs.dirty[id] = true;
        return true;
    }

    if (previous_type != REG_TYPE_NONE && previous_type != REG_TYPE_HEAP) {
        vm_branch_cache_bump_generation(id);
    }

    vm.typed_regs.reg_types[id] = (uint8_t)new_type;
    vm.typed_regs.dirty[id] = true;
    return false;
}

static inline void store_i32_register(uint16_t id, int32_t value) {
    if (vm_typed_reg_in_range(id)) {
        vm.typed_regs.i32_regs[id] = value;
        vm.typed_regs.reg_types[id] = REG_TYPE_I32;
        vm.typed_regs.dirty[id] = false;
    }

    vm_typed_iterator_invalidate(id);

    Value boxed = I32_VAL(value);
    if (id < 256) {
        vm.registers[id] = boxed;
    } else {
        set_register(&vm.register_file, id, boxed);
    }
}

static inline void vm_store_i32_typed_hot(uint16_t id, int32_t value) {
    if (!vm_typed_reg_in_range(id)) {
        vm_set_register_safe(id, I32_VAL(value));
        return;
    }

    bool skip_boxed_write = vm_mark_typed_register_dirty(id, REG_TYPE_I32);
    vm.typed_regs.i32_regs[id] = value;
    vm.typed_regs.dirty[id] = skip_boxed_write;

    if (id < REGISTER_COUNT) {
        if (!skip_boxed_write) {
            vm.registers[id] = I32_VAL(value);
        }
    } else {
        set_register(&vm.register_file, id, I32_VAL(value));
    }
}

static inline void vm_store_i64_typed_hot(uint16_t id, int64_t value) {
    if (!vm_typed_reg_in_range(id)) {
        vm_set_register_safe(id, I64_VAL(value));
        return;
    }

    bool skip_boxed_write = vm_mark_typed_register_dirty(id, REG_TYPE_I64);
    vm.typed_regs.i64_regs[id] = value;
    vm.typed_regs.dirty[id] = skip_boxed_write;

    if (id < REGISTER_COUNT) {
        if (!skip_boxed_write) {
            vm.registers[id] = I64_VAL(value);
        }
    } else {
        set_register(&vm.register_file, id, I64_VAL(value));
    }
}

static inline void vm_store_u32_typed_hot(uint16_t id, uint32_t value) {
    if (!vm_typed_reg_in_range(id)) {
        vm_set_register_safe(id, U32_VAL(value));
        return;
    }

    bool skip_boxed_write = vm_mark_typed_register_dirty(id, REG_TYPE_U32);
    vm.typed_regs.u32_regs[id] = value;
    vm.typed_regs.dirty[id] = skip_boxed_write;

    if (id < REGISTER_COUNT) {
        if (!skip_boxed_write) {
            vm.registers[id] = U32_VAL(value);
        }
    } else {
        set_register(&vm.register_file, id, U32_VAL(value));
    }
}

static inline void vm_store_u64_typed_hot(uint16_t id, uint64_t value) {
    if (!vm_typed_reg_in_range(id)) {
        vm_set_register_safe(id, U64_VAL(value));
        return;
    }

    bool skip_boxed_write = vm_mark_typed_register_dirty(id, REG_TYPE_U64);
    vm.typed_regs.u64_regs[id] = value;
    vm.typed_regs.dirty[id] = skip_boxed_write;

    if (id < REGISTER_COUNT) {
        if (!skip_boxed_write) {
            vm.registers[id] = U64_VAL(value);
        }
    } else {
        set_register(&vm.register_file, id, U64_VAL(value));
    }
}

static inline void vm_store_i64_typed_hot(uint16_t id, int64_t value) {
    if (!vm_typed_reg_in_range(id)) {
        vm_set_register_safe(id, I64_VAL(value));
        return;
    }

    vm_typed_iterator_invalidate(id);
    vm.typed_regs.i64_regs[id] = value;
    vm.typed_regs.reg_types[id] = REG_TYPE_I64;
    vm.typed_regs.dirty[id] = true;

    Value boxed = I64_VAL(value);
    if (id < REGISTER_COUNT) {
        vm.registers[id] = boxed;
    } else {
        set_register(&vm.register_file, id, boxed);
    }
}

static inline void vm_store_u32_typed_hot(uint16_t id, uint32_t value) {
    if (!vm_typed_reg_in_range(id)) {
        vm_set_register_safe(id, U32_VAL(value));
        return;
    }

    vm_typed_iterator_invalidate(id);
    vm.typed_regs.u32_regs[id] = value;
    vm.typed_regs.reg_types[id] = REG_TYPE_U32;
    vm.typed_regs.dirty[id] = true;

    Value boxed = U32_VAL(value);
    if (id < REGISTER_COUNT) {
        vm.registers[id] = boxed;
    } else {
        set_register(&vm.register_file, id, boxed);
    }
}

static inline void vm_store_u64_typed_hot(uint16_t id, uint64_t value) {
    if (!vm_typed_reg_in_range(id)) {
        vm_set_register_safe(id, U64_VAL(value));
        return;
    }

    vm_typed_iterator_invalidate(id);
    vm.typed_regs.u64_regs[id] = value;
    vm.typed_regs.reg_types[id] = REG_TYPE_U64;
    vm.typed_regs.dirty[id] = true;

    Value boxed = U64_VAL(value);
    if (id < REGISTER_COUNT) {
        vm.registers[id] = boxed;
    } else {
        set_register(&vm.register_file, id, boxed);
    }
}

static inline void store_i64_register(uint16_t id, int64_t value) {
    if (vm_typed_reg_in_range(id)) {
        vm.typed_regs.i64_regs[id] = value;
        vm.typed_regs.reg_types[id] = REG_TYPE_I64;
        vm.typed_regs.dirty[id] = false;
    }

    vm_typed_iterator_invalidate(id);

    Value boxed = I64_VAL(value);
    if (id < 256) {
        vm.registers[id] = boxed;
    } else {
        set_register(&vm.register_file, id, boxed);
    }
}

static inline void store_u32_register(uint16_t id, uint32_t value) {
    if (vm_typed_reg_in_range(id)) {
        vm.typed_regs.u32_regs[id] = value;
        vm.typed_regs.reg_types[id] = REG_TYPE_U32;
        vm.typed_regs.dirty[id] = false;
    }

    vm_typed_iterator_invalidate(id);

    Value boxed = U32_VAL(value);
    if (id < 256) {
        vm.registers[id] = boxed;
    } else {
        set_register(&vm.register_file, id, boxed);
    }
}

static inline void store_u64_register(uint16_t id, uint64_t value) {
    if (vm_typed_reg_in_range(id)) {
        vm.typed_regs.u64_regs[id] = value;
        vm.typed_regs.reg_types[id] = REG_TYPE_U64;
        vm.typed_regs.dirty[id] = false;
    }

    vm_typed_iterator_invalidate(id);

    Value boxed = U64_VAL(value);
    if (id < 256) {
        vm.registers[id] = boxed;
    } else {
        set_register(&vm.register_file, id, boxed);
    }
}

static inline void store_f64_register(uint16_t id, double value) {
    if (vm_typed_reg_in_range(id)) {
        vm.typed_regs.f64_regs[id] = value;
        vm.typed_regs.reg_types[id] = REG_TYPE_F64;
        vm.typed_regs.dirty[id] = false;
    }

    vm_typed_iterator_invalidate(id);

    Value boxed = F64_VAL(value);
    if (id < 256) {
        vm.registers[id] = boxed;
    } else {
        set_register(&vm.register_file, id, boxed);
    }
}

static inline void store_bool_register(uint16_t id, bool value) {
    if (vm_typed_reg_in_range(id)) {
        vm.typed_regs.bool_regs[id] = value;
        vm.typed_regs.reg_types[id] = REG_TYPE_BOOL;
        vm.typed_regs.dirty[id] = false;
    }

    vm_typed_iterator_invalidate(id);

    Value boxed = BOOL_VAL(value);
    if (id < 256) {
        vm.registers[id] = boxed;
    } else {
        set_register(&vm.register_file, id, boxed);
    }
}

static inline void vm_store_i32_register(uint16_t id, int32_t value) {
    store_i32_register(id, value);
}

static inline void vm_store_i64_register(uint16_t id, int64_t value) {
    store_i64_register(id, value);
}

static inline void vm_store_u32_register(uint16_t id, uint32_t value) {
    store_u32_register(id, value);
}

static inline void vm_store_u64_register(uint16_t id, uint64_t value) {
    store_u64_register(id, value);
}

static inline void vm_store_f64_register(uint16_t id, double value) {
    store_f64_register(id, value);
}

static inline void vm_store_bool_register(uint16_t id, bool value) {
    store_bool_register(id, value);
}

// Equality comparisons
#define CMP_EQ(dst, a, b) \
    do { \
        vm_set_register_safe((dst), BOOL_VAL(valuesEqual(vm_get_register_safe(a), vm_get_register_safe(b)))); \
    } while (0)

#define CMP_NE(dst, a, b) \
    do { \
        vm_set_register_safe((dst), BOOL_VAL(!valuesEqual(vm_get_register_safe(a), vm_get_register_safe(b)))); \
    } while (0)

// Signed 32-bit comparisons
#define CMP_I32_LT(dst, a, b) \
    do { \
        int32_t val_a_i32; \
        int32_t val_b_i32; \
        bool left_typed = vm_try_read_i32_typed(a, &val_a_i32); \
        bool right_typed = vm_try_read_i32_typed(b, &val_b_i32); \
        if (left_typed && right_typed) { \
            vm_trace_loop_event(LOOP_TRACE_TYPED_HIT); \
            vm_store_bool_register((dst), val_a_i32 < val_b_i32); \
        } else { \
            Value val_a = vm_get_register_safe(a); \
            Value val_b = vm_get_register_safe(b); \
            if (!IS_I32(val_a) || !IS_I32(val_b)) { \
                vm_trace_loop_event(LOOP_TRACE_TYPE_MISMATCH); \
                runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i32"); \
                RETURN(INTERPRET_RUNTIME_ERROR); \
            } \
            vm_trace_loop_event(LOOP_TRACE_TYPED_MISS); \
            val_a_i32 = AS_I32(val_a); \
            val_b_i32 = AS_I32(val_b); \
            vm_cache_i32_typed(a, val_a_i32); \
            vm_cache_i32_typed(b, val_b_i32); \
            vm_store_bool_register((dst), val_a_i32 < val_b_i32); \
        } \
    } while (0)

#define CMP_I32_LE(dst, a, b) \
    do { \
        int32_t val_a_i32; \
        int32_t val_b_i32; \
        bool left_typed = vm_try_read_i32_typed(a, &val_a_i32); \
        bool right_typed = vm_try_read_i32_typed(b, &val_b_i32); \
        if (left_typed && right_typed) { \
            vm_trace_loop_event(LOOP_TRACE_TYPED_HIT); \
            vm_store_bool_register((dst), val_a_i32 <= val_b_i32); \
        } else { \
            Value val_a = vm_get_register_safe(a); \
            Value val_b = vm_get_register_safe(b); \
            if (!IS_I32(val_a) || !IS_I32(val_b)) { \
                vm_trace_loop_event(LOOP_TRACE_TYPE_MISMATCH); \
                runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i32"); \
                RETURN(INTERPRET_RUNTIME_ERROR); \
            } \
            vm_trace_loop_event(LOOP_TRACE_TYPED_MISS); \
            val_a_i32 = AS_I32(val_a); \
            val_b_i32 = AS_I32(val_b); \
            vm_cache_i32_typed(a, val_a_i32); \
            vm_cache_i32_typed(b, val_b_i32); \
            vm_store_bool_register((dst), val_a_i32 <= val_b_i32); \
        } \
    } while (0)

#define CMP_I32_GT(dst, a, b) \
    do { \
        int32_t val_a_i32; \
        int32_t val_b_i32; \
        bool left_typed = vm_try_read_i32_typed(a, &val_a_i32); \
        bool right_typed = vm_try_read_i32_typed(b, &val_b_i32); \
        if (left_typed && right_typed) { \
            vm_trace_loop_event(LOOP_TRACE_TYPED_HIT); \
            vm_store_bool_register((dst), val_a_i32 > val_b_i32); \
        } else { \
            Value val_a = vm_get_register_safe(a); \
            Value val_b = vm_get_register_safe(b); \
            if (!IS_I32(val_a) || !IS_I32(val_b)) { \
                vm_trace_loop_event(LOOP_TRACE_TYPE_MISMATCH); \
                runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i32"); \
                RETURN(INTERPRET_RUNTIME_ERROR); \
            } \
            vm_trace_loop_event(LOOP_TRACE_TYPED_MISS); \
            val_a_i32 = AS_I32(val_a); \
            val_b_i32 = AS_I32(val_b); \
            vm_cache_i32_typed(a, val_a_i32); \
            vm_cache_i32_typed(b, val_b_i32); \
            vm_store_bool_register((dst), val_a_i32 > val_b_i32); \
        } \
    } while (0)

#define CMP_I32_GE(dst, a, b) \
    do { \
        int32_t val_a_i32; \
        int32_t val_b_i32; \
        bool left_typed = vm_try_read_i32_typed(a, &val_a_i32); \
        bool right_typed = vm_try_read_i32_typed(b, &val_b_i32); \
        if (left_typed && right_typed) { \
            vm_trace_loop_event(LOOP_TRACE_TYPED_HIT); \
            vm_store_bool_register((dst), val_a_i32 >= val_b_i32); \
        } else { \
            Value val_a = vm_get_register_safe(a); \
            Value val_b = vm_get_register_safe(b); \
            if (!IS_I32(val_a) || !IS_I32(val_b)) { \
                vm_trace_loop_event(LOOP_TRACE_TYPE_MISMATCH); \
                runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i32"); \
                RETURN(INTERPRET_RUNTIME_ERROR); \
            } \
            vm_trace_loop_event(LOOP_TRACE_TYPED_MISS); \
            val_a_i32 = AS_I32(val_a); \
            val_b_i32 = AS_I32(val_b); \
            vm_cache_i32_typed(a, val_a_i32); \
            vm_cache_i32_typed(b, val_b_i32); \
            vm_store_bool_register((dst), val_a_i32 >= val_b_i32); \
        } \
    } while (0)

// Signed 64-bit comparisons
#define CMP_I64_LT(dst, a, b) \
    do { \
        int64_t val_a_i64; \
        int64_t val_b_i64; \
        if (vm_try_read_i64_typed(a, &val_a_i64) && vm_try_read_i64_typed(b, &val_b_i64)) { \
            vm_store_bool_register((dst), val_a_i64 < val_b_i64); \
        } else { \
            Value val_a = vm_get_register_safe(a); \
            Value val_b = vm_get_register_safe(b); \
            if (!IS_I64(val_a) || !IS_I64(val_b)) { \
                runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i64"); \
                RETURN(INTERPRET_RUNTIME_ERROR); \
            } \
            val_a_i64 = AS_I64(val_a); \
            val_b_i64 = AS_I64(val_b); \
            vm_cache_i64_typed(a, val_a_i64); \
            vm_cache_i64_typed(b, val_b_i64); \
            vm_store_bool_register((dst), val_a_i64 < val_b_i64); \
        } \
    } while (0)

#define CMP_I64_LE(dst, a, b) \
    do { \
        int64_t val_a_i64; \
        int64_t val_b_i64; \
        if (vm_try_read_i64_typed(a, &val_a_i64) && vm_try_read_i64_typed(b, &val_b_i64)) { \
            vm_store_bool_register((dst), val_a_i64 <= val_b_i64); \
        } else { \
            Value val_a = vm_get_register_safe(a); \
            Value val_b = vm_get_register_safe(b); \
            if (!IS_I64(val_a) || !IS_I64(val_b)) { \
                runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i64"); \
                RETURN(INTERPRET_RUNTIME_ERROR); \
            } \
            val_a_i64 = AS_I64(val_a); \
            val_b_i64 = AS_I64(val_b); \
            vm_cache_i64_typed(a, val_a_i64); \
            vm_cache_i64_typed(b, val_b_i64); \
            vm_store_bool_register((dst), val_a_i64 <= val_b_i64); \
        } \
    } while (0)

#define CMP_I64_GT(dst, a, b) \
    do { \
        int64_t val_a_i64; \
        int64_t val_b_i64; \
        if (vm_try_read_i64_typed(a, &val_a_i64) && vm_try_read_i64_typed(b, &val_b_i64)) { \
            vm_store_bool_register((dst), val_a_i64 > val_b_i64); \
        } else { \
            Value val_a = vm_get_register_safe(a); \
            Value val_b = vm_get_register_safe(b); \
            if (!IS_I64(val_a) || !IS_I64(val_b)) { \
                runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i64"); \
                RETURN(INTERPRET_RUNTIME_ERROR); \
            } \
            val_a_i64 = AS_I64(val_a); \
            val_b_i64 = AS_I64(val_b); \
            vm_cache_i64_typed(a, val_a_i64); \
            vm_cache_i64_typed(b, val_b_i64); \
            vm_store_bool_register((dst), val_a_i64 > val_b_i64); \
        } \
    } while (0)

#define CMP_I64_GE(dst, a, b) \
    do { \
        int64_t val_a_i64; \
        int64_t val_b_i64; \
        if (vm_try_read_i64_typed(a, &val_a_i64) && vm_try_read_i64_typed(b, &val_b_i64)) { \
            vm_store_bool_register((dst), val_a_i64 >= val_b_i64); \
        } else { \
            Value val_a = vm_get_register_safe(a); \
            Value val_b = vm_get_register_safe(b); \
            if (!IS_I64(val_a) || !IS_I64(val_b)) { \
                runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i64"); \
                RETURN(INTERPRET_RUNTIME_ERROR); \
            } \
            val_a_i64 = AS_I64(val_a); \
            val_b_i64 = AS_I64(val_b); \
            vm_cache_i64_typed(a, val_a_i64); \
            vm_cache_i64_typed(b, val_b_i64); \
            vm_store_bool_register((dst), val_a_i64 >= val_b_i64); \
        } \
    } while (0)

// Unsigned 32-bit comparisons
#define CMP_U32_LT(dst, a, b) \
    do { \
        uint32_t val_a_u32; \
        uint32_t val_b_u32; \
        if (vm_try_read_u32_typed(a, &val_a_u32) && vm_try_read_u32_typed(b, &val_b_u32)) { \
            vm_store_bool_register((dst), val_a_u32 < val_b_u32); \
        } else { \
            Value val_a = vm_get_register_safe(a); \
            Value val_b = vm_get_register_safe(b); \
            if (!IS_U32(val_a) || !IS_U32(val_b)) { \
                runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u32"); \
                RETURN(INTERPRET_RUNTIME_ERROR); \
            } \
            val_a_u32 = AS_U32(val_a); \
            val_b_u32 = AS_U32(val_b); \
            vm_cache_u32_typed(a, val_a_u32); \
            vm_cache_u32_typed(b, val_b_u32); \
            vm_store_bool_register((dst), val_a_u32 < val_b_u32); \
        } \
    } while (0)

#define CMP_U32_LE(dst, a, b) \
    do { \
        uint32_t val_a_u32; \
        uint32_t val_b_u32; \
        if (vm_try_read_u32_typed(a, &val_a_u32) && vm_try_read_u32_typed(b, &val_b_u32)) { \
            vm_store_bool_register((dst), val_a_u32 <= val_b_u32); \
        } else { \
            Value val_a = vm_get_register_safe(a); \
            Value val_b = vm_get_register_safe(b); \
            if (!IS_U32(val_a) || !IS_U32(val_b)) { \
                runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u32"); \
                RETURN(INTERPRET_RUNTIME_ERROR); \
            } \
            val_a_u32 = AS_U32(val_a); \
            val_b_u32 = AS_U32(val_b); \
            vm_cache_u32_typed(a, val_a_u32); \
            vm_cache_u32_typed(b, val_b_u32); \
            vm_store_bool_register((dst), val_a_u32 <= val_b_u32); \
        } \
    } while (0)

#define CMP_U32_GT(dst, a, b) \
    do { \
        uint32_t val_a_u32; \
        uint32_t val_b_u32; \
        if (vm_try_read_u32_typed(a, &val_a_u32) && vm_try_read_u32_typed(b, &val_b_u32)) { \
            vm_store_bool_register((dst), val_a_u32 > val_b_u32); \
        } else { \
            Value val_a = vm_get_register_safe(a); \
            Value val_b = vm_get_register_safe(b); \
            if (!IS_U32(val_a) || !IS_U32(val_b)) { \
                runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u32"); \
                RETURN(INTERPRET_RUNTIME_ERROR); \
            } \
            val_a_u32 = AS_U32(val_a); \
            val_b_u32 = AS_U32(val_b); \
            vm_cache_u32_typed(a, val_a_u32); \
            vm_cache_u32_typed(b, val_b_u32); \
            vm_store_bool_register((dst), val_a_u32 > val_b_u32); \
        } \
    } while (0)

#define CMP_U32_GE(dst, a, b) \
    do { \
        uint32_t val_a_u32; \
        uint32_t val_b_u32; \
        if (vm_try_read_u32_typed(a, &val_a_u32) && vm_try_read_u32_typed(b, &val_b_u32)) { \
            vm_store_bool_register((dst), val_a_u32 >= val_b_u32); \
        } else { \
            Value val_a = vm_get_register_safe(a); \
            Value val_b = vm_get_register_safe(b); \
            if (!IS_U32(val_a) || !IS_U32(val_b)) { \
                runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u32"); \
                RETURN(INTERPRET_RUNTIME_ERROR); \
            } \
            val_a_u32 = AS_U32(val_a); \
            val_b_u32 = AS_U32(val_b); \
            vm_cache_u32_typed(a, val_a_u32); \
            vm_cache_u32_typed(b, val_b_u32); \
            vm_store_bool_register((dst), val_a_u32 >= val_b_u32); \
        } \
    } while (0)

// Unsigned 64-bit comparisons
#define CMP_U64_LT(dst, a, b) \
    do { \
        uint64_t val_a_u64; \
        uint64_t val_b_u64; \
        if (vm_try_read_u64_typed(a, &val_a_u64) && vm_try_read_u64_typed(b, &val_b_u64)) { \
            vm_store_bool_register((dst), val_a_u64 < val_b_u64); \
        } else { \
            Value val_a = vm_get_register_safe(a); \
            Value val_b = vm_get_register_safe(b); \
            if (!IS_U64(val_a) || !IS_U64(val_b)) { \
                runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u64"); \
                RETURN(INTERPRET_RUNTIME_ERROR); \
            } \
            val_a_u64 = AS_U64(val_a); \
            val_b_u64 = AS_U64(val_b); \
            vm_cache_u64_typed(a, val_a_u64); \
            vm_cache_u64_typed(b, val_b_u64); \
            vm_store_bool_register((dst), val_a_u64 < val_b_u64); \
        } \
    } while (0)

#define CMP_U64_LE(dst, a, b) \
    do { \
        uint64_t val_a_u64; \
        uint64_t val_b_u64; \
        if (vm_try_read_u64_typed(a, &val_a_u64) && vm_try_read_u64_typed(b, &val_b_u64)) { \
            vm_store_bool_register((dst), val_a_u64 <= val_b_u64); \
        } else { \
            Value val_a = vm_get_register_safe(a); \
            Value val_b = vm_get_register_safe(b); \
            if (!IS_U64(val_a) || !IS_U64(val_b)) { \
                runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u64"); \
                RETURN(INTERPRET_RUNTIME_ERROR); \
            } \
            val_a_u64 = AS_U64(val_a); \
            val_b_u64 = AS_U64(val_b); \
            vm_cache_u64_typed(a, val_a_u64); \
            vm_cache_u64_typed(b, val_b_u64); \
            vm_store_bool_register((dst), val_a_u64 <= val_b_u64); \
        } \
    } while (0)

#define CMP_U64_GT(dst, a, b) \
    do { \
        uint64_t val_a_u64; \
        uint64_t val_b_u64; \
        if (vm_try_read_u64_typed(a, &val_a_u64) && vm_try_read_u64_typed(b, &val_b_u64)) { \
            vm_store_bool_register((dst), val_a_u64 > val_b_u64); \
        } else { \
            Value val_a = vm_get_register_safe(a); \
            Value val_b = vm_get_register_safe(b); \
            if (!IS_U64(val_a) || !IS_U64(val_b)) { \
                runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u64"); \
                RETURN(INTERPRET_RUNTIME_ERROR); \
            } \
            val_a_u64 = AS_U64(val_a); \
            val_b_u64 = AS_U64(val_b); \
            vm_cache_u64_typed(a, val_a_u64); \
            vm_cache_u64_typed(b, val_b_u64); \
            vm_store_bool_register((dst), val_a_u64 > val_b_u64); \
        } \
    } while (0)

#define CMP_U64_GE(dst, a, b) \
    do { \
        uint64_t val_a_u64; \
        uint64_t val_b_u64; \
        if (vm_try_read_u64_typed(a, &val_a_u64) && vm_try_read_u64_typed(b, &val_b_u64)) { \
            vm_store_bool_register((dst), val_a_u64 >= val_b_u64); \
        } else { \
            Value val_a = vm_get_register_safe(a); \
            Value val_b = vm_get_register_safe(b); \
            if (!IS_U64(val_a) || !IS_U64(val_b)) { \
                runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u64"); \
                RETURN(INTERPRET_RUNTIME_ERROR); \
            } \
            val_a_u64 = AS_U64(val_a); \
            val_b_u64 = AS_U64(val_b); \
            vm_cache_u64_typed(a, val_a_u64); \
            vm_cache_u64_typed(b, val_b_u64); \
            vm_store_bool_register((dst), val_a_u64 >= val_b_u64); \
        } \
    } while (0)

// Double comparisons
#define CMP_F64_LT(dst, a, b) \
    do { \
        double val_a_f64; \
        double val_b_f64; \
        if (vm_try_read_f64_typed(a, &val_a_f64) && vm_try_read_f64_typed(b, &val_b_f64)) { \
            vm_store_bool_register((dst), val_a_f64 < val_b_f64); \
        } else { \
            Value val_a = vm_get_register_safe(a); \
            Value val_b = vm_get_register_safe(b); \
            if (!IS_F64(val_a) || !IS_F64(val_b)) { \
                fprintf(stderr, "[F64_LT_ERROR_TRACE] CMP_F64_LT triggered: dst=%d, a=%d, b=%d\n", (dst), (a), (b)); \
                fprintf(stderr, "[F64_LT_ERROR_TRACE] Register[%d] type: %d, Register[%d] type: %d\n", (a), val_a.type, (b), val_b.type); \
                fflush(stderr); \
                runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be f64"); \
                RETURN(INTERPRET_RUNTIME_ERROR); \
            } \
            val_a_f64 = AS_F64(val_a); \
            val_b_f64 = AS_F64(val_b); \
            vm_cache_f64_typed(a, val_a_f64); \
            vm_cache_f64_typed(b, val_b_f64); \
            vm_store_bool_register((dst), val_a_f64 < val_b_f64); \
        } \
    } while (0)

#define CMP_F64_LE(dst, a, b) \
    do { \
        double val_a_f64; \
        double val_b_f64; \
        if (vm_try_read_f64_typed(a, &val_a_f64) && vm_try_read_f64_typed(b, &val_b_f64)) { \
            vm_store_bool_register((dst), val_a_f64 <= val_b_f64); \
        } else { \
            Value val_a = vm_get_register_safe(a); \
            Value val_b = vm_get_register_safe(b); \
            if (!IS_F64(val_a) || !IS_F64(val_b)) { \
                fprintf(stderr, "[F64_LE_ERROR_TRACE] CMP_F64_LE triggered: dst=%d, a=%d, b=%d\n", (dst), (a), (b)); \
                fprintf(stderr, "[F64_LE_ERROR_TRACE] Register[%d] type: %d, Register[%d] type: %d\n", (a), val_a.type, (b), val_b.type); \
                fflush(stderr); \
                runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be f64"); \
                RETURN(INTERPRET_RUNTIME_ERROR); \
            } \
            val_a_f64 = AS_F64(val_a); \
            val_b_f64 = AS_F64(val_b); \
            vm_cache_f64_typed(a, val_a_f64); \
            vm_cache_f64_typed(b, val_b_f64); \
            vm_store_bool_register((dst), val_a_f64 <= val_b_f64); \
        } \
    } while (0)

#define CMP_F64_GT(dst, a, b) \
    do { \
        double val_a_f64; \
        double val_b_f64; \
        if (vm_try_read_f64_typed(a, &val_a_f64) && vm_try_read_f64_typed(b, &val_b_f64)) { \
            vm_store_bool_register((dst), val_a_f64 > val_b_f64); \
        } else { \
            Value val_a = vm_get_register_safe(a); \
            Value val_b = vm_get_register_safe(b); \
            if (!IS_F64(val_a) || !IS_F64(val_b)) { \
                fprintf(stderr, "[F64_GT_ERROR_TRACE] CMP_F64_GT triggered: dst=%d, a=%d, b=%d\n", (dst), (a), (b)); \
                fprintf(stderr, "[F64_GT_ERROR_TRACE] Register[%d] type: %d, Register[%d] type: %d\n", (a), val_a.type, (b), val_b.type); \
                fflush(stderr); \
                runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be f64"); \
                RETURN(INTERPRET_RUNTIME_ERROR); \
            } \
            val_a_f64 = AS_F64(val_a); \
            val_b_f64 = AS_F64(val_b); \
            vm_cache_f64_typed(a, val_a_f64); \
            vm_cache_f64_typed(b, val_b_f64); \
            vm_store_bool_register((dst), val_a_f64 > val_b_f64); \
        } \
    } while (0)

#define CMP_F64_GE(dst, a, b) \
    do { \
        double val_a_f64; \
        double val_b_f64; \
        if (vm_try_read_f64_typed(a, &val_a_f64) && vm_try_read_f64_typed(b, &val_b_f64)) { \
            vm_store_bool_register((dst), val_a_f64 >= val_b_f64); \
        } else { \
            Value val_a = vm_get_register_safe(a); \
            Value val_b = vm_get_register_safe(b); \
            if (!IS_F64(val_a) || !IS_F64(val_b)) { \
                fprintf(stderr, "[F64_GE_ERROR_TRACE] CMP_F64_GE triggered: dst=%d, a=%d, b=%d\n", (dst), (a), (b)); \
                fprintf(stderr, "[F64_GE_ERROR_TRACE] Register[%d] type: %d, Register[%d] type: %d\n", (a), val_a.type, (b), val_b.type); \
                fflush(stderr); \
                runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be f64"); \
                RETURN(INTERPRET_RUNTIME_ERROR); \
            } \
            val_a_f64 = AS_F64(val_a); \
            val_b_f64 = AS_F64(val_b); \
            vm_cache_f64_typed(a, val_a_f64); \
            vm_cache_f64_typed(b, val_b_f64); \
            vm_store_bool_register((dst), val_a_f64 >= val_b_f64); \
        } \
    } while (0)

#endif // ORUS_VM_COMPARISON_H
