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
            uint8_t constant = chunk->code[offset + 2];
            printf("%-16s R%d, #%d '", "LOAD_CONST", reg, constant);
            printValue(chunk->constants.values[constant]);
            printf("'\n");
            return offset + 3;
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

        case OP_HALT:
            printf("%-16s\n", "HALT");
            return offset + 1;

        default:
            printf("Unknown opcode %d\n", instruction);
            return offset + 1;
    }
}
