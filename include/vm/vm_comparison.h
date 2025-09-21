#ifndef ORUS_VM_COMPARISON_H
#define ORUS_VM_COMPARISON_H

#include "../../src/vm/core/vm_internal.h"
#include "vm/register_file.h"

#define VM_TYPED_REGISTER_LIMIT \
    ((uint8_t)(sizeof(((TypedRegisters*)0)->i32_regs) / sizeof(int32_t)))

// Frame-aware register access helpers shared across dispatch implementations

static inline uint16_t vm_typed_register_capacity(void) {
    return (uint16_t)(sizeof(vm.typed_regs.i32_regs) / sizeof(vm.typed_regs.i32_regs[0]));
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
}

static inline void vm_update_typed_register(uint16_t id, Value value) {
    uint16_t capacity = vm_typed_register_capacity();
    if (id >= capacity) {
        return;
    }

    RegisterType new_type = vm_register_type_from_value(value);
    if (new_type == REG_TYPE_NONE) {
        return;
    }

    uint8_t old_type = vm.typed_regs.reg_types[id];
    if (old_type != new_type) {
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
}

static inline Value vm_get_register_safe(uint16_t id) {
    if (id < 256) {
        return vm.registers[id];
    }

    Value* reg_ptr = get_register(&vm.register_file, id);
    return reg_ptr ? *reg_ptr : BOOL_VAL(false);
}

static inline void vm_set_register_safe(uint16_t id, Value value) {
    if (id < 256) {
        vm.registers[id] = value;
        vm_update_typed_register(id, value);
        return;
    }

    set_register(&vm.register_file, id, value);
}

static inline bool vm_typed_reg_in_range(uint16_t id) {
    return id < VM_TYPED_REGISTER_LIMIT;
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

    Value current = vm_peek_register(id);
    if (!IS_I32(current)) {
        vm.typed_regs.reg_types[id] = REG_TYPE_NONE;
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

    Value current = vm_peek_register(id);
    if (!IS_I64(current)) {
        vm.typed_regs.reg_types[id] = REG_TYPE_NONE;
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

    Value current = vm_peek_register(id);
    if (!IS_U32(current)) {
        vm.typed_regs.reg_types[id] = REG_TYPE_NONE;
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

    Value current = vm_peek_register(id);
    if (!IS_U64(current)) {
        vm.typed_regs.reg_types[id] = REG_TYPE_NONE;
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

    Value current = vm_peek_register(id);
    if (!IS_F64(current)) {
        vm.typed_regs.reg_types[id] = REG_TYPE_NONE;
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

    Value current = vm_peek_register(id);
    if (!IS_BOOL(current)) {
        vm.typed_regs.reg_types[id] = REG_TYPE_NONE;
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
    }
}

static inline void vm_cache_i64_typed(uint16_t id, int64_t value) {
    if (vm_typed_reg_in_range(id)) {
        vm.typed_regs.i64_regs[id] = value;
        vm.typed_regs.reg_types[id] = REG_TYPE_I64;
    }
}

static inline void vm_cache_u32_typed(uint16_t id, uint32_t value) {
    if (vm_typed_reg_in_range(id)) {
        vm.typed_regs.u32_regs[id] = value;
        vm.typed_regs.reg_types[id] = REG_TYPE_U32;
    }
}

static inline void vm_cache_u64_typed(uint16_t id, uint64_t value) {
    if (vm_typed_reg_in_range(id)) {
        vm.typed_regs.u64_regs[id] = value;
        vm.typed_regs.reg_types[id] = REG_TYPE_U64;
    }
}

static inline void vm_cache_f64_typed(uint16_t id, double value) {
    if (vm_typed_reg_in_range(id)) {
        vm.typed_regs.f64_regs[id] = value;
        vm.typed_regs.reg_types[id] = REG_TYPE_F64;
    }
}

static inline void vm_cache_bool_typed(uint16_t id, bool value) {
    if (vm_typed_reg_in_range(id)) {
        vm.typed_regs.bool_regs[id] = value;
        vm.typed_regs.reg_types[id] = REG_TYPE_BOOL;
    }
}

static inline void vm_store_i32_register(uint16_t id, int32_t value) {
    vm_cache_i32_typed(id, value);
    vm_set_register_safe(id, I32_VAL(value));
}

static inline void vm_store_i64_register(uint16_t id, int64_t value) {
    vm_cache_i64_typed(id, value);
    vm_set_register_safe(id, I64_VAL(value));
}

static inline void vm_store_u32_register(uint16_t id, uint32_t value) {
    vm_cache_u32_typed(id, value);
    vm_set_register_safe(id, U32_VAL(value));
}

static inline void vm_store_u64_register(uint16_t id, uint64_t value) {
    vm_cache_u64_typed(id, value);
    vm_set_register_safe(id, U64_VAL(value));
}

static inline void vm_store_f64_register(uint16_t id, double value) {
    vm_cache_f64_typed(id, value);
    vm_set_register_safe(id, F64_VAL(value));
}

static inline void vm_store_bool_register(uint16_t id, bool value) {
    vm_cache_bool_typed(id, value);
    vm_set_register_safe(id, BOOL_VAL(value));
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
        Value val_a = vm_get_register_safe(a); \
        Value val_b = vm_get_register_safe(b); \
        if (!IS_I32(val_a) || !IS_I32(val_b)) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i32"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm_set_register_safe((dst), BOOL_VAL(AS_I32(val_a) < AS_I32(val_b))); \
    } while (0)

#define CMP_I32_LE(dst, a, b) \
    do { \
        Value val_a = vm_get_register_safe(a); \
        Value val_b = vm_get_register_safe(b); \
        if (!IS_I32(val_a) || !IS_I32(val_b)) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i32"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm_set_register_safe((dst), BOOL_VAL(AS_I32(val_a) <= AS_I32(val_b))); \
    } while (0)

#define CMP_I32_GT(dst, a, b) \
    do { \
        Value val_a = vm_get_register_safe(a); \
        Value val_b = vm_get_register_safe(b); \
        if (!IS_I32(val_a) || !IS_I32(val_b)) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i32"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm_set_register_safe((dst), BOOL_VAL(AS_I32(val_a) > AS_I32(val_b))); \
    } while (0)

#define CMP_I32_GE(dst, a, b) \
    do { \
        Value val_a = vm_get_register_safe(a); \
        Value val_b = vm_get_register_safe(b); \
        if (!IS_I32(val_a) || !IS_I32(val_b)) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i32"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm_set_register_safe((dst), BOOL_VAL(AS_I32(val_a) >= AS_I32(val_b))); \
    } while (0)

// Signed 64-bit comparisons
#define CMP_I64_LT(dst, a, b) \
    do { \
        Value val_a = vm_get_register_safe(a); \
        Value val_b = vm_get_register_safe(b); \
        if (!IS_I64(val_a) || !IS_I64(val_b)) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i64"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm_set_register_safe((dst), BOOL_VAL(AS_I64(val_a) < AS_I64(val_b))); \
    } while (0)

#define CMP_I64_LE(dst, a, b) \
    do { \
        Value val_a = vm_get_register_safe(a); \
        Value val_b = vm_get_register_safe(b); \
        if (!IS_I64(val_a) || !IS_I64(val_b)) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i64"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm_set_register_safe((dst), BOOL_VAL(AS_I64(val_a) <= AS_I64(val_b))); \
    } while (0)

#define CMP_I64_GT(dst, a, b) \
    do { \
        Value val_a = vm_get_register_safe(a); \
        Value val_b = vm_get_register_safe(b); \
        if (!IS_I64(val_a) || !IS_I64(val_b)) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i64"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm_set_register_safe((dst), BOOL_VAL(AS_I64(val_a) > AS_I64(val_b))); \
    } while (0)

#define CMP_I64_GE(dst, a, b) \
    do { \
        Value val_a = vm_get_register_safe(a); \
        Value val_b = vm_get_register_safe(b); \
        if (!IS_I64(val_a) || !IS_I64(val_b)) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be i64"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm_set_register_safe((dst), BOOL_VAL(AS_I64(val_a) >= AS_I64(val_b))); \
    } while (0)

// Unsigned 32-bit comparisons
#define CMP_U32_LT(dst, a, b) \
    do { \
        Value val_a = vm_get_register_safe(a); \
        Value val_b = vm_get_register_safe(b); \
        if (!IS_U32(val_a) || !IS_U32(val_b)) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u32"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm_set_register_safe((dst), BOOL_VAL(AS_U32(val_a) < AS_U32(val_b))); \
    } while (0)

#define CMP_U32_LE(dst, a, b) \
    do { \
        Value val_a = vm_get_register_safe(a); \
        Value val_b = vm_get_register_safe(b); \
        if (!IS_U32(val_a) || !IS_U32(val_b)) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u32"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm_set_register_safe((dst), BOOL_VAL(AS_U32(val_a) <= AS_U32(val_b))); \
    } while (0)

#define CMP_U32_GT(dst, a, b) \
    do { \
        Value val_a = vm_get_register_safe(a); \
        Value val_b = vm_get_register_safe(b); \
        if (!IS_U32(val_a) || !IS_U32(val_b)) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u32"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm_set_register_safe((dst), BOOL_VAL(AS_U32(val_a) > AS_U32(val_b))); \
    } while (0)

#define CMP_U32_GE(dst, a, b) \
    do { \
        Value val_a = vm_get_register_safe(a); \
        Value val_b = vm_get_register_safe(b); \
        if (!IS_U32(val_a) || !IS_U32(val_b)) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u32"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm_set_register_safe((dst), BOOL_VAL(AS_U32(val_a) >= AS_U32(val_b))); \
    } while (0)

// Unsigned 64-bit comparisons
#define CMP_U64_LT(dst, a, b) \
    do { \
        Value val_a = vm_get_register_safe(a); \
        Value val_b = vm_get_register_safe(b); \
        if (!IS_U64(val_a) || !IS_U64(val_b)) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u64"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm_set_register_safe((dst), BOOL_VAL(AS_U64(val_a) < AS_U64(val_b))); \
    } while (0)

#define CMP_U64_LE(dst, a, b) \
    do { \
        Value val_a = vm_get_register_safe(a); \
        Value val_b = vm_get_register_safe(b); \
        if (!IS_U64(val_a) || !IS_U64(val_b)) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u64"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm_set_register_safe((dst), BOOL_VAL(AS_U64(val_a) <= AS_U64(val_b))); \
    } while (0)

#define CMP_U64_GT(dst, a, b) \
    do { \
        Value val_a = vm_get_register_safe(a); \
        Value val_b = vm_get_register_safe(b); \
        if (!IS_U64(val_a) || !IS_U64(val_b)) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u64"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm_set_register_safe((dst), BOOL_VAL(AS_U64(val_a) > AS_U64(val_b))); \
    } while (0)

#define CMP_U64_GE(dst, a, b) \
    do { \
        Value val_a = vm_get_register_safe(a); \
        Value val_b = vm_get_register_safe(b); \
        if (!IS_U64(val_a) || !IS_U64(val_b)) { \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be u64"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm_set_register_safe((dst), BOOL_VAL(AS_U64(val_a) >= AS_U64(val_b))); \
    } while (0)

// Double comparisons
#define CMP_F64_LT(dst, a, b) \
    do { \
        Value val_a = vm_get_register_safe(a); \
        Value val_b = vm_get_register_safe(b); \
        if (!IS_F64(val_a) || !IS_F64(val_b)) { \
            fprintf(stderr, "[F64_LT_ERROR_TRACE] CMP_F64_LT triggered: dst=%d, a=%d, b=%d\n", (dst), (a), (b)); \
            fprintf(stderr, "[F64_LT_ERROR_TRACE] Register[%d] type: %d, Register[%d] type: %d\n", (a), val_a.type, (b), val_b.type); \
            fflush(stderr); \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be f64"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm_set_register_safe((dst), BOOL_VAL(AS_F64(val_a) < AS_F64(val_b))); \
    } while (0)

#define CMP_F64_LE(dst, a, b) \
    do { \
        Value val_a = vm_get_register_safe(a); \
        Value val_b = vm_get_register_safe(b); \
        if (!IS_F64(val_a) || !IS_F64(val_b)) { \
            fprintf(stderr, "[F64_LE_ERROR_TRACE] CMP_F64_LE triggered: dst=%d, a=%d, b=%d\n", (dst), (a), (b)); \
            fprintf(stderr, "[F64_LE_ERROR_TRACE] Register[%d] type: %d, Register[%d] type: %d\n", (a), val_a.type, (b), val_b.type); \
            fflush(stderr); \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be f64"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm_set_register_safe((dst), BOOL_VAL(AS_F64(val_a) <= AS_F64(val_b))); \
    } while (0)

#define CMP_F64_GT(dst, a, b) \
    do { \
        Value val_a = vm_get_register_safe(a); \
        Value val_b = vm_get_register_safe(b); \
        if (!IS_F64(val_a) || !IS_F64(val_b)) { \
            fprintf(stderr, "[F64_GT_ERROR_TRACE] CMP_F64_GT triggered: dst=%d, a=%d, b=%d\n", (dst), (a), (b)); \
            fprintf(stderr, "[F64_GT_ERROR_TRACE] Register[%d] type: %d, Register[%d] type: %d\n", (a), val_a.type, (b), val_b.type); \
            fflush(stderr); \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be f64"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm_set_register_safe((dst), BOOL_VAL(AS_F64(val_a) > AS_F64(val_b))); \
    } while (0)

#define CMP_F64_GE(dst, a, b) \
    do { \
        Value val_a = vm_get_register_safe(a); \
        Value val_b = vm_get_register_safe(b); \
        if (!IS_F64(val_a) || !IS_F64(val_b)) { \
            fprintf(stderr, "[F64_GE_ERROR_TRACE] CMP_F64_GE triggered: dst=%d, a=%d, b=%d\n", (dst), (a), (b)); \
            fprintf(stderr, "[F64_GE_ERROR_TRACE] Register[%d] type: %d, Register[%d] type: %d\n", (a), val_a.type, (b), val_b.type); \
            fflush(stderr); \
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Operands must be f64"); \
            RETURN(INTERPRET_RUNTIME_ERROR); \
        } \
        vm_set_register_safe((dst), BOOL_VAL(AS_F64(val_a) >= AS_F64(val_b))); \
    } while (0)

#endif // ORUS_VM_COMPARISON_H
