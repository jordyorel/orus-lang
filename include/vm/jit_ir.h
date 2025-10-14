// Orus Language Project
// ---------------------------------------------------------------------------
// File: include/vm/jit_ir.h
// Description: Minimal OrusJit intermediate representation describing
//              architecture-neutral operations for DynASM-backed codegen.

#ifndef ORUS_VM_JIT_IR_H
#define ORUS_VM_JIT_IR_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Chunk;

typedef enum OrusJitValueKind {
    ORUS_JIT_VALUE_I32 = 0,
    ORUS_JIT_VALUE_I64,
    ORUS_JIT_VALUE_U32,
    ORUS_JIT_VALUE_U64,
    ORUS_JIT_VALUE_F64,
    ORUS_JIT_VALUE_BOOL,
    ORUS_JIT_VALUE_STRING,
    ORUS_JIT_VALUE_BOXED,
    ORUS_JIT_VALUE_KIND_COUNT,
} OrusJitValueKind;

typedef enum OrusJitIROpcode {
    ORUS_JIT_IR_OP_RETURN = 0,

    ORUS_JIT_IR_OP_LOAD_I32_CONST,
    ORUS_JIT_IR_OP_LOAD_I64_CONST,
    ORUS_JIT_IR_OP_LOAD_U32_CONST,
    ORUS_JIT_IR_OP_LOAD_U64_CONST,
    ORUS_JIT_IR_OP_LOAD_F64_CONST,
    ORUS_JIT_IR_OP_LOAD_STRING_CONST,
    ORUS_JIT_IR_OP_LOAD_VALUE_CONST,

    ORUS_JIT_IR_OP_MOVE_I32,
    ORUS_JIT_IR_OP_MOVE_I64,
    ORUS_JIT_IR_OP_MOVE_U32,
    ORUS_JIT_IR_OP_MOVE_U64,
    ORUS_JIT_IR_OP_MOVE_F64,
    ORUS_JIT_IR_OP_MOVE_BOOL,
    ORUS_JIT_IR_OP_MOVE_STRING,
    ORUS_JIT_IR_OP_MOVE_VALUE,

    ORUS_JIT_IR_OP_ADD_I32,
    ORUS_JIT_IR_OP_ADD_I64,
    ORUS_JIT_IR_OP_ADD_U32,
    ORUS_JIT_IR_OP_ADD_U64,
    ORUS_JIT_IR_OP_ADD_F64,

    ORUS_JIT_IR_OP_SUB_I32,
    ORUS_JIT_IR_OP_SUB_I64,
    ORUS_JIT_IR_OP_SUB_U32,
    ORUS_JIT_IR_OP_SUB_U64,
    ORUS_JIT_IR_OP_SUB_F64,

    ORUS_JIT_IR_OP_MUL_I32,
    ORUS_JIT_IR_OP_MUL_I64,
    ORUS_JIT_IR_OP_MUL_U32,
    ORUS_JIT_IR_OP_MUL_U64,
    ORUS_JIT_IR_OP_MUL_F64,

    ORUS_JIT_IR_OP_DIV_I32,
    ORUS_JIT_IR_OP_DIV_I64,
    ORUS_JIT_IR_OP_DIV_U32,
    ORUS_JIT_IR_OP_DIV_U64,
    ORUS_JIT_IR_OP_DIV_F64,

    ORUS_JIT_IR_OP_MOD_I32,
    ORUS_JIT_IR_OP_MOD_I64,
    ORUS_JIT_IR_OP_MOD_U32,
    ORUS_JIT_IR_OP_MOD_U64,
    ORUS_JIT_IR_OP_MOD_F64,

    ORUS_JIT_IR_OP_CONCAT_STRING,
    ORUS_JIT_IR_OP_TO_STRING,

    ORUS_JIT_IR_OP_TIME_STAMP,
    ORUS_JIT_IR_OP_MAKE_ARRAY,
    ORUS_JIT_IR_OP_ARRAY_PUSH,
    ORUS_JIT_IR_OP_ENUM_NEW,
    ORUS_JIT_IR_OP_PRINT,
    ORUS_JIT_IR_OP_ASSERT_EQ,
    ORUS_JIT_IR_OP_CALL_NATIVE,

    ORUS_JIT_IR_OP_GET_ITER,
    ORUS_JIT_IR_OP_ITER_NEXT,
    ORUS_JIT_IR_OP_RANGE,

    ORUS_JIT_IR_OP_LT_I32,
    ORUS_JIT_IR_OP_LE_I32,
    ORUS_JIT_IR_OP_GT_I32,
    ORUS_JIT_IR_OP_GE_I32,

    ORUS_JIT_IR_OP_LT_I64,
    ORUS_JIT_IR_OP_LE_I64,
    ORUS_JIT_IR_OP_GT_I64,
    ORUS_JIT_IR_OP_GE_I64,

    ORUS_JIT_IR_OP_LT_U32,
    ORUS_JIT_IR_OP_LE_U32,
    ORUS_JIT_IR_OP_GT_U32,
    ORUS_JIT_IR_OP_GE_U32,

    ORUS_JIT_IR_OP_LT_U64,
    ORUS_JIT_IR_OP_LE_U64,
    ORUS_JIT_IR_OP_GT_U64,
    ORUS_JIT_IR_OP_GE_U64,

    ORUS_JIT_IR_OP_LT_F64,
    ORUS_JIT_IR_OP_LE_F64,
    ORUS_JIT_IR_OP_GT_F64,
    ORUS_JIT_IR_OP_GE_F64,

    ORUS_JIT_IR_OP_EQ_I32,
    ORUS_JIT_IR_OP_NE_I32,
    ORUS_JIT_IR_OP_EQ_I64,
    ORUS_JIT_IR_OP_NE_I64,
    ORUS_JIT_IR_OP_EQ_U32,
    ORUS_JIT_IR_OP_NE_U32,
    ORUS_JIT_IR_OP_EQ_U64,
    ORUS_JIT_IR_OP_NE_U64,
    ORUS_JIT_IR_OP_EQ_F64,
    ORUS_JIT_IR_OP_NE_F64,
    ORUS_JIT_IR_OP_EQ_BOOL,
    ORUS_JIT_IR_OP_NE_BOOL,

    ORUS_JIT_IR_OP_I32_TO_I64,
    ORUS_JIT_IR_OP_U32_TO_U64,
    ORUS_JIT_IR_OP_U32_TO_I32,
    ORUS_JIT_IR_OP_I32_TO_F64,
    ORUS_JIT_IR_OP_I64_TO_F64,
    ORUS_JIT_IR_OP_F64_TO_I32,
    ORUS_JIT_IR_OP_F64_TO_I64,
    ORUS_JIT_IR_OP_F64_TO_U32,
    ORUS_JIT_IR_OP_U32_TO_F64,
    ORUS_JIT_IR_OP_I32_TO_U32,
    ORUS_JIT_IR_OP_I64_TO_U32,
    ORUS_JIT_IR_OP_I32_TO_U64,
    ORUS_JIT_IR_OP_I64_TO_U64,
    ORUS_JIT_IR_OP_U64_TO_I32,
    ORUS_JIT_IR_OP_U64_TO_I64,
    ORUS_JIT_IR_OP_U64_TO_U32,
    ORUS_JIT_IR_OP_F64_TO_U64,
    ORUS_JIT_IR_OP_U64_TO_F64,

    ORUS_JIT_IR_OP_SAFEPOINT,
    ORUS_JIT_IR_OP_LOOP_BACK,
    ORUS_JIT_IR_OP_JUMP_SHORT,
    ORUS_JIT_IR_OP_JUMP_BACK_SHORT,
    ORUS_JIT_IR_OP_JUMP_IF_NOT_SHORT,
    ORUS_JIT_IR_OP_INC_CMP_JUMP,
    ORUS_JIT_IR_OP_DEC_CMP_JUMP,
} OrusJitIROpcode;

