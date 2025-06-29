/**
 * @file debug.c
 * @brief Bytecode disassembly helpers.
 */
#include <stdio.h>

#include "../../include/debug.h"
#include "../../include/value.h"

// Forward declarations for all static functions
static int constantInstruction(const char* name, Chunk* chunk, int offset);
static int simpleInstruction(const char* name, int offset);
static int byteInstruction(const char* name, Chunk* chunk, int offset);
static int jumpInstruction(const char* name, int sign, Chunk* chunk, int offset);

/**
 * Print a human readable disassembly of a chunk.
 *
 * @param chunk Chunk to disassemble.
 * @param name  Label printed at the top of the output.
 */
void disassembleChunk(Chunk* chunk, const char* name) {
    printf("== %s ==\n", name);

    for (int offset = 0; offset < len(chunk);) {
        offset = disassembleInstruction(chunk, offset);
    }
}

// Handles the disassembly of instructions that have a single operand (a
// constant). Parameters:
//   name - The name of the opcode to print (e.g., "OP_CONSTANT").
//   chunk - Pointer to the Chunk containing the bytecode instructions.
//   offset - The current index of the instruction.
// Returns:
//   The offset of the next instruction (current offset + 2).
/**
 * Disassemble an instruction that has a constant operand.
 *
 * @param name   Opcode name.
 * @param chunk  Chunk containing the instruction.
 * @param offset Offset into the chunk.
 * @return       Offset of the next instruction.
 */
static int constantInstruction(const char* name, Chunk* chunk, int offset) {
    uint8_t constant = get_code(chunk, offset);
    printf("%-16s %4d '", name, constant);
    printValue(get_constant(chunk, offset));
    printf("'\n");
    return offset + 2;
}

// Disassembles a single instruction at a specific offset in the chunk.
// Parameters:
//   chunk - Pointer to the Chunk containing the bytecode instructions.
//   offset - The current index of the instruction to disassemble.
// Returns:
//   The offset of the next instruction after the one being disassembled.
/**
 * Disassemble a single instruction from a chunk.
 *
 * @param chunk  Chunk containing the code.
 * @param offset Offset of the instruction.
 * @return       Offset of the next instruction.
 */
