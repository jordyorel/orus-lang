// debug.c - Debug utilities for the VM
#include "tools/debug.h"
#include "public/common.h"
#include "vm/vm_constants.h"
#include <stdio.h>

void disassembleChunk(Chunk* chunk, const char* name) {
    printf("== %s ==\n", name);

    for (int offset = 0; offset < chunk->count;) {
        offset = disassembleInstruction(chunk, offset);
    }
}

int disassembleInstruction(Chunk* chunk, int offset) {
    printf("%04d ", offset);

    if (offset > 0 && chunk->lines[offset] == chunk->lines[offset - 1]) {
        printf("   | ");
    } else {
        printf("%4d ", chunk->lines[offset]);
    }

    uint8_t instruction = chunk->code[offset];
    switch (instruction) {
        case OP_LOAD_CONST: {
            uint8_t reg = chunk->code[offset + 1];
            uint16_t constant = (uint16_t)((chunk->code[offset + 2] << 8) | chunk->code[offset + 3]);
            printf("%-16s R%d, #%d '", "LOAD_CONST", reg, constant);
            if (constant < chunk->constants.count) {
                printValue(chunk->constants.values[constant]);
            } else {
                printf("INVALID_CONSTANT_INDEX");
            }
            printf("'\n");
            return offset + 4;
        }


        case OP_MOVE: {
            uint8_t dst = chunk->code[offset + 1];
            uint8_t src = chunk->code[offset + 2];
            printf("%-16s R%d, R%d\n", "MOVE", dst, src);
            return offset + 3;
        }

        case OP_ADD_I32_R: {
            uint8_t dst = chunk->code[offset + 1];
            uint8_t src1 = chunk->code[offset + 2];
            uint8_t src2 = chunk->code[offset + 3];
            printf("%-16s R%d, R%d, R%d\n", "ADD_I32", dst, src1, src2);
            return offset + 4;
        }

        case OP_SUB_I32_R: {
            uint8_t dst = chunk->code[offset + 1];
            uint8_t src1 = chunk->code[offset + 2];
            uint8_t src2 = chunk->code[offset + 3];
            printf("%-16s R%d, R%d, R%d\n", "SUB_I32", dst, src1, src2);
            return offset + 4;
        }

        case OP_MUL_I32_R: {
            uint8_t dst = chunk->code[offset + 1];
            uint8_t src1 = chunk->code[offset + 2];
            uint8_t src2 = chunk->code[offset + 3];
            printf("%-16s R%d, R%d, R%d\n", "MUL_I32", dst, src1, src2);
            return offset + 4;
        }

        case OP_INC_I32_R: {
            uint8_t reg = chunk->code[offset + 1];
            printf("%-16s R%d\n", "INC_I32", reg);
            return offset + 2;
        }

        case OP_DEC_I32_R: {
            uint8_t reg = chunk->code[offset + 1];
            printf("%-16s R%d\n", "DEC_I32", reg);
            return offset + 2;
        }

        case OP_ADD_I64_R: {
            uint8_t dst = chunk->code[offset + 1];
            uint8_t src1 = chunk->code[offset + 2];
            uint8_t src2 = chunk->code[offset + 3];
            printf("%-16s R%d, R%d, R%d\n", "ADD_I64", dst, src1, src2);
            return offset + 4;
        }

        case OP_SUB_I64_R: {
            uint8_t dst = chunk->code[offset + 1];
            uint8_t src1 = chunk->code[offset + 2];
            uint8_t src2 = chunk->code[offset + 3];
            printf("%-16s R%d, R%d, R%d\n", "SUB_I64", dst, src1, src2);
            return offset + 4;
        }

        case OP_MUL_I64_R: {
            uint8_t dst = chunk->code[offset + 1];
            uint8_t src1 = chunk->code[offset + 2];
            uint8_t src2 = chunk->code[offset + 3];
            printf("%-16s R%d, R%d, R%d\n", "MUL_I64", dst, src1, src2);
            return offset + 4;
        }

        case OP_DIV_I64_R: {
            uint8_t dst = chunk->code[offset + 1];
            uint8_t src1 = chunk->code[offset + 2];
            uint8_t src2 = chunk->code[offset + 3];
            printf("%-16s R%d, R%d, R%d\n", "DIV_I64", dst, src1, src2);
            return offset + 4;
        }

        case OP_MOD_I64_R: {
            uint8_t dst = chunk->code[offset + 1];
            uint8_t src1 = chunk->code[offset + 2];
            uint8_t src2 = chunk->code[offset + 3];
            printf("%-16s R%d, R%d, R%d\n", "MOD_I64", dst, src1, src2);
            return offset + 4;
        }

        case OP_I32_TO_I64_R: {
            uint8_t dst = chunk->code[offset + 1];
            uint8_t src = chunk->code[offset + 2];
            printf("%-16s R%d, R%d\n", "I32_TO_I64", dst, src);
            return offset + 3;
        }

        case OP_LT_I64_R: {
            uint8_t dst = chunk->code[offset + 1];
            uint8_t src1 = chunk->code[offset + 2];
            uint8_t src2 = chunk->code[offset + 3];
            printf("%-16s R%d, R%d, R%d\n", "LT_I64", dst, src1, src2);
            return offset + 4;
        }

        case OP_LE_I64_R: {
            uint8_t dst = chunk->code[offset + 1];
            uint8_t src1 = chunk->code[offset + 2];
            uint8_t src2 = chunk->code[offset + 3];
            printf("%-16s R%d, R%d, R%d\n", "LE_I64", dst, src1, src2);
            return offset + 4;
        }

        case OP_GT_I64_R: {
            uint8_t dst = chunk->code[offset + 1];
            uint8_t src1 = chunk->code[offset + 2];
            uint8_t src2 = chunk->code[offset + 3];
            printf("%-16s R%d, R%d, R%d\n", "GT_I64", dst, src1, src2);
            return offset + 4;
        }

        case OP_GE_I64_R: {
            uint8_t dst = chunk->code[offset + 1];
            uint8_t src1 = chunk->code[offset + 2];
            uint8_t src2 = chunk->code[offset + 3];
            printf("%-16s R%d, R%d, R%d\n", "GE_I64", dst, src1, src2);
            return offset + 4;
        }

        case OP_LT_I32_R: {
            uint8_t dst = chunk->code[offset + 1];
            uint8_t src1 = chunk->code[offset + 2];
            uint8_t src2 = chunk->code[offset + 3];
            printf("%-16s R%d, R%d, R%d\n", "LT_I32", dst, src1, src2);
            return offset + 4;
        }

        case OP_PRINT_MULTI_R: {
            uint8_t first = chunk->code[offset + 1];
            uint8_t count = chunk->code[offset + 2];
            uint8_t nl = chunk->code[offset + 3];
            printf("%-16s R%d, count=%d, newline=%d\n", "PRINT_MULTI", first, count, nl);
            return offset + 4;
        }

        case OP_PRINT_R: {
            uint8_t reg = chunk->code[offset + 1];
            printf("%-16s R%d\n", "PRINT", reg);
            return offset + 2;
        }

        case OP_PRINT_NO_NL_R: {
            uint8_t reg = chunk->code[offset + 1];
            printf("%-16s R%d\n", "PRINT_NO_NL_R", reg);
            return offset + 2;
        }

        case OP_MAKE_ARRAY_R: {
            uint8_t dst = chunk->code[offset + 1];
            uint8_t first = chunk->code[offset + 2];
            uint8_t count = chunk->code[offset + 3];
            printf("%-16s R%d, R%d, count=%d\n", "MAKE_ARRAY", dst, first, count);
            return offset + 4;
        }

        case OP_ENUM_NEW_R: {
            uint8_t dst = chunk->code[offset + 1];
            uint8_t variant = chunk->code[offset + 2];
            uint8_t payload = chunk->code[offset + 3];
            uint8_t start = chunk->code[offset + 4];
            uint16_t typeConst = (uint16_t)((chunk->code[offset + 5] << 8) | chunk->code[offset + 6]);
            uint16_t variantConst = (uint16_t)((chunk->code[offset + 7] << 8) | chunk->code[offset + 8]);
            printf("%-16s R%d, variant=%d, count=%d, start=R%d, typeConst=%d, variantConst=%d\n",
                   "ENUM_NEW", dst, variant, payload, start, typeConst, variantConst);
            return offset + 9;
        }
        case OP_ENUM_TAG_EQ_R: {
            uint8_t dst = chunk->code[offset + 1];
            uint8_t enumReg = chunk->code[offset + 2];
            uint8_t variant = chunk->code[offset + 3];
            printf("%-16s R%d, enum=R%d, variant=%d\n", "ENUM_TAG_EQ", dst, enumReg, variant);
            return offset + 4;
        }
        case OP_ENUM_PAYLOAD_R: {
            uint8_t dst = chunk->code[offset + 1];
            uint8_t enumReg = chunk->code[offset + 2];
            uint8_t variant = chunk->code[offset + 3];
            uint8_t field = chunk->code[offset + 4];
            printf("%-16s R%d, enum=R%d, variant=%d, field=%d\n",
                   "ENUM_PAYLOAD", dst, enumReg, variant, field);
            return offset + 5;
        }

        case OP_ARRAY_GET_R: {
            uint8_t dst = chunk->code[offset + 1];
            uint8_t array_reg = chunk->code[offset + 2];
            uint8_t index_reg = chunk->code[offset + 3];
            printf("%-16s R%d, R%d, R%d\n", "ARRAY_GET", dst, array_reg, index_reg);
            return offset + 4;
        }

        case OP_ARRAY_SET_R: {
            uint8_t array_reg = chunk->code[offset + 1];
            uint8_t index_reg = chunk->code[offset + 2];
            uint8_t value_reg = chunk->code[offset + 3];
            printf("%-16s R%d, R%d, R%d\n", "ARRAY_SET", array_reg, index_reg, value_reg);
            return offset + 4;
        }

        case OP_ARRAY_LEN_R: {
            uint8_t dst = chunk->code[offset + 1];
            uint8_t array_reg = chunk->code[offset + 2];
            printf("%-16s R%d, R%d\n", "ARRAY_LEN", dst, array_reg);
            return offset + 3;
        }

        case OP_ARRAY_PUSH_R: {
            uint8_t array_reg = chunk->code[offset + 1];
            uint8_t value_reg = chunk->code[offset + 2];
            printf("%-16s R%d, R%d\n", "ARRAY_PUSH", array_reg, value_reg);
            return offset + 3;
        }

        case OP_ARRAY_POP_R: {
            uint8_t dst = chunk->code[offset + 1];
            uint8_t array_reg = chunk->code[offset + 2];
            printf("%-16s R%d, R%d\n", "ARRAY_POP", dst, array_reg);
            return offset + 3;
        }

        case OP_ARRAY_SLICE_R: {
            uint8_t dst = chunk->code[offset + 1];
            uint8_t array_reg = chunk->code[offset + 2];
            uint8_t start_reg = chunk->code[offset + 3];
            uint8_t end_reg = chunk->code[offset + 4];
            printf("%-16s R%d, R%d, R%d, R%d\n", "ARRAY_SLICE", dst, array_reg, start_reg, end_reg);
            return offset + 5;
        }

        case OP_CALL_NATIVE_R: {
            uint8_t native_index = chunk->code[offset + 1];
            uint8_t first_arg = chunk->code[offset + 2];
            uint8_t arg_count = chunk->code[offset + 3];
            uint8_t result_reg = chunk->code[offset + 4];
            printf("%-16s native=%d, args=R%d..R%d, result=R%d\n", 
                   "CALL_NATIVE", native_index, first_arg, first_arg + arg_count - 1, result_reg);
            return offset + 5;
        }

        case OP_RETURN_R: {
            uint8_t reg = chunk->code[offset + 1];
            printf("%-16s R%d\n", "RETURN", reg);
            return offset + 2;
        }

        // Short jump optimizations
        case OP_JUMP_SHORT: {
            uint8_t offset_val = chunk->code[offset + 1];
            printf("%-16s +%d\n", "JUMP_SHORT", offset_val);
            return offset + 2;
        }

        case OP_JUMP_BACK_SHORT: {
            uint8_t offset_val = chunk->code[offset + 1];
            printf("%-16s -%d\n", "JUMP_BACK_SHORT", offset_val);
            return offset + 2;
        }

        case OP_JUMP_IF_NOT_SHORT: {
            uint8_t reg = chunk->code[offset + 1];
            uint8_t offset_val = chunk->code[offset + 2];
            printf("%-16s R%d, +%d\n", "JUMP_IF_NOT_SHORT", reg, offset_val);
            return offset + 3;
        }

        case OP_LOOP_SHORT: {
            uint8_t offset_val = chunk->code[offset + 1];
            printf("%-16s -%d\n", "LOOP_SHORT", offset_val);
            return offset + 2;
        }

        // Typed operations for performance
        case OP_ADD_I32_TYPED: {
            uint8_t dst = chunk->code[offset + 1];
            uint8_t left = chunk->code[offset + 2];
            uint8_t right = chunk->code[offset + 3];
            printf("%-16s R%d, R%d, R%d (typed)\n", "ADD_I32", dst, left, right);
            return offset + 4;
        }

        case OP_SUB_I32_TYPED: {
            uint8_t dst = chunk->code[offset + 1];
            uint8_t left = chunk->code[offset + 2];
            uint8_t right = chunk->code[offset + 3];
            printf("%-16s R%d, R%d, R%d (typed)\n", "SUB_I32", dst, left, right);
            return offset + 4;
        }

        case OP_MUL_I32_TYPED: {
            uint8_t dst = chunk->code[offset + 1];
            uint8_t left = chunk->code[offset + 2];
            uint8_t right = chunk->code[offset + 3];
            printf("%-16s R%d, R%d, R%d (typed)\n", "MUL_I32", dst, left, right);
            return offset + 4;
        }

        case OP_LOAD_I32_CONST: {
            uint8_t reg = chunk->code[offset + 1];
            uint16_t constant = (uint16_t)((chunk->code[offset + 2] << 8) | chunk->code[offset + 3]);
            printf("%-16s R%d, #%d (typed)\n", "LOAD_I32_CONST", reg, constant);
            return offset + 4;
        }

        case OP_MOVE_I32: {
            uint8_t dst = chunk->code[offset + 1];
            uint8_t src = chunk->code[offset + 2];
            printf("%-16s R%d, R%d (typed)\n", "MOVE_I32", dst, src);
            return offset + 3;
        }

        case OP_TIME_STAMP: {
            uint8_t dst = chunk->code[offset + 1];
            printf("%-16s R%d\n", "TIME_STAMP", dst);
            return offset + 2;
        }

        case OP_RETURN_VOID:
            printf("%-16s\n", "RETURN_VOID");
            return offset + 1;

        case OP_HALT:
            printf("%-16s\n", "HALT");
            return offset + 1;

        case OP_LOAD_GLOBAL: {
            uint8_t reg = chunk->code[offset + 1];
            uint8_t global = chunk->code[offset + 2];
            printf("%-16s R%d, #%d\n", "LOAD_GLOBAL", reg, global);
            return offset + 3;
        }

        case OP_STORE_GLOBAL: {
            uint8_t global = chunk->code[offset + 1];
            uint8_t reg = chunk->code[offset + 2];
            printf("%-16s #%d, R%d\n", "STORE_GLOBAL", global, reg);
            return offset + 3;
        }

        case OP_JUMP_IF_NOT_R: {
            uint8_t reg = chunk->code[offset + 1];
            uint16_t jump = (uint16_t)((chunk->code[offset + 2] << 8) | chunk->code[offset + 3]);
            printf("%-16s R%d, +%d\n", "JUMP_IF_NOT_R", reg, jump);
            return offset + 4;
        }

        case OP_LOOP: {
            uint16_t jump = (uint16_t)((chunk->code[offset + 1] << 8) | chunk->code[offset + 2]);
            printf("%-16s -%d\n", "LOOP", jump);
            return offset + 3;
        }

        case OP_TRY_BEGIN: {
            uint8_t reg = chunk->code[offset + 1];
            uint16_t jump = (uint16_t)((chunk->code[offset + 2] << 8) | chunk->code[offset + 3]);
            if (reg == 0xFF) {
                printf("%-16s catch=<none>, +%d\n", "TRY_BEGIN", jump);
            } else {
                printf("%-16s catch=R%u, +%d\n", "TRY_BEGIN", reg, jump);
            }
            return offset + 4;
        }

        case OP_TRY_END:
            printf("%-16s\n", "TRY_END");
            return offset + 1;

        case OP_THROW: {
            uint8_t reg = chunk->code[offset + 1];
            printf("%-16s R%u\n", "THROW", reg);
            return offset + 2;
        }

        case OP_JUMP: {
            uint16_t jump = (uint16_t)((chunk->code[offset + 1] << 8) | chunk->code[offset + 2]);
            printf("%-16s +%d\n", "JUMP", jump);
            return offset + 3;
        }

        default:
            printf("Unknown opcode %d\n", instruction);
            return offset + 1;
    }
}

void dumpProfile(void) {
    printf("=== VM Instruction Profile ===\n");
    for (int i = 0; i < VM_DISPATCH_TABLE_SIZE; i++) {
        if (vm.profile.instruction_counts[i] > 0) {
            printf("%3d: %llu\n", i,
                   (unsigned long long)vm.profile.instruction_counts[i]);
        }
    }
}
