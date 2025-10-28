// Orus Language Project

#include "vm/vm_opcode_handlers.h"
#include "vm/vm_dispatch.h"
#include "vm/vm_comparison.h"
#include <assert.h>
#include <math.h>

#if !ORUS_VM_ENABLE_TYPED_OPS

static inline int32_t read_i32_operand_typed_fast(uint16_t reg) {
    int32_t value = 0;
    bool ok = vm_try_read_i32_typed(reg, &value);
    if (!ok) {
        assert(ok && "Expected i32 typed register residency");
#if defined(__GNUC__) || defined(__clang__)
        __builtin_unreachable();
#endif
    }
    return value;
}

static inline int64_t read_i64_operand_typed_fast(uint16_t reg) {
    int64_t value = 0;
    bool ok = vm_try_read_i64_typed(reg, &value);
    if (!ok) {
        assert(ok && "Expected i64 typed register residency");
#if defined(__GNUC__) || defined(__clang__)
        __builtin_unreachable();
#endif
    }
    return value;
}

static inline uint32_t read_u32_operand_typed_fast(uint16_t reg) {
    uint32_t value = 0;
    bool ok = vm_try_read_u32_typed(reg, &value);
    if (!ok) {
        assert(ok && "Expected u32 typed register residency");
#if defined(__GNUC__) || defined(__clang__)
        __builtin_unreachable();
#endif
    }
    return value;
}

static inline uint64_t read_u64_operand_typed_fast(uint16_t reg) {
    uint64_t value = 0;
    bool ok = vm_try_read_u64_typed(reg, &value);
    if (!ok) {
        assert(ok && "Expected u64 typed register residency");
#if defined(__GNUC__) || defined(__clang__)
        __builtin_unreachable();
#endif
    }
    return value;
}

static inline double read_f64_operand_typed_fast(uint16_t reg) {
    double value = 0.0;
    bool ok = vm_try_read_f64_typed(reg, &value);
    if (!ok) {
        assert(ok && "Expected f64 typed register residency");
#if defined(__GNUC__) || defined(__clang__)
        __builtin_unreachable();
#endif
    }
    return value;
}

#define DEFINE_TYPED_ARITH_HANDLER(OP_NAME, TYPE_SUFFIX, CTYPE, READ_FN, STORE_FN, ZERO_GUARD, RESULT_EXPR) \
    void handle_##OP_NAME##_##TYPE_SUFFIX##_typed(void) { \
        uint8_t dst = READ_BYTE(); \
        uint8_t left = READ_BYTE(); \
        uint8_t right = READ_BYTE(); \
        CTYPE left_val = READ_FN(left); \
        CTYPE right_val = READ_FN(right); \
        ZERO_GUARD; \
        STORE_FN(dst, (RESULT_EXPR)); \
    }

#define NO_EXTRA_GUARD do { } while (0)
#define GUARD_DIV_ZERO_INT do { \
    if ((right_val) == 0) { \
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero"); \
        return; \
    } \
} while (0)
#define GUARD_DIV_ZERO_F64 do { \
    if ((right_val) == 0.0) { \
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0}, "Division by zero"); \
        return; \
    } \
} while (0)

#define READ_I32_OPERAND_TYPED_FAST(reg) read_i32_operand_typed_fast((reg))
#define READ_I64_OPERAND_TYPED_FAST(reg) read_i64_operand_typed_fast((reg))
#define READ_U32_OPERAND_TYPED_FAST(reg) read_u32_operand_typed_fast((reg))
#define READ_U64_OPERAND_TYPED_FAST(reg) read_u64_operand_typed_fast((reg))
#define READ_F64_OPERAND_TYPED_FAST(reg) read_f64_operand_typed_fast((reg))

#define STORE_I32_RESULT(dst_reg, value) vm_store_i32_typed_hot((dst_reg), (value))
#define STORE_I64_RESULT(dst_reg, value) vm_store_i64_typed_hot((dst_reg), (value))
#define STORE_U32_RESULT(dst_reg, value) vm_store_u32_typed_hot((dst_reg), (value))
#define STORE_U64_RESULT(dst_reg, value) vm_store_u64_typed_hot((dst_reg), (value))
#define STORE_F64_RESULT(dst_reg, value) vm_store_f64_typed_hot((dst_reg), (value))

// ====== I32 Typed Arithmetic Handlers ======

DEFINE_TYPED_ARITH_HANDLER(add, i32, int32_t, READ_I32_OPERAND_TYPED_FAST, STORE_I32_RESULT, NO_EXTRA_GUARD, left_val + right_val)
DEFINE_TYPED_ARITH_HANDLER(sub, i32, int32_t, READ_I32_OPERAND_TYPED_FAST, STORE_I32_RESULT, NO_EXTRA_GUARD, left_val - right_val)
DEFINE_TYPED_ARITH_HANDLER(mul, i32, int32_t, READ_I32_OPERAND_TYPED_FAST, STORE_I32_RESULT, NO_EXTRA_GUARD, left_val * right_val)
DEFINE_TYPED_ARITH_HANDLER(div, i32, int32_t, READ_I32_OPERAND_TYPED_FAST, STORE_I32_RESULT, GUARD_DIV_ZERO_INT, left_val / right_val)
DEFINE_TYPED_ARITH_HANDLER(mod, i32, int32_t, READ_I32_OPERAND_TYPED_FAST, STORE_I32_RESULT, GUARD_DIV_ZERO_INT, left_val % right_val)

