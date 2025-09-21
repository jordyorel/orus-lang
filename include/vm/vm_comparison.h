#ifndef ORUS_VM_COMPARISON_H
#define ORUS_VM_COMPARISON_H

#include "../../src/vm/core/vm_internal.h"
#include "vm/register_file.h"

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
