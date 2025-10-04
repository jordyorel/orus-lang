//  Orus Language Project
//  ---------------------------------------------------------------------------
//  File: src/vm/dispatch/vm_dispatch_goto.c
//  Author: Jordy Orel KONDA
//  Copyright (c) 2023 Jordy Orel KONDA
//  License: MIT License (see LICENSE file in the project root)
//  Description: Provides the goto-threaded dispatch implementation optimized for GCC and Clang.


#include "vm/vm_dispatch.h"
#include "vm/spill_manager.h"
#include "runtime/builtins.h"
#include "runtime/memory.h"
#include "vm/vm_constants.h"
#include "vm/vm_string_ops.h"
#include "vm/vm_tagged_union.h"
#include "vm/vm_arithmetic.h"
#include "vm/vm_control_flow.h"
#include "vm/vm_comparison.h"
#include "vm/vm_typed_ops.h"
#include "vm/vm_opcode_handlers.h"
#include "vm/register_file.h"
#include "vm/vm_profiling.h"
#include "debug/debug_config.h"

#include <math.h>
#include <limits.h>
#include <inttypes.h>

#define VM_HANDLE_INC_I32_SLOW_PATH(reg) \
    do { \
        Value val_reg__ = vm_get_register_safe((reg)); \
        if (!IS_I32(val_reg__)) { \
            if (vm_typed_reg_in_range((reg))) { \
                vm.typed_regs.reg_types[(reg)] = REG_TYPE_HEAP; \
                vm.typed_regs.dirty[(reg)] = false; \
            } \
            VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be i32"); \
        } \
        int32_t current__ = AS_I32(val_reg__); \
        int32_t next_value__; \
        if (__builtin_add_overflow(current__, 1, &next_value__)) { \
            VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Integer overflow"); \
        } \
        vm_store_i32_typed_hot((reg), next_value__); \
    } while (0)

#define VM_HANDLE_INC_I64_SLOW_PATH(reg) \
    do { \
        Value val_reg__ = vm_get_register_safe((reg)); \
        if (!IS_I64(val_reg__)) { \
            if (vm_typed_reg_in_range((reg))) { \
                vm.typed_regs.reg_types[(reg)] = REG_TYPE_HEAP; \
                vm.typed_regs.dirty[(reg)] = false; \
            } \
            VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be i64"); \
        } \
        int64_t current__ = AS_I64(val_reg__); \
        int64_t next_value__; \
        if (__builtin_add_overflow(current__, (int64_t)1, &next_value__)) { \
            VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Integer overflow"); \
        } \
        vm_store_i64_typed_hot((reg), next_value__); \
    } while (0)

