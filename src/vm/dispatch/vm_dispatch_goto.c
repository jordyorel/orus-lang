#include "vm/vm_dispatch.h"
#include "vm/spill_manager.h"
#include "runtime/builtins.h"
#include "vm/vm_constants.h"
#include "vm/vm_string_ops.h"
#include "vm/vm_arithmetic.h"
#include "vm/vm_control_flow.h"
#include "vm/vm_comparison.h"
#include "vm/vm_typed_ops.h"
#include "vm/vm_opcode_handlers.h"
#include "vm/register_file.h"
#include "vm/vm_profiling.h"

#include <math.h>

// Bridge functions for accessing frame and spill registers
// static inline Value vm_get_register_safe(uint16_t id) {
//     if (id < 256) {
//         // Legacy global registers
//         return vm.registers[id];
//     } else {
//         // Use register file for frame/spill registers
//         Value* reg_ptr = get_register(&vm.register_file, id);
//         return reg_ptr ? *reg_ptr : BOOL_VAL(false);
//     }
// }

// static inline void vm_set_register_safe(uint16_t id, Value value) {
//     if (id < 256) {
//         // Legacy global registers
//         vm.registers[id] = value;
//     } else {
//         // Use register file for frame/spill registers
//         set_register(&vm.register_file, id, value);
//     }
// }

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

InterpretResult vm_run_dispatch(void) {

    double start_time = get_time_vm();
    #define RETURN(val) \
        do { \
            vm.lastExecutionTime = get_time_vm() - start_time; \
            return (val); \
        } while (0)

    // Initialize dispatch table with label addresses - this only runs ONCE per process
    static bool global_dispatch_initialized = false;
    if (!global_dispatch_initialized) {
        // Phase 1.3 Optimization: Hot opcodes first for better cache locality
        // Most frequently used typed operations (hot path)
        vm_dispatch_table[OP_ADD_I32_TYPED] = &&LABEL_OP_ADD_I32_TYPED;
        vm_dispatch_table[OP_SUB_I32_TYPED] = &&LABEL_OP_SUB_I32_TYPED;
        vm_dispatch_table[OP_MUL_I32_TYPED] = &&LABEL_OP_MUL_I32_TYPED;
        vm_dispatch_table[OP_LT_I32_TYPED] = &&LABEL_OP_LT_I32_TYPED;
        vm_dispatch_table[OP_LE_I32_TYPED] = &&LABEL_OP_LE_I32_TYPED;
        vm_dispatch_table[OP_GT_I32_TYPED] = &&LABEL_OP_GT_I32_TYPED;
        vm_dispatch_table[OP_GE_I32_TYPED] = &&LABEL_OP_GE_I32_TYPED;
        
        // Short jumps for tight loops
        vm_dispatch_table[OP_JUMP_SHORT] = &&LABEL_OP_JUMP_SHORT;
        vm_dispatch_table[OP_JUMP_BACK_SHORT] = &&LABEL_OP_JUMP_BACK_SHORT;
        vm_dispatch_table[OP_JUMP_IF_NOT_SHORT] = &&LABEL_OP_JUMP_IF_NOT_SHORT;
        vm_dispatch_table[OP_LOOP_SHORT] = &&LABEL_OP_LOOP_SHORT;
        
        // Loop-critical operations
        vm_dispatch_table[OP_INC_I32_R] = &&LABEL_OP_INC_I32_R;
        
        // Remaining typed operations
        vm_dispatch_table[OP_DIV_I32_TYPED] = &&LABEL_OP_DIV_I32_TYPED;
        vm_dispatch_table[OP_MOD_I32_TYPED] = &&LABEL_OP_MOD_I32_TYPED;
        vm_dispatch_table[OP_ADD_I64_TYPED] = &&LABEL_OP_ADD_I64_TYPED;
        vm_dispatch_table[OP_SUB_I64_TYPED] = &&LABEL_OP_SUB_I64_TYPED;
        vm_dispatch_table[OP_MUL_I64_TYPED] = &&LABEL_OP_MUL_I64_TYPED;
        vm_dispatch_table[OP_DIV_I64_TYPED] = &&LABEL_OP_DIV_I64_TYPED;
        vm_dispatch_table[OP_MOD_I64_TYPED] = &&LABEL_OP_MOD_I64_TYPED;
        vm_dispatch_table[OP_ADD_F64_TYPED] = &&LABEL_OP_ADD_F64_TYPED;
        vm_dispatch_table[OP_SUB_F64_TYPED] = &&LABEL_OP_SUB_F64_TYPED;
        vm_dispatch_table[OP_MUL_F64_TYPED] = &&LABEL_OP_MUL_F64_TYPED;
        vm_dispatch_table[OP_DIV_F64_TYPED] = &&LABEL_OP_DIV_F64_TYPED;
        vm_dispatch_table[OP_MOD_F64_TYPED] = &&LABEL_OP_MOD_F64_TYPED;
        
        // U32 and U64 typed operations
        vm_dispatch_table[OP_ADD_U32_TYPED] = &&LABEL_OP_ADD_U32_TYPED;
        vm_dispatch_table[OP_SUB_U32_TYPED] = &&LABEL_OP_SUB_U32_TYPED;
        vm_dispatch_table[OP_MUL_U32_TYPED] = &&LABEL_OP_MUL_U32_TYPED;
        vm_dispatch_table[OP_DIV_U32_TYPED] = &&LABEL_OP_DIV_U32_TYPED;
        vm_dispatch_table[OP_MOD_U32_TYPED] = &&LABEL_OP_MOD_U32_TYPED;
        
        vm_dispatch_table[OP_ADD_U64_TYPED] = &&LABEL_OP_ADD_U64_TYPED;
        vm_dispatch_table[OP_SUB_U64_TYPED] = &&LABEL_OP_SUB_U64_TYPED;
        vm_dispatch_table[OP_MUL_U64_TYPED] = &&LABEL_OP_MUL_U64_TYPED;
        vm_dispatch_table[OP_DIV_U64_TYPED] = &&LABEL_OP_DIV_U64_TYPED;
        vm_dispatch_table[OP_MOD_U64_TYPED] = &&LABEL_OP_MOD_U64_TYPED;
        
        // TODO: Removed mixed-type op for Rust-style strict typing
        
        // Constant loading (also hot)
        vm_dispatch_table[OP_LOAD_I32_CONST] = &&LABEL_OP_LOAD_I32_CONST;
        vm_dispatch_table[OP_LOAD_I64_CONST] = &&LABEL_OP_LOAD_I64_CONST;
        vm_dispatch_table[OP_LOAD_F64_CONST] = &&LABEL_OP_LOAD_F64_CONST;
        
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
        vm_dispatch_table[OP_I32_TO_I64_R] = &&LABEL_OP_I32_TO_I64_R;
        vm_dispatch_table[OP_I32_TO_U32_R] = &&LABEL_OP_I32_TO_U32_R;
        vm_dispatch_table[OP_I32_TO_BOOL_R] = &&LABEL_OP_I32_TO_BOOL_R;
        vm_dispatch_table[OP_U32_TO_I32_R] = &&LABEL_OP_U32_TO_I32_R;
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
        vm_dispatch_table[OP_TO_STRING_R] = &&LABEL_OP_TO_STRING_R;
        vm_dispatch_table[OP_JUMP] = &&LABEL_OP_JUMP;
        vm_dispatch_table[OP_JUMP_IF_NOT_R] = &&LABEL_OP_JUMP_IF_NOT_R;
        vm_dispatch_table[OP_LOOP] = &&LABEL_OP_LOOP;
        vm_dispatch_table[OP_GET_ITER_R] = &&LABEL_OP_GET_ITER_R;
        vm_dispatch_table[OP_ITER_NEXT_R] = &&LABEL_OP_ITER_NEXT_R;
        vm_dispatch_table[OP_PRINT_MULTI_R] = &&LABEL_OP_PRINT_MULTI_R;
        vm_dispatch_table[OP_PRINT_MULTI_SEP_R] = &&LABEL_OP_PRINT_MULTI_SEP_R;
        vm_dispatch_table[OP_PRINT_R] = &&LABEL_OP_PRINT_R;
        vm_dispatch_table[OP_PRINT_NO_NL_R] = &&LABEL_OP_PRINT_NO_NL_R;
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
        
        vm_dispatch_table[OP_MOVE_I32] = &&LABEL_OP_MOVE_I32;
        vm_dispatch_table[OP_MOVE_I64] = &&LABEL_OP_MOVE_I64;
        vm_dispatch_table[OP_MOVE_F64] = &&LABEL_OP_MOVE_F64;
        
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
    }

    uint8_t instruction;

    // Phase 1.1 Optimization: Fast DISPATCH macro for production builds
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
            vm.registers[reg] = vm.globals[globalIndex];
            DISPATCH();
        }

    LABEL_OP_STORE_GLOBAL: {
            uint8_t globalIndex = READ_BYTE();
            uint8_t reg = READ_BYTE();
            
            // CREATIVE SOLUTION: Type safety enforcement with intelligent literal coercion
            // This maintains single-pass design while being flexible for compatible types
            Value valueToStore = vm.registers[reg];
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
            if (IS_STRING(vm.registers[src1]) || IS_STRING(vm.registers[src2])) {
                // [string concatenation code remains unchanged...]
                Value left = vm.registers[src1];
                Value right = vm.registers[src2];
                
                // Convert left operand to string if needed
                if (!IS_STRING(left)) {
                    char buffer[64];
                    if (IS_I32(left)) {
                        snprintf(buffer, sizeof(buffer), "%d", AS_I32(left));
                    } else if (IS_I64(left)) {
                        snprintf(buffer, sizeof(buffer), "%lld", AS_I64(left));
                    } else if (IS_U32(left)) {
                        snprintf(buffer, sizeof(buffer), "%u", AS_U32(left));
                    } else if (IS_U64(left)) {
                        snprintf(buffer, sizeof(buffer), "%llu", AS_U64(left));
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
                        snprintf(buffer, sizeof(buffer), "%lld", AS_I64(right));
                    } else if (IS_U32(right)) {
                        snprintf(buffer, sizeof(buffer), "%u", AS_U32(right));
                    } else if (IS_U64(right)) {
                        snprintf(buffer, sizeof(buffer), "%llu", AS_U64(right));
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
                    vm.registers[dst] = STRING_VAL(result);
                } else {
                    StringBuilder* sb = createStringBuilder(newLength + 1);
                    appendToStringBuilder(sb, leftStr->chars, leftStr->length);
                    appendToStringBuilder(sb, rightStr->chars, rightStr->length);
                    ObjString* result = stringBuilderToString(sb);
                    freeStringBuilder(sb);
                    vm.registers[dst] = STRING_VAL(result);
                }
                DISPATCH();
            }
            
            // STRICT TYPE SAFETY: No automatic coercion, types must match exactly
            Value val1 = vm.registers[src1];
            Value val2 = vm.registers[src2];
            
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
            vm.registers[dst] = I32_VAL(a + b);
#else
            // Strict same-type arithmetic only (after coercion)
            if (IS_I32(val1)) {
                int32_t a = AS_I32(val1);
                int32_t b = AS_I32(val2);
                vm.registers[dst] = I32_VAL(a + b);
            } else if (IS_I64(val1)) {
                int64_t a = AS_I64(val1);
                int64_t b = AS_I64(val2);
                vm.registers[dst] = I64_VAL(a + b);
            } else if (IS_U32(val1)) {
                uint32_t a = AS_U32(val1);
                uint32_t b = AS_U32(val2);
                vm.registers[dst] = U32_VAL(a + b);
            } else if (IS_U64(val1)) {
                uint64_t a = AS_U64(val1);
                uint64_t b = AS_U64(val2);
                vm.registers[dst] = U64_VAL(a + b);
            } else if (IS_F64(val1)) {
                double a = AS_F64(val1);
                double b = AS_F64(val2);
                vm.registers[dst] = F64_VAL(a + b);
            }
#endif
            DISPATCH();
        }

    LABEL_OP_SUB_I32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            
            // Strict type safety for numeric operations: both operands must be the same numeric type
            if (vm.registers[src1].type != vm.registers[src2].type) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be the same type. Use 'as' for explicit type conversion.");
            }

            // Ensure both operands are numeric
            if (!(IS_I32(vm.registers[src1]) || IS_I64(vm.registers[src1]) || IS_U32(vm.registers[src1]) || IS_U64(vm.registers[src1]) || IS_F64(vm.registers[src1]))) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be numeric (i32, i64, u32, u64, or f64)");
            }