typedef enum OrusJitIRLoopStepKind {
    ORUS_JIT_IR_LOOP_STEP_INVALID = 0,
    ORUS_JIT_IR_LOOP_STEP_INCREMENT = 1,
    ORUS_JIT_IR_LOOP_STEP_DECREMENT = -1,
} OrusJitIRLoopStepKind;

typedef enum OrusJitIRLoopCompareKind {
    ORUS_JIT_IR_LOOP_COMPARE_INVALID = 0,
    ORUS_JIT_IR_LOOP_COMPARE_LESS_THAN,
    ORUS_JIT_IR_LOOP_COMPARE_GREATER_THAN,
} OrusJitIRLoopCompareKind;

typedef struct OrusJitIRFusedLoopOperands {
    uint16_t counter_reg;
    uint16_t limit_reg;
    int16_t jump_offset;
    int8_t step;
    uint8_t compare_kind; // OrusJitIRLoopCompareKind
} OrusJitIRFusedLoopOperands;

typedef struct OrusJitIRInstruction {
    OrusJitIROpcode opcode;
    OrusJitValueKind value_kind;
    uint32_t bytecode_offset;
    uint32_t optimization_flags;
    union {
        struct {
            uint16_t dst_reg;
            uint16_t src_reg;
        } move;
        struct {
            uint16_t dst_reg;
            uint16_t src_reg;
        } unary;
        struct {
            uint16_t dst_reg;
            uint16_t lhs_reg;
            uint16_t rhs_reg;
        } arithmetic;
        struct {
            uint16_t dst_reg;
            uint16_t constant_index;
            uint64_t immediate_bits;
        } load_const;
        struct {
            uint16_t dst_reg;
            uint16_t iterable_reg;
        } get_iter;
        struct {
            uint16_t value_reg;
            uint16_t iterator_reg;
            uint16_t has_value_reg;
        } iter_next;
        struct {
            uint16_t dst_reg;
            uint16_t arg_count;
            uint16_t arg_regs[3];
        } range;
        struct {
            uint16_t dst_reg;
        } time_stamp;
        struct {
            uint16_t dst_reg;
            uint16_t first_reg;
            uint16_t count;
        } make_array;
        struct {
            uint16_t array_reg;
            uint16_t value_reg;
        } array_push;
        struct {
            uint16_t dst_reg;
            uint16_t variant_index;
            uint16_t payload_count;
            uint16_t payload_start;
            uint16_t type_const_index;
            uint16_t variant_const_index;
        } enum_new;
        struct {
            uint16_t dst_reg;
            uint16_t label_reg;
            uint16_t actual_reg;
            uint16_t expected_reg;
        } assert_eq;
        struct {
            uint16_t first_reg;
            uint16_t arg_count;
            uint16_t newline;
        } print;
        struct {
            uint16_t dst_reg;
            uint16_t first_arg_reg;
            uint16_t arg_count;
            uint16_t native_index;
            uint16_t spill_base;
            uint16_t spill_count;
        } call_native;
        struct {
            uint16_t offset;
            uint16_t bytecode_length;
        } jump_short;
        struct {
            uint16_t back_offset;
        } jump_back_short;
        struct {
            uint16_t predicate_reg;
            uint16_t offset;
            uint16_t bytecode_length;
        } jump_if_not_short;
        struct {
            uint16_t back_offset;
        } loop_back;
        OrusJitIRFusedLoopOperands fused_loop;
    } operands;
} OrusJitIRInstruction;

#define ORUS_JIT_IR_FLAG_VECTOR_HEAD    (1u << 0)
#define ORUS_JIT_IR_FLAG_VECTOR_TAIL    (1u << 1)
#define ORUS_JIT_IR_FLAG_INLINE_CACHE   (1u << 2)
#define ORUS_JIT_IR_FLAG_LOOP_INVARIANT (1u << 3)

typedef struct OrusJitIRProgram {
    OrusJitIRInstruction* instructions;
    size_t count;
    size_t capacity;
    const struct Chunk* source_chunk;
    uint16_t function_index;
    uint16_t loop_index;
    uint32_t loop_start_offset;
    uint32_t loop_end_offset;
} OrusJitIRProgram;

void orus_jit_ir_program_init(OrusJitIRProgram* program);
void orus_jit_ir_program_reset(OrusJitIRProgram* program);
bool orus_jit_ir_program_reserve(OrusJitIRProgram* program, size_t additional);
OrusJitIRInstruction* orus_jit_ir_program_append(OrusJitIRProgram* program);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // ORUS_VM_JIT_IR_H