#define VM_HANDLE_INC_U32_SLOW_PATH(reg) \
    do { \
        Value val_reg__ = vm_get_register_safe((reg)); \
        if (!IS_U32(val_reg__)) { \
            if (vm_typed_reg_in_range((reg))) { \
                vm.typed_regs.reg_types[(reg)] = REG_TYPE_HEAP; \
                vm.typed_regs.dirty[(reg)] = false; \
            } \
            VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be u32"); \
        } \
        uint32_t next_value__ = AS_U32(val_reg__) + (uint32_t)1; \
        vm_store_u32_typed_hot((reg), next_value__); \
    } while (0)

#define VM_HANDLE_INC_U64_SLOW_PATH(reg) \
    do { \
        Value val_reg__ = vm_get_register_safe((reg)); \
        if (!IS_U64(val_reg__)) { \
            if (vm_typed_reg_in_range((reg))) { \
                vm.typed_regs.reg_types[(reg)] = REG_TYPE_HEAP; \
                vm.typed_regs.dirty[(reg)] = false; \
            } \
            VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be u64"); \
        } \
        uint64_t next_value__ = AS_U64(val_reg__) + (uint64_t)1; \
        vm_store_u64_typed_hot((reg), next_value__); \
    } while (0)

static inline bool value_to_index(Value value, int* out_index) {
    if (IS_I32(value)) {
        int32_t idx = AS_I32(value);
        if (idx < 0) {
            return false;
        }
        *out_index = idx;
        return true;
    }
    if (IS_I64(value)) {
        int64_t idx = AS_I64(value);
        if (idx < 0 || idx > INT_MAX) {
            return false;
        }
        *out_index = (int)idx;
        return true;
    }
    if (IS_U32(value)) {
        uint32_t idx = AS_U32(value);
        if (idx > (uint32_t)INT_MAX) {
            return false;
        }
        *out_index = (int)idx;
        return true;
    }
    if (IS_U64(value)) {
        uint64_t idx = AS_U64(value);
        if (idx > (uint64_t)INT_MAX) {
            return false;
        }
        *out_index = (int)idx;
        return true;
    }
    return false;
}



// Auto-detect computed goto support
#ifndef USE_COMPUTED_GOTO
  #if defined(__GNUC__) || defined(__clang__)
    #define USE_COMPUTED_GOTO 1
  #else
    #define USE_COMPUTED_GOTO 0
  #endif
#endif

#if USE_COMPUTED_GOTO

// Profiling timing variable accessible to DISPATCH macro
static uint64_t instruction_start_time = 0;

#if ORUS_VM_ENABLE_TYPED_OPS
#define VM_REGISTER_TYPED_LABEL(opcode) \
    vm_dispatch_table[(opcode)] = &&LABEL_##opcode
#else
#define VM_REGISTER_TYPED_LABEL(opcode) \
    vm_dispatch_table[(opcode)] = &&LABEL_TYPED_OPS_UNAVAILABLE
#endif

InterpretResult vm_run_dispatch(void) {
    // printf("[DISPATCH_TRACE] vm_run_dispatch() entry");
    fflush(stdout);

    double start_time = get_time_vm();
    #define RETURN(val) \
        do { \
            InterpretResult _return_val = (val); \
            if (_return_val == INTERPRET_RUNTIME_ERROR) { \
                vm_report_unhandled_error(); \
            } \
            vm.lastExecutionTime = get_time_vm() - start_time; \
            return _return_val; \
        } while (0)

    // Initialize dispatch table with label addresses - this only runs ONCE per process
    static bool global_dispatch_initialized = false;
    // printf("[DISPATCH_TRACE] global_dispatch_initialized = %s\n", global_dispatch_initialized ? "true" : "false");
    fflush(stdout);
    if (!global_dispatch_initialized) {
        DEBUG_VM_DISPATCH_PRINT("Initializing dispatch table...");
        fflush(stdout);
        
        // Initialize all entries to NULL first to catch missing mappings
        for (int i = 0; i < VM_DISPATCH_TABLE_SIZE; i++) {
            vm_dispatch_table[i] = NULL;
        }
        // Phase 1.3 Optimization: Hot opcodes first for better cache locality
        // Most frequently used typed operations (hot path)
        VM_REGISTER_TYPED_LABEL(OP_ADD_I32_TYPED);
        VM_REGISTER_TYPED_LABEL(OP_SUB_I32_TYPED);
        VM_REGISTER_TYPED_LABEL(OP_MUL_I32_TYPED);
        VM_REGISTER_TYPED_LABEL(OP_LT_I32_TYPED);
        VM_REGISTER_TYPED_LABEL(OP_LE_I32_TYPED);
        VM_REGISTER_TYPED_LABEL(OP_GT_I32_TYPED);
        VM_REGISTER_TYPED_LABEL(OP_GE_I32_TYPED);
        VM_REGISTER_TYPED_LABEL(OP_LT_I64_TYPED);
        VM_REGISTER_TYPED_LABEL(OP_LE_I64_TYPED);
        VM_REGISTER_TYPED_LABEL(OP_GT_I64_TYPED);
        VM_REGISTER_TYPED_LABEL(OP_GE_I64_TYPED);
        VM_REGISTER_TYPED_LABEL(OP_LT_F64_TYPED);
        VM_REGISTER_TYPED_LABEL(OP_LE_F64_TYPED);
        VM_REGISTER_TYPED_LABEL(OP_GT_F64_TYPED);
        VM_REGISTER_TYPED_LABEL(OP_GE_F64_TYPED);
        VM_REGISTER_TYPED_LABEL(OP_LT_U32_TYPED);
        VM_REGISTER_TYPED_LABEL(OP_LE_U32_TYPED);
        VM_REGISTER_TYPED_LABEL(OP_GT_U32_TYPED);
        VM_REGISTER_TYPED_LABEL(OP_GE_U32_TYPED);
        VM_REGISTER_TYPED_LABEL(OP_LT_U64_TYPED);
        VM_REGISTER_TYPED_LABEL(OP_LE_U64_TYPED);
        VM_REGISTER_TYPED_LABEL(OP_GT_U64_TYPED);
        VM_REGISTER_TYPED_LABEL(OP_GE_U64_TYPED);
        
        // Short jumps for tight loops
        vm_dispatch_table[OP_JUMP_SHORT] = &&LABEL_OP_JUMP_SHORT;
        vm_dispatch_table[OP_JUMP_BACK_SHORT] = &&LABEL_OP_JUMP_BACK_SHORT;
        vm_dispatch_table[OP_JUMP_IF_NOT_SHORT] = &&LABEL_OP_JUMP_IF_NOT_SHORT;
        vm_dispatch_table[OP_LOOP_SHORT] = &&LABEL_OP_LOOP_SHORT;
        vm_dispatch_table[OP_BRANCH_TYPED] = &&LABEL_OP_BRANCH_TYPED;

        // Loop-critical operations
        vm_dispatch_table[OP_INC_I32_R] = &&LABEL_OP_INC_I32_R;
        vm_dispatch_table[OP_INC_I32_CHECKED] = &&LABEL_OP_INC_I32_CHECKED;
        vm_dispatch_table[OP_INC_I64_R] = &&LABEL_OP_INC_I64_R;
        vm_dispatch_table[OP_INC_I64_CHECKED] = &&LABEL_OP_INC_I64_CHECKED;
        vm_dispatch_table[OP_INC_U32_R] = &&LABEL_OP_INC_U32_R;
        vm_dispatch_table[OP_INC_U32_CHECKED] = &&LABEL_OP_INC_U32_CHECKED;
        vm_dispatch_table[OP_INC_U64_R] = &&LABEL_OP_INC_U64_R;
        vm_dispatch_table[OP_INC_U64_CHECKED] = &&LABEL_OP_INC_U64_CHECKED;

        // Remaining typed operations
        VM_REGISTER_TYPED_LABEL(OP_DIV_I32_TYPED);
        VM_REGISTER_TYPED_LABEL(OP_MOD_I32_TYPED);
        VM_REGISTER_TYPED_LABEL(OP_ADD_I64_TYPED);
        VM_REGISTER_TYPED_LABEL(OP_SUB_I64_TYPED);
        VM_REGISTER_TYPED_LABEL(OP_MUL_I64_TYPED);
        VM_REGISTER_TYPED_LABEL(OP_DIV_I64_TYPED);
        VM_REGISTER_TYPED_LABEL(OP_MOD_I64_TYPED);
        VM_REGISTER_TYPED_LABEL(OP_ADD_F64_TYPED);
        VM_REGISTER_TYPED_LABEL(OP_SUB_F64_TYPED);
        VM_REGISTER_TYPED_LABEL(OP_MUL_F64_TYPED);
        VM_REGISTER_TYPED_LABEL(OP_DIV_F64_TYPED);
        VM_REGISTER_TYPED_LABEL(OP_MOD_F64_TYPED);
        
        // U32 and U64 typed operations
        VM_REGISTER_TYPED_LABEL(OP_ADD_U32_TYPED);
        VM_REGISTER_TYPED_LABEL(OP_SUB_U32_TYPED);
        VM_REGISTER_TYPED_LABEL(OP_MUL_U32_TYPED);
        VM_REGISTER_TYPED_LABEL(OP_DIV_U32_TYPED);
        VM_REGISTER_TYPED_LABEL(OP_MOD_U32_TYPED);

        VM_REGISTER_TYPED_LABEL(OP_ADD_U64_TYPED);
        VM_REGISTER_TYPED_LABEL(OP_SUB_U64_TYPED);
        VM_REGISTER_TYPED_LABEL(OP_MUL_U64_TYPED);
        VM_REGISTER_TYPED_LABEL(OP_DIV_U64_TYPED);
        VM_REGISTER_TYPED_LABEL(OP_MOD_U64_TYPED);
        
        // TODO: Removed mixed-type op for Rust-style strict typing
        
        // Constant loading (also hot)
        VM_REGISTER_TYPED_LABEL(OP_LOAD_I32_CONST);
        VM_REGISTER_TYPED_LABEL(OP_LOAD_I64_CONST);
        VM_REGISTER_TYPED_LABEL(OP_LOAD_F64_CONST);
        
        // Standard operations (less hot)
        vm_dispatch_table[OP_LOAD_CONST] = &&LABEL_OP_LOAD_CONST;
        vm_dispatch_table[OP_LOAD_TRUE] = &&LABEL_OP_LOAD_TRUE;
        vm_dispatch_table[OP_LOAD_FALSE] = &&LABEL_OP_LOAD_FALSE;
        vm_dispatch_table[OP_MOVE] = &&LABEL_OP_MOVE;
        vm_dispatch_table[OP_LOAD_GLOBAL] = &&LABEL_OP_LOAD_GLOBAL;
        vm_dispatch_table[OP_STORE_GLOBAL] = &&LABEL_OP_STORE_GLOBAL;
        vm_dispatch_table[OP_ADD_I32_R] = &&LABEL_OP_ADD_I32_R;
        vm_dispatch_table[OP_SUB_I32_R] = &&LABEL_OP_SUB_I32_R;
        vm_dispatch_table[OP_MUL_I32_R] = &&LABEL_OP_MUL_I32_R;
        vm_dispatch_table[OP_DIV_I32_R] = &&LABEL_OP_DIV_I32_R;
        vm_dispatch_table[OP_MOD_I32_R] = &&LABEL_OP_MOD_I32_R;
        vm_dispatch_table[OP_INC_I32_R] = &&LABEL_OP_INC_I32_R;
        vm_dispatch_table[OP_INC_I32_CHECKED] = &&LABEL_OP_INC_I32_CHECKED;
        vm_dispatch_table[OP_DEC_I32_R] = &&LABEL_OP_DEC_I32_R;
        vm_dispatch_table[OP_NEG_I32_R] = &&LABEL_OP_NEG_I32_R;
        vm_dispatch_table[OP_ADD_I64_R] = &&LABEL_OP_ADD_I64_R;
        vm_dispatch_table[OP_SUB_I64_R] = &&LABEL_OP_SUB_I64_R;
        vm_dispatch_table[OP_MUL_I64_R] = &&LABEL_OP_MUL_I64_R;
        vm_dispatch_table[OP_DIV_I64_R] = &&LABEL_OP_DIV_I64_R;
        vm_dispatch_table[OP_MOD_I64_R] = &&LABEL_OP_MOD_I64_R;
        vm_dispatch_table[OP_ADD_U32_R] = &&LABEL_OP_ADD_U32_R;
        vm_dispatch_table[OP_SUB_U32_R] = &&LABEL_OP_SUB_U32_R;
        vm_dispatch_table[OP_MUL_U32_R] = &&LABEL_OP_MUL_U32_R;
        vm_dispatch_table[OP_DIV_U32_R] = &&LABEL_OP_DIV_U32_R;
        vm_dispatch_table[OP_MOD_U32_R] = &&LABEL_OP_MOD_U32_R;
        vm_dispatch_table[OP_ADD_U64_R] = &&LABEL_OP_ADD_U64_R;
        vm_dispatch_table[OP_SUB_U64_R] = &&LABEL_OP_SUB_U64_R;
        vm_dispatch_table[OP_MUL_U64_R] = &&LABEL_OP_MUL_U64_R;
        vm_dispatch_table[OP_DIV_U64_R] = &&LABEL_OP_DIV_U64_R;
        vm_dispatch_table[OP_MOD_U64_R] = &&LABEL_OP_MOD_U64_R;
        vm_dispatch_table[OP_BOOL_TO_I32_R] = &&LABEL_OP_BOOL_TO_I32_R;
        vm_dispatch_table[OP_BOOL_TO_I64_R] = &&LABEL_OP_BOOL_TO_I64_R;
        vm_dispatch_table[OP_BOOL_TO_U32_R] = &&LABEL_OP_BOOL_TO_U32_R;
        vm_dispatch_table[OP_BOOL_TO_U64_R] = &&LABEL_OP_BOOL_TO_U64_R;
        vm_dispatch_table[OP_BOOL_TO_F64_R] = &&LABEL_OP_BOOL_TO_F64_R;
        vm_dispatch_table[OP_I32_TO_I64_R] = &&LABEL_OP_I32_TO_I64_R;
        vm_dispatch_table[OP_I32_TO_U32_R] = &&LABEL_OP_I32_TO_U32_R;
        vm_dispatch_table[OP_I32_TO_BOOL_R] = &&LABEL_OP_I32_TO_BOOL_R;
        vm_dispatch_table[OP_U32_TO_I32_R] = &&LABEL_OP_U32_TO_I32_R;
        vm_dispatch_table[OP_I64_TO_I32_R] = &&LABEL_OP_I64_TO_I32_R;
        vm_dispatch_table[OP_I64_TO_BOOL_R] = &&LABEL_OP_I64_TO_BOOL_R;
        vm_dispatch_table[OP_ADD_F64_R] = &&LABEL_OP_ADD_F64_R;
        vm_dispatch_table[OP_SUB_F64_R] = &&LABEL_OP_SUB_F64_R;
        vm_dispatch_table[OP_MUL_F64_R] = &&LABEL_OP_MUL_F64_R;
        vm_dispatch_table[OP_DIV_F64_R] = &&LABEL_OP_DIV_F64_R;
        vm_dispatch_table[OP_MOD_F64_R] = &&LABEL_OP_MOD_F64_R;

        // Bitwise operations
        vm_dispatch_table[OP_AND_I32_R] = &&LABEL_OP_AND_I32_R;
        vm_dispatch_table[OP_OR_I32_R] = &&LABEL_OP_OR_I32_R;
        vm_dispatch_table[OP_XOR_I32_R] = &&LABEL_OP_XOR_I32_R;
        vm_dispatch_table[OP_NOT_I32_R] = &&LABEL_OP_NOT_I32_R;
        vm_dispatch_table[OP_SHL_I32_R] = &&LABEL_OP_SHL_I32_R;
        vm_dispatch_table[OP_SHR_I32_R] = &&LABEL_OP_SHR_I32_R;

        vm_dispatch_table[OP_LT_F64_R] = &&LABEL_OP_LT_F64_R;
        vm_dispatch_table[OP_LE_F64_R] = &&LABEL_OP_LE_F64_R;
        vm_dispatch_table[OP_GT_F64_R] = &&LABEL_OP_GT_F64_R;
        vm_dispatch_table[OP_GE_F64_R] = &&LABEL_OP_GE_F64_R;
        vm_dispatch_table[OP_I32_TO_F64_R] = &&LABEL_OP_I32_TO_F64_R;
        vm_dispatch_table[OP_I64_TO_F64_R] = &&LABEL_OP_I64_TO_F64_R;
        vm_dispatch_table[OP_F64_TO_I32_R] = &&LABEL_OP_F64_TO_I32_R;
        vm_dispatch_table[OP_F64_TO_I64_R] = &&LABEL_OP_F64_TO_I64_R;
        
        // U64 cast handlers
        vm_dispatch_table[OP_I32_TO_U64_R] = &&LABEL_OP_I32_TO_U64_R;
        vm_dispatch_table[OP_I64_TO_U64_R] = &&LABEL_OP_I64_TO_U64_R;
        vm_dispatch_table[OP_U64_TO_I32_R] = &&LABEL_OP_U64_TO_I32_R;
        vm_dispatch_table[OP_U64_TO_I64_R] = &&LABEL_OP_U64_TO_I64_R;
        vm_dispatch_table[OP_U32_TO_U64_R] = &&LABEL_OP_U32_TO_U64_R;
        vm_dispatch_table[OP_U64_TO_U32_R] = &&LABEL_OP_U64_TO_U32_R;
        vm_dispatch_table[OP_F64_TO_U64_R] = &&LABEL_OP_F64_TO_U64_R;
        vm_dispatch_table[OP_U64_TO_F64_R] = &&LABEL_OP_U64_TO_F64_R;
        vm_dispatch_table[OP_U32_TO_BOOL_R] = &&LABEL_OP_U32_TO_BOOL_R;
        vm_dispatch_table[OP_U64_TO_BOOL_R] = &&LABEL_OP_U64_TO_BOOL_R;
        vm_dispatch_table[OP_F64_TO_BOOL_R] = &&LABEL_OP_F64_TO_BOOL_R;
        
        // Additional cast handlers for u32<->f64
        vm_dispatch_table[OP_U32_TO_F64_R] = &&LABEL_OP_U32_TO_F64_R;
        vm_dispatch_table[OP_F64_TO_U32_R] = &&LABEL_OP_F64_TO_U32_R;
        vm_dispatch_table[OP_LT_I32_R] = &&LABEL_OP_LT_I32_R;
        vm_dispatch_table[OP_LE_I32_R] = &&LABEL_OP_LE_I32_R;
        vm_dispatch_table[OP_GT_I32_R] = &&LABEL_OP_GT_I32_R;
        vm_dispatch_table[OP_GE_I32_R] = &&LABEL_OP_GE_I32_R;
        vm_dispatch_table[OP_LT_I64_R] = &&LABEL_OP_LT_I64_R;
        vm_dispatch_table[OP_LE_I64_R] = &&LABEL_OP_LE_I64_R;
        vm_dispatch_table[OP_GT_I64_R] = &&LABEL_OP_GT_I64_R;
        vm_dispatch_table[OP_GE_I64_R] = &&LABEL_OP_GE_I64_R;
        vm_dispatch_table[OP_LT_U32_R] = &&LABEL_OP_LT_U32_R;
        vm_dispatch_table[OP_LE_U32_R] = &&LABEL_OP_LE_U32_R;
        vm_dispatch_table[OP_GT_U32_R] = &&LABEL_OP_GT_U32_R;
        vm_dispatch_table[OP_GE_U32_R] = &&LABEL_OP_GE_U32_R;
        vm_dispatch_table[OP_LT_U64_R] = &&LABEL_OP_LT_U64_R;
        vm_dispatch_table[OP_LE_U64_R] = &&LABEL_OP_LE_U64_R;
        vm_dispatch_table[OP_GT_U64_R] = &&LABEL_OP_GT_U64_R;
        vm_dispatch_table[OP_GE_U64_R] = &&LABEL_OP_GE_U64_R;
        vm_dispatch_table[OP_EQ_R] = &&LABEL_OP_EQ_R;
        vm_dispatch_table[OP_NE_R] = &&LABEL_OP_NE_R;
        vm_dispatch_table[OP_AND_BOOL_R] = &&LABEL_OP_AND_BOOL_R;
        vm_dispatch_table[OP_OR_BOOL_R] = &&LABEL_OP_OR_BOOL_R;
        vm_dispatch_table[OP_NOT_BOOL_R] = &&LABEL_OP_NOT_BOOL_R;
        vm_dispatch_table[OP_CONCAT_R] = &&LABEL_OP_CONCAT_R;
        vm_dispatch_table[OP_STRING_INDEX_R] = &&LABEL_OP_STRING_INDEX_R;
        vm_dispatch_table[OP_MAKE_ARRAY_R] = &&LABEL_OP_MAKE_ARRAY_R;
        vm_dispatch_table[OP_ENUM_NEW_R] = &&LABEL_OP_ENUM_NEW_R;
        vm_dispatch_table[OP_ENUM_TAG_EQ_R] = &&LABEL_OP_ENUM_TAG_EQ_R;
        vm_dispatch_table[OP_ENUM_PAYLOAD_R] = &&LABEL_OP_ENUM_PAYLOAD_R;
        vm_dispatch_table[OP_STRING_GET_R] = &&LABEL_OP_STRING_GET_R;
        vm_dispatch_table[OP_ARRAY_GET_R] = &&LABEL_OP_ARRAY_GET_R;
        vm_dispatch_table[OP_ARRAY_SET_R] = &&LABEL_OP_ARRAY_SET_R;
        vm_dispatch_table[OP_ARRAY_LEN_R] = &&LABEL_OP_ARRAY_LEN_R;
        vm_dispatch_table[OP_ARRAY_PUSH_R] = &&LABEL_OP_ARRAY_PUSH_R;
        vm_dispatch_table[OP_ARRAY_POP_R] = &&LABEL_OP_ARRAY_POP_R;
        vm_dispatch_table[OP_ARRAY_SORTED_R] = &&LABEL_OP_ARRAY_SORTED_R;
        vm_dispatch_table[OP_ARRAY_SLICE_R] = &&LABEL_OP_ARRAY_SLICE_R;
        vm_dispatch_table[OP_TO_STRING_R] = &&LABEL_OP_TO_STRING_R;
        vm_dispatch_table[OP_TRY_BEGIN] = &&LABEL_OP_TRY_BEGIN;
        vm_dispatch_table[OP_TRY_END] = &&LABEL_OP_TRY_END;
        vm_dispatch_table[OP_THROW] = &&LABEL_OP_THROW;
        vm_dispatch_table[OP_JUMP] = &&LABEL_OP_JUMP;
        vm_dispatch_table[OP_JUMP_IF_NOT_R] = &&LABEL_OP_JUMP_IF_NOT_R;
        vm_dispatch_table[OP_JUMP_IF_NOT_I32_TYPED] = &&LABEL_OP_JUMP_IF_NOT_I32_TYPED;
        vm_dispatch_table[OP_LOOP] = &&LABEL_OP_LOOP;
        vm_dispatch_table[OP_GET_ITER_R] = &&LABEL_OP_GET_ITER_R;
        vm_dispatch_table[OP_ITER_NEXT_R] = &&LABEL_OP_ITER_NEXT_R;
        vm_dispatch_table[OP_PARSE_INT_R] = &&LABEL_OP_PARSE_INT_R;
        vm_dispatch_table[OP_PARSE_FLOAT_R] = &&LABEL_OP_PARSE_FLOAT_R;
        vm_dispatch_table[OP_TYPE_OF_R] = &&LABEL_OP_TYPE_OF_R;
        vm_dispatch_table[OP_IS_TYPE_R] = &&LABEL_OP_IS_TYPE_R;
        vm_dispatch_table[OP_INPUT_R] = &&LABEL_OP_INPUT_R;
        vm_dispatch_table[OP_RANGE_R] = &&LABEL_OP_RANGE_R;
        vm_dispatch_table[OP_PRINT_MULTI_R] = &&LABEL_OP_PRINT_MULTI_R;
        vm_dispatch_table[OP_PRINT_R] = &&LABEL_OP_PRINT_R;
        vm_dispatch_table[OP_PRINT_NO_NL_R] = &&LABEL_OP_PRINT_NO_NL_R;
        vm_dispatch_table[OP_ASSERT_EQ_R] = &&LABEL_OP_ASSERT_EQ_R;
        vm_dispatch_table[OP_CALL_R] = &&LABEL_OP_CALL_R;
        vm_dispatch_table[OP_TAIL_CALL_R] = &&LABEL_OP_TAIL_CALL_R;
        vm_dispatch_table[OP_RETURN_R] = &&LABEL_OP_RETURN_R;
        vm_dispatch_table[OP_RETURN_VOID] = &&LABEL_OP_RETURN_VOID;
        
        // Phase 1: Frame register operations
        vm_dispatch_table[OP_LOAD_FRAME] = &&LABEL_OP_LOAD_FRAME;
        vm_dispatch_table[OP_STORE_FRAME] = &&LABEL_OP_STORE_FRAME;
        vm_dispatch_table[OP_LOAD_SPILL] = &&LABEL_OP_LOAD_SPILL;
        vm_dispatch_table[OP_STORE_SPILL] = &&LABEL_OP_STORE_SPILL;
        vm_dispatch_table[OP_ENTER_FRAME] = &&LABEL_OP_ENTER_FRAME;
        vm_dispatch_table[OP_EXIT_FRAME] = &&LABEL_OP_EXIT_FRAME;
        vm_dispatch_table[OP_MOVE_FRAME] = &&LABEL_OP_MOVE_FRAME;
        
        // Closure operations
        vm_dispatch_table[OP_CLOSURE_R] = &&LABEL_OP_CLOSURE_R;
        vm_dispatch_table[OP_GET_UPVALUE_R] = &&LABEL_OP_GET_UPVALUE_R;
        vm_dispatch_table[OP_SET_UPVALUE_R] = &&LABEL_OP_SET_UPVALUE_R;
        vm_dispatch_table[OP_CLOSE_UPVALUE_R] = &&LABEL_OP_CLOSE_UPVALUE_R;
        
        // Note: Hot opcodes already assigned above for optimal cache locality
        
        VM_REGISTER_TYPED_LABEL(OP_MOVE_I32);
        VM_REGISTER_TYPED_LABEL(OP_MOVE_I64);
        VM_REGISTER_TYPED_LABEL(OP_MOVE_F64);
        
        // Phase 2.2: Fused instruction dispatch entries
        vm_dispatch_table[OP_ADD_I32_IMM] = &&LABEL_OP_ADD_I32_IMM;
        vm_dispatch_table[OP_SUB_I32_IMM] = &&LABEL_OP_SUB_I32_IMM;
        vm_dispatch_table[OP_MUL_I32_IMM] = &&LABEL_OP_MUL_I32_IMM;
        vm_dispatch_table[OP_CMP_I32_IMM] = &&LABEL_OP_CMP_I32_IMM;
        vm_dispatch_table[OP_INC_CMP_JMP] = &&LABEL_OP_INC_CMP_JMP;
        vm_dispatch_table[OP_DEC_CMP_JMP] = &&LABEL_OP_DEC_CMP_JMP;
        vm_dispatch_table[OP_MUL_ADD_I32] = &&LABEL_OP_MUL_ADD_I32;
        
        // Built-in functions
        vm_dispatch_table[OP_TIME_STAMP] = &&LABEL_OP_TIME_STAMP;
        
        vm_dispatch_table[OP_HALT] = &&LABEL_OP_HALT;
        
        // Mark dispatch table as initialized to prevent re-initialization
        global_dispatch_initialized = true;
        
        // Check for NULL entries in critical opcodes only
        int critical_opcodes[] = {OP_EQ_R, OP_LT_I32_R, OP_JUMP_IF_NOT_R,
                                  OP_JUMP_IF_NOT_I32_TYPED, OP_ADD_I32_TYPED, -1};
        for (int i = 0; critical_opcodes[i] != -1; i++) {
            int opcode = critical_opcodes[i];
            if (opcode < VM_DISPATCH_TABLE_SIZE) {
                if (vm_dispatch_table[opcode] == NULL) {
                    DEBUG_VM_PRINT("[DISPATCH_ERROR] Critical opcode %d (0x%02X) has NULL dispatch entry!\n", opcode, opcode);
                } else {
                    DEBUG_VM_DISPATCH_PRINT("Opcode %d -> %p\n", opcode, vm_dispatch_table[opcode]);
                }
            }
        }
        fflush(stdout);
    }

    uint8_t instruction = 0;
    
    // printf("[DISPATCH_TRACE] About to start bytecode execution loop");
    fflush(stdout);

    // Macros are defined in vm_dispatch.h to avoid redefinition warnings
    
    // Profiling hook: Initialize timing for first instruction
    if (g_profiling.isActive) {
        instruction_start_time = getTimestamp();
        g_profiling.totalInstructions++;
    }
    
        DISPATCH();

    LABEL_OP_LOAD_CONST: {
            handle_load_const();
            DISPATCH();
        }


    LABEL_OP_LOAD_TRUE: {
            handle_load_true();
            DISPATCH();
        }

    LABEL_OP_LOAD_FALSE: {
            handle_load_false();
            DISPATCH();
        }

    LABEL_OP_MOVE: {
            handle_move_reg();
            DISPATCH();
        }

    LABEL_OP_LOAD_GLOBAL: {
            uint8_t reg = READ_BYTE();
            uint8_t globalIndex = READ_BYTE();
            if (globalIndex >= vm.variableCount || vm.globalTypes[globalIndex] == NULL) {
                VM_ERROR_RETURN(ERROR_NAME, CURRENT_LOCATION(), "Undefined variable");
            }
            vm_set_register_safe(reg, vm.globals[globalIndex]);
            DISPATCH();
        }

    LABEL_OP_STORE_GLOBAL: {
            uint8_t globalIndex = READ_BYTE();
            uint8_t reg = READ_BYTE();
            
            Value valueToStore = vm_get_register_safe(reg);
            Type* declaredType = vm.globalTypes[globalIndex];
            
            // Check if the value being stored matches the declared type
            if (declaredType && declaredType->kind != TYPE_ANY) {
                bool typeMatches = false;
                Value coercedValue = valueToStore; // Default to original value
                
                switch (declaredType->kind) {
                    case TYPE_I32:
                        typeMatches = IS_I32(valueToStore);
                        break;
                    case TYPE_I64:
                        if (IS_I64(valueToStore)) {
                            typeMatches = true;
                        } else if (IS_I32(valueToStore)) {
                            // SMART COERCION: i32 literals can be coerced to i64
                            int32_t val = AS_I32(valueToStore);
                            coercedValue = I64_VAL((int64_t)val);
                            typeMatches = true;
                        }
                        break;
                    case TYPE_U32:
                        if (IS_U32(valueToStore)) {
                            typeMatches = true;
                        } else if (IS_I32(valueToStore)) {
                            // SMART COERCION: non-negative i32 literals can be coerced to u32
                            int32_t val = AS_I32(valueToStore);
                            if (val >= 0) {
                                coercedValue = U32_VAL((uint32_t)val);
                                typeMatches = true;
                            }
                        }
                        break;
                    case TYPE_U64:
                        if (IS_U64(valueToStore)) {
                            typeMatches = true;
                        } else if (IS_I32(valueToStore)) {
                            // SMART COERCION: non-negative i32 literals can be coerced to u64
                            int32_t val = AS_I32(valueToStore);
                            if (val >= 0) {
                                coercedValue = U64_VAL((uint64_t)val);
                                typeMatches = true;
                            }
                        }
                        break;
                    case TYPE_F64:
                        if (IS_F64(valueToStore)) {
                            typeMatches = true;
                        } else if (IS_I32(valueToStore)) {
                            // SMART COERCION: i32 literals can be coerced to f64
                            int32_t val = AS_I32(valueToStore);
                            coercedValue = F64_VAL((double)val);
                            typeMatches = true;
                        }
                        break;
                    case TYPE_BOOL:
                        typeMatches = IS_BOOL(valueToStore);
                        break;
                    case TYPE_STRING:
                        typeMatches = IS_STRING(valueToStore);
                        break;
                    default:
                        typeMatches = true; // TYPE_ANY allows anything
                        break;
                }
                
                if (!typeMatches) {
                    const char* expectedTypeName = "unknown";
                    switch (declaredType->kind) {
                        case TYPE_I32: expectedTypeName = "i32"; break;
                        case TYPE_I64: expectedTypeName = "i64"; break;
                        case TYPE_U32: expectedTypeName = "u32"; break;
                        case TYPE_U64: expectedTypeName = "u64"; break;
                        case TYPE_F64: expectedTypeName = "f64"; break;
                        case TYPE_BOOL: expectedTypeName = "bool"; break;
                        case TYPE_STRING: expectedTypeName = "string"; break;
                        default: break;
                    }
                    
                    VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Type mismatch: cannot assign value to variable of type '%s'. Use 'as' for explicit conversion.",
                                expectedTypeName);
                }
                
                // Store the coerced value
                vm.globals[globalIndex] = coercedValue;
            } else {
                // No declared type, store as-is
                vm.globals[globalIndex] = valueToStore;
            }
            
            DISPATCH();
        }

    LABEL_OP_ADD_I32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            
            // Check if either operand is a string - if so, do string concatenation
            Value val1 = vm_get_register_safe(src1);
            Value val2 = vm_get_register_safe(src2);
            if (IS_STRING(val1) || IS_STRING(val2)) {
                // [string concatenation code - use frame-aware values]
                Value left = val1;
                Value right = val2;
                
                // Convert left operand to string if needed
                if (!IS_STRING(left)) {
                    char buffer[64];
                    if (IS_I32(left)) {
                        snprintf(buffer, sizeof(buffer), "%d", AS_I32(left));
                    } else if (IS_I64(left)) {
                        snprintf(buffer, sizeof(buffer), "%" PRId64, (int64_t)AS_I64(left));
                    } else if (IS_U32(left)) {
                        snprintf(buffer, sizeof(buffer), "%u", AS_U32(left));
                    } else if (IS_U64(left)) {
                        snprintf(buffer, sizeof(buffer), "%" PRIu64, (uint64_t)AS_U64(left));
                    } else if (IS_F64(left)) {
                        snprintf(buffer, sizeof(buffer), "%.6g", AS_F64(left));
                    } else if (IS_BOOL(left)) {
                        snprintf(buffer, sizeof(buffer), "%s", AS_BOOL(left) ? "true" : "false");
                    } else {
                        snprintf(buffer, sizeof(buffer), "nil");
                    }
                    ObjString* leftStr = allocateString(buffer, (int)strlen(buffer));
                    left = STRING_VAL(leftStr);
                }
                
                // Convert right operand to string if needed
                if (!IS_STRING(right)) {
                    char buffer[64];
                    if (IS_I32(right)) {
                        snprintf(buffer, sizeof(buffer), "%d", AS_I32(right));
                    } else if (IS_I64(right)) {
                        snprintf(buffer, sizeof(buffer), "%" PRId64, (int64_t)AS_I64(right));
                    } else if (IS_U32(right)) {
                        snprintf(buffer, sizeof(buffer), "%u", AS_U32(right));
                    } else if (IS_U64(right)) {
                        snprintf(buffer, sizeof(buffer), "%" PRIu64, (uint64_t)AS_U64(right));
                    } else if (IS_F64(right)) {
                        snprintf(buffer, sizeof(buffer), "%.6g", AS_F64(right));
                    } else if (IS_BOOL(right)) {
                        snprintf(buffer, sizeof(buffer), "%s", AS_BOOL(right) ? "true" : "false");
                    } else {
                        snprintf(buffer, sizeof(buffer), "nil");
                    }
                    ObjString* rightStr = allocateString(buffer, (int)strlen(buffer));
                    right = STRING_VAL(rightStr);
                }
                
                // Concatenate the strings
                ObjString* leftStr = AS_STRING(left);
                ObjString* rightStr = AS_STRING(right);
                int newLength = leftStr->length + rightStr->length;
                
                // Use stack buffer for small strings, otherwise fall back to a StringBuilder
                if (newLength < VM_SMALL_STRING_BUFFER) {
                    char buffer[VM_SMALL_STRING_BUFFER];
                    memcpy(buffer, leftStr->chars, leftStr->length);
                    memcpy(buffer + leftStr->length, rightStr->chars, rightStr->length);
                    buffer[newLength] = '\0';
                    ObjString* result = allocateString(buffer, newLength);
                    vm_set_register_safe(dst, STRING_VAL(result));
                } else {
                    StringBuilder* sb = createStringBuilder(newLength + 1);
                    appendToStringBuilder(sb, leftStr->chars, leftStr->length);
                    appendToStringBuilder(sb, rightStr->chars, rightStr->length);
                    ObjString* result = stringBuilderToString(sb);
                    freeStringBuilder(sb);
                    vm_set_register_safe(dst, STRING_VAL(result));
                }
                DISPATCH();
            }
            
            // STRICT TYPE SAFETY: No automatic coercion, types must match exactly
            // Values already loaded above for string check
            
            // Enforce strict type matching - no coercion allowed
            if (val1.type != val2.type) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be the same type. Use 'as' for explicit type conversion.");
            }

            // Ensure both operands are numeric
            if (!(IS_I32(val1) || IS_I64(val1) || IS_U32(val1) || IS_U64(val1) || IS_F64(val1))) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be numeric (i32, i64, u32, u64, or f64)");
            }

#if USE_FAST_ARITH
            // Fast path: assume i32, no overflow checking
            int32_t a = AS_I32(val1);
            int32_t b = AS_I32(val2);
            vm_set_register_safe(dst, I32_VAL(a + b));
#else
            // Strict same-type arithmetic only (after coercion)
            if (IS_I32(val1)) {
                int32_t a = AS_I32(val1);
                int32_t b = AS_I32(val2);
                vm_set_register_safe(dst, I32_VAL(a + b));
            } else if (IS_I64(val1)) {
                int64_t a = AS_I64(val1);
                int64_t b = AS_I64(val2);
                vm_set_register_safe(dst, I64_VAL(a + b));
            } else if (IS_U32(val1)) {
                uint32_t a = AS_U32(val1);
                uint32_t b = AS_U32(val2);
                vm_set_register_safe(dst, U32_VAL(a + b));
            } else if (IS_U64(val1)) {
                uint64_t a = AS_U64(val1);
                uint64_t b = AS_U64(val2);
                vm_set_register_safe(dst, U64_VAL(a + b));
            } else if (IS_F64(val1)) {
                double a = AS_F64(val1);
                double b = AS_F64(val2);
                vm_set_register_safe(dst, F64_VAL(a + b));
            }
#endif
            DISPATCH();
        }

    LABEL_OP_SUB_I32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            
            // Strict type safety for numeric operations: both operands must be the same numeric type
            Value val1 = vm_get_register_safe(src1);
            Value val2 = vm_get_register_safe(src2);
            if (val1.type != val2.type) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be the same type. Use 'as' for explicit type conversion.");
            }

            // Ensure both operands are numeric
            if (!(IS_I32(val1) || IS_I64(val1) || IS_U32(val1) || IS_U64(val1) || IS_F64(val1))) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be numeric (i32, i64, u32, u64, or f64)");
            }