int disassembleInstruction(Chunk* chunk, int offset) {
    printf("%04d ", offset);

    // Use get_line function instead of directly accessing lines
    int current_line = get_line(chunk, offset);
    int previous_line = (offset > 0) ? get_line(chunk, offset - 1) : -1;

    if (offset > 0 && current_line == previous_line) {
        printf("   | ");
    } else {
        printf("%4d ", current_line);
    }

    uint8_t instruction = chunk->code[offset];
    switch (instruction) {
        case OP_CONSTANT:
            return constantInstruction("OP_CONSTANT", chunk, offset);
        case OP_CONSTANT_LONG:
            return constantInstruction("OP_CONSTANT_LONG", chunk, offset);
        case OP_I64_CONST:
            return constantInstruction("OP_I64_CONST", chunk, offset);

        // Integer operations
        case OP_ADD_I32:
            return simpleInstruction("OP_ADD_I32", offset);
        case OP_SUBTRACT_I32:
            return simpleInstruction("OP_SUBTRACT_I32", offset);
        case OP_MULTIPLY_I32:
            return simpleInstruction("OP_MULTIPLY_I32", offset);
        case OP_DIVIDE_I32:
            return simpleInstruction("OP_DIVIDE_I32", offset);
        case OP_NEGATE_I32:
            return simpleInstruction("OP_NEGATE_I32", offset);

        case OP_ADD_I64:
            return simpleInstruction("OP_ADD_I64", offset);
        case OP_SUBTRACT_I64:
            return simpleInstruction("OP_SUBTRACT_I64", offset);
        case OP_MULTIPLY_I64:
            return simpleInstruction("OP_MULTIPLY_I64", offset);
        case OP_DIVIDE_I64:
            return simpleInstruction("OP_DIVIDE_I64", offset);
        case OP_NEGATE_I64:
            return simpleInstruction("OP_NEGATE_I64", offset);
        case OP_INC_I64:
            return simpleInstruction("OP_INC_I64", offset);
        case OP_ITER_NEXT_I64:
            return simpleInstruction("OP_ITER_NEXT_I64", offset);
        case OP_GC_PAUSE:
            return simpleInstruction("OP_GC_PAUSE", offset);
        case OP_GC_RESUME:
            return simpleInstruction("OP_GC_RESUME", offset);

        // Unsigned integer operations
        case OP_ADD_U32:
            return simpleInstruction("OP_ADD_U32", offset);
        case OP_SUBTRACT_U32:
            return simpleInstruction("OP_SUBTRACT_U32", offset);
        case OP_MULTIPLY_U32:
            return simpleInstruction("OP_MULTIPLY_U32", offset);
        case OP_DIVIDE_U32:
            return simpleInstruction("OP_DIVIDE_U32", offset);
        case OP_NEGATE_U32:
            return simpleInstruction("OP_NEGATE_U32", offset);
        case OP_ADD_U64:
            return simpleInstruction("OP_ADD_U64", offset);
        case OP_SUBTRACT_U64:
            return simpleInstruction("OP_SUBTRACT_U64", offset);
        case OP_MULTIPLY_U64:
            return simpleInstruction("OP_MULTIPLY_U64", offset);
        case OP_DIVIDE_U64:
            return simpleInstruction("OP_DIVIDE_U64", offset);
        case OP_NEGATE_U64:
            return simpleInstruction("OP_NEGATE_U64", offset);

        // Floating point operations
        case OP_ADD_F64:
            return simpleInstruction("OP_ADD_F64", offset);
        case OP_SUBTRACT_F64:
            return simpleInstruction("OP_SUBTRACT_F64", offset);
        case OP_MULTIPLY_F64:
            return simpleInstruction("OP_MULTIPLY_F64", offset);
        case OP_DIVIDE_F64:
            return simpleInstruction("OP_DIVIDE_F64", offset);
        case OP_NEGATE_F64:
            return simpleInstruction("OP_NEGATE_F64", offset);
        case OP_ADD_NUMERIC:
            return simpleInstruction("OP_ADD_NUMERIC", offset);
        case OP_SUBTRACT_NUMERIC:
            return simpleInstruction("OP_SUBTRACT_NUMERIC", offset);
        case OP_MULTIPLY_NUMERIC:
            return simpleInstruction("OP_MULTIPLY_NUMERIC", offset);
        case OP_DIVIDE_NUMERIC:
            return simpleInstruction("OP_DIVIDE_NUMERIC", offset);
        case OP_NEGATE_NUMERIC:
            return simpleInstruction("OP_NEGATE_NUMERIC", offset);
        case OP_MODULO_NUMERIC:
            return simpleInstruction("OP_MODULO_NUMERIC", offset);
        case OP_MODULO_I32:
            return simpleInstruction("OP_MODULO_I32", offset);
        case OP_MODULO_I64:
            return simpleInstruction("OP_MODULO_I64", offset);
        case OP_MODULO_U32:
            return simpleInstruction("OP_MODULO_U32", offset);

        // Comparison operations
        case OP_EQUAL:
            return simpleInstruction("OP_EQUAL", offset);
        case OP_NOT_EQUAL:
            return simpleInstruction("OP_NOT_EQUAL", offset);
        case OP_EQUAL_I64:
            return simpleInstruction("OP_EQUAL_I64", offset);
        case OP_NOT_EQUAL_I64:
            return simpleInstruction("OP_NOT_EQUAL_I64", offset);
        case OP_LESS_I32:
            return simpleInstruction("OP_LESS_I32", offset);
        case OP_LESS_I64:
            return simpleInstruction("OP_LESS_I64", offset);
        case OP_LESS_U32:
            return simpleInstruction("OP_LESS_U32", offset);
        case OP_LESS_U64:
            return simpleInstruction("OP_LESS_U64", offset);
        case OP_LESS_F64:
            return simpleInstruction("OP_LESS_F64", offset);
        case OP_LESS_EQUAL_I32:
            return simpleInstruction("OP_LESS_EQUAL_I32", offset);
        case OP_LESS_EQUAL_I64:
            return simpleInstruction("OP_LESS_EQUAL_I64", offset);
        case OP_LESS_EQUAL_U32:
            return simpleInstruction("OP_LESS_EQUAL_U32", offset);
        case OP_LESS_EQUAL_U64:
            return simpleInstruction("OP_LESS_EQUAL_U64", offset);
        case OP_LESS_EQUAL_F64:
            return simpleInstruction("OP_LESS_EQUAL_F64", offset);
        case OP_GREATER_I32:
            return simpleInstruction("OP_GREATER_I32", offset);
        case OP_GREATER_I64:
            return simpleInstruction("OP_GREATER_I64", offset);
        case OP_GREATER_U32:
            return simpleInstruction("OP_GREATER_U32", offset);
        case OP_GREATER_U64:
            return simpleInstruction("OP_GREATER_U64", offset);
        case OP_GREATER_F64:
            return simpleInstruction("OP_GREATER_F64", offset);
        case OP_GREATER_EQUAL_I32:
            return simpleInstruction("OP_GREATER_EQUAL_I32", offset);
        case OP_GREATER_EQUAL_I64:
            return simpleInstruction("OP_GREATER_EQUAL_I64", offset);
        case OP_GREATER_EQUAL_U32:
            return simpleInstruction("OP_GREATER_EQUAL_U32", offset);
        case OP_GREATER_EQUAL_U64:
            return simpleInstruction("OP_GREATER_EQUAL_U64", offset);
        case OP_GREATER_EQUAL_F64:
            return simpleInstruction("OP_GREATER_EQUAL_F64", offset);
            
        // Logical operators
        case OP_AND:
            return simpleInstruction("OP_AND", offset);
        case OP_OR:
            return simpleInstruction("OP_OR", offset);
        case OP_NOT:
            return simpleInstruction("OP_NOT", offset);
            
        case OP_POP:
            return simpleInstruction("OP_POP", offset);
        case OP_PRINT:
            return simpleInstruction("OP_PRINT", offset);
        case OP_PRINT_NO_NL:
            return simpleInstruction("OP_PRINT_NO_NL", offset);
        case OP_PRINT_I32:
            return simpleInstruction("OP_PRINT_I32", offset);
        case OP_PRINT_I32_NO_NL:
            return simpleInstruction("OP_PRINT_I32_NO_NL", offset);
        case OP_PRINT_I64:
            return simpleInstruction("OP_PRINT_I64", offset);
        case OP_PRINT_I64_NO_NL:
            return simpleInstruction("OP_PRINT_I64_NO_NL", offset);
        case OP_PRINT_U32:
            return simpleInstruction("OP_PRINT_U32", offset);
        case OP_PRINT_U32_NO_NL:
            return simpleInstruction("OP_PRINT_U32_NO_NL", offset);
        case OP_PRINT_U64:
            return simpleInstruction("OP_PRINT_U64", offset);
        case OP_PRINT_U64_NO_NL:
            return simpleInstruction("OP_PRINT_U64_NO_NL", offset);
        case OP_PRINT_F64:
            return simpleInstruction("OP_PRINT_F64", offset);
        case OP_PRINT_F64_NO_NL:
            return simpleInstruction("OP_PRINT_F64_NO_NL", offset);
        case OP_PRINT_BOOL:
            return simpleInstruction("OP_PRINT_BOOL", offset);
        case OP_PRINT_BOOL_NO_NL:
            return simpleInstruction("OP_PRINT_BOOL_NO_NL", offset);
        case OP_PRINT_STRING:
            return simpleInstruction("OP_PRINT_STRING", offset);
        case OP_PRINT_STRING_NO_NL:
            return simpleInstruction("OP_PRINT_STRING_NO_NL", offset);
        case OP_FORMAT_PRINT:
            return simpleInstruction("OP_FORMAT_PRINT", offset);
        case OP_FORMAT_PRINT_NO_NL:
            return simpleInstruction("OP_FORMAT_PRINT_NO_NL", offset);
        case OP_RETURN:
            return simpleInstruction("OP_RETURN", offset);

        case OP_NIL:
            return simpleInstruction("OP_NIL", offset);

        case OP_MAKE_ARRAY:
            return byteInstruction("OP_MAKE_ARRAY", chunk, offset);
        case OP_ARRAY_GET:
            return simpleInstruction("OP_ARRAY_GET", offset);
        case OP_ARRAY_SET:
            return simpleInstruction("OP_ARRAY_SET", offset);
        case OP_ARRAY_PUSH:
            return simpleInstruction("OP_ARRAY_PUSH", offset);
        case OP_ARRAY_POP:
            return simpleInstruction("OP_ARRAY_POP", offset);
        case OP_ARRAY_RESERVE:
            return simpleInstruction("OP_ARRAY_RESERVE", offset);
        case OP_ARRAY_FILL:
            return byteInstruction("OP_ARRAY_FILL", chunk, offset);
        case OP_LEN:
            return simpleInstruction("OP_LEN", offset);
        case OP_LEN_ARRAY:
            return simpleInstruction("OP_LEN_ARRAY", offset);
        case OP_LEN_STRING:
            return simpleInstruction("OP_LEN_STRING", offset);
        case OP_SUBSTRING:
            return simpleInstruction("OP_SUBSTRING", offset);
        case OP_SLICE:
            return simpleInstruction("OP_SLICE", offset);
        case OP_TYPE_OF_I32:
            return simpleInstruction("OP_TYPE_OF_I32", offset);
        case OP_TYPE_OF_I64:
            return simpleInstruction("OP_TYPE_OF_I64", offset);
        case OP_TYPE_OF_U32:
            return simpleInstruction("OP_TYPE_OF_U32", offset);
        case OP_TYPE_OF_U64:
            return simpleInstruction("OP_TYPE_OF_U64", offset);
        case OP_TYPE_OF_F64:
            return simpleInstruction("OP_TYPE_OF_F64", offset);
        case OP_TYPE_OF_BOOL:
            return simpleInstruction("OP_TYPE_OF_BOOL", offset);
        case OP_TYPE_OF_STRING:
            return simpleInstruction("OP_TYPE_OF_STRING", offset);
        case OP_TYPE_OF_ARRAY:
            return simpleInstruction("OP_TYPE_OF_ARRAY", offset);

        case OP_CALL: {
            uint8_t functionIndex = chunk->code[offset + 1];
            uint8_t argCount = chunk->code[offset + 2];
            printf("%-16s %4d %4d\n", "OP_CALL", functionIndex, argCount);
            return offset + 3;
        }

        case OP_CALL_NATIVE: {
            uint8_t index = chunk->code[offset + 1];
            uint8_t argCount = chunk->code[offset + 2];
            printf("%-16s %4d %4d\n", "OP_CALL_NATIVE", index, argCount);
            return offset + 3;
        }

        case OP_I32_TO_F64:
            return simpleInstruction("OP_I32_TO_F64", offset);
        case OP_U32_TO_F64:
            return simpleInstruction("OP_U32_TO_F64", offset);
        case OP_I32_TO_U32:
            return simpleInstruction("OP_I32_TO_U32", offset);
        case OP_U32_TO_I32:
            return simpleInstruction("OP_U32_TO_I32", offset);
        case OP_I32_TO_I64:
            return simpleInstruction("OP_I32_TO_I64", offset);
        case OP_U32_TO_I64:
            return simpleInstruction("OP_U32_TO_I64", offset);
        case OP_I64_TO_I32:
            return simpleInstruction("OP_I64_TO_I32", offset);
        case OP_I64_TO_U32:
            return simpleInstruction("OP_I64_TO_U32", offset);
        case OP_I32_TO_U64:
            return simpleInstruction("OP_I32_TO_U64", offset);
        case OP_U32_TO_U64:
            return simpleInstruction("OP_U32_TO_U64", offset);
        case OP_U64_TO_I32:
            return simpleInstruction("OP_U64_TO_I32", offset);
        case OP_U64_TO_U32:
            return simpleInstruction("OP_U64_TO_U32", offset);
        case OP_U64_TO_F64:
            return simpleInstruction("OP_U64_TO_F64", offset);
        case OP_F64_TO_U64:
            return simpleInstruction("OP_F64_TO_U64", offset);
        case OP_F64_TO_I32:
            return simpleInstruction("OP_F64_TO_I32", offset);
        case OP_F64_TO_U32:
            return simpleInstruction("OP_F64_TO_U32", offset);
        case OP_I64_TO_U64:
            return simpleInstruction("OP_I64_TO_U64", offset);
        case OP_U64_TO_I64:
            return simpleInstruction("OP_U64_TO_I64", offset);
        case OP_I64_TO_F64:
            return simpleInstruction("OP_I64_TO_F64", offset);
        case OP_F64_TO_I64:
            return simpleInstruction("OP_F64_TO_I64", offset);
        case OP_I32_TO_BOOL:
            return simpleInstruction("OP_I32_TO_BOOL", offset);
        case OP_U32_TO_BOOL:
            return simpleInstruction("OP_U32_TO_BOOL", offset);
        case OP_I64_TO_BOOL:
            return simpleInstruction("OP_I64_TO_BOOL", offset);
        case OP_U64_TO_BOOL:
            return simpleInstruction("OP_U64_TO_BOOL", offset);
        case OP_BOOL_TO_I32:
            return simpleInstruction("OP_BOOL_TO_I32", offset);
        case OP_BOOL_TO_U32:
            return simpleInstruction("OP_BOOL_TO_U32", offset);
        case OP_BOOL_TO_I64:
            return simpleInstruction("OP_BOOL_TO_I64", offset);
        case OP_BOOL_TO_U64:
            return simpleInstruction("OP_BOOL_TO_U64", offset);
        case OP_BOOL_TO_F64:
            return simpleInstruction("OP_BOOL_TO_F64", offset);
        case OP_F64_TO_BOOL:
            return simpleInstruction("OP_F64_TO_BOOL", offset);
        case OP_I64_TO_STRING:
            return simpleInstruction("OP_I64_TO_STRING", offset);
        case OP_U64_TO_STRING:
            return simpleInstruction("OP_U64_TO_STRING", offset);
        case OP_I32_TO_STRING:
            return simpleInstruction("OP_I32_TO_STRING", offset);
        case OP_U32_TO_STRING:
            return simpleInstruction("OP_U32_TO_STRING", offset);
        case OP_F64_TO_STRING:
            return simpleInstruction("OP_F64_TO_STRING", offset);
        case OP_BOOL_TO_STRING:
            return simpleInstruction("OP_BOOL_TO_STRING", offset);
        case OP_ARRAY_TO_STRING:
            return simpleInstruction("OP_ARRAY_TO_STRING", offset);
        case OP_CONCAT:
            return simpleInstruction("OP_CONCAT", offset);

        case OP_DEFINE_GLOBAL:
            return byteInstruction("OP_DEFINE_GLOBAL", chunk, offset);
        case OP_GET_GLOBAL:
            return byteInstruction("OP_GET_GLOBAL", chunk, offset);
        case OP_SET_GLOBAL:
            return byteInstruction("OP_SET_GLOBAL", chunk, offset);

        case OP_JUMP:
            return jumpInstruction("OP_JUMP", 1, chunk, offset);
        case OP_JUMP_IF_FALSE:
            return jumpInstruction("OP_JUMP_IF_FALSE", 1, chunk, offset);
        case OP_JUMP_IF_TRUE:
            return jumpInstruction("OP_JUMP_IF_TRUE", 1, chunk, offset);
        case OP_JUMP_IF_LT_I64:
            return jumpInstruction("OP_JUMP_IF_LT_I64", 1, chunk, offset);
        case OP_LOOP:
            return jumpInstruction("OP_LOOP", -1, chunk, offset);
        case OP_BREAK:
            return simpleInstruction("OP_BREAK", offset);
        case OP_CONTINUE:
            return simpleInstruction("OP_CONTINUE", offset);

        default:
            printf("Unknown opcode %d\n", instruction);
            return offset + 1;
    }
}

