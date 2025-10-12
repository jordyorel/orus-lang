// Orus Language Project
// ---------------------------------------------------------------------------
// File: src/vm/jit/orus_jit_ir_debug.c
// Description: Formatting helpers for debugging Orus JIT IR streams.

#include "vm/jit_ir_debug.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "vm/jit_translation.h"

const char*
orus_jit_ir_opcode_name(OrusJitIROpcode opcode) {
    switch (opcode) {
        case ORUS_JIT_IR_OP_RETURN: return "ORUS_JIT_IR_OP_RETURN";
        case ORUS_JIT_IR_OP_LOAD_I32_CONST: return "ORUS_JIT_IR_OP_LOAD_I32_CONST";
        case ORUS_JIT_IR_OP_LOAD_I64_CONST: return "ORUS_JIT_IR_OP_LOAD_I64_CONST";
        case ORUS_JIT_IR_OP_LOAD_U32_CONST: return "ORUS_JIT_IR_OP_LOAD_U32_CONST";
        case ORUS_JIT_IR_OP_LOAD_U64_CONST: return "ORUS_JIT_IR_OP_LOAD_U64_CONST";
        case ORUS_JIT_IR_OP_LOAD_F64_CONST: return "ORUS_JIT_IR_OP_LOAD_F64_CONST";
        case ORUS_JIT_IR_OP_LOAD_STRING_CONST: return "ORUS_JIT_IR_OP_LOAD_STRING_CONST";
        case ORUS_JIT_IR_OP_MOVE_I32: return "ORUS_JIT_IR_OP_MOVE_I32";
        case ORUS_JIT_IR_OP_MOVE_I64: return "ORUS_JIT_IR_OP_MOVE_I64";
        case ORUS_JIT_IR_OP_MOVE_U32: return "ORUS_JIT_IR_OP_MOVE_U32";
        case ORUS_JIT_IR_OP_MOVE_U64: return "ORUS_JIT_IR_OP_MOVE_U64";
        case ORUS_JIT_IR_OP_MOVE_F64: return "ORUS_JIT_IR_OP_MOVE_F64";
        case ORUS_JIT_IR_OP_MOVE_BOOL: return "ORUS_JIT_IR_OP_MOVE_BOOL";
        case ORUS_JIT_IR_OP_MOVE_STRING: return "ORUS_JIT_IR_OP_MOVE_STRING";
        case ORUS_JIT_IR_OP_MOVE_VALUE: return "ORUS_JIT_IR_OP_MOVE_VALUE";
        case ORUS_JIT_IR_OP_ADD_I32: return "ORUS_JIT_IR_OP_ADD_I32";
        case ORUS_JIT_IR_OP_ADD_I64: return "ORUS_JIT_IR_OP_ADD_I64";
        case ORUS_JIT_IR_OP_ADD_U32: return "ORUS_JIT_IR_OP_ADD_U32";
        case ORUS_JIT_IR_OP_ADD_U64: return "ORUS_JIT_IR_OP_ADD_U64";
        case ORUS_JIT_IR_OP_ADD_F64: return "ORUS_JIT_IR_OP_ADD_F64";
        case ORUS_JIT_IR_OP_SUB_I32: return "ORUS_JIT_IR_OP_SUB_I32";
        case ORUS_JIT_IR_OP_SUB_I64: return "ORUS_JIT_IR_OP_SUB_I64";
        case ORUS_JIT_IR_OP_SUB_U32: return "ORUS_JIT_IR_OP_SUB_U32";
        case ORUS_JIT_IR_OP_SUB_U64: return "ORUS_JIT_IR_OP_SUB_U64";
        case ORUS_JIT_IR_OP_SUB_F64: return "ORUS_JIT_IR_OP_SUB_F64";
        case ORUS_JIT_IR_OP_MUL_I32: return "ORUS_JIT_IR_OP_MUL_I32";
        case ORUS_JIT_IR_OP_MUL_I64: return "ORUS_JIT_IR_OP_MUL_I64";
        case ORUS_JIT_IR_OP_MUL_U32: return "ORUS_JIT_IR_OP_MUL_U32";
        case ORUS_JIT_IR_OP_MUL_U64: return "ORUS_JIT_IR_OP_MUL_U64";
        case ORUS_JIT_IR_OP_MUL_F64: return "ORUS_JIT_IR_OP_MUL_F64";
        case ORUS_JIT_IR_OP_DIV_I32: return "ORUS_JIT_IR_OP_DIV_I32";
        case ORUS_JIT_IR_OP_DIV_I64: return "ORUS_JIT_IR_OP_DIV_I64";
        case ORUS_JIT_IR_OP_DIV_U32: return "ORUS_JIT_IR_OP_DIV_U32";
        case ORUS_JIT_IR_OP_DIV_U64: return "ORUS_JIT_IR_OP_DIV_U64";
        case ORUS_JIT_IR_OP_DIV_F64: return "ORUS_JIT_IR_OP_DIV_F64";
        case ORUS_JIT_IR_OP_MOD_I32: return "ORUS_JIT_IR_OP_MOD_I32";
        case ORUS_JIT_IR_OP_MOD_I64: return "ORUS_JIT_IR_OP_MOD_I64";
        case ORUS_JIT_IR_OP_MOD_U32: return "ORUS_JIT_IR_OP_MOD_U32";
        case ORUS_JIT_IR_OP_MOD_U64: return "ORUS_JIT_IR_OP_MOD_U64";
        case ORUS_JIT_IR_OP_MOD_F64: return "ORUS_JIT_IR_OP_MOD_F64";
        case ORUS_JIT_IR_OP_CONCAT_STRING: return "ORUS_JIT_IR_OP_CONCAT_STRING";
        case ORUS_JIT_IR_OP_TO_STRING: return "ORUS_JIT_IR_OP_TO_STRING";
        case ORUS_JIT_IR_OP_TIME_STAMP: return "ORUS_JIT_IR_OP_TIME_STAMP";
        case ORUS_JIT_IR_OP_ARRAY_PUSH: return "ORUS_JIT_IR_OP_ARRAY_PUSH";
        case ORUS_JIT_IR_OP_PRINT: return "ORUS_JIT_IR_OP_PRINT";
        case ORUS_JIT_IR_OP_ASSERT_EQ: return "ORUS_JIT_IR_OP_ASSERT_EQ";
        case ORUS_JIT_IR_OP_CALL_NATIVE: return "ORUS_JIT_IR_OP_CALL_NATIVE";
        case ORUS_JIT_IR_OP_GET_ITER: return "ORUS_JIT_IR_OP_GET_ITER";
        case ORUS_JIT_IR_OP_ITER_NEXT: return "ORUS_JIT_IR_OP_ITER_NEXT";
        case ORUS_JIT_IR_OP_RANGE: return "ORUS_JIT_IR_OP_RANGE";
        case ORUS_JIT_IR_OP_LT_I32: return "ORUS_JIT_IR_OP_LT_I32";
        case ORUS_JIT_IR_OP_LE_I32: return "ORUS_JIT_IR_OP_LE_I32";
        case ORUS_JIT_IR_OP_GT_I32: return "ORUS_JIT_IR_OP_GT_I32";
        case ORUS_JIT_IR_OP_GE_I32: return "ORUS_JIT_IR_OP_GE_I32";
        case ORUS_JIT_IR_OP_LT_I64: return "ORUS_JIT_IR_OP_LT_I64";
        case ORUS_JIT_IR_OP_LE_I64: return "ORUS_JIT_IR_OP_LE_I64";
        case ORUS_JIT_IR_OP_GT_I64: return "ORUS_JIT_IR_OP_GT_I64";
        case ORUS_JIT_IR_OP_GE_I64: return "ORUS_JIT_IR_OP_GE_I64";
        case ORUS_JIT_IR_OP_LT_U32: return "ORUS_JIT_IR_OP_LT_U32";
        case ORUS_JIT_IR_OP_LE_U32: return "ORUS_JIT_IR_OP_LE_U32";
        case ORUS_JIT_IR_OP_GT_U32: return "ORUS_JIT_IR_OP_GT_U32";
        case ORUS_JIT_IR_OP_GE_U32: return "ORUS_JIT_IR_OP_GE_U32";
        case ORUS_JIT_IR_OP_LT_U64: return "ORUS_JIT_IR_OP_LT_U64";
        case ORUS_JIT_IR_OP_LE_U64: return "ORUS_JIT_IR_OP_LE_U64";
        case ORUS_JIT_IR_OP_GT_U64: return "ORUS_JIT_IR_OP_GT_U64";
        case ORUS_JIT_IR_OP_GE_U64: return "ORUS_JIT_IR_OP_GE_U64";
        case ORUS_JIT_IR_OP_LT_F64: return "ORUS_JIT_IR_OP_LT_F64";
        case ORUS_JIT_IR_OP_LE_F64: return "ORUS_JIT_IR_OP_LE_F64";
        case ORUS_JIT_IR_OP_GT_F64: return "ORUS_JIT_IR_OP_GT_F64";
        case ORUS_JIT_IR_OP_GE_F64: return "ORUS_JIT_IR_OP_GE_F64";
        case ORUS_JIT_IR_OP_EQ_I32: return "ORUS_JIT_IR_OP_EQ_I32";
        case ORUS_JIT_IR_OP_NE_I32: return "ORUS_JIT_IR_OP_NE_I32";
        case ORUS_JIT_IR_OP_EQ_I64: return "ORUS_JIT_IR_OP_EQ_I64";
        case ORUS_JIT_IR_OP_NE_I64: return "ORUS_JIT_IR_OP_NE_I64";
        case ORUS_JIT_IR_OP_EQ_U32: return "ORUS_JIT_IR_OP_EQ_U32";
        case ORUS_JIT_IR_OP_NE_U32: return "ORUS_JIT_IR_OP_NE_U32";
        case ORUS_JIT_IR_OP_EQ_U64: return "ORUS_JIT_IR_OP_EQ_U64";
        case ORUS_JIT_IR_OP_NE_U64: return "ORUS_JIT_IR_OP_NE_U64";
        case ORUS_JIT_IR_OP_EQ_F64: return "ORUS_JIT_IR_OP_EQ_F64";
        case ORUS_JIT_IR_OP_NE_F64: return "ORUS_JIT_IR_OP_NE_F64";
        case ORUS_JIT_IR_OP_EQ_BOOL: return "ORUS_JIT_IR_OP_EQ_BOOL";
        case ORUS_JIT_IR_OP_NE_BOOL: return "ORUS_JIT_IR_OP_NE_BOOL";
        case ORUS_JIT_IR_OP_I32_TO_I64: return "ORUS_JIT_IR_OP_I32_TO_I64";
        case ORUS_JIT_IR_OP_U32_TO_U64: return "ORUS_JIT_IR_OP_U32_TO_U64";
        case ORUS_JIT_IR_OP_U32_TO_I32: return "ORUS_JIT_IR_OP_U32_TO_I32";
        case ORUS_JIT_IR_OP_SAFEPOINT: return "ORUS_JIT_IR_OP_SAFEPOINT";
        case ORUS_JIT_IR_OP_LOOP_BACK: return "ORUS_JIT_IR_OP_LOOP_BACK";
        case ORUS_JIT_IR_OP_JUMP_SHORT: return "ORUS_JIT_IR_OP_JUMP_SHORT";
        case ORUS_JIT_IR_OP_JUMP_BACK_SHORT: return "ORUS_JIT_IR_OP_JUMP_BACK_SHORT";
        case ORUS_JIT_IR_OP_JUMP_IF_NOT_SHORT: return "ORUS_JIT_IR_OP_JUMP_IF_NOT_SHORT";
        case ORUS_JIT_IR_OP_INC_CMP_JUMP: return "ORUS_JIT_IR_OP_INC_CMP_JUMP";
        case ORUS_JIT_IR_OP_DEC_CMP_JUMP: return "ORUS_JIT_IR_OP_DEC_CMP_JUMP";
        default: break;
    }
    return "ORUS_JIT_IR_OP_UNKNOWN";
}

