#ifndef VM_CONFIG_H
#define VM_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

// VM Configuration structure to abstract VM characteristics
typedef struct {
    int registerCount;           // Number of registers (e.g., 256)
    int cacheLineSize;          // Cache line size (e.g., 64)
    int preferredWorkingSet;    // Preferred number of active registers
    bool supportsComputedGoto;  // Whether computed-goto is supported
    int maxConstantPoolSize;    // Maximum constant pool size
    int maxCallFrames;          // Maximum call frame depth
    int stackInitCapacity;      // Initial stack capacity
    int maxNatives;             // Maximum native functions
} VMConfig;

// Centralized opcode definitions
typedef enum {
    // Arithmetic operations
    OPCODE_ADD_I32_R = 0x10,
    OPCODE_SUB_I32_R = 0x11,
    OPCODE_MUL_I32_R = 0x12,
    OPCODE_DIV_I32_R = 0x13,
    OPCODE_MOD_I32_R = 0x14,
    
    // Comparison operations
    OPCODE_EQ_I32_R = 0x20,
    OPCODE_NE_I32_R = 0x21,
    OPCODE_LT_I32_R = 0x22,
    OPCODE_LE_I32_R = 0x23,
    OPCODE_GT_I32_R = 0x24,
    OPCODE_GE_I32_R = 0x25,
    
    // Type conversion operations
    OPCODE_TO_STRING_R = 0x30,
    OPCODE_TO_I32_R = 0x31,
    OPCODE_TO_I64_R = 0x32,
    OPCODE_TO_F64_R = 0x33,
    OPCODE_TO_BOOL_R = 0x34,
    
    // Memory operations
    OPCODE_LOAD_CONST = 0x40,
    OPCODE_LOAD_CONST_EXT = 0x41,
    OPCODE_MOVE_R = 0x42,
    OPCODE_STORE_LOCAL = 0x43,
    OPCODE_LOAD_LOCAL = 0x44,
    
    // Control flow operations
    OPCODE_JUMP = 0x50,
    OPCODE_JUMP_IF_FALSE_R = 0x51,
    OPCODE_JUMP_IF_TRUE_R = 0x52,
    OPCODE_CALL = 0x53,
    OPCODE_RETURN = 0x54,
    
    // Advanced operations
    OPCODE_CLOSURE = 0x60,
    OPCODE_GET_UPVALUE = 0x61,
    OPCODE_SET_UPVALUE = 0x62,
    OPCODE_CLOSE_UPVALUE = 0x63,
    
    // Array operations
    OPCODE_NEW_ARRAY = 0x70,
    OPCODE_GET_INDEX = 0x71,
    OPCODE_SET_INDEX = 0x72,
    OPCODE_ARRAY_LENGTH = 0x73,
    
    // String operations
    OPCODE_CONCAT_STRING = 0x80,
    OPCODE_STRING_LENGTH = 0x81,
    OPCODE_STRING_SLICE = 0x82,
    
    // System operations
    OPCODE_PRINT = 0x90,
    OPCODE_HALT = 0xFF
} Opcode;

// Global VM configuration instance
extern VMConfig g_vmConfig;

// VM configuration functions
void initVMConfig(VMConfig* config);
void initDefaultVMConfig(void);
VMConfig* getVMConfig(void);

// Opcode utility functions
const char* opcodeToString(Opcode opcode);
bool isArithmeticOpcode(Opcode opcode);
bool isComparisonOpcode(Opcode opcode);
bool isControlFlowOpcode(Opcode opcode);

// VM capability checks
bool vmSupportsExtendedConstants(void);
bool vmSupportsComputedGoto(void);
int vmGetRegisterCount(void);
int vmGetMaxConstantPoolSize(void);

#endif // VM_CONFIG_H