// Handles the disassembly of simple 1-byte instructions (no operands).
// Parameters:
//   name - The name of the opcode to print (e.g., "OP_ADD").
//   offset - The current index of the instruction.
// Returns:
//   The offset of the next instruction (current offset + 1).
/**
 * Print a one-byte instruction with no operands.
 *
 * @param name   Opcode name.
 * @param offset Current offset.
 * @return       Offset of the next instruction.
 */
static int simpleInstruction(const char* name, int offset) {
    printf("%s \n", name);
    return offset + 1;
}

// Implementation of byteInstruction
/**
 * Disassemble an instruction with a single byte operand.
 *
 * @param name   Opcode name.
 * @param chunk  Chunk containing the code.
 * @param offset Offset of the instruction.
 * @return       Offset of the next instruction.
 */
static int byteInstruction(const char* name, Chunk* chunk, int offset) {
    uint8_t slot = chunk->code[offset + 1];
    printf("%-16s %4d\n", name, slot);
    return offset + 2;  // Skip the opcode and the byte operand
}

// Implementation of jumpInstruction
/**
 * Disassemble a jump instruction which uses a 16-bit offset.
 *
 * @param name   Opcode name.
 * @param sign   Direction of the jump (+1 or -1).
 * @param chunk  Chunk containing the code.
 * @param offset Offset of the instruction.
 * @return       Offset of the next instruction.
 */
static int jumpInstruction(const char* name, int sign, Chunk* chunk, int offset) {
    uint16_t jump = (uint16_t)((chunk->code[offset + 1] << 8) | chunk->code[offset + 2]);
    printf("%-16s %4d -> %d\n", name, offset, offset + 3 + sign * jump);
    return offset + 3;  // Skip the opcode and the 2-byte operand
}