const char*
orus_jit_ir_loop_compare_name(OrusJitIRLoopCompareKind kind) {
    switch (kind) {
        case ORUS_JIT_IR_LOOP_COMPARE_LESS_THAN:
            return "less_than";
        case ORUS_JIT_IR_LOOP_COMPARE_GREATER_THAN:
            return "greater_than";
        default:
            return "invalid";
    }
}

static const char*
orus_jit_ir_step_name(int8_t step) {
    if (step > 0) {
        return "+1";
    }
    if (step < 0) {
        return "-1";
    }
    return "0";
}

static size_t
format_load_const(const OrusJitIRInstruction* inst, char* buffer, size_t size) {
    uint64_t bits = inst->operands.load_const.immediate_bits;
    switch (inst->value_kind) {
        case ORUS_JIT_VALUE_I32:
            return (size_t)snprintf(buffer, size, "%s dst=r%u imm=%" PRId32,
                                    orus_jit_ir_opcode_name(inst->opcode),
                                    inst->operands.load_const.dst_reg,
                                    (int32_t)bits);
        case ORUS_JIT_VALUE_I64:
            return (size_t)snprintf(buffer, size, "%s dst=r%u imm=%" PRId64,
                                    orus_jit_ir_opcode_name(inst->opcode),
                                    inst->operands.load_const.dst_reg,
                                    (int64_t)bits);
        case ORUS_JIT_VALUE_U32:
            return (size_t)snprintf(buffer, size, "%s dst=r%u imm=%" PRIu32,
                                    orus_jit_ir_opcode_name(inst->opcode),
                                    inst->operands.load_const.dst_reg,
                                    (uint32_t)bits);
        case ORUS_JIT_VALUE_U64:
            return (size_t)snprintf(buffer, size, "%s dst=r%u imm=%" PRIu64,
                                    orus_jit_ir_opcode_name(inst->opcode),
                                    inst->operands.load_const.dst_reg,
                                    (uint64_t)bits);
        case ORUS_JIT_VALUE_F64: {
            double value = 0.0;
            memcpy(&value, &bits, sizeof(double));
            return (size_t)snprintf(buffer, size, "%s dst=r%u imm=%g",
                                    orus_jit_ir_opcode_name(inst->opcode),
                                    inst->operands.load_const.dst_reg, value);
        }
        case ORUS_JIT_VALUE_STRING:
            return (size_t)snprintf(buffer, size,
                                    "%s dst=r%u const_index=%u ptr=0x%" PRIx64,
                                    orus_jit_ir_opcode_name(inst->opcode),
                                    inst->operands.load_const.dst_reg,
                                    inst->operands.load_const.constant_index,
                                    (uint64_t)bits);
        default:
            break;
    }
    return (size_t)snprintf(buffer, size, "%s dst=r%u bits=0x%" PRIx64,
                            orus_jit_ir_opcode_name(inst->opcode),
                            inst->operands.load_const.dst_reg, bits);
}