#if USE_FAST_ARITH
            // Fast path: assume i32, no overflow checking
            int32_t a = AS_I32(vm.registers[src1]);
            int32_t b = AS_I32(vm.registers[src2]);
            vm.registers[dst] = I32_VAL(a - b);
#else
            // Strict same-type arithmetic only
            if (IS_I32(vm.registers[src1])) {
                int32_t a = AS_I32(vm.registers[src1]);
                int32_t b = AS_I32(vm.registers[src2]);
                vm.registers[dst] = I32_VAL(a - b);
            } else if (IS_I64(vm.registers[src1])) {
                int64_t a = AS_I64(vm.registers[src1]);
                int64_t b = AS_I64(vm.registers[src2]);
                vm.registers[dst] = I64_VAL(a - b);
            } else if (IS_U32(vm.registers[src1])) {
                uint32_t a = AS_U32(vm.registers[src1]);
                uint32_t b = AS_U32(vm.registers[src2]);
                vm.registers[dst] = U32_VAL(a - b);
            } else if (IS_U64(vm.registers[src1])) {
                uint64_t a = AS_U64(vm.registers[src1]);
                uint64_t b = AS_U64(vm.registers[src2]);
                vm.registers[dst] = U64_VAL(a - b);
            } else if (IS_F64(vm.registers[src1])) {
                double a = AS_F64(vm.registers[src1]);
                double b = AS_F64(vm.registers[src2]);
                vm.registers[dst] = F64_VAL(a - b);
            }
#endif
            DISPATCH();
        }

    LABEL_OP_MUL_I32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            
            // STRICT TYPE SAFETY: No automatic coercion, types must match exactly
            Value val1 = vm.registers[src1];
            Value val2 = vm.registers[src2];
            
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
            vm.registers[dst] = I32_VAL(a * b);
#else
            // Strict same-type arithmetic only (after coercion)
            if (IS_I32(val1)) {
                int32_t a = AS_I32(val1);
                int32_t b = AS_I32(val2);
                vm.registers[dst] = I32_VAL(a * b);
            } else if (IS_I64(val1)) {
                int64_t a = AS_I64(val1);
                int64_t b = AS_I64(val2);
                vm.registers[dst] = I64_VAL(a * b);
            } else if (IS_U32(val1)) {
                uint32_t a = AS_U32(val1);
                uint32_t b = AS_U32(val2);
                vm.registers[dst] = U32_VAL(a * b);
            } else if (IS_U64(val1)) {
                uint64_t a = AS_U64(val1);
                uint64_t b = AS_U64(val2);
                vm.registers[dst] = U64_VAL(a * b);
            } else if (IS_F64(val1)) {
                double a = AS_F64(val1);
                double b = AS_F64(val2);
                vm.registers[dst] = F64_VAL(a * b);
            }
