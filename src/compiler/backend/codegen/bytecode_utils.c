// Author: Jordy Orel KONDA
// Copyright (c) 2024 Orus Language Project
// Description: Bytecode instruction length helpers shared across compiler and optimizer passes.

#include "compiler/bytecode_utils.h"

#include <stddef.h>

#include "vm/vm.h"
#include "vm/vm_constants.h"

size_t bytecode_prefix_size(uint8_t opcode) {
    switch (opcode) {
        case OP_JUMP_IF_NOT_I32_TYPED:
            return 3;  // opcode + left_reg + right_reg
        case OP_BRANCH_TYPED:
            return 4;  // opcode + loop_id_hi + loop_id_lo + predicate register
        case OP_JUMP_IF_NOT_R:
        case OP_JUMP_IF_R:
        case OP_TRY_BEGIN:
            return 2;  // opcode + condition register
        case OP_JUMP_IF_NOT_SHORT:
            return 2;  // opcode + condition register
        default:
            return 1;  // opcode only
    }
}

size_t bytecode_operand_size(uint8_t opcode) {
    switch (opcode) {
        case OP_JUMP_SHORT:
        case OP_JUMP_BACK_SHORT:
        case OP_JUMP_IF_NOT_SHORT:
        case OP_LOOP_SHORT:
            return 1;
        case OP_TRY_BEGIN:
            return 2;
        default:
            return 2;
    }
}

size_t bytecode_instruction_length(const BytecodeBuffer* buffer, size_t offset) {
    if (!buffer || offset >= (size_t)buffer->count) {
        return 0;
    }

    uint8_t opcode = buffer->instructions[offset];

    switch (opcode) {
        case OP_LOAD_FALSE:
        case OP_LOAD_TRUE:
            return 2;
        case OP_LOAD_CONST:
        case OP_LOAD_I32_CONST:
        case OP_LOAD_I64_CONST:
        case OP_LOAD_F64_CONST:
            return 4;
        case OP_MOVE:
        case OP_MOVE_I32:
        case OP_MOVE_I64:
        case OP_MOVE_F64:
            return 3;
        case OP_JUMP_SHORT:
            return 2;
        case OP_JUMP_BACK_SHORT:
            return 2;
        case OP_JUMP_IF_NOT_SHORT:
            return 3;
        case OP_LOOP_SHORT:
            return 2;
        case OP_BRANCH_TYPED:
            return 6;
        case OP_TIME_STAMP:
            return 2;
        case OP_RETURN_VOID:
        case OP_TRY_END:
        case OP_HALT:
            return 1;
        case OP_LOAD_GLOBAL:
        case OP_STORE_GLOBAL:
        case OP_LOOP:
            return 3;
        case OP_TRY_BEGIN:
        case OP_JUMP:
            return 3;
        case OP_JUMP_IF_NOT_R:
            return 4;
        case OP_JUMP_IF_NOT_I32_TYPED:
            return 5;
        default:
            break;
    }

    switch (opcode) {
        case OP_ADD_I32_R:
        case OP_SUB_I32_R:
        case OP_MUL_I32_R:
        case OP_ADD_I64_R:
        case OP_SUB_I64_R:
        case OP_MUL_I64_R:
        case OP_DIV_I64_R:
        case OP_MOD_I64_R:
        case OP_LT_I64_R:
        case OP_LE_I64_R:
        case OP_GT_I64_R:
        case OP_GE_I64_R:
        case OP_LT_I32_R:
        case OP_IS_TYPE_R:
        case OP_INPUT_R:
        case OP_MAKE_ARRAY_R:
        case OP_ENUM_TAG_EQ_R:
        case OP_TO_STRING_R:
        case OP_STRING_INDEX_R:
        case OP_ARRAY_GET_R:
        case OP_ARRAY_SET_R:
        case OP_ARRAY_SLICE_R:
        case OP_CALL_NATIVE_R:
        case OP_BRANCH_TYPED:
        case OP_ADD_I32_TYPED:
        case OP_SUB_I32_TYPED:
        case OP_MUL_I32_TYPED:
            return 4;
        case OP_INC_I32_R:
        case OP_INC_I32_CHECKED:
        case OP_INC_I64_R:
        case OP_INC_I64_CHECKED:
        case OP_INC_U32_R:
        case OP_INC_U32_CHECKED:
        case OP_INC_U64_R:
        case OP_INC_U64_CHECKED:
        case OP_DEC_I32_R:
        case OP_PRINT_R:
        case OP_RETURN_R:
        case OP_ARRAY_LEN_R:
        case OP_ARRAY_PUSH_R:
        case OP_ARRAY_POP_R:
        case OP_ARRAY_SORTED_R:
        case OP_TIME_STAMP:
            return 2;
        case OP_PARSE_INT_R:
        case OP_PARSE_FLOAT_R:
        case OP_TYPE_OF_R:
        case OP_PRINT_MULTI_R:
        case OP_ASSERT_EQ_R:
        case OP_ENUM_PAYLOAD_R:
        case OP_RANGE_R:
            return 3;
        case OP_ENUM_NEW_R:
            return 9;
        default:
            return 1;
    }
}
