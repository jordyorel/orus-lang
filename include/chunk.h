#ifndef clox_chunk_h
#define clox_chunk_h

#include "common.h"
#include "value.h"

/// Enum representing the possible bytecode operations.
///
/// This enum defines the operations supported in the bytecode,
/// each corresponding to a unique byte value. The values represent
/// the different operations that can be performed in the virtual machine.
typedef enum {
    // op constant
    OP_CONSTANT,
    OP_CONSTANT_LONG,
    OP_I64_CONST,

    // Integer (i32) operations
    OP_ADD_I32,
    OP_SUBTRACT_I32,
    OP_MULTIPLY_I32,
    OP_DIVIDE_I32,
    OP_NEGATE_I32,

    // 64-bit integer operations
    OP_ADD_I64,
    OP_SUBTRACT_I64,
    OP_MULTIPLY_I64,
    OP_DIVIDE_I64,
    OP_NEGATE_I64,

    // Unsigned integer (u32) operations
    OP_ADD_U32,
    OP_SUBTRACT_U32,
    OP_MULTIPLY_U32,
    OP_DIVIDE_U32,
    OP_NEGATE_U32,

    // Unsigned 64-bit integer operations
    OP_ADD_U64,
    OP_SUBTRACT_U64,
    OP_MULTIPLY_U64,
    OP_DIVIDE_U64,
    OP_NEGATE_U64,

    // Floating point (f64) operations
    OP_ADD_F64,
    OP_SUBTRACT_F64,
    OP_MULTIPLY_F64,
    OP_DIVIDE_F64,
    OP_NEGATE_F64,

    // Generic numeric operations
    OP_ADD_NUMERIC,
    OP_SUBTRACT_NUMERIC,
    OP_MULTIPLY_NUMERIC,
    OP_DIVIDE_NUMERIC,
    OP_NEGATE_NUMERIC,
    OP_MODULO_NUMERIC,

    OP_MODULO_I32,
    OP_MODULO_I64,
    OP_MODULO_U32,
    OP_MODULO_U64,

    // Bitwise operations
    OP_BIT_AND_I32,
    OP_BIT_AND_I64,
    OP_BIT_AND_U32,
    OP_BIT_AND_U64,
    OP_BIT_OR_I32,
    OP_BIT_OR_I64,
    OP_BIT_OR_U32,
    OP_BIT_OR_U64,
    OP_BIT_XOR_I32,
    OP_BIT_XOR_I64,
    OP_BIT_XOR_U32,
    OP_BIT_XOR_U64,
    OP_BIT_NOT_I32,
    OP_BIT_NOT_I64,
    OP_BIT_NOT_U32,
    OP_BIT_NOT_U64,
    OP_SHIFT_LEFT_I32,
    OP_SHIFT_LEFT_I64,
    OP_SHIFT_LEFT_U32,
    OP_SHIFT_LEFT_U64,
    OP_SHIFT_RIGHT_I32,
    OP_SHIFT_RIGHT_I64,
    OP_SHIFT_RIGHT_U32,
    OP_SHIFT_RIGHT_U64,

    // Comparison operations
    OP_EQUAL,
    OP_NOT_EQUAL,
    OP_EQUAL_I64,
    OP_NOT_EQUAL_I64,
    OP_LESS_I32,
    OP_LESS_I64,
    OP_LESS_U32,
    OP_LESS_U64,
    OP_LESS_F64,
    OP_LESS_EQUAL_I32,
    OP_LESS_EQUAL_I64,
    OP_LESS_EQUAL_U32,
    OP_LESS_EQUAL_U64,
    OP_LESS_EQUAL_F64,
    OP_GREATER_I32,
    OP_GREATER_I64,
    OP_GREATER_U32,
    OP_GREATER_U64,
    OP_GREATER_F64,
    OP_GREATER_EQUAL_I32,
    OP_GREATER_EQUAL_I64,
    OP_GREATER_EQUAL_U32,
    OP_GREATER_EQUAL_U64,
    OP_GREATER_EQUAL_F64,

    // Generic comparisons
    OP_LESS_GENERIC,
    OP_LESS_EQUAL_GENERIC,
    OP_GREATER_GENERIC,
    OP_GREATER_EQUAL_GENERIC,

    // Generic arithmetic
    OP_ADD_GENERIC,
    OP_SUBTRACT_GENERIC,
    OP_MULTIPLY_GENERIC,
    OP_DIVIDE_GENERIC,
    OP_MODULO_GENERIC,
    OP_NEGATE_GENERIC,

    // Type conversion opcodes
    OP_I32_TO_F64,
    OP_U32_TO_F64,
    OP_I32_TO_U32,
    OP_U32_TO_I32,
    OP_I32_TO_I64,
    OP_U32_TO_I64,
    OP_I64_TO_I32,
    OP_I64_TO_U32,
    OP_I32_TO_U64,
    OP_U32_TO_U64,
    OP_U64_TO_I32,
    OP_U64_TO_U32,
    OP_U64_TO_F64,
    OP_F64_TO_U64,
    OP_F64_TO_I32,
    OP_F64_TO_U32,
    OP_I64_TO_U64,
    OP_U64_TO_I64,
    OP_I64_TO_F64,
    OP_F64_TO_I64,
    OP_I32_TO_BOOL,
    OP_U32_TO_BOOL,
    OP_I64_TO_BOOL,
    OP_U64_TO_BOOL,
    OP_BOOL_TO_I32,
    OP_BOOL_TO_U32,
    OP_BOOL_TO_I64,
    OP_BOOL_TO_U64,
    OP_BOOL_TO_F64,
    OP_F64_TO_BOOL,
    OP_I64_TO_STRING,
    OP_U64_TO_STRING,
    OP_I32_TO_STRING,
    OP_U32_TO_STRING,
    OP_F64_TO_STRING,
    OP_BOOL_TO_STRING,
    OP_ARRAY_TO_STRING,
    OP_CONCAT,

    // Logical operators
    OP_AND,
    OP_OR,
    OP_NOT,

    // Control flow opcodes
    OP_JUMP,
    OP_JUMP_IF_FALSE,
    OP_JUMP_IF_TRUE,
    OP_LOOP,         // Jump backward (for loops)
    OP_BREAK,        // Break out of a loop
    OP_CONTINUE,     // Continue to the next iteration of a loop

    // Exception handling
    OP_SETUP_EXCEPT,
    OP_POP_EXCEPT,

    // Function opcodes
    OP_CALL,
    OP_CALL_NATIVE,
    OP_RETURN,

    OP_POP,
    OP_PRINT,
    OP_PRINT_NO_NL,
    OP_PRINT_I32,
    OP_PRINT_I32_NO_NL,
    OP_PRINT_I64,
    OP_PRINT_I64_NO_NL,
    OP_PRINT_U32,
    OP_PRINT_U32_NO_NL,
    OP_PRINT_U64,
    OP_PRINT_U64_NO_NL,
    OP_PRINT_F64,
    OP_PRINT_F64_NO_NL,
    OP_PRINT_BOOL,
    OP_PRINT_BOOL_NO_NL,
    OP_PRINT_STRING,
    OP_PRINT_STRING_NO_NL,
    OP_FORMAT_PRINT, // New opcode for string interpolation
    OP_FORMAT_PRINT_NO_NL,
    OP_DEFINE_GLOBAL,
    OP_GET_GLOBAL,
    OP_SET_GLOBAL,
    OP_IMPORT,
    OP_NIL,
    OP_MAKE_ARRAY,
    OP_ARRAY_GET,
    OP_ARRAY_SET,
    OP_ARRAY_PUSH,
    OP_ARRAY_POP,
    OP_ARRAY_RESERVE,
    OP_ARRAY_FILL,
    OP_LEN,
    OP_LEN_ARRAY,
    OP_LEN_STRING,
    OP_SUBSTRING,
    OP_SLICE,

    // Typed type_of opcodes
    OP_TYPE_OF_I32,
    OP_TYPE_OF_I64,
    OP_TYPE_OF_U32,
    OP_TYPE_OF_U64,
    OP_TYPE_OF_F64,
    OP_TYPE_OF_BOOL,
    OP_TYPE_OF_STRING,
    OP_TYPE_OF_ARRAY,
    OP_INC_I64,
    OP_JUMP_IF_LT_I64,
    OP_ITER_NEXT_I64,
    OP_GC_PAUSE,
    OP_GC_RESUME,
} opCode;

typedef struct {
    int line;
    int column;
    int run_length;
} LineInfo;

// Dynamic array of bytecode:
// count: Number of used entries
// capacity: Number of entries
// uint8_t: dynamic array of bytecode
// Lines: dynamic array of lines
// ValueArray: dynamic array of values
typedef struct {
    int count; // 4 bytes
    int capacity; // 4 bytes
    uint8_t* code; // 8 bytes : instruction
    LineInfo* line_info;
    int line_count; // 8 bytes
    int line_capcity;
    ValueArray constants; // 16 bytes
} Chunk; // 40 bytes


void initChunk(Chunk* chunk);
void freeChunk(Chunk* chunk);
void writeChunk(Chunk* chunk, uint8_t byte, int line, int column);
int addConstant(Chunk* Chunk, Value value);
void writeConstant(Chunk* chunk, Value value, int line, int column);
int len(Chunk* chunk);
int get_line(Chunk* chunk, int offset);
int get_column(Chunk* chunk, int offset);
uint8_t get_code(Chunk* chunk, int offset);
Value get_constant(Chunk* chunk, int offset);


#endif