#endif
            DISPATCH();
        }

    LABEL_OP_DIV_I32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();

            // Strict type safety for numeric operations
            if (vm.registers[src1].type != vm.registers[src2].type) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be the same type. Use 'as' for explicit type conversion.");
            }

            if (!(IS_I32(vm.registers[src1]) || IS_I64(vm.registers[src1]) ||
                  IS_U32(vm.registers[src1]) || IS_U64(vm.registers[src1]) || IS_F64(vm.registers[src1]))) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be numeric (i32, i64, u32, u64, or f64)");
            }

            if (IS_I32(vm.registers[src1])) {
                int32_t a = AS_I32(vm.registers[src1]);
                int32_t b = AS_I32(vm.registers[src2]);
                if (b == 0) {
                    VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Division by zero");
                }
                if (a == INT32_MIN && b == -1) {
                    VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Integer overflow");
                }
                vm.registers[dst] = I32_VAL(a / b);
            } else if (IS_I64(vm.registers[src1])) {
                int64_t a = AS_I64(vm.registers[src1]);
                int64_t b = AS_I64(vm.registers[src2]);
                if (b == 0) {
                    VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Division by zero");
                }
                if (a == INT64_MIN && b == -1) {
                    VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Integer overflow");
                }
                vm.registers[dst] = I64_VAL(a / b);
            } else if (IS_U32(vm.registers[src1])) {
                uint32_t a = AS_U32(vm.registers[src1]);
                uint32_t b = AS_U32(vm.registers[src2]);
                if (b == 0) {
                    VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Division by zero");
                }
                vm.registers[dst] = U32_VAL(a / b);
            } else if (IS_U64(vm.registers[src1])) {
                uint64_t a = AS_U64(vm.registers[src1]);
                uint64_t b = AS_U64(vm.registers[src2]);
                if (b == 0) {
                    VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Division by zero");
                }
                vm.registers[dst] = U64_VAL(a / b);
            } else {
                double a = AS_F64(vm.registers[src1]);
                double b = AS_F64(vm.registers[src2]);
                if (b == 0.0) {
                    VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Division by zero");
                }
                double res = a / b;
                if (!isfinite(res)) {
                    VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Floating-point overflow");
                }
                vm.registers[dst] = F64_VAL(res);
            }
            DISPATCH();
        }

    LABEL_OP_MOD_I32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();

            // Strict type safety for numeric operations
            if (vm.registers[src1].type != vm.registers[src2].type) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be the same type. Use 'as' for explicit type conversion.");
            }

            if (!(IS_I32(vm.registers[src1]) || IS_I64(vm.registers[src1]) ||
                  IS_U32(vm.registers[src1]) || IS_U64(vm.registers[src1]) || IS_F64(vm.registers[src1]))) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be numeric (i32, i64, u32, u64, or f64)");
            }

            if (IS_I32(vm.registers[src1])) {
                int32_t a = AS_I32(vm.registers[src1]);
                int32_t b = AS_I32(vm.registers[src2]);
                if (b == 0) {
                    VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Division by zero");
                }
                if (a == INT32_MIN && b == -1) {
                    VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Integer overflow");
                }
                vm.registers[dst] = I32_VAL(a % b);
            } else if (IS_I64(vm.registers[src1])) {
                int64_t a = AS_I64(vm.registers[src1]);
                int64_t b = AS_I64(vm.registers[src2]);
                if (b == 0) {
                    VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Division by zero");
                }
                if (a == INT64_MIN && b == -1) {
                    VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Integer overflow");
                }
                vm.registers[dst] = I64_VAL(a % b);
            } else if (IS_U32(vm.registers[src1])) {
                uint32_t a = AS_U32(vm.registers[src1]);
                uint32_t b = AS_U32(vm.registers[src2]);
                if (b == 0) {
                    VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Division by zero");
                }
                vm.registers[dst] = U32_VAL(a % b);
            } else if (IS_U64(vm.registers[src1])) {
                uint64_t a = AS_U64(vm.registers[src1]);
                uint64_t b = AS_U64(vm.registers[src2]);
                if (b == 0) {
                    VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Division by zero");
                }
                vm.registers[dst] = U64_VAL(a % b);
            } else {
                double a = AS_F64(vm.registers[src1]);
                double b = AS_F64(vm.registers[src2]);
                if (b == 0.0) {
                    VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Division by zero");
                }
                double res = fmod(a, b);
                if (!isfinite(res)) {
                    VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Floating-point overflow");
                }
                vm.registers[dst] = F64_VAL(res);
            }
            DISPATCH();
        }

    LABEL_OP_INC_I32_R: {
            uint8_t reg = READ_BYTE();
    #if USE_FAST_ARITH
            vm.registers[reg] = I32_VAL(AS_I32(vm.registers[reg]) + 1);
    #else
            int32_t val = AS_I32(vm.registers[reg]);
            int32_t result;
            if (__builtin_add_overflow(val, 1, &result)) {
                VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Integer overflow");
            }
            vm.registers[reg] = I32_VAL(result);
    #endif
            DISPATCH();
        }

    LABEL_OP_DEC_I32_R: {
            uint8_t reg = READ_BYTE();
    #if USE_FAST_ARITH
            vm.registers[reg] = I32_VAL(AS_I32(vm.registers[reg]) - 1);
    #else
            int32_t val = AS_I32(vm.registers[reg]);
            int32_t result;
            if (__builtin_sub_overflow(val, 1, &result)) {
                VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Integer overflow");
            }
            vm.registers[reg] = I32_VAL(result);
    #endif
            DISPATCH();
        }

    LABEL_OP_NEG_I32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src = READ_BYTE();
            
            // Type safety: negation only works on numeric types
            if (!(IS_I32(vm.registers[src]) || IS_I64(vm.registers[src]) || IS_U32(vm.registers[src]) || IS_U64(vm.registers[src]) || IS_F64(vm.registers[src]))) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Unary minus only works on numeric types (i32, i64, u32, u64, f64)");
            }
            
    #if USE_FAST_ARITH
            vm.registers[dst] = I32_VAL(-AS_I32(vm.registers[src]));
    #else
            // Handle different numeric types appropriately
            if (IS_I32(vm.registers[src])) {
                int32_t val = AS_I32(vm.registers[src]);
                if (val == INT32_MIN) {
                    VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Integer overflow: cannot negate INT32_MIN");
                }
                vm.registers[dst] = I32_VAL(-val);
            } else if (IS_I64(vm.registers[src])) {
                int64_t val = AS_I64(vm.registers[src]);
                vm.registers[dst] = I64_VAL(-val);
            } else if (IS_U32(vm.registers[src])) {
                uint32_t val = AS_U32(vm.registers[src]);
                // Convert to signed for negation
                vm.registers[dst] = I32_VAL(-((int32_t)val));
            } else if (IS_U64(vm.registers[src])) {
                uint64_t val = AS_U64(vm.registers[src]);
                // Convert to signed for negation
                vm.registers[dst] = I64_VAL(-((int64_t)val));
            } else if (IS_F64(vm.registers[src])) {
                double val = AS_F64(vm.registers[src]);
                vm.registers[dst] = F64_VAL(-val);
            }
    #endif
            DISPATCH();
        }

    LABEL_OP_ADD_I64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            if (!IS_I64(vm.registers[src1]) || !IS_I64(vm.registers[src2])) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be i64");
            }
            int64_t a = AS_I64(vm.registers[src1]);
            int64_t b = AS_I64(vm.registers[src2]);
    #if USE_FAST_ARITH
            vm.registers[dst] = I64_VAL(a + b);
    #else
            int64_t result;
            if (__builtin_add_overflow(a, b, &result)) {
                VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Integer overflow");
            }
            vm.registers[dst] = I64_VAL(result);
    #endif
            DISPATCH();
        }

    LABEL_OP_SUB_I64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            if (!IS_I64(vm.registers[src1]) || !IS_I64(vm.registers[src2])) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be i64");
            }
            int64_t a = AS_I64(vm.registers[src1]);
            int64_t b = AS_I64(vm.registers[src2]);
    #if USE_FAST_ARITH
            vm.registers[dst] = I64_VAL(a - b);
    #else
            int64_t result;
            if (__builtin_sub_overflow(a, b, &result)) {
                VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Integer overflow");
            }
            vm.registers[dst] = I64_VAL(result);
    #endif
            DISPATCH();
        }

    LABEL_OP_MUL_I64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            if (!IS_I64(vm.registers[src1]) || !IS_I64(vm.registers[src2])) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be i64");
            }
            int64_t a = AS_I64(vm.registers[src1]);
            int64_t b = AS_I64(vm.registers[src2]);
    #if USE_FAST_ARITH
            vm.registers[dst] = I64_VAL(a * b);
    #else
            int64_t result;
            if (__builtin_mul_overflow(a, b, &result)) {
                VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Integer overflow");
            }
            vm.registers[dst] = I64_VAL(result);
    #endif
            DISPATCH();
        }

    LABEL_OP_DIV_I64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            if (!IS_I64(vm.registers[src1]) || !IS_I64(vm.registers[src2])) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be i64");
            }
            int64_t b = AS_I64(vm.registers[src2]);
            if (b == 0) {
                VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Division by zero");
            }
            vm.registers[dst] = I64_VAL(AS_I64(vm.registers[src1]) / b);
            DISPATCH();
        }

    LABEL_OP_MOD_I64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            if (!IS_I64(vm.registers[src1]) || !IS_I64(vm.registers[src2])) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be i64");
            }
            int64_t b = AS_I64(vm.registers[src2]);
            if (b == 0) {
                VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Division by zero");
            }
            vm.registers[dst] = I64_VAL(AS_I64(vm.registers[src1]) % b);
            DISPATCH();
        }

    LABEL_OP_ADD_U32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            if (!IS_U32(vm.registers[src1]) || !IS_U32(vm.registers[src2])) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be u32");
            }
            vm.registers[dst] = U32_VAL(AS_U32(vm.registers[src1]) + AS_U32(vm.registers[src2]));
            DISPATCH();
        }

    LABEL_OP_SUB_U32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            if (!IS_U32(vm.registers[src1]) || !IS_U32(vm.registers[src2])) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be u32");
            }
            vm.registers[dst] = U32_VAL(AS_U32(vm.registers[src1]) - AS_U32(vm.registers[src2]));
            DISPATCH();
        }

    LABEL_OP_MUL_U32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            if (!IS_U32(vm.registers[src1]) || !IS_U32(vm.registers[src2])) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be u32");
            }
            vm.registers[dst] = U32_VAL(AS_U32(vm.registers[src1]) * AS_U32(vm.registers[src2]));
            DISPATCH();
        }

    LABEL_OP_DIV_U32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            if (!IS_U32(vm.registers[src1]) || !IS_U32(vm.registers[src2])) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be u32");
            }
            uint32_t b = AS_U32(vm.registers[src2]);
            if (b == 0) {
                VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Division by zero");
            }
            vm.registers[dst] = U32_VAL(AS_U32(vm.registers[src1]) / b);
            DISPATCH();
        }

    LABEL_OP_MOD_U32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            if (!IS_U32(vm.registers[src1]) || !IS_U32(vm.registers[src2])) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be u32");
            }
            uint32_t b = AS_U32(vm.registers[src2]);
            if (b == 0) {
                VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Division by zero");
            }
            vm.registers[dst] = U32_VAL(AS_U32(vm.registers[src1]) % b);
            DISPATCH();
        }

    LABEL_OP_ADD_U64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();

            if (!IS_U64(vm.registers[src1]) || !IS_U64(vm.registers[src2])) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be u64");
            }

            uint64_t a = AS_U64(vm.registers[src1]);
            uint64_t b = AS_U64(vm.registers[src2]);
            
            // Check for overflow: if a + b < a, then overflow occurred
            if (UINT64_MAX - a < b) {
                VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "u64 addition overflow");
            }

            vm.registers[dst] = U64_VAL(a + b);
            DISPATCH();
        }

    LABEL_OP_SUB_U64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();

            if (!IS_U64(vm.registers[src1]) || !IS_U64(vm.registers[src2])) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be u64");
            }

            uint64_t a = AS_U64(vm.registers[src1]);
            uint64_t b = AS_U64(vm.registers[src2]);
            
            // Check for underflow: if a < b, then underflow would occur
            if (a < b) {
                VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "u64 subtraction underflow");
            }

            vm.registers[dst] = U64_VAL(a - b);
            DISPATCH();
        }

    LABEL_OP_MUL_U64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();

            if (!IS_U64(vm.registers[src1]) || !IS_U64(vm.registers[src2])) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be u64");
            }

            uint64_t a = AS_U64(vm.registers[src1]);
            uint64_t b = AS_U64(vm.registers[src2]);
            
            // Check for multiplication overflow: if a != 0 && result / a != b
            if (a != 0 && b > UINT64_MAX / a) {
                VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "u64 multiplication overflow");
            }

            vm.registers[dst] = U64_VAL(a * b);
            DISPATCH();
        }

    LABEL_OP_DIV_U64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();

            if (!IS_U64(vm.registers[src1]) || !IS_U64(vm.registers[src2])) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be u64");
            }

            uint64_t b = AS_U64(vm.registers[src2]);
            if (b == 0) {
                VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Division by zero");
            }

            vm.registers[dst] = U64_VAL(AS_U64(vm.registers[src1]) / b);
            DISPATCH();
        }

    LABEL_OP_MOD_U64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();

            if (!IS_U64(vm.registers[src1]) || !IS_U64(vm.registers[src2])) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be u64");
            }

            uint64_t b = AS_U64(vm.registers[src2]);
            if (b == 0) {
                VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Division by zero");
            }

            vm.registers[dst] = U64_VAL(AS_U64(vm.registers[src1]) % b);
            DISPATCH();
        }

    LABEL_OP_I32_TO_I64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src = READ_BYTE();
            if (!IS_I32(vm.registers[src])) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be i32");
            }
            vm.registers[dst] = I64_VAL((int64_t)AS_I32(vm.registers[src]));
            DISPATCH();
        }

    LABEL_OP_I32_TO_U32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src = READ_BYTE();
            if (!IS_I32(vm.registers[src])) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be i32");
            }
            vm.registers[dst] = U32_VAL((uint32_t)AS_I32(vm.registers[src]));
            DISPATCH();
        }

    LABEL_OP_I32_TO_BOOL_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src = READ_BYTE();
            if (!IS_I32(vm.registers[src])) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be i32");
            }
            // Convert i32 to bool: 0 -> false, non-zero -> true
            vm.registers[dst] = BOOL_VAL(AS_I32(vm.registers[src]) != 0);
            DISPATCH();
        }

    LABEL_OP_U32_TO_I32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src = READ_BYTE();
            if (!IS_U32(vm.registers[src])) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be u32");
            }
            vm.registers[dst] = I32_VAL((int32_t)AS_U32(vm.registers[src]));
            DISPATCH();
        }

    // F64 Arithmetic Operations
    LABEL_OP_ADD_F64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            if (!IS_F64(vm.registers[src1]) || !IS_F64(vm.registers[src2])) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be f64");
            }
            vm.registers[dst] = F64_VAL(AS_F64(vm.registers[src1]) + AS_F64(vm.registers[src2]));
            DISPATCH();
        }

    LABEL_OP_SUB_F64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            if (!IS_F64(vm.registers[src1]) || !IS_F64(vm.registers[src2])) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be f64");
            }
            vm.registers[dst] = F64_VAL(AS_F64(vm.registers[src1]) - AS_F64(vm.registers[src2]));
            DISPATCH();
        }

    LABEL_OP_MUL_F64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            if (!IS_F64(vm.registers[src1]) || !IS_F64(vm.registers[src2])) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be f64");
            }
            vm.registers[dst] = F64_VAL(AS_F64(vm.registers[src1]) * AS_F64(vm.registers[src2]));
            DISPATCH();
        }

    LABEL_OP_DIV_F64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            if (!IS_F64(vm.registers[src1]) || !IS_F64(vm.registers[src2])) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be f64");
            }
            double a = AS_F64(vm.registers[src1]);
            double b = AS_F64(vm.registers[src2]);
            
            // IEEE 754 compliant: division by zero produces infinity, not error
            double result = a / b;
            
            // The result may be infinity, -infinity, or NaN
            // These are valid f64 values according to IEEE 754
            vm.registers[dst] = F64_VAL(result);
            DISPATCH();
        }

    LABEL_OP_MOD_F64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            if (!IS_F64(vm.registers[src1]) || !IS_F64(vm.registers[src2])) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be f64");
            }
            double a = AS_F64(vm.registers[src1]);
            double b = AS_F64(vm.registers[src2]);
            
            // IEEE 754 compliant: use fmod for floating point modulo
            double result = fmod(a, b);
            
            // The result may be infinity, -infinity, or NaN
            // These are valid f64 values according to IEEE 754
            vm.registers[dst] = F64_VAL(result);
            DISPATCH();
        }

    // Bitwise Operations
    LABEL_OP_AND_I32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            if (!IS_I32(vm.registers[src1]) || !IS_I32(vm.registers[src2])) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be i32");
            }
            vm.registers[dst] = I32_VAL(AS_I32(vm.registers[src1]) & AS_I32(vm.registers[src2]));
            DISPATCH();
        }

    LABEL_OP_OR_I32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            if (!IS_I32(vm.registers[src1]) || !IS_I32(vm.registers[src2])) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be i32");
            }
            vm.registers[dst] = I32_VAL(AS_I32(vm.registers[src1]) | AS_I32(vm.registers[src2]));
            DISPATCH();
        }

    LABEL_OP_XOR_I32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            if (!IS_I32(vm.registers[src1]) || !IS_I32(vm.registers[src2])) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be i32");
            }
            vm.registers[dst] = I32_VAL(AS_I32(vm.registers[src1]) ^ AS_I32(vm.registers[src2]));
            DISPATCH();
        }

    LABEL_OP_NOT_I32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src = READ_BYTE();
            if (!IS_I32(vm.registers[src])) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operand must be i32");
            }
            vm.registers[dst] = I32_VAL(~AS_I32(vm.registers[src]));
            DISPATCH();
        }

    LABEL_OP_SHL_I32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            if (!IS_I32(vm.registers[src1]) || !IS_I32(vm.registers[src2])) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be i32");
            }
            vm.registers[dst] = I32_VAL(AS_I32(vm.registers[src1]) << AS_I32(vm.registers[src2]));
            DISPATCH();
        }

    LABEL_OP_SHR_I32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src1 = READ_BYTE();
            uint8_t src2 = READ_BYTE();
            if (!IS_I32(vm.registers[src1]) || !IS_I32(vm.registers[src2])) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be i32");
            }
            vm.registers[dst] = I32_VAL(AS_I32(vm.registers[src1]) >> AS_I32(vm.registers[src2]));
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
            if (!IS_I32(vm.registers[src])) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be i32");
            }
            vm.registers[dst] = F64_VAL((double)AS_I32(vm.registers[src]));
            DISPATCH();
        }

    LABEL_OP_I64_TO_F64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src = READ_BYTE();
            if (!IS_I64(vm.registers[src])) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be i64");
            }
            vm.registers[dst] = F64_VAL((double)AS_I64(vm.registers[src]));
            DISPATCH();
        }

    LABEL_OP_F64_TO_I32_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src = READ_BYTE();
            if (!IS_F64(vm.registers[src])) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be f64");
            }
            vm.registers[dst] = I32_VAL((int32_t)AS_F64(vm.registers[src]));
            DISPATCH();
        }

    LABEL_OP_F64_TO_I64_R: {
            uint8_t dst = READ_BYTE();
            uint8_t src = READ_BYTE();
            if (!IS_F64(vm.registers[src])) {
                VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be f64");
            }
            vm.registers[dst] = I64_VAL((int64_t)AS_F64(vm.registers[src]));
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
        
        // Convert operands to boolean using truthiness rules
        bool left_bool = false;
        bool right_bool = false;
        
        // Convert left operand to boolean
        if (IS_BOOL(vm.registers[src1])) {
            left_bool = AS_BOOL(vm.registers[src1]);
        } else if (IS_I32(vm.registers[src1])) {
            left_bool = AS_I32(vm.registers[src1]) != 0;
        } else if (IS_I64(vm.registers[src1])) {
            left_bool = AS_I64(vm.registers[src1]) != 0;
        } else if (IS_U32(vm.registers[src1])) {
            left_bool = AS_U32(vm.registers[src1]) != 0;
        } else if (IS_U64(vm.registers[src1])) {
            left_bool = AS_U64(vm.registers[src1]) != 0;
        } else if (IS_F64(vm.registers[src1])) {
            left_bool = AS_F64(vm.registers[src1]) != 0.0;
        } else if (IS_BOOL(vm.registers[src1])) {
            left_bool = false;
        } else {
            left_bool = true; // Objects, strings, etc. are truthy
        }
        
        // Convert right operand to boolean
        if (IS_BOOL(vm.registers[src2])) {
            right_bool = AS_BOOL(vm.registers[src2]);
        } else if (IS_I32(vm.registers[src2])) {
            right_bool = AS_I32(vm.registers[src2]) != 0;
        } else if (IS_I64(vm.registers[src2])) {
            right_bool = AS_I64(vm.registers[src2]) != 0;
        } else if (IS_U32(vm.registers[src2])) {
            right_bool = AS_U32(vm.registers[src2]) != 0;
        } else if (IS_U64(vm.registers[src2])) {
            right_bool = AS_U64(vm.registers[src2]) != 0;
        } else if (IS_F64(vm.registers[src2])) {
            right_bool = AS_F64(vm.registers[src2]) != 0.0;
        } else if (IS_BOOL(vm.registers[src2])) {
            right_bool = false;
        } else {
            right_bool = true; // Objects, strings, etc. are truthy
        }
        
        vm.registers[dst] = BOOL_VAL(left_bool && right_bool);
        DISPATCH();
    }

    LABEL_OP_OR_BOOL_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        
        // Convert operands to boolean using truthiness rules
        bool left_bool = false;
        bool right_bool = false;
        
        // Convert left operand to boolean
        if (IS_BOOL(vm.registers[src1])) {
            left_bool = AS_BOOL(vm.registers[src1]);
        } else if (IS_I32(vm.registers[src1])) {
            left_bool = AS_I32(vm.registers[src1]) != 0;
        } else if (IS_I64(vm.registers[src1])) {
            left_bool = AS_I64(vm.registers[src1]) != 0;
        } else if (IS_U32(vm.registers[src1])) {
            left_bool = AS_U32(vm.registers[src1]) != 0;
        } else if (IS_U64(vm.registers[src1])) {
            left_bool = AS_U64(vm.registers[src1]) != 0;
        } else if (IS_F64(vm.registers[src1])) {
            left_bool = AS_F64(vm.registers[src1]) != 0.0;
        } else if (IS_BOOL(vm.registers[src1])) {
            left_bool = false;
        } else {
            left_bool = true; // Objects, strings, etc. are truthy
        }
        
        // Convert right operand to boolean
        if (IS_BOOL(vm.registers[src2])) {
            right_bool = AS_BOOL(vm.registers[src2]);
        } else if (IS_I32(vm.registers[src2])) {
            right_bool = AS_I32(vm.registers[src2]) != 0;
        } else if (IS_I64(vm.registers[src2])) {
            right_bool = AS_I64(vm.registers[src2]) != 0;
        } else if (IS_U32(vm.registers[src2])) {
            right_bool = AS_U32(vm.registers[src2]) != 0;
        } else if (IS_U64(vm.registers[src2])) {
            right_bool = AS_U64(vm.registers[src2]) != 0;
        } else if (IS_F64(vm.registers[src2])) {
            right_bool = AS_F64(vm.registers[src2]) != 0.0;
        } else if (IS_BOOL(vm.registers[src2])) {
            right_bool = false;
        } else {
            right_bool = true; // Objects, strings, etc. are truthy
        }
        
        vm.registers[dst] = BOOL_VAL(left_bool || right_bool);
        DISPATCH();
    }

    LABEL_OP_NOT_BOOL_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src = READ_BYTE();
        
        // Convert operand to boolean using truthiness rules, then negate
        bool src_bool = false;
        if (IS_BOOL(vm.registers[src])) {
            src_bool = AS_BOOL(vm.registers[src]);
        } else if (IS_I32(vm.registers[src])) {
            src_bool = AS_I32(vm.registers[src]) != 0;
        } else if (IS_I64(vm.registers[src])) {
            src_bool = AS_I64(vm.registers[src]) != 0;
        } else if (IS_U32(vm.registers[src])) {
            src_bool = AS_U32(vm.registers[src]) != 0;
        } else if (IS_U64(vm.registers[src])) {
            src_bool = AS_U64(vm.registers[src]) != 0;
        } else if (IS_F64(vm.registers[src])) {
            src_bool = AS_F64(vm.registers[src]) != 0.0;
        } else if (IS_BOOL(vm.registers[src])) {
            src_bool = false;
        } else {
            src_bool = true; // Objects, strings, etc. are truthy
        }
        
        vm.registers[dst] = BOOL_VAL(!src_bool);
        DISPATCH();
    }

    LABEL_OP_CONCAT_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        if (!IS_STRING(vm.registers[src1]) || !IS_STRING(vm.registers[src2])) {
            VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be string");
        }
        ObjString* a = AS_STRING(vm.registers[src1]);
        ObjString* b = AS_STRING(vm.registers[src2]);
        int newLen = a->length + b->length;
        char* buf = malloc(newLen + 1);
        memcpy(buf, a->chars, a->length);
        memcpy(buf + a->length, b->chars, b->length);
        buf[newLen] = '\0';
        ObjString* res = allocateString(buf, newLen);
        free(buf);
        vm.registers[dst] = STRING_VAL(res);
        DISPATCH();
    }

    LABEL_OP_TO_STRING_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src = READ_BYTE();
        Value val = vm.registers[src];
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
            vm.registers[dst] = val;
            DISPATCH();
        } else {
            snprintf(buffer, sizeof(buffer), "nil");
        }
        
        ObjString* result = allocateString(buffer, (int)strlen(buffer));
        vm.registers[dst] = STRING_VAL(result);
        DISPATCH();
    }

    LABEL_OP_JUMP: {
            uint16_t offset = READ_SHORT();
            CF_JUMP(offset);
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

    LABEL_OP_LOOP: {
        uint16_t offset = READ_SHORT();
        
        // Hot path detection: Profile loop iterations
        if (g_profiling.isActive && (g_profiling.enabledFlags & PROFILE_HOT_PATHS)) {
            static uint64_t loop_iterations = 0;
            loop_iterations++;
            profileHotPath((void*)(vm.ip - vm.chunk->code), loop_iterations);
        }
        
        CF_LOOP(offset);
        DISPATCH();
    }

    LABEL_OP_GET_ITER_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src = READ_BYTE();
        Value v = vm.registers[src];
        if (!IS_RANGE_ITERATOR(v)) {
            VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Value not iterable");
        }
        vm.registers[dst] = v;
        DISPATCH();
    }

    LABEL_OP_ITER_NEXT_R: {
        uint8_t dst = READ_BYTE();
        uint8_t iterReg = READ_BYTE();
        uint8_t hasReg = READ_BYTE();
        if (!IS_RANGE_ITERATOR(vm.registers[iterReg])) {
            VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Invalid iterator");
        }
        ObjRangeIterator* it = AS_RANGE_ITERATOR(vm.registers[iterReg]);
        if (it->current >= it->end) {
            vm.registers[hasReg] = BOOL_VAL(false);
        } else {
            vm.registers[dst] = I64_VAL(it->current);
            it->current++;
            vm.registers[hasReg] = BOOL_VAL(true);
        }
        DISPATCH();
    }

    LABEL_OP_PRINT_MULTI_R: {
            uint8_t first = READ_BYTE();
            uint8_t count = READ_BYTE();
            uint8_t nl = READ_BYTE();
            builtin_print(&vm.registers[first], count, nl != 0, NULL);
            DISPATCH();
        }

    LABEL_OP_PRINT_MULTI_SEP_R: {
            uint8_t first = READ_BYTE();
            uint8_t count = READ_BYTE();
            uint8_t sep_reg = READ_BYTE();
            uint8_t nl = READ_BYTE();
            builtin_print_with_sep_value(&vm.registers[first], count, nl != 0, vm.registers[sep_reg]);
            DISPATCH();
        }

    LABEL_OP_PRINT_R: {
            uint8_t reg = READ_BYTE();
            builtin_print(&vm.registers[reg], 1, true, NULL);
            DISPATCH();
        }

    LABEL_OP_PRINT_NO_NL_R: {
            uint8_t reg = READ_BYTE();
            builtin_print(&vm.registers[reg], 1, false, NULL);
            DISPATCH();
        }

    LABEL_OP_CALL_R: {
            uint8_t funcReg = READ_BYTE();
            uint8_t firstArgReg = READ_BYTE();
            uint8_t argCount = READ_BYTE();
            uint8_t resultReg = READ_BYTE();
            
            Value funcValue = vm.registers[funcReg];
            
            if (IS_CLOSURE(funcValue)) {
                // Calling a closure
                ObjClosure* closure = AS_CLOSURE(funcValue);
                ObjFunction* function = closure->function;
                
                // Check arity
                if (argCount != function->arity) {
                    vm.registers[resultReg] = BOOL_VAL(false);
                    DISPATCH();
                }
                
                // Check if we have room for another call frame
                if (vm.frameCount >= FRAMES_MAX) {
                    vm.registers[resultReg] = BOOL_VAL(false);
                    DISPATCH();
                }
                
                // Create new call frame with closure context
                CallFrame* frame = &vm.frames[vm.frameCount++];
                frame->returnAddress = vm.ip;
                frame->previousChunk = vm.chunk;
                frame->baseRegister = resultReg;
                
                // Set up closure context (closure in register 0)
                vm.registers[0] = funcValue;  // Store closure in register 0 for upvalue access
                
                // Copy arguments to the start of register space for the function
                for (int i = 0; i < argCount; i++) {
                    vm.registers[256 - argCount + i] = vm.registers[firstArgReg + i];
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
                    vm.registers[resultReg] = BOOL_VAL(false);
                    DISPATCH();
                }
                
                // Check if we have room for another call frame
                if (vm.frameCount >= FRAMES_MAX) {
                    vm.registers[resultReg] = BOOL_VAL(false);
                    DISPATCH();
                }
                
                // Create new call frame
                CallFrame* frame = &vm.frames[vm.frameCount++];
                frame->returnAddress = vm.ip;
                frame->previousChunk = vm.chunk;
                frame->baseRegister = resultReg;
                
                // Simple parameter base calculation to match compiler
                uint8_t paramBase = 256 - objFunction->arity;
                if (paramBase < 1) paramBase = 1;
                
                // Copy arguments to parameter registers
                for (int i = 0; i < argCount; i++) {
                    vm.registers[paramBase + i] = vm.registers[firstArgReg + i];
                }
                
                // Switch to function's bytecode
                vm.chunk = objFunction->chunk;
                vm.ip = objFunction->chunk->code;
                
                DISPATCH();
            } else if (IS_I32(funcValue)) {
                int functionIndex = AS_I32(funcValue);
                
                if (functionIndex < 0 || functionIndex >= vm.functionCount) {
                    vm.registers[resultReg] = BOOL_VAL(false);
                    DISPATCH();
                }
                
                Function* function = &vm.functions[functionIndex];
                
                // Check arity
                if (argCount != function->arity) {
                    vm.registers[resultReg] = BOOL_VAL(false);
                    DISPATCH();
                }
                
                // Check if we have room for another call frame
                if (vm.frameCount >= FRAMES_MAX) {
                    vm.registers[resultReg] = BOOL_VAL(false);
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
                uint8_t paramBase = 256 - function->arity;
                if (paramBase < 1) paramBase = 1;
                frame->parameterBaseRegister = paramBase;
                
                // Save registers that will be overwritten by parameters
                frame->savedRegisterCount = argCount < 64 ? argCount : 64;
                for (int i = 0; i < frame->savedRegisterCount; i++) {
                    frame->savedRegisters[i] = vm.registers[paramBase + i];
                }
                
                // Copy arguments to parameter registers
                for (int i = 0; i < argCount; i++) {
                    vm.registers[paramBase + i] = vm.registers[firstArgReg + i];
                }
                
                // Switch to function's chunk
                vm.chunk = function->chunk;
                vm.ip = function->chunk->code + function->start;
                
            } else {
                vm.registers[resultReg] = BOOL_VAL(false);
            }
            
            DISPATCH();
        }

    LABEL_OP_TAIL_CALL_R: {
            uint8_t funcReg = READ_BYTE();
            uint8_t firstArgReg = READ_BYTE();
            uint8_t argCount = READ_BYTE();
            uint8_t resultReg = READ_BYTE();
            
            Value funcValue = vm.registers[funcReg];
            
            if (IS_I32(funcValue)) {
                int functionIndex = AS_I32(funcValue);
                
                if (functionIndex < 0 || functionIndex >= vm.functionCount) {
                    vm.registers[resultReg] = BOOL_VAL(false);
                    DISPATCH();
                }
                
                Function* function = &vm.functions[functionIndex];
                
                // Check arity
                if (argCount != function->arity) {
                    vm.registers[resultReg] = BOOL_VAL(false);
                    DISPATCH();
                }
                
                // For tail calls, we reuse the current frame instead of creating a new one
                // This prevents stack growth in recursive calls
                
                // Copy arguments to function's frame registers  
                // We need to be careful about overlapping registers
                Value tempArgs[256];
                for (int i = 0; i < argCount; i++) {
                    tempArgs[i] = vm.registers[firstArgReg + i];
                }
                
                // Clear the current frame's parameter registers first, then set new ones
                for (int i = 0; i < argCount; i++) {
                    uint16_t frame_reg_id = FRAME_REG_START + i;
                    set_register(&vm.register_file, frame_reg_id, tempArgs[i]);
                    vm.registers[200 + i] = tempArgs[i];  // Use safe parameter register range
                }
                
                // Switch to function's chunk - reuse current frame
                vm.chunk = function->chunk;
                vm.ip = function->chunk->code + function->start;
                
            } else {
                vm.registers[resultReg] = BOOL_VAL(false);
            }
            
            DISPATCH();
        }

    LABEL_OP_RETURN_R: {
            uint8_t reg = READ_BYTE();
            Value returnValue = vm.registers[reg];
            if (vm.frameCount > 0) {
                CallFrame* frame = &vm.frames[--vm.frameCount];
                
                // Close upvalues before restoring registers to prevent corruption
                printf("DEBUG VM: Closing upvalues before return\n");
                closeUpvalues(&vm.registers[frame->parameterBaseRegister]);
                
                // Restore saved registers - simple approach
                for (int i = 0; i < frame->savedRegisterCount; i++) {
                    vm.registers[frame->parameterBaseRegister + i] = frame->savedRegisters[i];
                }
                
                vm.chunk = frame->previousChunk;
                vm.ip = frame->returnAddress;
                vm.registers[frame->baseRegister] = returnValue;
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
                printf("DEBUG VM: Closing upvalues before void return\n");
                closeUpvalues(&vm.registers[frame->parameterBaseRegister]);
                
                // Restore saved registers - simple approach
                for (int i = 0; i < frame->savedRegisterCount; i++) {
                    vm.registers[frame->parameterBaseRegister + i] = frame->savedRegisters[i];
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
        vm.registers[reg] = *src;
        DISPATCH();
    }

    LABEL_OP_LOAD_SPILL: {
        uint8_t reg = READ_BYTE();
        uint8_t spill_id_high = READ_BYTE();
        uint8_t spill_id_low = READ_BYTE();
        uint16_t spill_id = (spill_id_high << 8) | spill_id_low;
        Value* src = get_register(&vm.register_file, spill_id);
        vm.registers[reg] = *src;
        DISPATCH();
    }

    LABEL_OP_STORE_SPILL: {
        uint8_t spill_id_high = READ_BYTE();
        uint8_t spill_id_low = READ_BYTE();
        uint8_t reg = READ_BYTE();
        uint16_t spill_id = (spill_id_high << 8) | spill_id_low;
        Value value = vm.registers[reg];
        set_register(&vm.register_file, spill_id, value);
        DISPATCH();
    }

    LABEL_OP_STORE_FRAME: {
        uint8_t frame_offset = READ_BYTE();
        uint8_t reg = READ_BYTE();
        uint16_t frame_reg_id = FRAME_REG_START + frame_offset;
        set_register(&vm.register_file, frame_reg_id, vm.registers[reg]);
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
        CF_JUMP_SHORT(offset);
        DISPATCH();
    }

    LABEL_OP_JUMP_BACK_SHORT: {
        uint8_t offset = READ_BYTE();
        CF_JUMP_BACK_SHORT(offset);
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
        
        CF_LOOP_SHORT(offset);
        DISPATCH();
    }

    // Typed arithmetic operations for maximum performance (bypass Value boxing)
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
        DISPATCH_TYPED();
    }

    LABEL_OP_MOD_I32_TYPED: {
        handle_mod_i32_typed();
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
        DISPATCH_TYPED();
    }

    LABEL_OP_MOD_I64_TYPED: {
        handle_mod_i64_typed();
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
        DISPATCH_TYPED();
    }

    LABEL_OP_MOD_F64_TYPED: {
        handle_mod_f64_typed();
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

    // U32 Typed Operations
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
        DISPATCH_TYPED();
    }

    LABEL_OP_MOD_U32_TYPED: {
        handle_mod_u32_typed();
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
        DISPATCH_TYPED();
    }

    LABEL_OP_MOD_U64_TYPED: {
        handle_mod_u64_typed();
        DISPATCH_TYPED();
    }

    // TODO: Removed mixed-type op for Rust-style strict typing

    LABEL_OP_TIME_STAMP: {
        uint8_t dst = READ_BYTE();
        
        // Get high-precision timestamp in seconds
        double timestamp = builtin_time_stamp();
        
        // Store in both typed register and regular register for compatibility
        vm.typed_regs.f64_regs[dst] = timestamp;
        vm.typed_regs.reg_types[dst] = REG_TYPE_F64;
        vm.registers[dst] = F64_VAL(timestamp);
        
        DISPATCH();
    }

    // Phase 2.2: Fused instruction implementations for better performance
    LABEL_OP_ADD_I32_IMM: {
        uint8_t dst = *vm.ip++;
        uint8_t src = *vm.ip++;
        int32_t imm = *(int32_t*)vm.ip;
        vm.ip += 4;
        
        // Compiler ensures this is only emitted for i32 operations, so trust it
        if (!IS_I32(vm.registers[src])) {
            VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operand must be i32");
        }
        
        int32_t result = AS_I32(vm.registers[src]) + imm;
        vm.registers[dst] = I32_VAL(result);
        
        DISPATCH_TYPED();
    }

    LABEL_OP_SUB_I32_IMM: {
        uint8_t dst = *vm.ip++;
        uint8_t src = *vm.ip++;
        int32_t imm = *(int32_t*)vm.ip;
        vm.ip += 4;
        
        // Compiler ensures this is only emitted for i32 operations, so trust it
        if (!IS_I32(vm.registers[src])) {
            VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operand must be i32");
        }
        
        int32_t result = AS_I32(vm.registers[src]) - imm;
        vm.registers[dst] = I32_VAL(result);
        
        DISPATCH_TYPED();
    }

    LABEL_OP_MUL_I32_IMM: {
        uint8_t dst = *vm.ip++;
        uint8_t src = *vm.ip++;
        int32_t imm = *(int32_t*)vm.ip;
        vm.ip += 4;
        
        // Compiler ensures this is only emitted for i32 operations, so trust it
        if (!IS_I32(vm.registers[src])) {
            VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operand must be i32");
        }
        
        int32_t result = AS_I32(vm.registers[src]) * imm;
        vm.registers[dst] = I32_VAL(result);
        
        DISPATCH_TYPED();
    }

    LABEL_OP_CMP_I32_IMM: {
        uint8_t dst = *vm.ip++;
        uint8_t src = *vm.ip++;
        int32_t imm = *(int32_t*)vm.ip;
        vm.ip += 4;
        
        vm.typed_regs.bool_regs[dst] = vm.typed_regs.i32_regs[src] < imm;
        
        DISPATCH_TYPED();
    }

    LABEL_OP_INC_CMP_JMP: {
        uint8_t reg = *vm.ip++;
        uint8_t limit_reg = *vm.ip++;
        int16_t offset = *(int16_t*)vm.ip;
        vm.ip += 2;
        
        // Compiler ensures this is only emitted for i32 operations, so trust it
        if (!IS_I32(vm.registers[reg]) || !IS_I32(vm.registers[limit_reg])) {
            VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be i32");
        }
        
        int32_t incremented = AS_I32(vm.registers[reg]) + 1;
        vm.registers[reg] = I32_VAL(incremented);
        if (incremented < AS_I32(vm.registers[limit_reg])) {
            vm.ip += offset;
        }
        
        DISPATCH_TYPED();
    }

    LABEL_OP_DEC_CMP_JMP: {
        uint8_t reg = *vm.ip++;
        uint8_t zero_test = *vm.ip++;
        int16_t offset = *(int16_t*)vm.ip;
        vm.ip += 2;
        
        // Fused decrement + compare + conditional jump
        if (--vm.typed_regs.i32_regs[reg] > vm.typed_regs.i32_regs[zero_test]) {
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
        vm.typed_regs.i32_regs[dst] = 
            vm.typed_regs.i32_regs[mul1] * vm.typed_regs.i32_regs[mul2] + 
            vm.typed_regs.i32_regs[add];
        
        DISPATCH_TYPED();
    }

    LABEL_OP_CLOSURE_R: {
        uint8_t dstReg = READ_BYTE();
        uint8_t functionReg = READ_BYTE();
        uint8_t upvalueCount = READ_BYTE();
        
        printf("DEBUG VM: Creating closure in reg[%d] from function reg[%d] with %d upvalues\n", 
               dstReg, functionReg, upvalueCount);
        
        Value functionValue = vm.registers[functionReg];
        if (!IS_FUNCTION(functionValue)) {
            VM_ERROR_RETURN(ERROR_RUNTIME, CURRENT_LOCATION(), "Expected function for closure creation");
        }
        
        ObjFunction* function = AS_FUNCTION(functionValue);
        ObjClosure* closure = allocateClosure(function);
        
        for (int i = 0; i < upvalueCount; i++) {
            uint8_t isLocal = READ_BYTE();
            uint8_t index = READ_BYTE();
            
            printf("DEBUG VM: Upvalue[%d]: isLocal=%d, index=%d\n", i, isLocal, index);
            
            if (isLocal) {
                Value localValue = vm.registers[index];
                printf("DEBUG VM: Capturing local register[%d] as upvalue\n", index);
                if (IS_I32(localValue)) {
                    printf("DEBUG VM: Local register[%d] contains i32: %d\n", index, AS_I32(localValue));
                } else if (IS_BOOL(localValue)) {
                    printf("DEBUG VM: Local register[%d] contains NIL\n", index);
                } else {
                    printf("DEBUG VM: Local register[%d] contains type: %d\n", index, localValue.type);
                }
                closure->upvalues[i] = captureUpvalue(&vm.registers[index]);
            } else {
                ObjClosure* enclosing = AS_CLOSURE(vm.registers[0]); // Current closure
                printf("DEBUG VM: Copying upvalue[%d] from enclosing closure\n", index);
                closure->upvalues[i] = enclosing->upvalues[index];
            }
        }
        
        vm.registers[dstReg] = CLOSURE_VAL(closure);
        DISPATCH();
    }

    LABEL_OP_GET_UPVALUE_R: {
        uint8_t dstReg = READ_BYTE();
        uint8_t upvalueIndex = READ_BYTE();
        
        printf("DEBUG VM: Getting upvalue[%d] into reg[%d]\n", upvalueIndex, dstReg);
        
        ObjClosure* closure = AS_CLOSURE(vm.registers[0]); // Current closure
        if (closure == NULL || upvalueIndex >= closure->function->upvalueCount) {
            printf("DEBUG VM: ERROR - Invalid upvalue access!\n");
            VM_ERROR_RETURN(ERROR_RUNTIME, CURRENT_LOCATION(), "Invalid upvalue access");
        }
        
        Value upvalue = *closure->upvalues[upvalueIndex]->location;
        vm.registers[dstReg] = upvalue;
        
        // Debug the actual value and type
        if (IS_I32(upvalue)) {
            printf("DEBUG VM: Upvalue[%d] retrieved i32 value: %d\n", upvalueIndex, AS_I32(upvalue));
        } else if (IS_BOOL(upvalue)) {
            printf("DEBUG VM: Upvalue[%d] retrieved bool value: %s\n", upvalueIndex, AS_BOOL(upvalue) ? "true" : "false");
        } else if (IS_BOOL(upvalue)) {
            printf("DEBUG VM: Upvalue[%d] retrieved NIL value\n", upvalueIndex);
        } else {
            printf("DEBUG VM: Upvalue[%d] retrieved unknown type: %d\n", upvalueIndex, upvalue.type);
        }
        DISPATCH();
    }

    LABEL_OP_SET_UPVALUE_R: {
        uint8_t upvalueIndex = READ_BYTE();
        uint8_t valueReg = READ_BYTE();
        
        ObjClosure* closure = AS_CLOSURE(vm.registers[0]); // Current closure
        *closure->upvalues[upvalueIndex]->location = vm.registers[valueReg];
        DISPATCH();
    }

    LABEL_OP_CLOSE_UPVALUE_R: {
        uint8_t localReg = READ_BYTE();
        closeUpvalues(&vm.registers[localReg]);
        DISPATCH();
    }

    LABEL_OP_HALT:
        vm.lastExecutionTime = get_time_vm() - start_time;
        RETURN(INTERPRET_OK);

    LABEL_UNKNOWN: __attribute__((unused))
        VM_ERROR_RETURN(ERROR_RUNTIME, CURRENT_LOCATION(), "Unknown opcode: %d", instruction);

    #undef RETURN
}

// Include the handlers implementation
#include "../handlers/vm_arithmetic_handlers.c"
#include "../handlers/vm_memory_handlers.c"
#include "../handlers/vm_control_flow_handlers.c"
#endif // USE_COMPUTED_GOTO