size_t
orus_jit_ir_format_instruction(const OrusJitIRInstruction* inst,
                               char* buffer,
                               size_t buffer_size) {
    if (!buffer || buffer_size == 0) {
        return 0;
    }
    if (!inst) {
        buffer[0] = '\0';
        return 0;
    }

    const char* opcode_name = orus_jit_ir_opcode_name(inst->opcode);

    switch (inst->opcode) {
        case ORUS_JIT_IR_OP_LOAD_I32_CONST:
        case ORUS_JIT_IR_OP_LOAD_I64_CONST:
        case ORUS_JIT_IR_OP_LOAD_U32_CONST:
        case ORUS_JIT_IR_OP_LOAD_U64_CONST:
        case ORUS_JIT_IR_OP_LOAD_F64_CONST:
        case ORUS_JIT_IR_OP_LOAD_STRING_CONST:
            return format_load_const(inst, buffer, buffer_size);

        case ORUS_JIT_IR_OP_MOVE_I32:
        case ORUS_JIT_IR_OP_MOVE_I64:
        case ORUS_JIT_IR_OP_MOVE_U32:
        case ORUS_JIT_IR_OP_MOVE_U64:
        case ORUS_JIT_IR_OP_MOVE_F64:
        case ORUS_JIT_IR_OP_MOVE_BOOL:
        case ORUS_JIT_IR_OP_MOVE_STRING:
        case ORUS_JIT_IR_OP_MOVE_VALUE:
            return (size_t)snprintf(buffer, buffer_size,
                                    "%s kind=%s dst=r%u src=r%u",
                                    opcode_name,
                                    orus_jit_value_kind_name(inst->value_kind),
                                    inst->operands.move.dst_reg,
                                    inst->operands.move.src_reg);

        case ORUS_JIT_IR_OP_ADD_I32:
        case ORUS_JIT_IR_OP_ADD_I64:
        case ORUS_JIT_IR_OP_ADD_U32:
        case ORUS_JIT_IR_OP_ADD_U64:
        case ORUS_JIT_IR_OP_ADD_F64:
        case ORUS_JIT_IR_OP_SUB_I32:
        case ORUS_JIT_IR_OP_SUB_I64:
        case ORUS_JIT_IR_OP_SUB_U32:
        case ORUS_JIT_IR_OP_SUB_U64:
        case ORUS_JIT_IR_OP_SUB_F64:
        case ORUS_JIT_IR_OP_MUL_I32:
        case ORUS_JIT_IR_OP_MUL_I64:
        case ORUS_JIT_IR_OP_MUL_U32:
        case ORUS_JIT_IR_OP_MUL_U64:
        case ORUS_JIT_IR_OP_MUL_F64:
        case ORUS_JIT_IR_OP_DIV_I32:
        case ORUS_JIT_IR_OP_DIV_I64:
        case ORUS_JIT_IR_OP_DIV_U32:
        case ORUS_JIT_IR_OP_DIV_U64:
        case ORUS_JIT_IR_OP_DIV_F64:
        case ORUS_JIT_IR_OP_MOD_I32:
        case ORUS_JIT_IR_OP_MOD_I64:
        case ORUS_JIT_IR_OP_MOD_U32:
        case ORUS_JIT_IR_OP_MOD_U64:
        case ORUS_JIT_IR_OP_MOD_F64:
        case ORUS_JIT_IR_OP_LT_I32:
        case ORUS_JIT_IR_OP_LE_I32:
        case ORUS_JIT_IR_OP_GT_I32:
        case ORUS_JIT_IR_OP_GE_I32:
        case ORUS_JIT_IR_OP_LT_I64:
        case ORUS_JIT_IR_OP_LE_I64:
        case ORUS_JIT_IR_OP_GT_I64:
        case ORUS_JIT_IR_OP_GE_I64:
        case ORUS_JIT_IR_OP_LT_U32:
        case ORUS_JIT_IR_OP_LE_U32:
        case ORUS_JIT_IR_OP_GT_U32:
        case ORUS_JIT_IR_OP_GE_U32:
        case ORUS_JIT_IR_OP_LT_U64:
        case ORUS_JIT_IR_OP_LE_U64:
        case ORUS_JIT_IR_OP_GT_U64:
        case ORUS_JIT_IR_OP_GE_U64:
        case ORUS_JIT_IR_OP_LT_F64:
        case ORUS_JIT_IR_OP_LE_F64:
        case ORUS_JIT_IR_OP_GT_F64:
        case ORUS_JIT_IR_OP_GE_F64:
        case ORUS_JIT_IR_OP_EQ_I32:
        case ORUS_JIT_IR_OP_NE_I32:
        case ORUS_JIT_IR_OP_EQ_I64:
        case ORUS_JIT_IR_OP_NE_I64:
        case ORUS_JIT_IR_OP_EQ_U32:
        case ORUS_JIT_IR_OP_NE_U32:
        case ORUS_JIT_IR_OP_EQ_U64:
        case ORUS_JIT_IR_OP_NE_U64:
        case ORUS_JIT_IR_OP_EQ_F64:
        case ORUS_JIT_IR_OP_NE_F64:
        case ORUS_JIT_IR_OP_EQ_BOOL:
        case ORUS_JIT_IR_OP_NE_BOOL:
            return (size_t)snprintf(buffer, buffer_size,
                                    "%s kind=%s dst=r%u lhs=r%u rhs=r%u",
                                    opcode_name,
                                    orus_jit_value_kind_name(inst->value_kind),
                                    inst->operands.arithmetic.dst_reg,
                                    inst->operands.arithmetic.lhs_reg,
                                    inst->operands.arithmetic.rhs_reg);

        case ORUS_JIT_IR_OP_JUMP_SHORT:
            return (size_t)snprintf(buffer, buffer_size,
                                    "%s offset=%u",
                                    opcode_name,
                                    inst->operands.jump_short.offset);
        case ORUS_JIT_IR_OP_JUMP_BACK_SHORT:
            return (size_t)snprintf(buffer, buffer_size,
                                    "%s back=%u",
                                    opcode_name,
                                    inst->operands.jump_back_short.back_offset);
        case ORUS_JIT_IR_OP_JUMP_IF_NOT_SHORT:
            return (size_t)snprintf(buffer, buffer_size,
                                    "%s predicate=r%u offset=%u",
                                    opcode_name,
                                    inst->operands.jump_if_not_short.predicate_reg,
                                    inst->operands.jump_if_not_short.offset);
        case ORUS_JIT_IR_OP_LOOP_BACK:
            return (size_t)snprintf(buffer, buffer_size,
                                    "%s back=%u",
                                    opcode_name,
                                    inst->operands.loop_back.back_offset);
        case ORUS_JIT_IR_OP_INC_CMP_JUMP:
        case ORUS_JIT_IR_OP_DEC_CMP_JUMP:
            return (size_t)snprintf(
                buffer, buffer_size,
                "%s kind=%s counter=r%u limit=r%u offset=%d step=%s compare=%s",
                opcode_name,
                orus_jit_value_kind_name(inst->value_kind),
                inst->operands.fused_loop.counter_reg,
                inst->operands.fused_loop.limit_reg,
                (int)inst->operands.fused_loop.jump_offset,
                orus_jit_ir_step_name(inst->operands.fused_loop.step),
                orus_jit_ir_loop_compare_name(
                    (OrusJitIRLoopCompareKind)inst->operands.fused_loop.compare_kind));
        case ORUS_JIT_IR_OP_SAFEPOINT:
            return (size_t)snprintf(buffer, buffer_size, "%s", opcode_name);
        default:
            break;
    }

    return (size_t)snprintf(buffer, buffer_size, "%s kind=%s",
                            opcode_name,
                            orus_jit_value_kind_name(inst->value_kind));
}

void
orus_jit_ir_dump_program(const OrusJitIRProgram* program, FILE* file) {
    if (!program || !file) {
        return;
    }

    fprintf(file,
            "[JIT] IR program: function=%u loop=%u count=%zu start=%u end=%u\n",
            program->function_index,
            program->loop_index,
            program->count,
            program->loop_start_offset,
            program->loop_end_offset);

    for (size_t i = 0; i < program->count; ++i) {
        const OrusJitIRInstruction* inst = &program->instructions[i];
        char buffer[256];
        orus_jit_ir_format_instruction(inst, buffer, sizeof(buffer));
        fprintf(file, "    [%zu] @%u %s\n", i, inst->bytecode_offset, buffer);
    }
}