#if USE_FAST_ARITH
            // Fast path: assume i32, no overflow checking
            int32_t a = AS_I32(val1);
            int32_t b = AS_I32(val2);
            vm_set_register_safe(dst, I32_VAL(a - b));
#else
            // Strict same-type arithmetic only
            if (IS_I32(val1)) {
                int32_t a = AS_I32(val1);
                int32_t b = AS_I32(val2);
                vm_set_register_safe(dst, I32_VAL(a - b));
            } else if (IS_I64(val1)) {
                int64_t a = AS_I64(val1);
                int64_t b = AS_I64(val2);
                vm_set_register_safe(dst, I64_VAL(a - b));
            } else if (IS_U32(val1)) {
                uint32_t a = AS_U32(val1);
                uint32_t b = AS_U32(val2);
                vm_set_register_safe(dst, U32_VAL(a - b));
            } else if (IS_U64(val1)) {
                uint64_t a = AS_U64(val1);
                uint64_t b = AS_U64(val2);
                vm_set_register_safe(dst, U64_VAL(a - b));
            } else if (IS_F64(val1)) {
                double a = AS_F64(val1);
                double b = AS_F64(val2);
                vm_set_register_safe(dst, F64_VAL(a - b));
            }
#endif
            DISPATCH();
        }

    LABEL_OP_MUL_I32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            
            // STRICT TYPE SAFETY: No automatic coercion, types must match exactly
            Value val1 = vm_get_register_safe(src1);
            Value val2 = vm_get_register_safe(src2);
            
            // Enforce strict type matching - no coercion allowed
            if (val1.type != val2.type) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be the same type. Use 'as' for explicit type conversion.");
            }

            // Ensure both operands are numeric
            if (!(IS_I32(val1) || IS_I64(val1) || IS_U32(val1) || IS_U64(val1) || IS_F64(val1))) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be numeric (i32, i64, u32, u64, or f64)");
            }

#if USE_FAST_ARITH
            // Fast path: assume i32, no overflow checking
            int32_t a = AS_I32(val1);
            int32_t b = AS_I32(val2);
            vm_set_register_safe(dst, I32_VAL(a * b));
#else
            // Strict same-type arithmetic only (after coercion)
            if (IS_I32(val1)) {
                int32_t a = AS_I32(val1);
                int32_t b = AS_I32(val2);
                vm_set_register_safe(dst, I32_VAL(a * b));
            } else if (IS_I64(val1)) {
                int64_t a = AS_I64(val1);
                int64_t b = AS_I64(val2);
                vm_set_register_safe(dst, I64_VAL(a * b));
            } else if (IS_U32(val1)) {
                uint32_t a = AS_U32(val1);
                uint32_t b = AS_U32(val2);
                vm_set_register_safe(dst, U32_VAL(a * b));
            } else if (IS_U64(val1)) {
                uint64_t a = AS_U64(val1);
                uint64_t b = AS_U64(val2);
                vm_set_register_safe(dst, U64_VAL(a * b));
            } else if (IS_F64(val1)) {
                double a = AS_F64(val1);
                double b = AS_F64(val2);
                vm_set_register_safe(dst, F64_VAL(a * b));
            }
