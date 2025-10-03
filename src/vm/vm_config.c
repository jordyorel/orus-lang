// Orus Language Project
// ---------------------------------------------------------------------------
// File: src/vm/vm_config.c
// Author: Jordy Orel KONDA
// Copyright (c) 2023 Jordy Orel KONDA
// License: MIT License (see LICENSE file in the project root)
// Description: Implements configuration handling for VM runtime settings.
 

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vm/vm_config.h"
#include "vm/vm_constants.h"

// Global VM configuration instance
VMConfig g_vmConfig = {0};

// Initialize the global configuration on first use
static bool g_vmConfigInitialized = false;

// Initialize VM configuration with default values
void initVMConfig(VMConfig* config) {
    if (!config) return;
    
    // Set default VM characteristics based on current implementation
    config->registerCount = VM_MAX_REGISTERS;           // From vm_constants.h
    config->cacheLineSize = 64;                         // Standard cache line size
    config->preferredWorkingSet = 128;                  // Balanced working set
    config->supportsComputedGoto = true;               // Modern compiler support
    config->maxConstantPoolSize = 65536;               // 16-bit constant addressing
    config->maxCallFrames = VM_MAX_CALL_FRAMES;        // From vm_constants.h
    config->stackInitCapacity = 256;                   // Initial stack size
    config->maxNatives = 256;                          // Maximum native functions
}

// Initialize the global VM configuration
void initDefaultVMConfig(void) {
    initVMConfig(&g_vmConfig);
}

// Get the global VM configuration
VMConfig* getVMConfig(void) {
    if (!g_vmConfigInitialized) {
        initVMConfig(&g_vmConfig);
        g_vmConfigInitialized = true;
    }
    return &g_vmConfig;
}

// Convert opcode to string for debugging
const char* opcodeToString(Opcode opcode) {
    switch (opcode) {
        // Arithmetic operations
        case OPCODE_ADD_I32_R: return "ADD_I32_R";
        case OPCODE_SUB_I32_R: return "SUB_I32_R";
        case OPCODE_MUL_I32_R: return "MUL_I32_R";
        case OPCODE_DIV_I32_R: return "DIV_I32_R";
        case OPCODE_MOD_I32_R: return "MOD_I32_R";
        
        // Comparison operations
        case OPCODE_EQ_I32_R: return "EQ_I32_R";
        case OPCODE_NE_I32_R: return "NE_I32_R";
        case OPCODE_LT_I32_R: return "LT_I32_R";
        case OPCODE_LE_I32_R: return "LE_I32_R";
        case OPCODE_GT_I32_R: return "GT_I32_R";
        case OPCODE_GE_I32_R: return "GE_I32_R";
        
        // Type conversion operations
        case OPCODE_TO_STRING_R: return "TO_STRING_R";
        case OPCODE_TO_I32_R: return "TO_I32_R";
        case OPCODE_TO_I64_R: return "TO_I64_R";
        case OPCODE_TO_F64_R: return "TO_F64_R";
        case OPCODE_TO_BOOL_R: return "TO_BOOL_R";
        
        // Memory operations
        case OPCODE_LOAD_CONST: return "LOAD_CONST";
        case OPCODE_LOAD_CONST_EXT: return "LOAD_CONST_EXT";
        case OPCODE_MOVE_R: return "MOVE_R";
        case OPCODE_STORE_LOCAL: return "STORE_LOCAL";
        case OPCODE_LOAD_LOCAL: return "LOAD_LOCAL";
        
        // Control flow operations
        case OPCODE_JUMP: return "JUMP";
        case OPCODE_JUMP_IF_FALSE_R: return "JUMP_IF_FALSE_R";
        case OPCODE_JUMP_IF_TRUE_R: return "JUMP_IF_TRUE_R";
        case OPCODE_CALL: return "CALL";
        case OPCODE_RETURN: return "RETURN";
        
        // Advanced operations
        case OPCODE_CLOSURE: return "CLOSURE";
        case OPCODE_GET_UPVALUE: return "GET_UPVALUE";
        case OPCODE_SET_UPVALUE: return "SET_UPVALUE";
        case OPCODE_CLOSE_UPVALUE: return "CLOSE_UPVALUE";
        
        // Array operations
        case OPCODE_NEW_ARRAY: return "NEW_ARRAY";
        case OPCODE_GET_INDEX: return "GET_INDEX";
        case OPCODE_SET_INDEX: return "SET_INDEX";
        case OPCODE_ARRAY_LENGTH: return "ARRAY_LENGTH";
        
        // String operations
        case OPCODE_CONCAT_STRING: return "CONCAT_STRING";
        case OPCODE_STRING_LENGTH: return "STRING_LENGTH";
        case OPCODE_STRING_SLICE: return "STRING_SLICE";
        
        // System operations
        case OPCODE_PRINT: return "PRINT";
        case OPCODE_HALT: return "HALT";
        
        default: return "UNKNOWN_OPCODE";
    }
}

// Check if opcode is arithmetic
bool isArithmeticOpcode(Opcode opcode) {
    return (opcode >= OPCODE_ADD_I32_R && opcode <= OPCODE_MOD_I32_R);
}

// Check if opcode is comparison
bool isComparisonOpcode(Opcode opcode) {
    return (opcode >= OPCODE_EQ_I32_R && opcode <= OPCODE_GE_I32_R);
}

// Check if opcode is control flow
bool isControlFlowOpcode(Opcode opcode) {
    return (opcode >= OPCODE_JUMP && opcode <= OPCODE_RETURN);
}

// VM capability check functions
bool vmSupportsExtendedConstants(void) {
    return g_vmConfig.maxConstantPoolSize > 256;
}

bool vmSupportsComputedGoto(void) {
    return g_vmConfig.supportsComputedGoto;
}

int vmGetRegisterCount(void) {
    return g_vmConfig.registerCount;
}

int vmGetMaxConstantPoolSize(void) {
    return g_vmConfig.maxConstantPoolSize;
}