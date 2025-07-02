// debug.c - Debug utilities for the VM
#include "../../include/debug.h"
#include "../../include/common.h"
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
            printValue(chunk->constants.values[constant]);
            printf("'\n");
            return offset + 4;
        }

        case OP_LOAD_NIL: {
            uint8_t reg = chunk->code[offset + 1];
            printf("%-16s R%d\n", "LOAD_NIL", reg);
            return offset + 2;
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

        case OP_PRINT_R: {
            uint8_t reg = chunk->code[offset + 1];
            printf("%-16s R%d\n", "PRINT", reg);
            return offset + 2;
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

        case OP_LOOP_GUARD_INIT: {
            uint8_t reg = chunk->code[offset + 1];
            uint32_t maxIterations = chunk->code[offset + 2] | 
                                   (chunk->code[offset + 3] << 8) | 
                                   (chunk->code[offset + 4] << 16) | 
                                   (chunk->code[offset + 5] << 24);
            printf("%-16s R%d, %u\n", "LOOP_GUARD_INIT", reg, maxIterations);
            return offset + 6;
        }

        case OP_LOOP_GUARD_CHECK: {
            uint8_t reg = chunk->code[offset + 1];
            printf("%-16s R%d\n", "LOOP_GUARD_CHECK", reg);
            return offset + 2;
        }

        case OP_HALT:
            printf("%-16s\n", "HALT");
            return offset + 1;

        default:
            printf("Unknown opcode %d\n", instruction);
            return offset + 1;
    }
}