#endif
            DISPATCH();
        }

    LABEL_OP_DIV_I32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();

            // Strict type safety for numeric operations
            Value val1 = vm_get_register_safe(src1);
            Value val2 = vm_get_register_safe(src2);
            
            if (val1.type != val2.type) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be the same type. Use 'as' for explicit type conversion.");
            }

            if (!(IS_I32(val1) || IS_I64(val1) ||
                  IS_U32(val1) || IS_U64(val1) || IS_F64(val1))) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be numeric (i32, i64, u32, u64, or f64)");
            }

            if (IS_I32(val1)) {
                int32_t a = AS_I32(val1);
                int32_t b = AS_I32(val2);
                if (b == 0) {
                    VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Division by zero");
                }
                if (a == INT32_MIN && b == -1) {
                    VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Integer overflow");
                }
                vm_set_register_safe(dst, I32_VAL(a / b));
            } else if (IS_I64(val1)) {
                int64_t a = AS_I64(val1);
                int64_t b = AS_I64(val2);
                if (b == 0) {
                    VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Division by zero");
                }
                if (a == INT64_MIN && b == -1) {
                    VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Integer overflow");
                }
                vm_set_register_safe(dst, I64_VAL(a / b));
            } else if (IS_U32(val1)) {
                uint32_t a = AS_U32(val1);
                uint32_t b = AS_U32(val2);
                if (b == 0) {
                    VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Division by zero");
                }
                vm_set_register_safe(dst, U32_VAL(a / b));
            } else if (IS_U64(val1)) {
                uint64_t a = AS_U64(val1);
                uint64_t b = AS_U64(val2);
                if (b == 0) {
                    VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Division by zero");
                }
                vm_set_register_safe(dst, U64_VAL(a / b));
            } else {
                double a = AS_F64(val1);
                double b = AS_F64(val2);
                if (b == 0.0) {
                    VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Division by zero");
                }
                double res = a / b;
                if (!isfinite(res)) {
                    VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Floating-point overflow");
                }
                vm_set_register_safe(dst, F64_VAL(res));
            }
            DISPATCH();
        }

    LABEL_OP_MOD_I32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();

            // Strict type safety for numeric operations
            Value val1 = vm_get_register_safe(src1);
            Value val2 = vm_get_register_safe(src2);
            
            if (val1.type != val2.type) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be the same type. Use 'as' for explicit type conversion.");
            }

            if (!(IS_I32(val1) || IS_I64(val1) ||
                  IS_U32(val1) || IS_U64(val1) || IS_F64(val1))) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be numeric (i32, i64, u32, u64, or f64)");
            }

            if (IS_I32(val1)) {
                int32_t a = AS_I32(val1);
                int32_t b = AS_I32(val2);
                if (b == 0) {
                    VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Division by zero");
                }
                if (a == INT32_MIN && b == -1) {
                    VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Integer overflow");
                }
                vm_set_register_safe(dst, I32_VAL(a % b));
            } else if (IS_I64(val1)) {
                int64_t a = AS_I64(val1);
                int64_t b = AS_I64(val2);
                if (b == 0) {
                    VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Division by zero");
                }
                if (a == INT64_MIN && b == -1) {
                    VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Integer overflow");
                }
                vm_set_register_safe(dst, I64_VAL(a % b));
            } else if (IS_U32(val1)) {
                uint32_t a = AS_U32(val1);
                uint32_t b = AS_U32(val2);
                if (b == 0) {
                    VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Division by zero");
                }
                vm_set_register_safe(dst, U32_VAL(a % b));
            } else if (IS_U64(val1)) {
                uint64_t a = AS_U64(val1);
                uint64_t b = AS_U64(val2);
                if (b == 0) {
                    VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Division by zero");
                }
                vm_set_register_safe(dst, U64_VAL(a % b));
            } else {
                double a = AS_F64(val1);
                double b = AS_F64(val2);
                if (b == 0.0) {
                    VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Division by zero");
                }
                double res = fmod(a, b);
                if (!isfinite(res)) {
                    VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Floating-point overflow");
                }
                vm_set_register_safe(dst, F64_VAL(res));
            }
            DISPATCH();
        }

    LABEL_OP_INC_I32_R: {
            uint8_t reg = READ_BYTE();
            VM_HANDLE_INC_I32_SLOW_PATH(reg);
            DISPATCH();
        }

    LABEL_OP_INC_I32_CHECKED: {
            uint8_t reg = READ_BYTE();
            VM_HANDLE_INC_I32_SLOW_PATH(reg);
            DISPATCH();
        }

    LABEL_OP_INC_I64_R: {
            uint8_t reg = READ_BYTE();
            VM_HANDLE_INC_I64_SLOW_PATH(reg);
            DISPATCH();
        }

    LABEL_OP_INC_I64_CHECKED: {
            uint8_t reg = READ_BYTE();
            VM_HANDLE_INC_I64_SLOW_PATH(reg);
            DISPATCH();
        }

    LABEL_OP_INC_U32_R: {
            uint8_t reg = READ_BYTE();
            VM_HANDLE_INC_U32_SLOW_PATH(reg);
            DISPATCH();
        }

    LABEL_OP_INC_U32_CHECKED: {
            uint8_t reg = READ_BYTE();
            VM_HANDLE_INC_U32_SLOW_PATH(reg);
            DISPATCH();
        }

    LABEL_OP_INC_U64_R: {
            uint8_t reg = READ_BYTE();
            VM_HANDLE_INC_U64_SLOW_PATH(reg);
            DISPATCH();
        }

    LABEL_OP_INC_U64_CHECKED: {
            uint8_t reg = READ_BYTE();
            VM_HANDLE_INC_U64_SLOW_PATH(reg);
            DISPATCH();
        }

    LABEL_OP_DEC_I32_R: {
            uint8_t reg = READ_BYTE();
            const uint8_t typed_limit = (uint8_t)(sizeof(vm.typed_regs.i32_regs) / sizeof(vm.typed_regs.i32_regs[0]));

            if (reg < typed_limit && vm.typed_regs.reg_types[reg] == REG_TYPE_I32) {
    #if USE_FAST_ARITH
                int32_t result = vm.typed_regs.i32_regs[reg] - 1;
    #else
                int32_t result;
                if (__builtin_sub_overflow(vm.typed_regs.i32_regs[reg], 1, &result)) {
                    VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Integer overflow");
                }
    #endif
                vm.typed_regs.i32_regs[reg] = result;
                vm_set_register_safe(reg, I32_VAL(result));
            } else {
    #if USE_FAST_ARITH
                Value val = vm_get_register_safe(reg);
                vm_set_register_safe(reg, I32_VAL(AS_I32(val) - 1));
    #else
                Value val_reg = vm_get_register_safe(reg);
                int32_t val = AS_I32(val_reg);
                int32_t result;
                if (__builtin_sub_overflow(val, 1, &result)) {
                    VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Integer overflow");
                }
                vm_set_register_safe(reg, I32_VAL(result));
    #endif
            }
            DISPATCH();
        }

    LABEL_OP_NEG_I32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src = READ_BYTE();
            
            // Type safety: negation only works on numeric types
            Value val = vm_get_register_safe(src);
            if (!(IS_I32(val) || IS_I64(val) || IS_U32(val) || IS_U64(val) || IS_F64(val))) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Unary minus only works on numeric types (i32, i64, u32, u64, f64)");
            }
            
    #if USE_FAST_ARITH
            // Handle the detected type appropriately in fast path too
            if (IS_I32(val)) {
                vm_set_register_safe(dst, I32_VAL(-AS_I32(val)));
            } else if (IS_I64(val)) {
                vm_set_register_safe(dst, I64_VAL(-AS_I64(val)));
            } else if (IS_U32(val)) {
                vm_set_register_safe(dst, I32_VAL(-((int32_t)AS_U32(val))));
            } else if (IS_U64(val)) {
                vm_set_register_safe(dst, I64_VAL(-((int64_t)AS_U64(val))));
            } else if (IS_F64(val)) {
                vm_set_register_safe(dst, F64_VAL(-AS_F64(val)));
            }
    #else
            // Handle different numeric types appropriately
            if (IS_I32(val)) {
                int32_t int_val = AS_I32(val);
                if (int_val == INT32_MIN) {
                    VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Integer overflow: cannot negate INT32_MIN");
                }
                vm_set_register_safe(dst, I32_VAL(-int_val));
            } else if (IS_I64(val)) {
                int64_t int_val = AS_I64(val);
                vm_set_register_safe(dst, I64_VAL(-int_val));
            } else if (IS_U32(val)) {
                uint32_t int_val = AS_U32(val);
                // Convert to signed for negation
                vm_set_register_safe(dst, I32_VAL(-((int32_t)int_val)));
            } else if (IS_U64(val)) {
                uint64_t int_val = AS_U64(val);
                // Convert to signed for negation
                vm_set_register_safe(dst, I64_VAL(-((int64_t)int_val)));
            } else if (IS_F64(val)) {
                double double_val = AS_F64(val);
                vm_set_register_safe(dst, F64_VAL(-double_val));
            }
    #endif
            DISPATCH();
        }

    LABEL_OP_ADD_I64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            Value val1 = vm_get_register_safe(src1);
            Value val2 = vm_get_register_safe(src2);
            if (!IS_I64(val1) || !IS_I64(val2)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be i64");
            }
            int64_t a = AS_I64(val1);
            int64_t b = AS_I64(val2);
    #if USE_FAST_ARITH
            vm_set_register_safe(dst, I64_VAL(a + b));
    #else
            int64_t result;
            if (__builtin_add_overflow(a, b, &result)) {
                VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Integer overflow");
            }
            vm_set_register_safe(dst, I64_VAL(result));
    #endif
            DISPATCH();
        }

    LABEL_OP_SUB_I64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            Value val1 = vm_get_register_safe(src1);
            Value val2 = vm_get_register_safe(src2);
            if (!IS_I64(val1) || !IS_I64(val2)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be i64");
            }
            int64_t a = AS_I64(val1);
            int64_t b = AS_I64(val2);
    #if USE_FAST_ARITH
            vm_set_register_safe(dst, I64_VAL(a - b));
    #else
            int64_t result;
            if (__builtin_sub_overflow(a, b, &result)) {
                VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Integer overflow");
            }
            vm_set_register_safe(dst, I64_VAL(result));
    #endif
            DISPATCH();
        }

    LABEL_OP_MUL_I64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            Value val1 = vm_get_register_safe(src1);
            Value val2 = vm_get_register_safe(src2);
            if (!IS_I64(val1) || !IS_I64(val2)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be i64");
            }
            int64_t a = AS_I64(val1);
            int64_t b = AS_I64(val2);
    #if USE_FAST_ARITH
            vm_set_register_safe(dst, I64_VAL(a * b));
    #else
            int64_t result;
            if (__builtin_mul_overflow(a, b, &result)) {
                VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Integer overflow");
            }
            vm_set_register_safe(dst, I64_VAL(result));
    #endif
            DISPATCH();
        }

    LABEL_OP_DIV_I64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            Value val1 = vm_get_register_safe(src1);
            Value val2 = vm_get_register_safe(src2);
            if (!IS_I64(val1) || !IS_I64(val2)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be i64");
            }
            int64_t b = AS_I64(val2);
            if (b == 0) {
                VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Division by zero");
            }
            vm_set_register_safe(dst, I64_VAL(AS_I64(val1) / b));
            DISPATCH();
        }

    LABEL_OP_MOD_I64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            Value val1 = vm_get_register_safe(src1);
            Value val2 = vm_get_register_safe(src2);
            if (!IS_I64(val1) || !IS_I64(val2)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be i64");
            }
            int64_t b = AS_I64(val2);
            if (b == 0) {
                VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Division by zero");
            }
            vm_set_register_safe(dst, I64_VAL(AS_I64(val1) % b));
            DISPATCH();
        }

    LABEL_OP_ADD_U32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            Value val1 = vm_get_register_safe(src1);
            Value val2 = vm_get_register_safe(src2);
            if (!IS_U32(val1) || !IS_U32(val2)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be u32");
            }
            vm_set_register_safe(dst, U32_VAL(AS_U32(val1) + AS_U32(val2)));
            DISPATCH();
        }

    LABEL_OP_SUB_U32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            Value val1 = vm_get_register_safe(src1);
            Value val2 = vm_get_register_safe(src2);
            if (!IS_U32(val1) || !IS_U32(val2)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be u32");
            }
            vm_set_register_safe(dst, U32_VAL(AS_U32(val1) - AS_U32(val2)));
            DISPATCH();
        }

    LABEL_OP_MUL_U32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            Value val1 = vm_get_register_safe(src1);
            Value val2 = vm_get_register_safe(src2);
            if (!IS_U32(val1) || !IS_U32(val2)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be u32");
            }
            vm_set_register_safe(dst, U32_VAL(AS_U32(val1) * AS_U32(val2)));
            DISPATCH();
        }

    LABEL_OP_DIV_U32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            Value val1 = vm_get_register_safe(src1);
            Value val2 = vm_get_register_safe(src2);
            if (!IS_U32(val1) || !IS_U32(val2)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be u32");
            }
            uint32_t b = AS_U32(val2);
            if (b == 0) {
                VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Division by zero");
            }
            vm_set_register_safe(dst, U32_VAL(AS_U32(val1) / b));
            DISPATCH();
        }

    LABEL_OP_MOD_U32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            Value val1 = vm_get_register_safe(src1);
            Value val2 = vm_get_register_safe(src2);
            if (!IS_U32(val1) || !IS_U32(val2)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be u32");
            }
            uint32_t b = AS_U32(val2);
            if (b == 0) {
                VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Division by zero");
            }
            vm_set_register_safe(dst, U32_VAL(AS_U32(val1) % b));
            DISPATCH();
        }

    LABEL_OP_ADD_U64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();

            Value val1 = vm_get_register_safe(src1);
            Value val2 = vm_get_register_safe(src2);
            if (!IS_U64(val1) || !IS_U64(val2)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be u64");
            }

            uint64_t a = AS_U64(val1);
            uint64_t b = AS_U64(val2);
            
            // Check for overflow: if a + b < a, then overflow occurred
            if (UINT64_MAX - a < b) {
                VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "u64 addition overflow");
            }

            vm_set_register_safe(dst, U64_VAL(a + b));
            DISPATCH();
        }

    LABEL_OP_SUB_U64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();

            Value val1 = vm_get_register_safe(src1);
            Value val2 = vm_get_register_safe(src2);
            if (!IS_U64(val1) || !IS_U64(val2)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be u64");
            }

            uint64_t a = AS_U64(val1);
            uint64_t b = AS_U64(val2);
            
            // Check for underflow: if a < b, then underflow would occur
            if (a < b) {
                VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "u64 subtraction underflow");
            }

            vm_set_register_safe(dst, U64_VAL(a - b));
            DISPATCH();
        }

    LABEL_OP_MUL_U64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();

            Value val1 = vm_get_register_safe(src1);
            Value val2 = vm_get_register_safe(src2);
            if (!IS_U64(val1) || !IS_U64(val2)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be u64");
            }

            uint64_t a = AS_U64(val1);
            uint64_t b = AS_U64(val2);
            
            // Check for multiplication overflow: if a != 0 && result / a != b
            if (a != 0 && b > UINT64_MAX / a) {
                VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "u64 multiplication overflow");
            }

            vm_set_register_safe(dst, U64_VAL(a * b));
            DISPATCH();
        }

    LABEL_OP_DIV_U64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();

            Value val1 = vm_get_register_safe(src1);
            Value val2 = vm_get_register_safe(src2);
            if (!IS_U64(val1) || !IS_U64(val2)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be u64");
            }

            uint64_t b = AS_U64(val2);
            if (b == 0) {
                VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Division by zero");
            }

            vm_set_register_safe(dst, U64_VAL(AS_U64(val1) / b));
            DISPATCH();
        }

    LABEL_OP_MOD_U64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();

            Value val1 = vm_get_register_safe(src1);
            Value val2 = vm_get_register_safe(src2);
            if (!IS_U64(val1) || !IS_U64(val2)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be u64");
            }

            uint64_t b = AS_U64(val2);
            if (b == 0) {
                VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Division by zero");
            }

            vm_set_register_safe(dst, U64_VAL(AS_U64(val1) % b));
            DISPATCH();
        }

    LABEL_OP_BOOL_TO_I32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src = READ_BYTE();
             (void)READ_BYTE(); // Skip third operand (unused)
            Value src_val = vm_get_register_safe(src);
            if (!IS_BOOL(src_val)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be bool");
            }
            vm_set_register_safe(dst, I32_VAL(AS_BOOL(src_val) ? 1 : 0));
            DISPATCH();
        }

    LABEL_OP_BOOL_TO_I64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src = READ_BYTE();
             (void)READ_BYTE(); // Skip third operand (unused)
            Value src_val = vm_get_register_safe(src);
            if (!IS_BOOL(src_val)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be bool");
            }
            vm_set_register_safe(dst, I64_VAL(AS_BOOL(src_val) ? 1 : 0));
            DISPATCH();
        }

    LABEL_OP_BOOL_TO_U32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src = READ_BYTE();
             (void)READ_BYTE(); // Skip third operand (unused)
            Value src_val = vm_get_register_safe(src);
            if (!IS_BOOL(src_val)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be bool");
            }
            vm_set_register_safe(dst, U32_VAL(AS_BOOL(src_val) ? 1u : 0u));
            DISPATCH();
        }

    LABEL_OP_BOOL_TO_U64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src = READ_BYTE();
             (void)READ_BYTE(); // Skip third operand (unused)
            Value src_val = vm_get_register_safe(src);
            if (!IS_BOOL(src_val)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be bool");
            }
            vm_set_register_safe(dst, U64_VAL(AS_BOOL(src_val) ? 1ull : 0ull));
            DISPATCH();
        }

    LABEL_OP_BOOL_TO_F64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src = READ_BYTE();
             (void)READ_BYTE(); // Skip third operand (unused)
            Value src_val = vm_get_register_safe(src);
            if (!IS_BOOL(src_val)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be bool");
            }
            vm_set_register_safe(dst, F64_VAL(AS_BOOL(src_val) ? 1.0 : 0.0));
            DISPATCH();
        }

    LABEL_OP_I32_TO_I64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src = READ_BYTE();
             (void)READ_BYTE(); // Skip third operand (unused)
            Value src_val = vm_get_register_safe(src);
            if (!IS_I32(src_val)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be i32");
            }
            vm_set_register_safe(dst, I64_VAL((int64_t)AS_I32(src_val)));
            DISPATCH();
        }

    LABEL_OP_I32_TO_U32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src = READ_BYTE();
             (void)READ_BYTE(); // Skip third operand (unused)
            Value src_val = vm_get_register_safe(src);
            if (!IS_I32(src_val)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be i32");
            }
            vm_set_register_safe(dst, U32_VAL((uint32_t)AS_I32(src_val)));
            DISPATCH();
        }

    LABEL_OP_I32_TO_BOOL_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src = READ_BYTE();
             (void)READ_BYTE(); // Skip third operand (unused)
            Value src_val = vm_get_register_safe(src);
            if (!IS_I32(src_val)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be i32");
            }
            // Convert i32 to bool: 0 -> false, non-zero -> true
            vm_set_register_safe(dst, BOOL_VAL(AS_I32(src_val) != 0));
            DISPATCH();
        }

    LABEL_OP_U32_TO_I32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src = READ_BYTE();
             (void)READ_BYTE(); // Skip third operand (unused)
            Value src_val = vm_get_register_safe(src);
            if (!IS_U32(src_val)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be u32");
            }
            vm_set_register_safe(dst, I32_VAL((int32_t)AS_U32(src_val)));
            DISPATCH();
        }

    LABEL_OP_I64_TO_I32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src = READ_BYTE();
             (void)READ_BYTE(); // Skip third operand (unused)
            Value src_val = vm_get_register_safe(src);
            if (!IS_I64(src_val)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be i64");
            }
            vm_set_register_safe(dst, I32_VAL((int32_t)AS_I64(src_val)));
            DISPATCH();
        }

    LABEL_OP_I64_TO_BOOL_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src = READ_BYTE();
             (void)READ_BYTE(); // Skip third operand (unused)
            Value src_val = vm_get_register_safe(src);
            if (!IS_I64(src_val)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be i64");
            }
            vm_set_register_safe(dst, BOOL_VAL(AS_I64(src_val) != 0));
            DISPATCH();
        }

    // F64 Arithmetic Operations
    LABEL_OP_ADD_F64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            Value val1 = vm_get_register_safe(src1);
            Value val2 = vm_get_register_safe(src2);
            if (!IS_F64(val1) || !IS_F64(val2)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be f64");
            }
            vm_set_register_safe(dst, F64_VAL(AS_F64(val1) + AS_F64(val2)));
            DISPATCH();
        }

    LABEL_OP_SUB_F64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            Value val1 = vm_get_register_safe(src1);
            Value val2 = vm_get_register_safe(src2);
            if (!IS_F64(val1) || !IS_F64(val2)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be f64");
            }
            vm_set_register_safe(dst, F64_VAL(AS_F64(val1) - AS_F64(val2)));
            DISPATCH();
        }

    LABEL_OP_MUL_F64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            Value val1 = vm_get_register_safe(src1);
            Value val2 = vm_get_register_safe(src2);
            if (!IS_F64(val1) || !IS_F64(val2)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be f64");
            }
            vm_set_register_safe(dst, F64_VAL(AS_F64(val1) * AS_F64(val2)));
            DISPATCH();
        }

    LABEL_OP_DIV_F64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            Value val1 = vm_get_register_safe(src1);
            Value val2 = vm_get_register_safe(src2);
            if (!IS_F64(val1) || !IS_F64(val2)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be f64");
            }
            double a = AS_F64(val1);
            double b = AS_F64(val2);
            
            // IEEE 754 compliant: division by zero produces infinity, not error
            double result = a / b;
            
            // The result may be infinity, -infinity, or NaN
            // These are valid f64 values according to IEEE 754
            vm_set_register_safe(dst, F64_VAL(result));
            DISPATCH();
        }

    LABEL_OP_MOD_F64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            Value val1 = vm_get_register_safe(src1);
            Value val2 = vm_get_register_safe(src2);
            if (!IS_F64(val1) || !IS_F64(val2)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be f64");
            }
            double a = AS_F64(val1);
            double b = AS_F64(val2);
            
            // IEEE 754 compliant: use fmod for floating point modulo
            double result = fmod(a, b);
            
            // The result may be infinity, -infinity, or NaN
            // These are valid f64 values according to IEEE 754
            vm_set_register_safe(dst, F64_VAL(result));
            DISPATCH();
        }

    // Bitwise Operations
    LABEL_OP_AND_I32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            Value val1 = vm_get_register_safe(src1);
            Value val2 = vm_get_register_safe(src2);
            if (!IS_I32(val1) || !IS_I32(val2)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be i32");
            }
            vm_set_register_safe(dst, I32_VAL(AS_I32(val1) & AS_I32(val2)));
            DISPATCH();
        }

    LABEL_OP_OR_I32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            Value val1 = vm_get_register_safe(src1);
            Value val2 = vm_get_register_safe(src2);
            if (!IS_I32(val1) || !IS_I32(val2)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be i32");
            }
            vm_set_register_safe(dst, I32_VAL(AS_I32(val1) | AS_I32(val2)));
            DISPATCH();
        }

    LABEL_OP_XOR_I32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            Value val1 = vm_get_register_safe(src1);
            Value val2 = vm_get_register_safe(src2);
            if (!IS_I32(val1) || !IS_I32(val2)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be i32");
            }
            vm_set_register_safe(dst, I32_VAL(AS_I32(val1) ^ AS_I32(val2)));
            DISPATCH();
        }

    LABEL_OP_NOT_I32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src = READ_BYTE();
            Value src_val = vm_get_register_safe(src);
            if (!IS_I32(src_val)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operand must be i32");
            }
            vm_set_register_safe(dst, I32_VAL(~AS_I32(src_val)));
            DISPATCH();
        }

    LABEL_OP_SHL_I32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            Value val1 = vm_get_register_safe(src1);
            Value val2 = vm_get_register_safe(src2);
            if (!IS_I32(val1) || !IS_I32(val2)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be i32");
            }
            vm_set_register_safe(dst, I32_VAL(AS_I32(val1) << AS_I32(val2)));
            DISPATCH();
        }

    LABEL_OP_SHR_I32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            Value val1 = vm_get_register_safe(src1);
            Value val2 = vm_get_register_safe(src2);
            if (!IS_I32(val1) || !IS_I32(val2)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be i32");
            }
            vm_set_register_safe(dst, I32_VAL(AS_I32(val1) >> AS_I32(val2)));
            DISPATCH();
        }

    // F64 Comparison Operations
    LABEL_OP_LT_F64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            CMP_F64_LT(dst, src1, src2);
            DISPATCH();
        }

    LABEL_OP_LE_F64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            CMP_F64_LE(dst, src1, src2);
            DISPATCH();
        }

    LABEL_OP_GT_F64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            CMP_F64_GT(dst, src1, src2);
            DISPATCH();
        }

    LABEL_OP_GE_F64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            CMP_F64_GE(dst, src1, src2);
            DISPATCH();
        }

    // F64 Type Conversion Operations
    LABEL_OP_I32_TO_F64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src = READ_BYTE();
             (void)READ_BYTE(); // Skip third operand (unused)
            Value src_val = vm_get_register_safe(src);
            if (!IS_I32(src_val)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be i32");
            }
            vm_set_register_safe(dst, F64_VAL((double)AS_I32(src_val)));
            DISPATCH();
        }

    LABEL_OP_I64_TO_F64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src = READ_BYTE();
             (void)READ_BYTE(); // Skip third operand (unused)
            Value src_val = vm_get_register_safe(src);
            if (!IS_I64(src_val)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be i64");
            }
            vm_set_register_safe(dst, F64_VAL((double)AS_I64(src_val)));
            DISPATCH();
        }

    LABEL_OP_F64_TO_I32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src = READ_BYTE();
             (void)READ_BYTE(); // Skip third operand (unused)
            Value src_val = vm_get_register_safe(src);
            if (!IS_F64(src_val)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be f64");
            }
            vm_set_register_safe(dst, I32_VAL((int32_t)AS_F64(src_val)));
            DISPATCH();
        }

    LABEL_OP_F64_TO_I64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src = READ_BYTE();
             (void)READ_BYTE(); // Skip third operand (unused)
            Value src_val = vm_get_register_safe(src);
            if (!IS_F64(src_val)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be f64");
            }
            vm_set_register_safe(dst, I64_VAL((int64_t)AS_F64(src_val)));
            DISPATCH();
        }

    // U64 cast handlers
    LABEL_OP_I32_TO_U64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src = READ_BYTE();
             (void)READ_BYTE(); // Skip third operand (unused)
            Value src_val = vm_get_register_safe(src);
            if (!IS_I32(src_val)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be i32");
            }
            int32_t val = AS_I32(src_val);
            if (val < 0) {
                VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Cannot convert negative i32 to u64");
            }
            vm_set_register_safe(dst, U64_VAL((uint64_t)val));
            DISPATCH();
        }

    LABEL_OP_I64_TO_U64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src = READ_BYTE();
             (void)READ_BYTE(); // Skip third operand (unused)
            Value src_val = vm_get_register_safe(src);
            if (!IS_I64(src_val)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be i64");
            }
            int64_t val = AS_I64(src_val);
            if (val < 0) {
                VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Cannot convert negative i64 to u64");
            }
            vm_set_register_safe(dst, U64_VAL((uint64_t)val));
            DISPATCH();
        }

    LABEL_OP_U64_TO_I32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src = READ_BYTE();
             (void)READ_BYTE(); // Skip third operand (unused)
            Value src_val = vm_get_register_safe(src);
            if (!IS_U64(src_val)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be u64");
            }
            uint64_t val = AS_U64(src_val);
            if (val > (uint64_t)INT32_MAX) {
                VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "u64 value too large for i32");
            }
            vm_set_register_safe(dst, I32_VAL((int32_t)val));
            DISPATCH();
        }

    LABEL_OP_U64_TO_I64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src = READ_BYTE();
             (void)READ_BYTE(); // Skip third operand (unused)
            Value src_val = vm_get_register_safe(src);
            if (!IS_U64(src_val)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be u64");
            }
            uint64_t val = AS_U64(src_val);
            if (val > (uint64_t)INT64_MAX) {
                VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "u64 value too large for i64");
            }
            vm_set_register_safe(dst, I64_VAL((int64_t)val));
            DISPATCH();
        }

    LABEL_OP_U32_TO_BOOL_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src = READ_BYTE();
             (void)READ_BYTE(); // Skip third operand (unused)
            Value src_val = vm_get_register_safe(src);
            if (!IS_U32(src_val)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be u32");
            }
            vm_set_register_safe(dst, BOOL_VAL(AS_U32(src_val) != 0));
            DISPATCH();
        }

    LABEL_OP_U32_TO_U64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src = READ_BYTE();
             (void)READ_BYTE(); // Skip third operand (unused)
            Value src_val = vm_get_register_safe(src);
            if (!IS_U32(src_val)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be u32");
            }
            vm_set_register_safe(dst, U64_VAL((uint64_t)AS_U32(src_val)));
            DISPATCH();
        }

    LABEL_OP_U64_TO_U32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src = READ_BYTE();
             (void)READ_BYTE(); // Skip third operand (unused)
            Value src_val = vm_get_register_safe(src);
            if (!IS_U64(src_val)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be u64");
            }
            uint64_t val = AS_U64(src_val);
            if (val > (uint64_t)UINT32_MAX) {
                VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "u64 value too large for u32");
            }
            vm_set_register_safe(dst, U32_VAL((uint32_t)val));
            DISPATCH();
        }

    LABEL_OP_F64_TO_U64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src = READ_BYTE();
             (void)READ_BYTE(); // Skip third operand (unused)
            Value src_val = vm_get_register_safe(src);
            if (!IS_F64(src_val)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be f64");
            }
            double val = AS_F64(src_val);
            if (val < 0.0 || val > (double)UINT64_MAX) {
                VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "f64 value out of u64 range");
            }
            vm_set_register_safe(dst, U64_VAL((uint64_t)val));
            DISPATCH();
        }

    LABEL_OP_U64_TO_F64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src = READ_BYTE();
             (void)READ_BYTE(); // Skip third operand (unused)
            Value src_val = vm_get_register_safe(src);
            if (!IS_U64(src_val)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be u64");
            }
            vm_set_register_safe(dst, F64_VAL((double)AS_U64(src_val)));
            DISPATCH();
        }

    LABEL_OP_U64_TO_BOOL_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src = READ_BYTE();
             (void)READ_BYTE(); // Skip third operand (unused)
            Value src_val = vm_get_register_safe(src);
            if (!IS_U64(src_val)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be u64");
            }
            vm_set_register_safe(dst, BOOL_VAL(AS_U64(src_val) != 0));
            DISPATCH();
        }

    LABEL_OP_F64_TO_BOOL_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src = READ_BYTE();
             (void)READ_BYTE(); // Skip third operand (unused)
            Value src_val = vm_get_register_safe(src);
            if (!IS_F64(src_val)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be f64");
            }
            vm_set_register_safe(dst, BOOL_VAL(AS_F64(src_val) != 0.0));
            DISPATCH();
        }

    LABEL_OP_U32_TO_F64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src = READ_BYTE();
             (void)READ_BYTE(); // Skip third operand (unused)
            Value src_val = vm_get_register_safe(src);
            if (!IS_U32(src_val)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be u32");
            }
            vm_set_register_safe(dst, F64_VAL((double)AS_U32(src_val)));
            DISPATCH();
        }

    LABEL_OP_F64_TO_U32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src = READ_BYTE();
             (void)READ_BYTE(); // Skip third operand (unused)
            Value src_val = vm_get_register_safe(src);
            if (!IS_F64(src_val)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be f64");
            }
            double val = AS_F64(src_val);
            if (val < 0.0 || val > (double)UINT32_MAX) {
                VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "f64 value out of u32 range");
            }
            vm_set_register_safe(dst, U32_VAL((uint32_t)val));
            DISPATCH();
        }

    LABEL_OP_LT_I32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            CMP_I32_LT(dst, src1, src2);
            DISPATCH();
        }

    LABEL_OP_EQ_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        CMP_EQ(dst, src1, src2);
        DISPATCH();
    }

    LABEL_OP_NE_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        CMP_NE(dst, src1, src2);
        DISPATCH();
    }

    LABEL_OP_LE_I32_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        CMP_I32_LE(dst, src1, src2);
        DISPATCH();
    }

    LABEL_OP_GT_I32_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        CMP_I32_GT(dst, src1, src2);
        DISPATCH();
    }

    LABEL_OP_GE_I32_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        CMP_I32_GE(dst, src1, src2);
        DISPATCH();
    }

    LABEL_OP_LT_I64_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        CMP_I64_LT(dst, src1, src2);
        DISPATCH();
    }

    LABEL_OP_LE_I64_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        CMP_I64_LE(dst, src1, src2);
        DISPATCH();
    }

    LABEL_OP_GT_I64_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        CMP_I64_GT(dst, src1, src2);
        DISPATCH();
    }

    LABEL_OP_GE_I64_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        CMP_I64_GE(dst, src1, src2);
        DISPATCH();
    }

    LABEL_OP_LT_U32_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        CMP_U32_LT(dst, src1, src2);
        DISPATCH();
    }

    LABEL_OP_LE_U32_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        CMP_U32_LE(dst, src1, src2);
        DISPATCH();
    }

    LABEL_OP_GT_U32_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        CMP_U32_GT(dst, src1, src2);
        DISPATCH();
    }

    LABEL_OP_GE_U32_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        CMP_U32_GE(dst, src1, src2);
        DISPATCH();
    }

    LABEL_OP_LT_U64_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        CMP_U64_LT(dst, src1, src2);
        DISPATCH();
    }

    LABEL_OP_LE_U64_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        CMP_U64_LE(dst, src1, src2);
        DISPATCH();
    }

    LABEL_OP_GT_U64_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        CMP_U64_GT(dst, src1, src2);
        DISPATCH();
    }

    LABEL_OP_GE_U64_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        CMP_U64_GE(dst, src1, src2);
        DISPATCH();
    }

    LABEL_OP_AND_BOOL_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        
        bool left_bool = vm_register_is_truthy(src1);
        bool right_bool = vm_register_is_truthy(src2);

        vm_set_register_safe(dst, BOOL_VAL(left_bool && right_bool));
        DISPATCH();
    }

    LABEL_OP_OR_BOOL_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();

        bool left_bool = vm_register_is_truthy(src1);
        bool right_bool = vm_register_is_truthy(src2);

        vm_set_register_safe(dst, BOOL_VAL(left_bool || right_bool));
        DISPATCH();
    }

    LABEL_OP_NOT_BOOL_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src = READ_BYTE();

        bool src_bool = vm_register_is_truthy(src);

        vm_set_register_safe(dst, BOOL_VAL(!src_bool));
        DISPATCH();
    }

    LABEL_OP_CONCAT_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        Value left_val = vm_get_register_safe(src1);
        Value right_val = vm_get_register_safe(src2);
        if (!IS_STRING(left_val) || !IS_STRING(right_val)) {
            VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be string");
        }
        ObjString* a = AS_STRING(left_val);
        ObjString* b = AS_STRING(right_val);
        int newLen = a->length + b->length;
        char* buf = malloc(newLen + 1);
        memcpy(buf, a->chars, a->length);
        memcpy(buf + a->length, b->chars, b->length);
        buf[newLen] = '\0';
        ObjString* res = allocateString(buf, newLen);
        free(buf);
        vm_set_register_safe(dst, STRING_VAL(res));
        DISPATCH();
    }

    LABEL_OP_MAKE_ARRAY_R: {
        uint8_t dst = READ_BYTE();
        uint8_t first = READ_BYTE();
        uint8_t count = READ_BYTE();

        ObjArray* array = allocateArray(count);
        if (!array) {
            VM_ERROR_RETURN(ERROR_RUNTIME, CURRENT_LOCATION(), "Failed to allocate array");
        }

        for (uint8_t i = 0; i < count; i++) {
            arrayEnsureCapacity(array, i + 1);
            array->elements[i] = vm_get_register_safe(first + i);
        }
        array->length = count;
        Value new_array = {.type = VAL_ARRAY, .as.obj = (Obj*)array};
        vm_set_register_safe(dst, new_array);
        DISPATCH();
    }

    LABEL_OP_ENUM_NEW_R: {
        uint8_t dst = READ_BYTE();
        uint8_t variantIndex = READ_BYTE();
        uint8_t payloadCount = READ_BYTE();
        uint8_t payloadStart = READ_BYTE();
        uint16_t typeConstIndex = READ_SHORT();
        uint16_t variantConstIndex = READ_SHORT();

        Value typeConst = READ_CONSTANT(typeConstIndex);
        if (!IS_STRING(typeConst)) {
            VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(),
                            "Enum constructor requires string type name constant");
        }

        ObjString* typeName = AS_STRING(typeConst);
        ObjString* variantName = NULL;
        Value variantConst = READ_CONSTANT(variantConstIndex);
        if (IS_STRING(variantConst)) {
            variantName = AS_STRING(variantConst);
        }

        Value payload_values[UINT8_MAX];
        const Value* payload_ptr = NULL;
        if (payloadCount > 0) {
            if ((int)payloadStart + payloadCount > REGISTER_COUNT) {
                VM_ERROR_RETURN(ERROR_RUNTIME, CURRENT_LOCATION(),
                                "Enum constructor payload exceeds register bounds");
            }
            for (uint8_t i = 0; i < payloadCount; i++) {
                payload_values[i] = vm_get_register_safe(payloadStart + i);
            }
            payload_ptr = payload_values;
        }

        TaggedUnionSpec spec = {
            .type_name = typeName->chars,
            .variant_name = variantName ? variantName->chars : NULL,
            .variant_index = variantIndex,
            .payload = payload_ptr,
            .payload_count = payloadCount,
        };

        Value enum_value;
        if (!vm_make_tagged_union(&spec, &enum_value)) {
            VM_ERROR_RETURN(ERROR_RUNTIME, CURRENT_LOCATION(), "Failed to allocate enum instance");
        }

        vm_set_register_safe(dst, enum_value);
        DISPATCH();
    }

    LABEL_OP_ENUM_TAG_EQ_R: {
        uint8_t dst = READ_BYTE();
        uint8_t enum_reg = READ_BYTE();
        uint8_t variantIndex = READ_BYTE();

        Value value = vm_get_register_safe(enum_reg);
        if (!IS_ENUM(value)) {
            VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Match subject is not an enum value");
        }

        ObjEnumInstance* instance = AS_ENUM(value);
        bool match = (instance && instance->variantIndex == variantIndex);
        vm_set_register_safe(dst, BOOL_VAL(match));
        DISPATCH();
    }

    LABEL_OP_ENUM_PAYLOAD_R: {
        uint8_t dst = READ_BYTE();
        uint8_t enum_reg = READ_BYTE();
        uint8_t variantIndex = READ_BYTE();
        uint8_t fieldIndex = READ_BYTE();

        Value value = vm_get_register_safe(enum_reg);
        if (!IS_ENUM(value)) {
            VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Attempted to destructure a non-enum value");
        }

        ObjEnumInstance* instance = AS_ENUM(value);
        if (!instance || instance->variantIndex != variantIndex) {
            const char* typeName = instance && instance->typeName ? instance->typeName->chars : "enum";
            VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(),
                            "Match arm expected %s variant index %u", typeName, variantIndex);
        }

        ObjArray* payload = instance->payload;
        if (!payload || fieldIndex >= payload->length) {
            VM_ERROR_RETURN(ERROR_RUNTIME, CURRENT_LOCATION(), "Enum payload index out of range");
        }

        vm_set_register_safe(dst, payload->elements[fieldIndex]);
        DISPATCH();
    }

    LABEL_OP_STRING_INDEX_R: {
        uint8_t dst = READ_BYTE();
        uint8_t string_reg = READ_BYTE();
        uint8_t index_reg = READ_BYTE();

        Value string_value = vm_get_register_safe(string_reg);
        if (!IS_STRING(string_value)) {
            VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Value is not a string");
        }

        int index;
        if (!value_to_index(vm_get_register_safe(index_reg), &index)) {
            VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(),
                            "String index must be a non-negative integer");
        }

        ObjString* source = AS_STRING(string_value);
        if (!source || index < 0 || index >= source->length) {
            VM_ERROR_RETURN(ERROR_INDEX, CURRENT_LOCATION(), "String index out of bounds");
        }

        ObjString* result = NULL;
        if (source->rope) {
            result = rope_index_to_string(source->rope, (size_t)index);
        }

        if (!result) {
            char buffer[2];
            buffer[0] = source->chars[index];
            buffer[1] = '\0';
            result = allocateString(buffer, 1);
        }

        if (!result) {
            VM_ERROR_RETURN(ERROR_RUNTIME, CURRENT_LOCATION(),
                            "Failed to extract string character");
        }

        vm_set_register_safe(dst, STRING_VAL(result));
        DISPATCH();
    }

    LABEL_OP_STRING_GET_R: {
        uint8_t dst = READ_BYTE();
        uint8_t string_reg = READ_BYTE();
        uint8_t index_reg = READ_BYTE();

        Value string_value = vm_get_register_safe(string_reg);
        if (!IS_STRING(string_value)) {
            VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Value is not a string");
        }

        int index;
        if (!value_to_index(vm_get_register_safe(index_reg), &index)) {
            VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(),
                            "String index must be a non-negative integer");
        }

        ObjString* source = AS_STRING(string_value);
        if (index < 0 || index >= source->length) {
            VM_ERROR_RETURN(ERROR_INDEX, CURRENT_LOCATION(),
                            "String index out of bounds");
        }

        ObjString* ch = string_char_at(source, (size_t)index);
        if (!ch) {
            VM_ERROR_RETURN(ERROR_RUNTIME, CURRENT_LOCATION(),
                            "Failed to extract string character");
        }

        vm_set_register_safe(dst, STRING_VAL(ch));
        DISPATCH();
    }

    LABEL_OP_ARRAY_GET_R: {
        uint8_t dst = READ_BYTE();
        uint8_t array_reg = READ_BYTE();
        uint8_t index_reg = READ_BYTE();

        Value array_value = vm_get_register_safe(array_reg);
        if (!IS_ARRAY(array_value)) {
            VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Value is not an array");
        }

        Value index_value = vm_get_register_safe(index_reg);
        int index;
        if (!value_to_index(index_value, &index)) {
            VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Array index must be a non-negative integer");
        }

        Value element;
        if (!arrayGet(AS_ARRAY(array_value), index, &element)) {
            VM_ERROR_RETURN(ERROR_INDEX, CURRENT_LOCATION(), "Array index out of bounds");
        }

        vm_set_register_safe(dst, element);
        DISPATCH();
    }

    LABEL_OP_ARRAY_SET_R: {
        uint8_t array_reg = READ_BYTE();
        uint8_t index_reg = READ_BYTE();
        uint8_t value_reg = READ_BYTE();

        Value array_value = vm_get_register_safe(array_reg);
        if (!IS_ARRAY(array_value)) {
            VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Value is not an array");
        }

        Value index_value = vm_get_register_safe(index_reg);
        int index;
        if (!value_to_index(index_value, &index)) {
            VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Array index must be a non-negative integer");
        }

        Value value = vm_get_register_safe(value_reg);
        if (!arraySet(AS_ARRAY(array_value), index, value)) {
            VM_ERROR_RETURN(ERROR_INDEX, CURRENT_LOCATION(), "Array index out of bounds");
        }

        DISPATCH();
    }

    LABEL_OP_ARRAY_LEN_R: {
        uint8_t dst = READ_BYTE();
        uint8_t array_reg = READ_BYTE();

        Value array_value = vm_get_register_safe(array_reg);
        if (!IS_ARRAY(array_value)) {
            VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Value is not an array");
        }

        ObjArray* array = AS_ARRAY(array_value);
        vm_set_register_safe(dst, I32_VAL(array->length));
        DISPATCH();
    }

    LABEL_OP_ARRAY_PUSH_R: {
        uint8_t array_reg = READ_BYTE();
        uint8_t value_reg = READ_BYTE();

        Value array_value = vm_get_register_safe(array_reg);
        if (!builtin_array_push(array_value, vm_get_register_safe(value_reg))) {
            if (!IS_ARRAY(array_value)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Value is not an array");
            }
            VM_ERROR_RETURN(ERROR_RUNTIME, CURRENT_LOCATION(), "Failed to push value onto array");
        }
        DISPATCH();
    }

    LABEL_OP_ARRAY_POP_R: {
        uint8_t dst = READ_BYTE();
        uint8_t array_reg = READ_BYTE();

        Value array_value = vm_get_register_safe(array_reg);
        Value popped;
        if (!builtin_array_pop(array_value, &popped)) {
            if (!IS_ARRAY(array_value)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Value is not an array");
            }
            VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Cannot pop from an empty array");
        }

        vm_set_register_safe(dst, popped);
        DISPATCH();
    }

    LABEL_OP_ARRAY_SORTED_R: {
        uint8_t dst = READ_BYTE();
        uint8_t array_reg = READ_BYTE();

        Value array_value = vm_get_register_safe(array_reg);
        Value result;
        if (!builtin_sorted(array_value, &result)) {
            if (!IS_ARRAY(array_value)) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Value is not an array");
            }
            VM_ERROR_RETURN(ERROR_RUNTIME, CURRENT_LOCATION(),
                            "sorted() requires an array of comparable elements");
        }

        vm_set_register_safe(dst, result);
        DISPATCH();
    }

    LABEL_OP_ARRAY_SLICE_R: {
        uint8_t dst = READ_BYTE();
        uint8_t array_reg = READ_BYTE();
        uint8_t start_reg = READ_BYTE();
        uint8_t end_reg = READ_BYTE();

        Value array_value = vm_get_register_safe(array_reg);
        if (!IS_ARRAY(array_value)) {
            VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Value is not an array");
        }

        Value start_value = vm_get_register_safe(start_reg);
        Value end_value = vm_get_register_safe(end_reg);

        int start_index;
        if (!value_to_index(start_value, &start_index)) {
            VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Array slice start must be a non-negative integer");
        }

        int end_index;
        if (!value_to_index(end_value, &end_index)) {
            VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Array slice end must be a non-negative integer");
        }

        ObjArray* array = AS_ARRAY(array_value);
        int array_length = array->length;
        if (start_index < 0 || start_index > array_length) {
            VM_ERROR_RETURN(ERROR_INDEX, CURRENT_LOCATION(), "Array slice start out of bounds");
        }
        if (end_index < 0) {
            VM_ERROR_RETURN(ERROR_INDEX, CURRENT_LOCATION(), "Array slice end before start");
        }
        if (end_index > array_length) {
            VM_ERROR_RETURN(ERROR_INDEX, CURRENT_LOCATION(), "Array slice end out of bounds");
        }

        int slice_length = 0;
        if (start_index == array_length) {
            if (end_index != array_length) {
                VM_ERROR_RETURN(ERROR_INDEX, CURRENT_LOCATION(), "Array slice end before start");
            }
            slice_length = 0;
        } else {
            int normalized_end = end_index == array_length ? array_length - 1 : end_index;
            if (normalized_end < start_index) {
                VM_ERROR_RETURN(ERROR_INDEX, CURRENT_LOCATION(), "Array slice end before start");
            }
            slice_length = normalized_end - start_index + 1;
        }

        ObjArray* result = allocateArray(slice_length);
        if (!result) {
            VM_ERROR_RETURN(ERROR_RUNTIME, CURRENT_LOCATION(), "Failed to allocate array slice");
        }

        if (slice_length > 0) {
            arrayEnsureCapacity(result, slice_length);
            for (int i = 0; i < slice_length; i++) {
                result->elements[i] = array->elements[start_index + i];
            }
        }
        result->length = slice_length;

        Value slice_value = {.type = VAL_ARRAY, .as.obj = (Obj*)result};
        vm_set_register_safe(dst, slice_value);
        DISPATCH();
    }

    LABEL_OP_TO_STRING_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src = READ_BYTE();
        (void)READ_BYTE();  // Padding byte emitted by generic instruction helper
        Value val = vm_get_register_safe(src);
        char buffer[64];

        if (IS_I32(val)) {
            snprintf(buffer, sizeof(buffer), "%d", AS_I32(val));
        } else if (IS_I64(val)) {
            snprintf(buffer, sizeof(buffer), "%lld", (long long)AS_I64(val));
        } else if (IS_U32(val)) {
            snprintf(buffer, sizeof(buffer), "%u", AS_U32(val));
        } else if (IS_U64(val)) {
            snprintf(buffer, sizeof(buffer), "%llu", (unsigned long long)AS_U64(val));
        } else if (IS_F64(val)) {
            snprintf(buffer, sizeof(buffer), "%g", AS_F64(val));
        } else if (IS_BOOL(val)) {
            snprintf(buffer, sizeof(buffer), "%s", AS_BOOL(val) ? "true" : "false");
        } else if (IS_STRING(val)) {
            // Already a string, just copy
            vm_set_register_safe(dst, val);
            DISPATCH();
        } else {
            snprintf(buffer, sizeof(buffer), "nil");
        }
        
        ObjString* result = allocateString(buffer, (int)strlen(buffer));
        vm_set_register_safe(dst, STRING_VAL(result));
        DISPATCH();
    }

    LABEL_OP_TRY_BEGIN: {
            uint8_t reg = READ_BYTE();
            uint16_t offset = READ_SHORT();
            if (vm.tryFrameCount >= TRY_MAX) {
                VM_ERROR_RETURN(ERROR_RUNTIME, CURRENT_LOCATION(), "Too many nested try blocks");
            }
            TryFrame* frame = &vm.tryFrames[vm.tryFrameCount++];
            frame->handler = vm.ip + offset;
            frame->catchRegister = (reg == 0xFF) ? TRY_CATCH_REGISTER_NONE : (uint16_t)reg;
            frame->stackDepth = vm.frameCount;
            DISPATCH();
        }

    LABEL_OP_TRY_END: {
            if (vm.tryFrameCount <= 0) {
                VM_ERROR_RETURN(ERROR_RUNTIME, CURRENT_LOCATION(), "TRY_END without matching TRY_BEGIN");
            }
            vm.tryFrameCount--;
            DISPATCH();
        }

    LABEL_OP_THROW: {
            uint8_t reg = READ_BYTE();
            Value err = vm_get_register_safe(reg);
            if (!IS_ERROR(err)) {
                if (IS_STRING(err)) {
                    ObjString* message = AS_STRING(err);
                    ObjError* converted = allocateError(ERROR_RUNTIME, message->chars, CURRENT_LOCATION());
                    if (!converted) {
                        VM_ERROR_RETURN(ERROR_RUNTIME, CURRENT_LOCATION(),
                                        "Failed to allocate error for throw");
                    }
                    err = ERROR_VAL(converted);
                    vm_set_register_safe(reg, err);
                } else {
                    VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(),
                                    "throw expects an error or string value");
                }
            }
            vm.lastError = err;
            vm_set_error_report_pending(true);
            goto HANDLE_RUNTIME_ERROR;
        }

    LABEL_OP_JUMP: {
            uint16_t offset = READ_SHORT();
            if (!CF_JUMP(offset)) {
                RETURN(INTERPRET_RUNTIME_ERROR);
            }
            DISPATCH();
        }

    LABEL_OP_JUMP_IF_NOT_R: {
            uint8_t reg = READ_BYTE();
            uint16_t offset = READ_SHORT();
            if (!CF_JUMP_IF_NOT(reg, offset)) {
                RETURN(INTERPRET_RUNTIME_ERROR);
            }
            DISPATCH();
        }

    LABEL_OP_JUMP_IF_NOT_I32_TYPED: {
            uint8_t left = READ_BYTE();
            uint8_t right = READ_BYTE();
            uint16_t offset = READ_SHORT();
            if (!CF_JUMP_IF_NOT_I32_TYPED(left, right, offset)) {
                RETURN(INTERPRET_RUNTIME_ERROR);
            }
            DISPATCH();
        }

    LABEL_OP_LOOP: {
        uint16_t offset = READ_SHORT();

        // Hot path detection: Profile loop iterations
        if (g_profiling.isActive && (g_profiling.enabledFlags & PROFILE_HOT_PATHS)) {
            static uint64_t loop_iterations = 0;
            loop_iterations++;
            profileHotPath((void*)(vm.ip - vm.chunk->code), loop_iterations);
        }

        if (!CF_LOOP(offset)) {
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        DISPATCH();
    }

    LABEL_OP_GET_ITER_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src = READ_BYTE();
        Value iterable = vm_get_register_safe(src);

        vm_set_register_safe(dst, iterable);

        if (IS_RANGE_ITERATOR(iterable) || IS_ARRAY_ITERATOR(iterable)) {
            DISPATCH();
        }

        if (IS_I32(iterable) || IS_I64(iterable) || IS_U32(iterable) || IS_U64(iterable)) {
            int64_t count = 0;
            if (IS_I32(iterable)) {
                count = (int64_t)AS_I32(iterable);
            } else if (IS_I64(iterable)) {
                count = AS_I64(iterable);
            } else if (IS_U32(iterable)) {
                count = (int64_t)AS_U32(iterable);
            } else {
                uint64_t unsigned_count = AS_U64(iterable);
                if (unsigned_count > (uint64_t)INT64_MAX) {
                    VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Integer too large to iterate");
                }
                count = (int64_t)unsigned_count;
            }

            if (count < 0) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Cannot iterate negative integer");
            }

            ObjRangeIterator* iterator = allocateRangeIterator(0, count, 1);
            if (!iterator) {
                VM_ERROR_RETURN(ERROR_RUNTIME, CURRENT_LOCATION(), "Failed to allocate range iterator");
            }

            Value iterator_value = {.type = VAL_RANGE_ITERATOR, .as.obj = (Obj*)iterator};
            vm_set_register_safe(dst, iterator_value);
            DISPATCH();
        }

        if (IS_ARRAY(iterable)) {
            ObjArray* array = AS_ARRAY(iterable);
            ObjArrayIterator* iterator = allocateArrayIterator(array);
            if (!iterator) {
                VM_ERROR_RETURN(ERROR_RUNTIME, CURRENT_LOCATION(), "Failed to allocate array iterator");
            }
            Value iterator_value = {.type = VAL_ARRAY_ITERATOR, .as.obj = (Obj*)iterator};
            vm_set_register_safe(dst, iterator_value);
            DISPATCH();
        }

        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Value not iterable");
    }

    LABEL_OP_ITER_NEXT_R: {
        uint8_t dst = READ_BYTE();
        uint8_t iterReg = READ_BYTE();
        uint8_t hasReg = READ_BYTE();

        Value iterValue = vm_get_register_safe(iterReg);
        if (IS_RANGE_ITERATOR(iterValue)) {
            ObjRangeIterator* it = AS_RANGE_ITERATOR(iterValue);
            int64_t current = it ? it->current : 0;
            int64_t end = it ? it->end : 0;
            int64_t step = it ? it->step : 1;
            if (step == 0) {
                vm_set_register_safe(hasReg, BOOL_VAL(false));
            } else if ((step > 0 && current >= end) || (step < 0 && current <= end)) {
                vm_set_register_safe(hasReg, BOOL_VAL(false));
            } else {
                vm_set_register_safe(dst, I64_VAL(current));
                it->current = current + step;
                vm_set_register_safe(hasReg, BOOL_VAL(true));
            }
        } else if (IS_ARRAY_ITERATOR(iterValue)) {
            ObjArrayIterator* it = AS_ARRAY_ITERATOR(iterValue);
            ObjArray* array = it ? it->array : NULL;
            if (!array || it->index >= array->length) {
                vm_set_register_safe(hasReg, BOOL_VAL(false));
            } else {
                vm_set_register_safe(dst, array->elements[it->index]);
                it->index++;
                vm_set_register_safe(hasReg, BOOL_VAL(true));
            }
        } else {
            VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Invalid iterator");
        }
        DISPATCH();
    }

    LABEL_OP_PARSE_INT_R: {
        uint8_t dst = READ_BYTE();
        uint8_t value_reg = READ_BYTE();

        Value source = vm_get_register_safe(value_reg);
        Value result;
        char message[128];
        BuiltinParseResult status = builtin_parse_int(source, &result, message, sizeof(message));

        if (status != BUILTIN_PARSE_OK) {
            const char* fallback = (status == BUILTIN_PARSE_OVERFLOW)
                                        ? "int() overflow"
                                        : "int() conversion failed";
            const char* text = message[0] ? message : fallback;
            VM_ERROR_RETURN(ERROR_CONVERSION, CURRENT_LOCATION(), "%s", text);
        }

        vm_set_register_safe(dst, result);
        DISPATCH();
    }

    LABEL_OP_PARSE_FLOAT_R: {
        uint8_t dst = READ_BYTE();
        uint8_t value_reg = READ_BYTE();

        Value source = vm_get_register_safe(value_reg);
        Value result;
        char message[128];
        BuiltinParseResult status = builtin_parse_float(source, &result, message, sizeof(message));

        if (status != BUILTIN_PARSE_OK) {
            const char* fallback = (status == BUILTIN_PARSE_OVERFLOW)
                                        ? "float() overflow"
                                        : "float() conversion failed";
            const char* text = message[0] ? message : fallback;
            VM_ERROR_RETURN(ERROR_CONVERSION, CURRENT_LOCATION(), "%s", text);
        }

        vm_set_register_safe(dst, result);
        DISPATCH();
    }

    LABEL_OP_TYPE_OF_R: {
        uint8_t dst = READ_BYTE();
        uint8_t value_reg = READ_BYTE();

        Value value = vm_get_register_safe(value_reg);
        Value result;
        if (!builtin_type_of(value, &result)) {
            VM_ERROR_RETURN(ERROR_RUNTIME, CURRENT_LOCATION(), "type_of() internal error");
        }

        vm_set_register_safe(dst, result);
        DISPATCH();
    }

    LABEL_OP_IS_TYPE_R: {
        uint8_t dst = READ_BYTE();
        uint8_t value_reg = READ_BYTE();
        uint8_t type_reg = READ_BYTE();

        Value value = vm_get_register_safe(value_reg);
        Value type_identifier = vm_get_register_safe(type_reg);
        Value result;
        if (!builtin_is_type(value, type_identifier, &result)) {
            VM_ERROR_RETURN(ERROR_RUNTIME, CURRENT_LOCATION(), "is_type() internal error");
        }

        vm_set_register_safe(dst, result);
        DISPATCH();
    }

    LABEL_OP_INPUT_R: {
        uint8_t dst = READ_BYTE();
        uint8_t arg_count = READ_BYTE();
        uint8_t prompt_reg = READ_BYTE();

        if (arg_count > 1) {
            VM_ERROR_RETURN(ERROR_ARGUMENT, CURRENT_LOCATION(),
                            "input() accepts at most one argument");
        }

        Value args_storage[1];
        Value* args_ptr = NULL;
        if (arg_count == 1) {
            args_storage[0] = vm_get_register_safe(prompt_reg);
            args_ptr = args_storage;
        }

        Value result;
        if (!builtin_input(args_ptr, (int)arg_count, &result)) {
            VM_ERROR_RETURN(ERROR_EOF, CURRENT_LOCATION(),
                            "input() reached end of file");
        }

        vm_set_register_safe(dst, result);
        DISPATCH();
    }

    LABEL_OP_RANGE_R: {
        uint8_t dst = READ_BYTE();
        uint8_t arg_count = READ_BYTE();
        uint8_t first_reg = READ_BYTE();
        uint8_t second_reg = READ_BYTE();
        uint8_t third_reg = READ_BYTE();

        if (arg_count < 1 || arg_count > 3) {
            VM_ERROR_RETURN(ERROR_ARGUMENT, CURRENT_LOCATION(),
                            "range() expects between 1 and 3 arguments");
        }

        Value args_storage[3];
        Value* args_ptr = NULL;
        if (arg_count > 0) {
            args_ptr = args_storage;
            args_storage[0] = vm_get_register_safe(first_reg);
            if (arg_count > 1) {
                args_storage[1] = vm_get_register_safe(second_reg);
            }
            if (arg_count > 2) {
                args_storage[2] = vm_get_register_safe(third_reg);
            }
        }

        Value result;
        if (!builtin_range(args_ptr, arg_count, &result)) {
            VM_ERROR_RETURN(ERROR_ARGUMENT, CURRENT_LOCATION(),
                            "Invalid arguments provided to range()");
        }

        vm_set_register_safe(dst, result);

        DISPATCH();
    }

    LABEL_OP_PRINT_MULTI_R: {
            uint8_t first = READ_BYTE();
            uint8_t count = READ_BYTE();
            uint8_t nl = READ_BYTE();

            // Copy values to temporary array using frame-aware access
            Value temp_values[256];  // Max possible count
            for (int i = 0; i < count; i++) {
                temp_values[i] = vm_get_register_safe(first + i);
            }
            builtin_print(temp_values, count, nl != 0);
            DISPATCH();
        }

    LABEL_OP_PRINT_R: {
            uint8_t reg = READ_BYTE();
            Value temp_value = vm_get_register_safe(reg);
            builtin_print(&temp_value, 1, true);
            DISPATCH();
        }

    LABEL_OP_PRINT_NO_NL_R: {
            uint8_t reg = READ_BYTE();
            Value temp_value = vm_get_register_safe(reg);
            builtin_print(&temp_value, 1, false);
            DISPATCH();
        }

    LABEL_OP_ASSERT_EQ_R: {
            uint8_t dst = READ_BYTE();
            uint8_t label_reg = READ_BYTE();
            uint8_t actual_reg = READ_BYTE();
            uint8_t expected_reg = READ_BYTE();

            Value label = vm_get_register_safe(label_reg);
            Value actual = vm_get_register_safe(actual_reg);
            Value expected = vm_get_register_safe(expected_reg);
            char* failure_message = NULL;
            bool ok = builtin_assert_eq(label, actual, expected, &failure_message);
            if (!ok) {
                const char* message = failure_message ? failure_message : "assert_eq failed";
                runtimeError(ERROR_RUNTIME, CURRENT_LOCATION(), "%s", message);
                free(failure_message);
                goto HANDLE_RUNTIME_ERROR;
            }
            free(failure_message);
            vm_store_bool_register(dst, true);
            DISPATCH();
        }

    LABEL_OP_CALL_R: {
            uint8_t funcReg = READ_BYTE();
            uint8_t firstArgReg = READ_BYTE();
            uint8_t argCount = READ_BYTE();
            uint8_t resultReg = READ_BYTE();
            
            Value funcValue = vm_get_register_safe(funcReg);
            
            if (IS_CLOSURE(funcValue)) {
                // Calling a closure
                ObjClosure* closure = AS_CLOSURE(funcValue);
                ObjFunction* function = closure->function;

                // Check arity
                if (argCount != function->arity) {
                    vm_set_register_safe(resultReg, BOOL_VAL(false));
                    DISPATCH();
                }

                // Check if we have room for another call frame
                if (vm.frameCount >= FRAMES_MAX) {
                    vm_set_register_safe(resultReg, BOOL_VAL(false));
                    DISPATCH();
                }

                // Create new call frame with closure context
                CallFrame* frame = &vm.frames[vm.frameCount++];
                frame->returnAddress = vm.ip;
                frame->previousChunk = vm.chunk;
                frame->baseRegister = resultReg;

                // Determine parameter base register
                uint16_t paramBase = FRAME_REG_START + FRAME_REGISTERS - function->arity;
                if (paramBase < FRAME_REG_START) paramBase = FRAME_REG_START;
                frame->parameterBaseRegister = paramBase;

                // Save frame and temp registers
                const int temp_reg_start = TEMP_REG_START; // MP_TEMP_REG_START
                const int temp_reg_count = TEMP_REGISTERS;  // R192-R239
                frame->savedRegisterCount = FRAME_REGISTERS + temp_reg_count;
                for (int i = 0; i < FRAME_REGISTERS; i++) {
                    frame->savedRegisters[i] = vm_get_register_safe(FRAME_REG_START + i);
                }
                for (int i = 0; i < temp_reg_count; i++) {
                    frame->savedRegisters[FRAME_REGISTERS + i] = vm_get_register_safe(temp_reg_start + i);
                }

                // Set up closure context (closure in register 0)
                vm_set_register_safe(0, funcValue);  // Store closure in register 0 for upvalue access

                // Copy arguments to parameter registers
                for (int i = 0; i < argCount; i++) {
                    Value arg = vm_get_register_safe(firstArgReg + i);
                    vm_set_register_safe(paramBase + i, arg);
                }

                // Switch to function's bytecode
                vm.chunk = function->chunk;
                vm.ip = function->chunk->code;

                DISPATCH();
            } else if (IS_FUNCTION(funcValue)) {
                // Calling a function object directly
                ObjFunction* objFunction = AS_FUNCTION(funcValue);
                
                // Check arity
                if (argCount != objFunction->arity) {
                    vm_set_register_safe(resultReg, BOOL_VAL(false));
                    DISPATCH();
                }
                
                // Check if we have room for another call frame
                if (vm.frameCount >= FRAMES_MAX) {
                    vm_set_register_safe(resultReg, BOOL_VAL(false));
                    DISPATCH();
                }
                
                // Create new call frame
                CallFrame* frame = &vm.frames[vm.frameCount++];
                frame->returnAddress = vm.ip;
                frame->previousChunk = vm.chunk;
                frame->baseRegister = resultReg;
                
                // Simple parameter base calculation to match compiler
                uint16_t paramBase = FRAME_REG_START + FRAME_REGISTERS - objFunction->arity;
                if (paramBase < FRAME_REG_START) paramBase = FRAME_REG_START;
                
                // Copy arguments to parameter registers
                for (int i = 0; i < argCount; i++) {
                    Value arg = vm_get_register_safe(firstArgReg + i);
                    vm_set_register_safe(paramBase + i, arg);
                }
                
                // Switch to function's bytecode
                vm.chunk = objFunction->chunk;
                vm.ip = objFunction->chunk->code;
                
                DISPATCH();
            } else if (IS_I32(funcValue)) {
                int functionIndex = AS_I32(funcValue);
                DEBUG_VM_PRINT("CALL: func_index=%d, args=%d\n", functionIndex, argCount);
                
                if (functionIndex < 0 || functionIndex >= vm.functionCount) {
                    vm_set_register_safe(resultReg, BOOL_VAL(false));
                    DISPATCH();
                }
                
                Function* function = &vm.functions[functionIndex];
                
                // Check arity
                if (argCount != function->arity) {
                    vm_set_register_safe(resultReg, BOOL_VAL(false));
                    DISPATCH();
                }
                
                // Check if we have room for another call frame
                if (vm.frameCount >= FRAMES_MAX) {
                    vm_set_register_safe(resultReg, BOOL_VAL(false));
                    DISPATCH();
                }
                
                // Create new call frame
                CallFrame* frame = &vm.frames[vm.frameCount++];
                frame->returnAddress = vm.ip;
                frame->previousChunk = vm.chunk;
                frame->baseRegister = resultReg;
                frame->register_count = argCount;
                frame->functionIndex = functionIndex;
                
                // Simple parameter base calculation to match compiler
                uint16_t paramBase = FRAME_REG_START + FRAME_REGISTERS - function->arity;
                if (paramBase < FRAME_REG_START) paramBase = FRAME_REG_START;
                frame->parameterBaseRegister = paramBase;
                
                frame->savedRegisterStart = FRAME_REG_START;

                const int temp_reg_start = TEMP_REG_START; // Matches MP_TEMP_REG_START
                const int temp_reg_count = TEMP_REGISTERS;  // Covers temp window

                for (int i = 0; i < FRAME_REGISTERS; i++) {
                    frame->savedRegisters[i] = vm_get_register_safe(FRAME_REG_START + i);
                }

                for (int i = 0; i < temp_reg_count; i++) {
                    frame->savedRegisters[FRAME_REGISTERS + i] = vm_get_register_safe(temp_reg_start + i);
                }

                frame->savedRegisterCount = FRAME_REGISTERS + temp_reg_count;

                
                // Copy arguments to parameter registers
                for (int i = 0; i < argCount; i++) {
                    Value arg = vm_get_register_safe(firstArgReg + i);
                    vm_set_register_safe(paramBase + i, arg);
                }
                
                // Switch to function's chunk
                vm.chunk = function->chunk;
                vm.ip = function->chunk->code + function->start;
                
            } else {
                vm_set_register_safe(resultReg, BOOL_VAL(false));
            }
            
            DISPATCH();
        }

    LABEL_OP_TAIL_CALL_R: {
            uint8_t funcReg = READ_BYTE();
            uint8_t firstArgReg = READ_BYTE();
            uint8_t argCount = READ_BYTE();
            uint8_t resultReg = READ_BYTE();
            
            Value funcValue = vm_get_register_safe(funcReg);
            
            if (IS_I32(funcValue)) {
                int functionIndex = AS_I32(funcValue);
                
                if (functionIndex < 0 || functionIndex >= vm.functionCount) {
                    vm_set_register_safe(resultReg, BOOL_VAL(false));
                    DISPATCH();
                }
                
                Function* function = &vm.functions[functionIndex];
                
                // Check arity
                if (argCount != function->arity) {
                    vm_set_register_safe(resultReg, BOOL_VAL(false));
                    DISPATCH();
                }
                
                // For tail calls, we reuse the current frame instead of creating a new one
                // This prevents stack growth in recursive calls
                
                // Copy arguments to function's frame registers  
                // We need to be careful about overlapping registers
                Value tempArgs[FRAME_REGISTERS];
                for (int i = 0; i < argCount; i++) {
                    tempArgs[i] = vm_get_register_safe(firstArgReg + i);
                }
                
                // Clear the current frame's parameter registers first, then set new ones
                for (int i = 0; i < argCount; i++) {
                    uint16_t frame_reg_id = FRAME_REG_START + i;
                    set_register(&vm.register_file, frame_reg_id, tempArgs[i]);
                    vm_set_register_safe(frame_reg_id, tempArgs[i]);  // Mirror into frame window
                }
                
                // Switch to function's chunk - reuse current frame
                vm.chunk = function->chunk;
                vm.ip = function->chunk->code + function->start;
                
            } else {
                vm_set_register_safe(resultReg, BOOL_VAL(false));
            }
            
            DISPATCH();
        }

    LABEL_OP_RETURN_R: {
            uint8_t reg = READ_BYTE();
            Value returnValue = vm_get_register_safe(reg);
            if (vm.frameCount > 0) {
                CallFrame* frame = &vm.frames[--vm.frameCount];
                
                // Close upvalues before restoring registers to prevent corruption
                vm_get_register_safe(frame->parameterBaseRegister);
                closeUpvalues(&vm.registers[frame->parameterBaseRegister]);
                
                const int temp_reg_start = TEMP_REG_START;
                const int temp_reg_count = TEMP_REGISTERS;

                // Restore saved registers from the frame window and any preserved temporaries
                if (frame->savedRegisterCount == FRAME_REGISTERS + temp_reg_count) {
                    for (int i = 0; i < FRAME_REGISTERS; i++) {
                        vm_set_register_safe(FRAME_REG_START + i, frame->savedRegisters[i]);
                    }
                    for (int i = 0; i < temp_reg_count; i++) {
                        vm_set_register_safe(temp_reg_start + i, frame->savedRegisters[FRAME_REGISTERS + i]);
                    }
                } else {  // Legacy single-range format for backward compatibility
                    for (int i = 0; i < frame->savedRegisterCount; i++) {
                        vm_set_register_safe(frame->savedRegisterStart + i, frame->savedRegisters[i]);
                    }
                }
                
                vm.chunk = frame->previousChunk;
                vm.ip = frame->returnAddress;
                vm_set_register_safe(frame->baseRegister, returnValue);
            } else {
                vm.lastExecutionTime = get_time_vm() - start_time;
                RETURN(INTERPRET_OK);
            }
            DISPATCH();
        }

    LABEL_OP_RETURN_VOID: {
            if (vm.frameCount > 0) {
                CallFrame* frame = &vm.frames[--vm.frameCount];
                
                // Close upvalues before restoring registers to prevent corruption
                vm_get_register_safe(frame->parameterBaseRegister);
                closeUpvalues(&vm.registers[frame->parameterBaseRegister]);
                
                const int temp_reg_start = TEMP_REG_START;
                const int temp_reg_count = TEMP_REGISTERS;

                if (frame->savedRegisterCount == FRAME_REGISTERS + temp_reg_count) {
                    for (int i = 0; i < FRAME_REGISTERS; i++) {
                        vm_set_register_safe(FRAME_REG_START + i, frame->savedRegisters[i]);
                    }
                    for (int i = 0; i < temp_reg_count; i++) {
                        vm_set_register_safe(temp_reg_start + i, frame->savedRegisters[FRAME_REGISTERS + i]);
                    }
                } else {
                    for (int i = 0; i < frame->savedRegisterCount; i++) {
                        vm_set_register_safe(frame->savedRegisterStart + i, frame->savedRegisters[i]);
                    }
                }
                
                vm.chunk = frame->previousChunk;
                vm.ip = frame->returnAddress;
            } else {
                vm.lastExecutionTime = get_time_vm() - start_time;
                RETURN(INTERPRET_OK);
            }
            DISPATCH();
        }

    // Phase 1: Frame register operations
    LABEL_OP_LOAD_FRAME: {
        uint8_t reg = READ_BYTE();
        uint8_t frame_offset = READ_BYTE();
        uint16_t frame_reg_id = FRAME_REG_START + frame_offset;
        Value* src = get_register(&vm.register_file, frame_reg_id);
        vm_set_register_safe(reg, *src);
        DISPATCH();
    }

    LABEL_OP_LOAD_SPILL: {
        uint8_t reg = READ_BYTE();
        uint8_t spill_id_high = READ_BYTE();
        uint8_t spill_id_low = READ_BYTE();
        uint16_t spill_id = (spill_id_high << 8) | spill_id_low;
        Value* src = get_register(&vm.register_file, spill_id);
        vm_set_register_safe(reg, *src);
        DISPATCH();
    }

    LABEL_OP_STORE_SPILL: {
        uint8_t spill_id_high = READ_BYTE();
        uint8_t spill_id_low = READ_BYTE();
        uint8_t reg = READ_BYTE();
        uint16_t spill_id = (spill_id_high << 8) | spill_id_low;
        Value value = vm_get_register_safe(reg);
        set_register(&vm.register_file, spill_id, value);
        DISPATCH();
    }

    LABEL_OP_STORE_FRAME: {
        uint8_t frame_offset = READ_BYTE();
        uint8_t reg = READ_BYTE();
        uint16_t frame_reg_id = FRAME_REG_START + frame_offset;
        Value val = vm_get_register_safe(reg);
        set_register(&vm.register_file, frame_reg_id, val);
        DISPATCH();
    }

    LABEL_OP_ENTER_FRAME: {
        uint8_t frame_size = READ_BYTE();
        (void)frame_size; // Frame size is implicit in current implementation
        allocate_frame(&vm.register_file);
        DISPATCH();
    }

    LABEL_OP_EXIT_FRAME: {
        deallocate_frame(&vm.register_file);
        DISPATCH();
    }

    LABEL_OP_MOVE_FRAME: {
        uint8_t dst_offset = READ_BYTE();
        uint8_t src_offset = READ_BYTE();
        uint16_t dst_reg_id = FRAME_REG_START + dst_offset;
        uint16_t src_reg_id = FRAME_REG_START + src_offset;
        Value* src = get_register(&vm.register_file, src_reg_id);
        set_register(&vm.register_file, dst_reg_id, *src);
        DISPATCH();
    }

    // Short jump optimizations for performance
    LABEL_OP_JUMP_SHORT: {
        uint8_t offset = READ_BYTE();
        if (!CF_JUMP_SHORT(offset)) {
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        DISPATCH();
    }

    LABEL_OP_JUMP_BACK_SHORT: {
        uint8_t offset = READ_BYTE();
        if (!CF_JUMP_BACK_SHORT(offset)) {
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        DISPATCH();
    }

    LABEL_OP_JUMP_IF_NOT_SHORT: {
        uint8_t reg = READ_BYTE();
        uint8_t offset = READ_BYTE();
        if (!CF_JUMP_IF_NOT_SHORT(reg, offset)) {
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        DISPATCH();
    }

    LABEL_OP_LOOP_SHORT: {
        uint8_t offset = READ_BYTE();

        // Hot path detection: Profile short loop iterations (tight loops)
        if (g_profiling.isActive && (g_profiling.enabledFlags & PROFILE_HOT_PATHS)) {
            static uint64_t short_loop_iterations = 0;
            short_loop_iterations++;
            profileHotPath((void*)(vm.ip - vm.chunk->code), short_loop_iterations);
        }

        if (!CF_LOOP_SHORT(offset)) {
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        DISPATCH();
    }

    LABEL_OP_BRANCH_TYPED: {
        uint16_t loop_id = READ_SHORT();
        uint8_t reg = READ_BYTE();
        uint16_t offset = READ_SHORT();
        if (!CF_BRANCH_TYPED(loop_id, reg, offset)) {
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        DISPATCH();
    }

    // Typed arithmetic operations for maximum performance (bypass Value boxing)
#if ORUS_VM_ENABLE_TYPED_OPS
    LABEL_OP_ADD_I32_TYPED: {
        handle_add_i32_typed();
        DISPATCH_TYPED();
    }

    LABEL_OP_SUB_I32_TYPED: {
        handle_sub_i32_typed();
        DISPATCH_TYPED();
    }

    LABEL_OP_MUL_I32_TYPED: {
        handle_mul_i32_typed();
        DISPATCH_TYPED();
    }

    LABEL_OP_DIV_I32_TYPED: {
        handle_div_i32_typed();
        CHECK_RUNTIME_ERROR();
        DISPATCH_TYPED();
    }

    LABEL_OP_MOD_I32_TYPED: {
        handle_mod_i32_typed();
        CHECK_RUNTIME_ERROR();
        DISPATCH_TYPED();
    }

    LABEL_OP_ADD_I64_TYPED: {
        handle_add_i64_typed();
        DISPATCH_TYPED();
    }

    LABEL_OP_SUB_I64_TYPED: {
        handle_sub_i64_typed();
        DISPATCH_TYPED();
    }

    LABEL_OP_MUL_I64_TYPED: {
        handle_mul_i64_typed();
        DISPATCH_TYPED();
    }

    LABEL_OP_DIV_I64_TYPED: {
        handle_div_i64_typed();
        CHECK_RUNTIME_ERROR();
        DISPATCH_TYPED();
    }

    LABEL_OP_MOD_I64_TYPED: {
        handle_mod_i64_typed();
        CHECK_RUNTIME_ERROR();
        DISPATCH_TYPED();
    }

    LABEL_OP_ADD_F64_TYPED: {
        handle_add_f64_typed();
        DISPATCH_TYPED();
    }

    LABEL_OP_SUB_F64_TYPED: {
        handle_sub_f64_typed();
        DISPATCH_TYPED();
    }

    LABEL_OP_MUL_F64_TYPED: {
        handle_mul_f64_typed();
        DISPATCH_TYPED();
    }

    LABEL_OP_DIV_F64_TYPED: {
        handle_div_f64_typed();
        CHECK_RUNTIME_ERROR();
        DISPATCH_TYPED();
    }

    LABEL_OP_MOD_F64_TYPED: {
        handle_mod_f64_typed();
        CHECK_RUNTIME_ERROR();
        DISPATCH_TYPED();
    }

    LABEL_OP_LT_I32_TYPED: {
        VM_TYPED_CMP_OP(i32_regs, <);
        DISPATCH_TYPED();
    }

    LABEL_OP_LE_I32_TYPED: {
        VM_TYPED_CMP_OP(i32_regs, <=);
        DISPATCH_TYPED();
    }

    LABEL_OP_GT_I32_TYPED: {
        VM_TYPED_CMP_OP(i32_regs, >);
        DISPATCH_TYPED();
    }

    LABEL_OP_GE_I32_TYPED: {
        VM_TYPED_CMP_OP(i32_regs, >=);
        DISPATCH_TYPED();
    }

    LABEL_OP_LT_I64_TYPED: {
        VM_TYPED_CMP_OP(i64_regs, <);
        DISPATCH_TYPED();
    }

    LABEL_OP_LE_I64_TYPED: {
        VM_TYPED_CMP_OP(i64_regs, <=);
        DISPATCH_TYPED();
    }

    LABEL_OP_GT_I64_TYPED: {
        VM_TYPED_CMP_OP(i64_regs, >);
        DISPATCH_TYPED();
    }

    LABEL_OP_GE_I64_TYPED: {
        VM_TYPED_CMP_OP(i64_regs, >=);
        DISPATCH_TYPED();
    }

    LABEL_OP_LT_F64_TYPED: {
        VM_TYPED_CMP_OP(f64_regs, <);
        DISPATCH_TYPED();
    }

    LABEL_OP_LE_F64_TYPED: {
        VM_TYPED_CMP_OP(f64_regs, <=);
        DISPATCH_TYPED();
    }

    LABEL_OP_GT_F64_TYPED: {
        VM_TYPED_CMP_OP(f64_regs, >);
        DISPATCH_TYPED();
    }

    LABEL_OP_GE_F64_TYPED: {
        VM_TYPED_CMP_OP(f64_regs, >=);
        DISPATCH_TYPED();
    }

    LABEL_OP_LT_U32_TYPED: {
        VM_TYPED_CMP_OP(u32_regs, <);
        DISPATCH_TYPED();
    }

    LABEL_OP_LE_U32_TYPED: {
        VM_TYPED_CMP_OP(u32_regs, <=);
        DISPATCH_TYPED();
    }

    LABEL_OP_GT_U32_TYPED: {
        VM_TYPED_CMP_OP(u32_regs, >);
        DISPATCH_TYPED();
    }

    LABEL_OP_GE_U32_TYPED: {
        VM_TYPED_CMP_OP(u32_regs, >=);
        DISPATCH_TYPED();
    }

    LABEL_OP_LT_U64_TYPED: {
        VM_TYPED_CMP_OP(u64_regs, <);
        DISPATCH_TYPED();
    }

    LABEL_OP_LE_U64_TYPED: {
        VM_TYPED_CMP_OP(u64_regs, <=);
        DISPATCH_TYPED();
    }

    LABEL_OP_GT_U64_TYPED: {
        VM_TYPED_CMP_OP(u64_regs, >);
        DISPATCH_TYPED();
    }

    LABEL_OP_GE_U64_TYPED: {
        VM_TYPED_CMP_OP(u64_regs, >=);
        DISPATCH_TYPED();
    }

    LABEL_OP_LOAD_I32_CONST: {
        handle_load_i32_const();
        DISPATCH();
    }

    LABEL_OP_LOAD_I64_CONST: {
        handle_load_i64_const();
        DISPATCH();
    }

    LABEL_OP_LOAD_F64_CONST: {
        handle_load_f64_const();
        DISPATCH();
    }

    LABEL_OP_MOVE_I32: {
        handle_move_i32();
        DISPATCH();
    }

    LABEL_OP_MOVE_I64: {
        handle_move_i64();
        DISPATCH();
    }

    LABEL_OP_MOVE_F64: {
        handle_move_f64();
        DISPATCH();
    }

    LABEL_OP_ADD_U32_TYPED: {
        handle_add_u32_typed();
        DISPATCH_TYPED();
    }

    LABEL_OP_SUB_U32_TYPED: {
        handle_sub_u32_typed();
        DISPATCH_TYPED();
    }

    LABEL_OP_MUL_U32_TYPED: {
        handle_mul_u32_typed();
        DISPATCH_TYPED();
    }

    LABEL_OP_DIV_U32_TYPED: {
        handle_div_u32_typed();
        CHECK_RUNTIME_ERROR();
        DISPATCH_TYPED();
    }

    LABEL_OP_MOD_U32_TYPED: {
        handle_mod_u32_typed();
        CHECK_RUNTIME_ERROR();
        DISPATCH_TYPED();
    }

    // U64 Typed Operations
    LABEL_OP_ADD_U64_TYPED: {
        handle_add_u64_typed();
        DISPATCH_TYPED();
    }

    LABEL_OP_SUB_U64_TYPED: {
        handle_sub_u64_typed();
        DISPATCH_TYPED();
    }

    LABEL_OP_MUL_U64_TYPED: {
        handle_mul_u64_typed();
        DISPATCH_TYPED();
    }

    LABEL_OP_DIV_U64_TYPED: {
        handle_div_u64_typed();
        CHECK_RUNTIME_ERROR();
        DISPATCH_TYPED();
    }

    LABEL_OP_MOD_U64_TYPED: {
        handle_mod_u64_typed();
        CHECK_RUNTIME_ERROR();
        DISPATCH_TYPED();
    }
#else
    LABEL_TYPED_OPS_UNAVAILABLE: {
        VM_ERROR_RETURN(ERROR_RUNTIME, CURRENT_LOCATION(),
                        "Typed operations are disabled at build time");
        DISPATCH();
    }
#endif

    // TODO: Removed mixed-type op for Rust-style strict typing

    LABEL_OP_TIME_STAMP: {
        uint8_t dst = READ_BYTE();
        
        // Get high-precision timestamp in seconds
        double timestamp = builtin_time_stamp();
        
        // Store in both typed register and regular register for compatibility
        vm.typed_regs.f64_regs[dst] = timestamp;
        vm.typed_regs.reg_types[dst] = REG_TYPE_F64;
        vm_set_register_safe(dst, F64_VAL(timestamp));
        
        DISPATCH();
    }

    // Phase 2.2: Fused instruction implementations for better performance
    LABEL_OP_ADD_I32_IMM: {
        uint8_t dst = *vm.ip++;
        uint8_t src = *vm.ip++;
        int32_t imm = *(int32_t*)vm.ip;
        vm.ip += 4;

        // Compiler ensures this is only emitted for i32 operations, so trust it
        Value src_value = vm_get_register_safe(src);
        if (!IS_I32(src_value)) {
            VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operand must be i32");
        }

        int32_t result = AS_I32(src_value) + imm;
        vm_store_i32_typed_hot(dst, result);

        DISPATCH_TYPED();
    }

    LABEL_OP_SUB_I32_IMM: {
        uint8_t dst = *vm.ip++;
        uint8_t src = *vm.ip++;
        int32_t imm = *(int32_t*)vm.ip;
        vm.ip += 4;
        
        // Compiler ensures this is only emitted for i32 operations, so trust it
        Value val = vm_get_register_safe(src);
        if (!IS_I32(val)) {
            VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operand must be i32");
        }
        
        int32_t result = AS_I32(val) - imm;
        vm_set_register_safe(dst, I32_VAL(result));
        
        DISPATCH_TYPED();
    }

    LABEL_OP_MUL_I32_IMM: {
        uint8_t dst = *vm.ip++;
        uint8_t src = *vm.ip++;
        int32_t imm = *(int32_t*)vm.ip;
        vm.ip += 4;
        
        // Compiler ensures this is only emitted for i32 operations, so trust it
        Value mul_val = vm_get_register_safe(src);
        if (!IS_I32(mul_val)) {
            VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operand must be i32");
        }

        int32_t result = AS_I32(mul_val) * imm;
        vm_set_register_safe(dst, I32_VAL(result));
        
        DISPATCH_TYPED();
    }

    LABEL_OP_CMP_I32_IMM: {
        uint8_t dst = *vm.ip++;
        uint8_t src = *vm.ip++;
        int32_t imm = *(int32_t*)vm.ip;
        vm.ip += 4;

        bool result = vm.typed_regs.i32_regs[src] < imm;
        vm_store_bool_register(dst, result);

        DISPATCH_TYPED();
    }

    LABEL_OP_INC_CMP_JMP: {
        uint8_t reg = *vm.ip++;
        uint8_t limit_reg = *vm.ip++;
        int16_t offset = *(int16_t*)vm.ip;
        vm.ip += 2;

        Value counter = vm_get_register_safe(reg);
        Value limit = vm_get_register_safe(limit_reg);

        if (IS_I32(counter) && IS_I32(limit)) {
            int32_t current = AS_I32(counter);
            int32_t incremented;
            if (__builtin_add_overflow(current, 1, &incremented)) {
                VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Integer overflow");
            }
            store_i32_register(reg, incremented);
            if (incremented < AS_I32(limit)) {
                vm.ip += offset;
            }
            DISPATCH_TYPED();
        }

        if (IS_I64(counter) && IS_I64(limit)) {
            int64_t current = AS_I64(counter);
            int64_t incremented;
            if (__builtin_add_overflow(current, (int64_t)1, &incremented)) {
                VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Integer overflow");
            }
            store_i64_register(reg, incremented);
            if (incremented < AS_I64(limit)) {
                vm.ip += offset;
            }
            DISPATCH_TYPED();
        }

        if (IS_U32(counter) && IS_U32(limit)) {
            uint32_t incremented = AS_U32(counter) + 1u;
            store_u32_register(reg, incremented);
            if (incremented < AS_U32(limit)) {
                vm.ip += offset;
            }
            DISPATCH_TYPED();
        }

        if (IS_U64(counter) && IS_U64(limit)) {
            uint64_t incremented = AS_U64(counter) + 1u;
            store_u64_register(reg, incremented);
            if (incremented < AS_U64(limit)) {
                vm.ip += offset;
            }
            DISPATCH_TYPED();
        }

        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(),
                        "Operands must be homogeneous integers");
    }

    LABEL_OP_DEC_CMP_JMP: {
        uint8_t reg = *vm.ip++;
        uint8_t zero_test = *vm.ip++;
        int16_t offset = *(int16_t*)vm.ip;
        vm.ip += 2;

        // Fused decrement + compare + conditional jump
        int32_t decremented = vm.typed_regs.i32_regs[reg] - 1;
        vm_store_i32_typed_hot(reg, decremented);
        if (decremented > vm.typed_regs.i32_regs[zero_test]) {
            vm.ip += offset;
        }

        DISPATCH_TYPED();
    }

    LABEL_OP_MUL_ADD_I32: {
        uint8_t dst = *vm.ip++;
        uint8_t mul1 = *vm.ip++;
        uint8_t mul2 = *vm.ip++;
        uint8_t add = *vm.ip++;

        // Fused multiply-add (single operation on modern CPUs)
        int32_t result =
            vm.typed_regs.i32_regs[mul1] * vm.typed_regs.i32_regs[mul2] +
            vm.typed_regs.i32_regs[add];
        vm_store_i32_typed_hot(dst, result);

        DISPATCH_TYPED();
    }

    LABEL_OP_CLOSURE_R: {
        uint8_t dstReg = READ_BYTE();
        uint8_t functionReg = READ_BYTE();
        uint8_t upvalueCount = READ_BYTE();
        
        Value functionValue = vm_get_register_safe(functionReg);
        if (!IS_FUNCTION(functionValue)) {
            VM_ERROR_RETURN(ERROR_RUNTIME, CURRENT_LOCATION(), "Expected function for closure creation");
        }
        
        ObjFunction* function = AS_FUNCTION(functionValue);
        ObjClosure* closure = allocateClosure(function);
        
        for (int i = 0; i < upvalueCount; i++) {
            uint8_t isLocal = READ_BYTE();
            uint8_t index = READ_BYTE();
            
            
            if (isLocal) {
                vm_get_register_safe(index);
                closure->upvalues[i] = captureUpvalue(&vm.registers[index]);
            } else {
                Value enclosing_value = vm_get_register_safe(0);
                ObjClosure* enclosing = AS_CLOSURE(enclosing_value); // Current closure
                closure->upvalues[i] = enclosing->upvalues[index];
            }
        }
        
        vm_set_register_safe(dstReg, CLOSURE_VAL(closure));
        DISPATCH();
    }

    LABEL_OP_GET_UPVALUE_R: {
        uint8_t dstReg = READ_BYTE();
        uint8_t upvalueIndex = READ_BYTE();

        Value closureValue = vm_get_register_safe(0);
        if (!IS_CLOSURE(closureValue)) {
            VM_ERROR_RETURN(ERROR_RUNTIME, CURRENT_LOCATION(), "Invalid upvalue access");
        }

        ObjClosure* closure = AS_CLOSURE(closureValue); // Current closure
        if (closure == NULL || closure->upvalues == NULL ||
            upvalueIndex >= closure->upvalueCount ||
            closure->upvalues[upvalueIndex] == NULL ||
            closure->upvalues[upvalueIndex]->location == NULL) {
            VM_ERROR_RETURN(ERROR_RUNTIME, CURRENT_LOCATION(), "Invalid upvalue access");
        }

        vm_set_register_safe(dstReg, *closure->upvalues[upvalueIndex]->location);
        DISPATCH();
    }

    LABEL_OP_SET_UPVALUE_R: {
        uint8_t upvalueIndex = READ_BYTE();
        uint8_t valueReg = READ_BYTE();

        Value closureValue = vm_get_register_safe(0);
        if (!IS_CLOSURE(closureValue)) {
            VM_ERROR_RETURN(ERROR_RUNTIME, CURRENT_LOCATION(), "Invalid upvalue access");
        }

        ObjClosure* closure = AS_CLOSURE(closureValue); // Current closure
        if (closure == NULL || closure->upvalues == NULL ||
            upvalueIndex >= closure->upvalueCount ||
            closure->upvalues[upvalueIndex] == NULL ||
            closure->upvalues[upvalueIndex]->location == NULL) {
            VM_ERROR_RETURN(ERROR_RUNTIME, CURRENT_LOCATION(), "Invalid upvalue access");
        }

        *closure->upvalues[upvalueIndex]->location = vm_get_register_safe(valueReg);
        DISPATCH();
    }

    LABEL_OP_CLOSE_UPVALUE_R: {
        uint8_t localReg = READ_BYTE();
        vm_get_register_safe(localReg);
        closeUpvalues(&vm.registers[localReg]);
        DISPATCH();
    }

    LABEL_OP_HALT:
        // printf("[DISPATCH_TRACE] OP_HALT reached - program should terminate");
        fflush(stdout);
        vm.lastExecutionTime = get_time_vm() - start_time;
        vm.isShuttingDown = true;  // Set shutdown flag before returning
        // printf("[DISPATCH_TRACE] About to return INTERPRET_OK from OP_HALT");
        fflush(stdout);
        RETURN(INTERPRET_OK);

    HANDLE_RUNTIME_ERROR: __attribute__((unused))
        if (!vm_handle_pending_error()) {
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        DISPATCH();

    LABEL_UNKNOWN: __attribute__((unused))
        VM_ERROR_RETURN(ERROR_RUNTIME, CURRENT_LOCATION(), "Unknown opcode: %d", instruction);

    #undef RETURN
}

// Include the handlers implementation
#include "../handlers/vm_arithmetic_handlers.c"
#include "../handlers/vm_memory_handlers.c"
#include "../handlers/vm_control_flow_handlers.c"

#undef VM_REGISTER_TYPED_LABEL
#endif // USE_COMPUTED_GOTO