// ====== I64 Typed Arithmetic Handlers ======

DEFINE_TYPED_ARITH_HANDLER(add, i64, int64_t, READ_I64_OPERAND_TYPED_FAST, STORE_I64_RESULT, NO_EXTRA_GUARD, left_val + right_val)
DEFINE_TYPED_ARITH_HANDLER(sub, i64, int64_t, READ_I64_OPERAND_TYPED_FAST, STORE_I64_RESULT, NO_EXTRA_GUARD, left_val - right_val)
DEFINE_TYPED_ARITH_HANDLER(mul, i64, int64_t, READ_I64_OPERAND_TYPED_FAST, STORE_I64_RESULT, NO_EXTRA_GUARD, left_val * right_val)
DEFINE_TYPED_ARITH_HANDLER(div, i64, int64_t, READ_I64_OPERAND_TYPED_FAST, STORE_I64_RESULT, GUARD_DIV_ZERO_INT, left_val / right_val)
DEFINE_TYPED_ARITH_HANDLER(mod, i64, int64_t, READ_I64_OPERAND_TYPED_FAST, STORE_I64_RESULT, GUARD_DIV_ZERO_INT, left_val % right_val)

// ====== F64 Typed Arithmetic Handlers ======

DEFINE_TYPED_ARITH_HANDLER(add, f64, double, READ_F64_OPERAND_TYPED_FAST, STORE_F64_RESULT, NO_EXTRA_GUARD, left_val + right_val)
DEFINE_TYPED_ARITH_HANDLER(sub, f64, double, READ_F64_OPERAND_TYPED_FAST, STORE_F64_RESULT, NO_EXTRA_GUARD, left_val - right_val)
DEFINE_TYPED_ARITH_HANDLER(mul, f64, double, READ_F64_OPERAND_TYPED_FAST, STORE_F64_RESULT, NO_EXTRA_GUARD, left_val * right_val)
DEFINE_TYPED_ARITH_HANDLER(div, f64, double, READ_F64_OPERAND_TYPED_FAST, STORE_F64_RESULT, GUARD_DIV_ZERO_F64, left_val / right_val)
DEFINE_TYPED_ARITH_HANDLER(mod, f64, double, READ_F64_OPERAND_TYPED_FAST, STORE_F64_RESULT, GUARD_DIV_ZERO_F64, fmod(left_val, right_val))

// ====== U32 Typed Arithmetic Handlers ======

DEFINE_TYPED_ARITH_HANDLER(add, u32, uint32_t, READ_U32_OPERAND_TYPED_FAST, STORE_U32_RESULT, NO_EXTRA_GUARD, left_val + right_val)
DEFINE_TYPED_ARITH_HANDLER(sub, u32, uint32_t, READ_U32_OPERAND_TYPED_FAST, STORE_U32_RESULT, NO_EXTRA_GUARD, left_val - right_val)
DEFINE_TYPED_ARITH_HANDLER(mul, u32, uint32_t, READ_U32_OPERAND_TYPED_FAST, STORE_U32_RESULT, NO_EXTRA_GUARD, left_val * right_val)
DEFINE_TYPED_ARITH_HANDLER(div, u32, uint32_t, READ_U32_OPERAND_TYPED_FAST, STORE_U32_RESULT, GUARD_DIV_ZERO_INT, left_val / right_val)
DEFINE_TYPED_ARITH_HANDLER(mod, u32, uint32_t, READ_U32_OPERAND_TYPED_FAST, STORE_U32_RESULT, GUARD_DIV_ZERO_INT, left_val % right_val)

// ====== U64 Typed Arithmetic Handlers ======

DEFINE_TYPED_ARITH_HANDLER(add, u64, uint64_t, READ_U64_OPERAND_TYPED_FAST, STORE_U64_RESULT, NO_EXTRA_GUARD, left_val + right_val)
DEFINE_TYPED_ARITH_HANDLER(sub, u64, uint64_t, READ_U64_OPERAND_TYPED_FAST, STORE_U64_RESULT, NO_EXTRA_GUARD, left_val - right_val)
DEFINE_TYPED_ARITH_HANDLER(mul, u64, uint64_t, READ_U64_OPERAND_TYPED_FAST, STORE_U64_RESULT, NO_EXTRA_GUARD, left_val * right_val)
DEFINE_TYPED_ARITH_HANDLER(div, u64, uint64_t, READ_U64_OPERAND_TYPED_FAST, STORE_U64_RESULT, GUARD_DIV_ZERO_INT, left_val / right_val)
DEFINE_TYPED_ARITH_HANDLER(mod, u64, uint64_t, READ_U64_OPERAND_TYPED_FAST, STORE_U64_RESULT, GUARD_DIV_ZERO_INT, left_val % right_val)
#endif // !ORUS_VM_ENABLE_TYPED_OPS
