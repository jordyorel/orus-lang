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
    ORUS_JIT_VALUE_STRING,
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

    ORUS_JIT_IR_OP_MOVE_I32,
    ORUS_JIT_IR_OP_MOVE_I64,
    ORUS_JIT_IR_OP_MOVE_U32,
    ORUS_JIT_IR_OP_MOVE_U64,
    ORUS_JIT_IR_OP_MOVE_F64,
    ORUS_JIT_IR_OP_MOVE_STRING,

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

    ORUS_JIT_IR_OP_I32_TO_I64,
    ORUS_JIT_IR_OP_U32_TO_U64,

    ORUS_JIT_IR_OP_SAFEPOINT,
    ORUS_JIT_IR_OP_LOOP_BACK,
    ORUS_JIT_IR_OP_JUMP_SHORT,
    ORUS_JIT_IR_OP_JUMP_BACK_SHORT,
    ORUS_JIT_IR_OP_JUMP_IF_NOT_SHORT,
} OrusJitIROpcode;

typedef struct OrusJitIRInstruction {
    OrusJitIROpcode opcode;
    OrusJitValueKind value_kind;
    uint32_t bytecode_offset;
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
            uint16_t offset;
        } jump_short;
        struct {
            uint16_t back_offset;
        } jump_back_short;
        struct {
            uint16_t predicate_reg;
            uint16_t offset;
        } jump_if_not_short;
        struct {
            uint16_t back_offset;
        } loop_back;
    } operands;
} OrusJitIRInstruction;

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
