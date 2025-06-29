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

static int constantInstruction(const char* name, Chunk* chunk, int offset) {
    uint8_t reg = chunk->code[offset + 1];
    uint8_t constant = chunk->code[offset + 2];
    printf("%-16s %4d -> R%d\n", name, constant, reg);
    return offset + 3;
}

static int registerInstruction(const char* name, Chunk* chunk, int offset) {
    uint8_t reg = chunk->code[offset + 1];
    printf("%-16s R%d\n", name, reg);
    return offset + 2;
}

static int registerBinaryInstruction(const char* name, Chunk* chunk, int offset) {
    uint8_t dst = chunk->code[offset + 1];
    uint8_t src1 = chunk->code[offset + 2];
    uint8_t src2 = chunk->code[offset + 3];
    printf("%-16s R%d = R%d, R%d\n", name, dst, src1, src2);
    return offset + 4;
}

static int simpleInstruction(const char* name, int offset) {
    printf("%s\n", name);
    return offset + 1;
}

int disassembleInstruction(Chunk* chunk, int offset) {
    printf("%04d ", offset);
    
    uint8_t instruction = chunk->code[offset];
    switch (instruction) {
        case OP_LOAD_CONST:
            return constantInstruction("OP_LOAD_CONST", chunk, offset);
        case OP_LOAD_NIL:
            return registerInstruction("OP_LOAD_NIL", chunk, offset);
        case OP_LOAD_TRUE:
            return registerInstruction("OP_LOAD_TRUE", chunk, offset);
        case OP_LOAD_FALSE:
            return registerInstruction("OP_LOAD_FALSE", chunk, offset);
        case OP_ADD_I32_R:
            return registerBinaryInstruction("OP_ADD_I32_R", chunk, offset);
        case OP_SUB_I32_R:
            return registerBinaryInstruction("OP_SUB_I32_R", chunk, offset);
        case OP_MUL_I32_R:
            return registerBinaryInstruction("OP_MUL_I32_R", chunk, offset);
        case OP_DIV_I32_R:
            return registerBinaryInstruction("OP_DIV_I32_R", chunk, offset);
        case OP_PRINT_R:
            return registerInstruction("OP_PRINT_R", chunk, offset);
        case OP_PRINT_NO_NL_R:
            return registerInstruction("OP_PRINT_NO_NL_R", chunk, offset);
        case OP_HALT:
            return simpleInstruction("OP_HALT", offset);
        default:
            printf("Unknown opcode %d\n", instruction);
            return offset + 1;
    }
}