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
#include "vm/vm_loop_fastpaths.h"
#include "vm/vm_opcode_handlers.h"
#include "vm/register_file.h"
#include "vm/vm_profiling.h"
#include <math.h>
#include <limits.h>

#define VM_HANDLE_INC_I32_SLOW_PATH(reg) \
    do { \
        Value val_reg__ = vm_get_register_safe((reg)); \
        if (!IS_I32(val_reg__)) { \
            vm_trace_loop_event(LOOP_TRACE_TYPE_MISMATCH); \
            if (vm_typed_reg_in_range((reg))) { \
                vm.typed_regs.reg_types[(reg)] = REG_TYPE_HEAP; \
                vm.typed_regs.dirty[(reg)] = false; \
            } \
            VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be i32"); \
        } \
        int32_t current__ = AS_I32(val_reg__); \
        int32_t next_value__; \
        if (__builtin_add_overflow(current__, 1, &next_value__)) { \
            vm_trace_loop_event(LOOP_TRACE_OVERFLOW_GUARD); \
            VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Integer overflow"); \
        } \
        if (vm_typed_reg_in_range((reg)) && \
            vm.typed_regs.reg_types[(reg)] == REG_TYPE_I32) { \
            vm.typed_regs.i32_regs[(reg)] = next_value__; \
            vm.typed_regs.dirty[(reg)] = false; \
        } \
        vm_set_register_safe((reg), I32_VAL(next_value__)); \
    } while (0)

// Only needed for the switch-dispatch backend
#if !USE_COMPUTED_GOTO
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
#endif




// ✅ Auto-detect computed goto support
#ifndef USE_COMPUTED_GOTO
  #if defined(__GNUC__) || defined(__clang__)
    #define USE_COMPUTED_GOTO 1
  #else
    #define USE_COMPUTED_GOTO 0
  #endif
#endif

#if !USE_COMPUTED_GOTO
InterpretResult vm_run_dispatch(void) {
    double start_time = get_time_vm();
    #define RETURN(val) \
        do { \
            vm.lastExecutionTime = get_time_vm() - start_time; \
            return (val); \
        } while (0)

        for (;;) {
            if (vm.trace) {
                // Debug trace
                DEBUG_VM_PRINT("        ");
                for (int i = 0; i < 8; i++) {
                    DEBUG_VM_PRINT("[ R%d: ", i);
                    printValue(vm_get_register_safe(i));
                    DEBUG_VM_PRINT(" ]");
                }
                DEBUG_VM_PRINT("\n");

                disassembleInstruction(vm.chunk, (int)(vm.ip - vm.chunk->code));
            }

            vm.instruction_count++;

            uint8_t instruction = READ_BYTE();
            PROFILE_INC(instruction);

            switch (instruction) {
                case OP_LOAD_CONST: {
                    handle_load_const();
                    break;
                }


                case OP_LOAD_TRUE: {
                    handle_load_true();
                    break;
                }

                case OP_LOAD_FALSE: {
                    handle_load_false();
                    break;
                }

                case OP_MOVE: {
                    handle_move_reg();
                    break;
                }

                case OP_LOAD_GLOBAL: {
                    handle_load_global();
                    break;
                }

                case OP_STORE_GLOBAL: {
                    handle_store_global();
                    break;
                }

                // Arithmetic operations with intelligent overflow handling
                case OP_ADD_I32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    // Check if either operand is a string - if so, do string concatenation
                    if (IS_STRING(vm_get_register_safe(src1)) || IS_STRING(vm_get_register_safe(src2))) {
                        // Convert both operands to strings and concatenate
                        Value left = vm_get_register_safe(src1);
                        Value right = vm_get_register_safe(src2);
                        
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
                            vm_set_register_safe(dst, STRING_VAL(result));
                        } else {
                            StringBuilder* sb = createStringBuilder(newLength + 1);
                            appendToStringBuilder(sb, leftStr->chars, leftStr->length);
                            appendToStringBuilder(sb, rightStr->chars, rightStr->length);
                            ObjString* result = stringBuilderToString(sb);
                            freeStringBuilder(sb);
                            vm_set_register_safe(dst, STRING_VAL(result));
                        }
                        break;
                    }

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
                    break;
                }

                case OP_SUB_I32_R: {
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
                    vm_set_register_safe(dst, I32_VAL(a - b));
#else
                    // Strict same-type arithmetic only (after coercion)
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
                    break;
                }

                case OP_MUL_I32_R: {
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
                    break;
                }

                case OP_DIV_I32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (vm_get_register_safe(src1).type != vm_get_register_safe(src2).type) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be the same type. Use 'as' for explicit type conversion.");
                    }

                    if (!(IS_I32(vm_get_register_safe(src1)) || IS_I64(vm_get_register_safe(src1)) ||
                          IS_U32(vm_get_register_safe(src1)) || IS_U64(vm_get_register_safe(src1)) || IS_F64(vm_get_register_safe(src1)))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be numeric (i32, i64, u32, u64, or f64)");
                    }

                    if (IS_I32(vm_get_register_safe(src1))) {
                        HANDLE_I32_OVERFLOW_DIV(AS_I32(vm_get_register_safe(src1)), AS_I32(vm_get_register_safe(src2)), dst);
                    } else if (IS_I64(vm_get_register_safe(src1))) {
                        HANDLE_I64_OVERFLOW_DIV(AS_I64(vm_get_register_safe(src1)), AS_I64(vm_get_register_safe(src2)), dst);
                    } else if (IS_U32(vm_get_register_safe(src1))) {
                        HANDLE_U32_OVERFLOW_DIV(AS_U32(vm_get_register_safe(src1)), AS_U32(vm_get_register_safe(src2)), dst);
                    } else if (IS_U64(vm_get_register_safe(src1))) {
                        HANDLE_U64_OVERFLOW_DIV(AS_U64(vm_get_register_safe(src1)), AS_U64(vm_get_register_safe(src2)), dst);
                    } else {
                        HANDLE_F64_OVERFLOW_DIV(AS_F64(vm_get_register_safe(src1)), AS_F64(vm_get_register_safe(src2)), dst);
                    }
                    break;
                }

                case OP_MOD_I32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (vm_get_register_safe(src1).type != vm_get_register_safe(src2).type) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be the same type. Use 'as' for explicit type conversion.");
                    }

                    if (!(IS_I32(vm_get_register_safe(src1)) || IS_I64(vm_get_register_safe(src1)) ||
                          IS_U32(vm_get_register_safe(src1)) || IS_U64(vm_get_register_safe(src1)) || IS_F64(vm_get_register_safe(src1)))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be numeric (i32, i64, u32, u64, or f64)");
                    }

                    if (IS_I32(vm_get_register_safe(src1))) {
                        HANDLE_I32_OVERFLOW_MOD(AS_I32(vm_get_register_safe(src1)), AS_I32(vm_get_register_safe(src2)), dst);
                    } else if (IS_I64(vm_get_register_safe(src1))) {
                        HANDLE_I64_OVERFLOW_MOD(AS_I64(vm_get_register_safe(src1)), AS_I64(vm_get_register_safe(src2)), dst);
                    } else if (IS_U32(vm_get_register_safe(src1))) {
                        HANDLE_U32_OVERFLOW_MOD(AS_U32(vm_get_register_safe(src1)), AS_U32(vm_get_register_safe(src2)), dst);
                    } else if (IS_U64(vm_get_register_safe(src1))) {
                        HANDLE_U64_OVERFLOW_MOD(AS_U64(vm_get_register_safe(src1)), AS_U64(vm_get_register_safe(src2)), dst);
                    } else {
                        HANDLE_F64_OVERFLOW_MOD(AS_F64(vm_get_register_safe(src1)), AS_F64(vm_get_register_safe(src2)), dst);
                    }
                    break;
                }

                case OP_INC_I32_R: {
                    uint8_t reg = READ_BYTE();
                    if (vm_exec_inc_i32_checked(reg)) {
                        break;
                    }
                    VM_HANDLE_INC_I32_SLOW_PATH(reg);
                    break;
                }

                case OP_INC_I32_CHECKED: {
                    uint8_t reg = READ_BYTE();
                    if (vm_exec_inc_i32_checked(reg)) {
                        break;
                    }
                    VM_HANDLE_INC_I32_SLOW_PATH(reg);
                    break;
                }

                case OP_DEC_I32_R: {
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
                        vm_set_register_safe(reg, I32_VAL(AS_I32(vm_get_register_safe(reg)) - 1));
    #else
                        int32_t val = AS_I32(vm_get_register_safe(reg));
                        int32_t result;
                        if (__builtin_sub_overflow(val, 1, &result)) {
                            VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Integer overflow");
                        }
                        vm_set_register_safe(reg, I32_VAL(result));
    #endif
                    }
                    break;
                }

                case OP_NEG_I32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    
                    // Type safety: negation only works on numeric types
                    if (!(IS_I32(vm_get_register_safe(src)) || IS_I64(vm_get_register_safe(src)) || IS_U32(vm_get_register_safe(src)) || IS_U64(vm_get_register_safe(src)) || IS_F64(vm_get_register_safe(src)))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Unary minus only works on numeric types (i32, i64, u32, u64, f64)");
                    }
                    
    #if USE_FAST_ARITH
                    vm_set_register_safe(dst, I32_VAL(-AS_I32(vm_get_register_safe(src))));
    #else
                    // Handle different numeric types appropriately
                    if (IS_I32(vm_get_register_safe(src))) {
                        int32_t val = AS_I32(vm_get_register_safe(src));
                        if (val == INT32_MIN) {
                            VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Integer overflow: cannot negate INT32_MIN");
                        }
                        vm_set_register_safe(dst, I32_VAL(-val));
                    } else if (IS_I64(vm_get_register_safe(src))) {
                        int64_t val = AS_I64(vm_get_register_safe(src));
                        vm_set_register_safe(dst, I64_VAL(-val));
                    } else if (IS_U32(vm_get_register_safe(src))) {
                        uint32_t val = AS_U32(vm_get_register_safe(src));
                        // Convert to signed for negation
                        vm_set_register_safe(dst, I32_VAL(-((int32_t)val)));
                    } else if (IS_U64(vm_get_register_safe(src))) {
                        uint64_t val = AS_U64(vm_get_register_safe(src));
                        // Convert to signed for negation
                        vm_set_register_safe(dst, I64_VAL(-((int64_t)val)));
                    } else if (IS_F64(vm_get_register_safe(src))) {
                        double val = AS_F64(vm_get_register_safe(src));
                        vm_set_register_safe(dst, F64_VAL(-val));
                    }
    #endif
                    break;
                }

                // I64 arithmetic operations
                case OP_ADD_I64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (!IS_I64(vm_get_register_safe(src1)) ||
                        !IS_I64(vm_get_register_safe(src2))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be i64");
                    }

                    int64_t a = AS_I64(vm_get_register_safe(src1));
                    int64_t b = AS_I64(vm_get_register_safe(src2));
    #if USE_FAST_ARITH
                    vm_set_register_safe(dst, I64_VAL(a + b));
    #else
                    int64_t result;
                    if (__builtin_add_overflow(a, b, &result)) {
                        VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Integer overflow");
                    }
                    vm_set_register_safe(dst, I64_VAL(result));
    #endif
                    break;
                }

                case OP_SUB_I64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (!IS_I64(vm_get_register_safe(src1)) ||
                        !IS_I64(vm_get_register_safe(src2))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be i64");
                    }

                    int64_t a = AS_I64(vm_get_register_safe(src1));
                    int64_t b = AS_I64(vm_get_register_safe(src2));
    #if USE_FAST_ARITH
                    vm_set_register_safe(dst, I64_VAL(a - b));
    #else
                    int64_t result;
                    if (__builtin_sub_overflow(a, b, &result)) {
                        VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Integer overflow");
                    }
                    vm_set_register_safe(dst, I64_VAL(result));
    #endif
                    break;
                }

                case OP_MUL_I64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (!IS_I64(vm_get_register_safe(src1)) ||
                        !IS_I64(vm_get_register_safe(src2))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be i64");
                    }

                    int64_t a = AS_I64(vm_get_register_safe(src1));
                    int64_t b = AS_I64(vm_get_register_safe(src2));
    #if USE_FAST_ARITH
                    vm_set_register_safe(dst, I64_VAL(a * b));
    #else
                    int64_t result;
                    if (__builtin_mul_overflow(a, b, &result)) {
                        VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Integer overflow");
                    }
                    vm_set_register_safe(dst, I64_VAL(result));
    #endif
                    break;
                }

                case OP_DIV_I64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (!IS_I64(vm_get_register_safe(src1)) ||
                        !IS_I64(vm_get_register_safe(src2))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be i64");
                    }

                    int64_t b = AS_I64(vm_get_register_safe(src2));
                    if (b == 0) {
                        VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Division by zero");
                    }

                    vm_set_register_safe(dst, I64_VAL(AS_I64(vm_get_register_safe(src1)) / b));
                    break;
                }

                case OP_MOD_I64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (!IS_I64(vm_get_register_safe(src1)) ||
                        !IS_I64(vm_get_register_safe(src2))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be i64");
                    }

                    int64_t b = AS_I64(vm_get_register_safe(src2));
                    if (b == 0) {
                        VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Division by zero");
                    }

                    vm_set_register_safe(dst, I64_VAL(AS_I64(vm_get_register_safe(src1)) % b));
                    break;
                }

                case OP_ADD_U32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (!IS_U32(vm_get_register_safe(src1)) || !IS_U32(vm_get_register_safe(src2))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be u32");
                    }

                    uint32_t a = AS_U32(vm_get_register_safe(src1));
                    uint32_t b = AS_U32(vm_get_register_safe(src2));
                    
                    // Check for overflow: if a + b < a, then overflow occurred
                    if (UINT32_MAX - a < b) {
                        VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "u32 addition overflow");
                    }

                    vm_set_register_safe(dst, U32_VAL(a + b));
                    break;
                }

                case OP_SUB_U32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (!IS_U32(vm_get_register_safe(src1)) || !IS_U32(vm_get_register_safe(src2))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be u32");
                    }

                    uint32_t a = AS_U32(vm_get_register_safe(src1));
                    uint32_t b = AS_U32(vm_get_register_safe(src2));
                    
                    // Check for underflow: if a < b, then underflow would occur
                    if (a < b) {
                        VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "u32 subtraction underflow");
                    }

                    vm_set_register_safe(dst, U32_VAL(a - b));
                    break;
                }

                case OP_MUL_U32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (!IS_U32(vm_get_register_safe(src1)) || !IS_U32(vm_get_register_safe(src2))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be u32");
                    }

                    uint32_t a = AS_U32(vm_get_register_safe(src1));
                    uint32_t b = AS_U32(vm_get_register_safe(src2));
                    
                    // Check for multiplication overflow: if a != 0 && result / a != b
                    if (a != 0 && b > UINT32_MAX / a) {
                        VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "u32 multiplication overflow");
                    }

                    vm_set_register_safe(dst, U32_VAL(a * b));
                    break;
                }

                case OP_DIV_U32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (!IS_U32(vm_get_register_safe(src1)) || !IS_U32(vm_get_register_safe(src2))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be u32");
                    }

                    uint32_t b = AS_U32(vm_get_register_safe(src2));
                    if (b == 0) {
                        VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Division by zero");
                    }

                    vm_set_register_safe(dst, U32_VAL(AS_U32(vm_get_register_safe(src1)) / b));
                    break;
                }

                case OP_MOD_U32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (!IS_U32(vm_get_register_safe(src1)) || !IS_U32(vm_get_register_safe(src2))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be u32");
                    }

                    uint32_t b = AS_U32(vm_get_register_safe(src2));
                    if (b == 0) {
                        VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Division by zero");
                    }

                    vm_set_register_safe(dst, U32_VAL(AS_U32(vm_get_register_safe(src1)) % b));
                    break;
                }

                case OP_ADD_U64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (!IS_U64(vm_get_register_safe(src1)) || !IS_U64(vm_get_register_safe(src2))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be u64");
                    }

                    uint64_t a = AS_U64(vm_get_register_safe(src1));
                    uint64_t b = AS_U64(vm_get_register_safe(src2));
                    
                    // Check for overflow: if a + b < a, then overflow occurred
                    if (UINT64_MAX - a < b) {
                        VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "u64 addition overflow");
                    }

                    vm_set_register_safe(dst, U64_VAL(a + b));
                    break;
                }

                case OP_SUB_U64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (!IS_U64(vm_get_register_safe(src1)) || !IS_U64(vm_get_register_safe(src2))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be u64");
                    }

                    uint64_t a = AS_U64(vm_get_register_safe(src1));
                    uint64_t b = AS_U64(vm_get_register_safe(src2));
                    
                    // Check for underflow: if a < b, then underflow would occur
                    if (a < b) {
                        VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "u64 subtraction underflow");
                    }

                    vm_set_register_safe(dst, U64_VAL(a - b));
                    break;
                }

                case OP_MUL_U64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (!IS_U64(vm_get_register_safe(src1)) || !IS_U64(vm_get_register_safe(src2))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be u64");
                    }

                    uint64_t a = AS_U64(vm_get_register_safe(src1));
                    uint64_t b = AS_U64(vm_get_register_safe(src2));
                    
                    // Check for multiplication overflow: if a != 0 && result / a != b
                    if (a != 0 && b > UINT64_MAX / a) {
                        VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "u64 multiplication overflow");
                    }

                    vm_set_register_safe(dst, U64_VAL(a * b));
                    break;
                }

                case OP_DIV_U64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (!IS_U64(vm_get_register_safe(src1)) || !IS_U64(vm_get_register_safe(src2))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be u64");
                    }

                    uint64_t b = AS_U64(vm_get_register_safe(src2));
                    if (b == 0) {
                        VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Division by zero");
                    }

                    vm_set_register_safe(dst, U64_VAL(AS_U64(vm_get_register_safe(src1)) / b));
                    break;
                }

                case OP_MOD_U64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (!IS_U64(vm_get_register_safe(src1)) || !IS_U64(vm_get_register_safe(src2))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be u64");
                    }

                    uint64_t b = AS_U64(vm_get_register_safe(src2));
                    if (b == 0) {
                        VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Division by zero");
                    }

                    vm_set_register_safe(dst, U64_VAL(AS_U64(vm_get_register_safe(src1)) % b));
                    break;
                }

                case OP_BOOL_TO_I32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    READ_BYTE(); // Skip third operand (unused)
                    if (!IS_BOOL(vm_get_register_safe(src))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be bool");
                    }
                    vm_set_register_safe(dst, I32_VAL(AS_BOOL(vm_get_register_safe(src)) ? 1 : 0));
                    break;
                }

                case OP_BOOL_TO_I64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    READ_BYTE(); // Skip third operand (unused)
                    if (!IS_BOOL(vm_get_register_safe(src))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be bool");
                    }
                    vm_set_register_safe(dst, I64_VAL(AS_BOOL(vm_get_register_safe(src)) ? 1 : 0));
                    break;
                }

                case OP_BOOL_TO_U32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    READ_BYTE(); // Skip third operand (unused)
                    if (!IS_BOOL(vm_get_register_safe(src))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be bool");
                    }
                    vm_set_register_safe(dst, U32_VAL(AS_BOOL(vm_get_register_safe(src)) ? 1u : 0u));
                    break;
                }

                case OP_BOOL_TO_U64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    READ_BYTE(); // Skip third operand (unused)
                    if (!IS_BOOL(vm_get_register_safe(src))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be bool");
                    }
                    vm_set_register_safe(dst, U64_VAL(AS_BOOL(vm_get_register_safe(src)) ? 1ull : 0ull));
                    break;
                }

                case OP_BOOL_TO_F64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    READ_BYTE(); // Skip third operand (unused)
                    if (!IS_BOOL(vm_get_register_safe(src))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be bool");
                    }
                    vm_set_register_safe(dst, F64_VAL(AS_BOOL(vm_get_register_safe(src)) ? 1.0 : 0.0));
                    break;
                }

                case OP_I32_TO_I64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    READ_BYTE(); // Skip third operand (unused)
                    if (!IS_I32(vm_get_register_safe(src))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be i32");
                    }
                    vm_set_register_safe(dst, I64_VAL((int64_t)AS_I32(vm_get_register_safe(src))));
                    break;
                }

                case OP_I32_TO_U32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    READ_BYTE(); // Skip third operand (unused)
                    if (!IS_I32(vm_get_register_safe(src))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be i32");
                    }
                    vm_set_register_safe(dst, U32_VAL((uint32_t)AS_I32(vm_get_register_safe(src))));
                    break;
                }

                case OP_I32_TO_BOOL_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    READ_BYTE(); // Skip third operand (unused)
                    if (!IS_I32(vm_get_register_safe(src))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be i32");
                    }
                    // Convert i32 to bool: 0 -> false, non-zero -> true
                    vm_set_register_safe(dst, BOOL_VAL(AS_I32(vm_get_register_safe(src)) != 0));
                    break;
                }

                case OP_U32_TO_I32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    READ_BYTE(); // Skip third operand (unused)
                    if (!IS_U32(vm_get_register_safe(src))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be u32");
                    }
                    vm_set_register_safe(dst, I32_VAL((int32_t)AS_U32(vm_get_register_safe(src))));
                    break;
                }

                case OP_I64_TO_I32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    READ_BYTE(); // Skip third operand (unused)
                    if (!IS_I64(vm_get_register_safe(src))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be i64");
                    }
                    vm_set_register_safe(dst, I32_VAL((int32_t)AS_I64(vm_get_register_safe(src))));
                    break;
                }

                case OP_I64_TO_BOOL_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    READ_BYTE(); // Skip third operand (unused)
                    if (!IS_I64(vm_get_register_safe(src))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be i64");
                    }
                    vm_set_register_safe(dst, BOOL_VAL(AS_I64(vm_get_register_safe(src)) != 0));
                    break;
                }

                case OP_F64_TO_U32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    READ_BYTE(); // Skip third operand (unused)
                    if (!IS_F64(vm_get_register_safe(src))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be f64");
                    }
                    double val = AS_F64(vm_get_register_safe(src));
                    if (val < 0.0 || val > (double)UINT32_MAX) {
                        VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "f64 value out of u32 range");
                    }
                    vm_set_register_safe(dst, U32_VAL((uint32_t)val));
                    break;
                }

                case OP_U32_TO_F64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    READ_BYTE(); // Skip third operand (unused)
                    if (!IS_U32(vm_get_register_safe(src))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be u32");
                    }
                    vm_set_register_safe(dst, F64_VAL((double)AS_U32(vm_get_register_safe(src))));
                    break;
                }

                case OP_I32_TO_U64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    READ_BYTE(); // Skip third operand (unused)
                    if (!IS_I32(vm_get_register_safe(src))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be i32");
                    }
                    int32_t val = AS_I32(vm_get_register_safe(src));
                    if (val < 0) {
                        VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Cannot convert negative i32 to u64");
                    }
                    vm_set_register_safe(dst, U64_VAL((uint64_t)val));
                    break;
                }

                case OP_I64_TO_U64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    READ_BYTE(); // Skip third operand (unused)
                    if (!IS_I64(vm_get_register_safe(src))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be i64");
                    }
                    int64_t val = AS_I64(vm_get_register_safe(src));
                    if (val < 0) {
                        VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Cannot convert negative i64 to u64");
                    }
                    vm_set_register_safe(dst, U64_VAL((uint64_t)val));
                    break;
                }

                case OP_U64_TO_I32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    READ_BYTE(); // Skip third operand (unused)
                    if (!IS_U64(vm_get_register_safe(src))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be u64");
                    }
                    uint64_t val = AS_U64(vm_get_register_safe(src));
                    if (val > (uint64_t)INT32_MAX) {
                        VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "u64 value too large for i32");
                    }
                    vm_set_register_safe(dst, I32_VAL((int32_t)val));
                    break;
                }

                case OP_U64_TO_I64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    READ_BYTE(); // Skip third operand (unused)
                    if (!IS_U64(vm_get_register_safe(src))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be u64");
                    }
                    uint64_t val = AS_U64(vm_get_register_safe(src));
                    if (val > (uint64_t)INT64_MAX) {
                        VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "u64 value too large for i64");
                    }
                    vm_set_register_safe(dst, I64_VAL((int64_t)val));
                    break;
                }

                case OP_U32_TO_BOOL_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    READ_BYTE(); // Skip third operand (unused)
                    if (!IS_U32(vm_get_register_safe(src))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be u32");
                    }
                    vm_set_register_safe(dst, BOOL_VAL(AS_U32(vm_get_register_safe(src)) != 0));
                    break;
                }

                case OP_U32_TO_U64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    READ_BYTE(); // Skip third operand (unused)
                    if (!IS_U32(vm_get_register_safe(src))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be u32");
                    }
                    vm_set_register_safe(dst, U64_VAL((uint64_t)AS_U32(vm_get_register_safe(src))));
                    break;
                }

                case OP_U64_TO_U32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    READ_BYTE(); // Skip third operand (unused)
                    if (!IS_U64(vm_get_register_safe(src))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be u64");
                    }
                    uint64_t val = AS_U64(vm_get_register_safe(src));
                    if (val > (uint64_t)UINT32_MAX) {
                        VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "u64 value too large for u32");
                    }
                    vm_set_register_safe(dst, U32_VAL((uint32_t)val));
                    break;
                }

                case OP_F64_TO_U64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    READ_BYTE(); // Skip third operand (unused)
                    if (!IS_F64(vm_get_register_safe(src))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be f64");
                    }
                    double val = AS_F64(vm_get_register_safe(src));
                    if (val < 0.0 || val > (double)UINT64_MAX) {
                        VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "f64 value out of u64 range");
                    }
                    vm_set_register_safe(dst, U64_VAL((uint64_t)val));
                    break;
                }

                case OP_U64_TO_F64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    READ_BYTE(); // Skip third operand (unused)
                    if (!IS_U64(vm_get_register_safe(src))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be u64");
                    }
                    vm_set_register_safe(dst, F64_VAL((double)AS_U64(vm_get_register_safe(src))));
                    break;
                }

                case OP_U64_TO_BOOL_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    READ_BYTE(); // Skip third operand (unused)
                    if (!IS_U64(vm_get_register_safe(src))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be u64");
                    }
                    vm_set_register_safe(dst, BOOL_VAL(AS_U64(vm_get_register_safe(src)) != 0));
                    break;
                }

                case OP_F64_TO_BOOL_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    READ_BYTE(); // Skip third operand (unused)
                    if (!IS_F64(vm_get_register_safe(src))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be f64");
                    }
                    vm_set_register_safe(dst, BOOL_VAL(AS_F64(vm_get_register_safe(src)) != 0.0));
                    break;
                }

                // F64 Arithmetic Operations
                case OP_ADD_F64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    if (!IS_F64(vm_get_register_safe(src1)) || !IS_F64(vm_get_register_safe(src2))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be f64");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    vm_set_register_safe(dst, F64_VAL(AS_F64(vm_get_register_safe(src1)) + AS_F64(vm_get_register_safe(src2))));
                    break;
                }

                case OP_SUB_F64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    if (!IS_F64(vm_get_register_safe(src1)) || !IS_F64(vm_get_register_safe(src2))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be f64");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    vm_set_register_safe(dst, F64_VAL(AS_F64(vm_get_register_safe(src1)) - AS_F64(vm_get_register_safe(src2))));
                    break;
                }

                case OP_MUL_F64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    if (!IS_F64(vm_get_register_safe(src1)) || !IS_F64(vm_get_register_safe(src2))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be f64");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    vm_set_register_safe(dst, F64_VAL(AS_F64(vm_get_register_safe(src1)) * AS_F64(vm_get_register_safe(src2))));
                    break;
                }

                case OP_DIV_F64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    if (!IS_F64(vm_get_register_safe(src1)) || !IS_F64(vm_get_register_safe(src2))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be f64");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    double a = AS_F64(vm_get_register_safe(src1));
                    double b = AS_F64(vm_get_register_safe(src2));
                    
                    // IEEE 754 compliant: division by zero produces infinity, not error
                    double result = a / b;
                    
                    // The result may be infinity, -infinity, or NaN
                    // These are valid f64 values according to IEEE 754
                    vm_set_register_safe(dst, F64_VAL(result));
                    break;
                }

                case OP_MOD_F64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    if (!IS_F64(vm_get_register_safe(src1)) || !IS_F64(vm_get_register_safe(src2))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be f64");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    double a = AS_F64(vm_get_register_safe(src1));
                    double b = AS_F64(vm_get_register_safe(src2));
                    
                    // IEEE 754 compliant: use fmod for floating point modulo
                    double result = fmod(a, b);
                    
                    // The result may be infinity, -infinity, or NaN
                    // These are valid f64 values according to IEEE 754
                    vm_set_register_safe(dst, F64_VAL(result));
                    break;
                }

                // Bitwise Operations
                case OP_AND_I32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    if (!IS_I32(vm_get_register_safe(src1)) || !IS_I32(vm_get_register_safe(src2))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be i32");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    vm_set_register_safe(dst, I32_VAL(AS_I32(vm_get_register_safe(src1)) & AS_I32(vm_get_register_safe(src2))));
                    break;
                }

                case OP_OR_I32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    if (!IS_I32(vm_get_register_safe(src1)) || !IS_I32(vm_get_register_safe(src2))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be i32");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    vm_set_register_safe(dst, I32_VAL(AS_I32(vm_get_register_safe(src1)) | AS_I32(vm_get_register_safe(src2))));
                    break;
                }

                case OP_XOR_I32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    if (!IS_I32(vm_get_register_safe(src1)) || !IS_I32(vm_get_register_safe(src2))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be i32");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    vm_set_register_safe(dst, I32_VAL(AS_I32(vm_get_register_safe(src1)) ^ AS_I32(vm_get_register_safe(src2))));
                    break;
                }

                case OP_NOT_I32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    if (!IS_I32(vm_get_register_safe(src))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operand must be i32");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    vm_set_register_safe(dst, I32_VAL(~AS_I32(vm_get_register_safe(src))));
                    break;
                }

                case OP_SHL_I32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    if (!IS_I32(vm_get_register_safe(src1)) || !IS_I32(vm_get_register_safe(src2))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be i32");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    vm_set_register_safe(dst, I32_VAL(AS_I32(vm_get_register_safe(src1)) << AS_I32(vm_get_register_safe(src2))));
                    break;
                }

                case OP_SHR_I32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    if (!IS_I32(vm_get_register_safe(src1)) || !IS_I32(vm_get_register_safe(src2))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be i32");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    vm_set_register_safe(dst, I32_VAL(AS_I32(vm_get_register_safe(src1)) >> AS_I32(vm_get_register_safe(src2))));
                    break;
                }

                // F64 Comparison Operations
                case OP_LT_F64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    CMP_F64_LT(dst, src1, src2);
                    break;
                }

                case OP_LE_F64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    CMP_F64_LE(dst, src1, src2);
                    break;
                }

                case OP_GT_F64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    CMP_F64_GT(dst, src1, src2);
                    break;
                }

                case OP_GE_F64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    CMP_F64_GE(dst, src1, src2);
                    break;
                }

                // F64 Type Conversion Operations
                case OP_I32_TO_F64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    READ_BYTE(); // Skip third operand (unused)
                    if (!IS_I32(vm_get_register_safe(src))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be i32");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    vm_set_register_safe(dst, F64_VAL((double)AS_I32(vm_get_register_safe(src))));
                    break;
                }

                case OP_I64_TO_F64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    READ_BYTE(); // Skip third operand (unused)
                    if (!IS_I64(vm_get_register_safe(src))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be i64");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    vm_set_register_safe(dst, F64_VAL((double)AS_I64(vm_get_register_safe(src))));
                    break;
                }

                case OP_F64_TO_I32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    READ_BYTE(); // Skip third operand (unused)
                    if (!IS_F64(vm_get_register_safe(src))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be f64");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    vm_set_register_safe(dst, I32_VAL((int32_t)AS_F64(vm_get_register_safe(src))));
                    break;
                }

                case OP_F64_TO_I64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    READ_BYTE(); // Skip third operand (unused)
                    if (!IS_F64(vm_get_register_safe(src))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be f64");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    vm_set_register_safe(dst, I64_VAL((int64_t)AS_F64(vm_get_register_safe(src))));
                    break;
                }

                // Comparison operations
                case OP_LT_I32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    CMP_I32_LT(dst, src1, src2);
                    break;
                }

                case OP_LE_I32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    CMP_I32_LE(dst, src1, src2);
                    break;
                }

                case OP_GT_I32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    CMP_I32_GT(dst, src1, src2);
                    break;
                }

                case OP_GE_I32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    CMP_I32_GE(dst, src1, src2);
                    break;
                }

                // I64 comparison operations
                case OP_LT_I64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    CMP_I64_LT(dst, src1, src2);
                    break;
                }

                case OP_LE_I64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    CMP_I64_LE(dst, src1, src2);
                    break;
                }

                case OP_GT_I64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    CMP_I64_GT(dst, src1, src2);
                    break;
                }

                case OP_GE_I64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    CMP_I64_GE(dst, src1, src2);
                    break;
                }

                case OP_LT_U32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    CMP_U32_LT(dst, src1, src2);
                    break;
                }

                case OP_LE_U32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    CMP_U32_LE(dst, src1, src2);
                    break;
                }

                case OP_GT_U32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    CMP_U32_GT(dst, src1, src2);
                    break;
                }

                case OP_GE_U32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    CMP_U32_GE(dst, src1, src2);
                    break;
                }

                case OP_LT_U64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    CMP_U64_LT(dst, src1, src2);
                    break;
                }

                case OP_LE_U64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    CMP_U64_LE(dst, src1, src2);
                    break;
                }

                case OP_GT_U64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    CMP_U64_GT(dst, src1, src2);
                    break;
                }

                case OP_GE_U64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    CMP_U64_GE(dst, src1, src2);
                    break;
                }

                case OP_EQ_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    CMP_EQ(dst, src1, src2);
                    break;
                }

                case OP_NE_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    CMP_NE(dst, src1, src2);
                    break;
                }

                case OP_AND_BOOL_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    // Convert operands to boolean using truthiness rules
                    bool left_bool = false;
                    bool right_bool = false;
                    
                    // Convert left operand to boolean
                    if (IS_BOOL(vm_get_register_safe(src1))) {
                        left_bool = AS_BOOL(vm_get_register_safe(src1));
                    } else if (IS_I32(vm_get_register_safe(src1))) {
                        left_bool = AS_I32(vm_get_register_safe(src1)) != 0;
                    } else if (IS_I64(vm_get_register_safe(src1))) {
                        left_bool = AS_I64(vm_get_register_safe(src1)) != 0;
                    } else if (IS_U32(vm_get_register_safe(src1))) {
                        left_bool = AS_U32(vm_get_register_safe(src1)) != 0;
                    } else if (IS_U64(vm_get_register_safe(src1))) {
                        left_bool = AS_U64(vm_get_register_safe(src1)) != 0;
                    } else if (IS_F64(vm_get_register_safe(src1))) {
                        left_bool = AS_F64(vm_get_register_safe(src1)) != 0.0;
                    } else if (IS_BOOL(vm_get_register_safe(src1))) {
                        left_bool = false;
                    } else {
                        left_bool = true; // Objects, strings, etc. are truthy
                    }
                    
                    // Convert right operand to boolean
                    if (IS_BOOL(vm_get_register_safe(src2))) {
                        right_bool = AS_BOOL(vm_get_register_safe(src2));
                    } else if (IS_I32(vm_get_register_safe(src2))) {
                        right_bool = AS_I32(vm_get_register_safe(src2)) != 0;
                    } else if (IS_I64(vm_get_register_safe(src2))) {
                        right_bool = AS_I64(vm_get_register_safe(src2)) != 0;
                    } else if (IS_U32(vm_get_register_safe(src2))) {
                        right_bool = AS_U32(vm_get_register_safe(src2)) != 0;
                    } else if (IS_U64(vm_get_register_safe(src2))) {
                        right_bool = AS_U64(vm_get_register_safe(src2)) != 0;
                    } else if (IS_F64(vm_get_register_safe(src2))) {
                        right_bool = AS_F64(vm_get_register_safe(src2)) != 0.0;
                    } else if (IS_BOOL(vm_get_register_safe(src2))) {
                        right_bool = false;
                    } else {
                        right_bool = true; // Objects, strings, etc. are truthy
                    }

                    vm_set_register_safe(dst, BOOL_VAL(left_bool && right_bool));
                    break;
                }

                case OP_OR_BOOL_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    // Convert operands to boolean using truthiness rules
                    bool left_bool = false;
                    bool right_bool = false;
                    
                    // Convert left operand to boolean
                    if (IS_BOOL(vm_get_register_safe(src1))) {
                        left_bool = AS_BOOL(vm_get_register_safe(src1));
                    } else if (IS_I32(vm_get_register_safe(src1))) {
                        left_bool = AS_I32(vm_get_register_safe(src1)) != 0;
                    } else if (IS_I64(vm_get_register_safe(src1))) {
                        left_bool = AS_I64(vm_get_register_safe(src1)) != 0;
                    } else if (IS_U32(vm_get_register_safe(src1))) {
                        left_bool = AS_U32(vm_get_register_safe(src1)) != 0;
                    } else if (IS_U64(vm_get_register_safe(src1))) {
                        left_bool = AS_U64(vm_get_register_safe(src1)) != 0;
                    } else if (IS_F64(vm_get_register_safe(src1))) {
                        left_bool = AS_F64(vm_get_register_safe(src1)) != 0.0;
                    } else if (IS_BOOL(vm_get_register_safe(src1))) {
                        left_bool = false;
                    } else {
                        left_bool = true; // Objects, strings, etc. are truthy
                    }
                    
                    // Convert right operand to boolean
                    if (IS_BOOL(vm_get_register_safe(src2))) {
                        right_bool = AS_BOOL(vm_get_register_safe(src2));
                    } else if (IS_I32(vm_get_register_safe(src2))) {
                        right_bool = AS_I32(vm_get_register_safe(src2)) != 0;
                    } else if (IS_I64(vm_get_register_safe(src2))) {
                        right_bool = AS_I64(vm_get_register_safe(src2)) != 0;
                    } else if (IS_U32(vm_get_register_safe(src2))) {
                        right_bool = AS_U32(vm_get_register_safe(src2)) != 0;
                    } else if (IS_U64(vm_get_register_safe(src2))) {
                        right_bool = AS_U64(vm_get_register_safe(src2)) != 0;
                    } else if (IS_F64(vm_get_register_safe(src2))) {
                        right_bool = AS_F64(vm_get_register_safe(src2)) != 0.0;
                    } else if (IS_BOOL(vm_get_register_safe(src2))) {
                        right_bool = false;
                    } else {
                        right_bool = true; // Objects, strings, etc. are truthy
                    }

                    vm_set_register_safe(dst, BOOL_VAL(left_bool || right_bool));
                    break;
                }

                case OP_NOT_BOOL_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();

                    // Convert operand to boolean using truthiness rules, then negate
                    bool src_bool = false;
                    if (IS_BOOL(vm_get_register_safe(src))) {
                        src_bool = AS_BOOL(vm_get_register_safe(src));
                    } else if (IS_I32(vm_get_register_safe(src))) {
                        src_bool = AS_I32(vm_get_register_safe(src)) != 0;
                    } else if (IS_I64(vm_get_register_safe(src))) {
                        src_bool = AS_I64(vm_get_register_safe(src)) != 0;
                    } else if (IS_U32(vm_get_register_safe(src))) {
                        src_bool = AS_U32(vm_get_register_safe(src)) != 0;
                    } else if (IS_U64(vm_get_register_safe(src))) {
                        src_bool = AS_U64(vm_get_register_safe(src)) != 0;
                    } else if (IS_F64(vm_get_register_safe(src))) {
                        src_bool = AS_F64(vm_get_register_safe(src)) != 0.0;
                    } else {
                        src_bool = true; // Objects, strings, etc. are truthy
                    }

                    vm_set_register_safe(dst, BOOL_VAL(!src_bool));
                    break;
                }

                case OP_CONCAT_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (!IS_STRING(vm_get_register_safe(src1)) || !IS_STRING(vm_get_register_safe(src2))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be string");
                    }

                    ObjString* a = AS_STRING(vm_get_register_safe(src1));
                    ObjString* b = AS_STRING(vm_get_register_safe(src2));
                    int newLen = a->length + b->length;
                    char* buf = malloc(newLen + 1);
                    memcpy(buf, a->chars, a->length);
                    memcpy(buf + a->length, b->chars, b->length);
                    buf[newLen] = '\0';
                    ObjString* res = allocateString(buf, newLen);
                    free(buf);
                    vm_set_register_safe(dst, STRING_VAL(res));
                    break;
                }

                case OP_MAKE_ARRAY_R: {
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
                    break;
                }

                case OP_ENUM_NEW_R: {
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
                    break;
                }

                case OP_ENUM_TAG_EQ_R: {
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
                    break;
                }

                case OP_ENUM_PAYLOAD_R: {
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
                    break;
                }

                case OP_ARRAY_GET_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t array_reg = READ_BYTE();
                    uint8_t index_reg = READ_BYTE();

                    Value array_value = vm_get_register_safe(array_reg);
                    if (!IS_ARRAY(array_value)) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Value is not an array");
                    }

                    int index;
                    if (!value_to_index(vm_get_register_safe(index_reg), &index)) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Array index must be a non-negative integer");
                    }

                    Value element;
                    if (!arrayGet(AS_ARRAY(array_value), index, &element)) {
                        VM_ERROR_RETURN(ERROR_INDEX, CURRENT_LOCATION(), "Array index out of bounds");
                    }

                    vm_set_register_safe(dst, element);
                    break;
                }

                case OP_ARRAY_SET_R: {
                    uint8_t array_reg = READ_BYTE();
                    uint8_t index_reg = READ_BYTE();
                    uint8_t value_reg = READ_BYTE();

                    Value array_value = vm_get_register_safe(array_reg);
                    if (!IS_ARRAY(array_value)) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Value is not an array");
                    }

                    int index;
                    if (!value_to_index(vm_get_register_safe(index_reg), &index)) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Array index must be a non-negative integer");
                    }

                    Value value = vm_get_register_safe(value_reg);
                    if (!arraySet(AS_ARRAY(array_value), index, value)) {
                        VM_ERROR_RETURN(ERROR_INDEX, CURRENT_LOCATION(), "Array index out of bounds");
                    }
                    break;
                }

                case OP_ARRAY_LEN_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t array_reg = READ_BYTE();

                    Value array_value = vm_get_register_safe(array_reg);
                    if (!IS_ARRAY(array_value)) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Value is not an array");
                    }

                    vm_set_register_safe(dst, I32_VAL(AS_ARRAY(array_value)->length));
                    break;
                }

                case OP_ARRAY_PUSH_R: {
                    uint8_t array_reg = READ_BYTE();
                    uint8_t value_reg = READ_BYTE();

                    Value array_value = vm_get_register_safe(array_reg);
                    if (!builtin_array_push(array_value, vm_get_register_safe(value_reg))) {
                        if (!IS_ARRAY(array_value)) {
                            VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Value is not an array");
                        }
                        VM_ERROR_RETURN(ERROR_RUNTIME, CURRENT_LOCATION(), "Failed to push value onto array");
                    }
                    break;
                }

                case OP_ARRAY_POP_R: {
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
                    break;
                }

                case OP_ARRAY_SLICE_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t array_reg = READ_BYTE();
                    uint8_t start_reg = READ_BYTE();
                    uint8_t end_reg = READ_BYTE();

                    Value array_value = vm_get_register_safe(array_reg);
                    if (!IS_ARRAY(array_value)) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Value is not an array");
                    }

                    int start_index;
                    if (!value_to_index(vm_get_register_safe(start_reg), &start_index)) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Array slice start must be a non-negative integer");
                    }

                    int end_index;
                    if (!value_to_index(vm_get_register_safe(end_reg), &end_index)) {
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
                    break;
                }

                case OP_GET_ITER_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    Value iterable = vm_get_register_safe(src);
                    vm_typed_iterator_invalidate(dst);

                    if (IS_RANGE_ITERATOR(iterable)) {
                        vm_set_register_safe(dst, iterable);
                    } else if (!vm.config.force_boxed_iterators &&
                               (IS_I32(iterable) || IS_I64(iterable) || IS_U32(iterable) || IS_U64(iterable))) {
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

                        vm_set_register_safe(dst, I64_VAL(0));
                        vm_typed_iterator_bind_range(dst, 0, count);
                        vm_trace_loop_event(LOOP_TRACE_ITER_SAVED_ALLOCATIONS);
                    } else if (IS_I32(iterable) || IS_I64(iterable) || IS_U32(iterable) || IS_U64(iterable)) {
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

                        ObjRangeIterator* iterator = allocateRangeIterator(0, count);
                        if (!iterator) {
                            VM_ERROR_RETURN(ERROR_RUNTIME, CURRENT_LOCATION(), "Failed to allocate range iterator");
                        }

                        Value iterator_value = {.type = VAL_RANGE_ITERATOR, .as.obj = (Obj*)iterator};
                        vm_set_register_safe(dst, iterator_value);
                        vm_trace_loop_event(LOOP_TRACE_TYPED_MISS);
                        vm_trace_loop_event(LOOP_TRACE_ITER_FALLBACK);
                    } else if (IS_ARRAY(iterable)) {
                        ObjArray* array = AS_ARRAY(iterable);
                        if (!vm.config.force_boxed_iterators) {
                            vm_set_register_safe(dst, iterable);
                            if (vm_typed_iterator_bind_array(dst, array)) {
                                vm_trace_loop_event(LOOP_TRACE_ITER_SAVED_ALLOCATIONS);
                                DISPATCH();
                            }
                        }

                        ObjArrayIterator* iterator = allocateArrayIterator(array);
                        if (!iterator) {
                            VM_ERROR_RETURN(ERROR_RUNTIME, CURRENT_LOCATION(), "Failed to allocate array iterator");
                        }
                        Value iterator_value = {.type = VAL_ARRAY_ITERATOR, .as.obj = (Obj*)iterator};
                        vm_set_register_safe(dst, iterator_value);
                        vm_trace_loop_event(LOOP_TRACE_TYPED_MISS);
                        vm_trace_loop_event(LOOP_TRACE_ITER_FALLBACK);
                    } else if (IS_ARRAY_ITERATOR(iterable)) {
                        vm_set_register_safe(dst, iterable);
                    } else {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Value not iterable");
                    }
                    break;
                }

                case OP_ITER_NEXT_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t iter_reg = READ_BYTE();
                    uint8_t has_reg = READ_BYTE();
                    Value iterator_value = vm_get_register_safe(iter_reg);

                    Value typed_next;
                    bool typed_iter_was_active = vm_typed_iterator_is_active(iter_reg);
                    if (typed_iter_was_active && vm_typed_iterator_next(iter_reg, &typed_next)) {
                        vm_set_register_safe(dst, typed_next);
                        vm_set_register_safe(has_reg, BOOL_VAL(true));
                    } else if (typed_iter_was_active && !vm_typed_iterator_is_active(iter_reg)) {
                        vm_set_register_safe(has_reg, BOOL_VAL(false));
                    } else if (IS_RANGE_ITERATOR(iterator_value)) {
                        ObjRangeIterator* it = AS_RANGE_ITERATOR(iterator_value);
                        if (it->current >= it->end) {
                            vm_set_register_safe(has_reg, BOOL_VAL(false));
                        } else {
                            vm_set_register_safe(dst, I64_VAL(it->current));
                            it->current++;
                            vm_set_register_safe(has_reg, BOOL_VAL(true));
                        }
                    } else if (IS_ARRAY_ITERATOR(iterator_value)) {
                        ObjArrayIterator* it = AS_ARRAY_ITERATOR(iterator_value);
                        ObjArray* array = it ? it->array : NULL;
                        if (!array || it->index >= array->length) {
                            vm_set_register_safe(has_reg, BOOL_VAL(false));
                        } else {
                            vm_set_register_safe(dst, array->elements[it->index]);
                            it->index++;
                            vm_set_register_safe(has_reg, BOOL_VAL(true));
                        }
                    } else {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Invalid iterator");
                    }
                    break;
                }

                case OP_TO_STRING_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
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
                        break;
                    } else {
                        snprintf(buffer, sizeof(buffer), "nil");
                    }
                    
                    ObjString* result = allocateString(buffer, (int)strlen(buffer));
                    vm_set_register_safe(dst, STRING_VAL(result));
                    break;
                }

                // Control flow
                case OP_TRY_BEGIN: {
                    uint8_t reg = READ_BYTE();
                    uint16_t offset = READ_SHORT();
                    if (vm.tryFrameCount >= TRY_MAX) {
                        VM_ERROR_RETURN(ERROR_RUNTIME, CURRENT_LOCATION(), "Too many nested try blocks");
                    }
                    TryFrame* frame = &vm.tryFrames[vm.tryFrameCount++];
                    frame->handler = vm.ip + offset;
                    frame->catchRegister = (reg == 0xFF) ? TRY_CATCH_REGISTER_NONE : (uint16_t)reg;
                    frame->stackDepth = vm.frameCount;
                    break;
                }

                case OP_TRY_END: {
                    if (vm.tryFrameCount <= 0) {
                        VM_ERROR_RETURN(ERROR_RUNTIME, CURRENT_LOCATION(), "TRY_END without matching TRY_BEGIN");
                    }
                    vm.tryFrameCount--;
                    break;
                }

                case OP_THROW: {
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
                    goto handle_runtime_error;
                }

                case OP_JUMP: {
                    uint16_t offset = READ_SHORT();
                    if (!CF_JUMP(offset)) {
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    break;
                }

                case OP_JUMP_IF_NOT_R: {
                    uint8_t reg = READ_BYTE();
                    uint16_t offset = READ_SHORT();
                    if (!CF_JUMP_IF_NOT(reg, offset)) {
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    break;
                }

                case OP_JUMP_IF_NOT_I32_TYPED: {
                    uint8_t left = READ_BYTE();
                    uint8_t right = READ_BYTE();
                    uint16_t offset = READ_SHORT();
                    if (!CF_JUMP_IF_NOT_I32_TYPED(left, right, offset)) {
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    break;
                }

                case OP_LOOP: {
                    uint16_t offset = READ_SHORT();
                    if (!CF_LOOP(offset)) {
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    break;
                }

                // I/O operations
                case OP_PRINT_MULTI_R: {
                    handle_print_multi();
                    break;
                }

                case OP_PRINT_R: {
                    handle_print();
                    break;
                }

                case OP_PRINT_NO_NL_R: {
                    handle_print_no_nl();
                    break;
                }

                // Function operations
                case OP_CALL_R: {
                    DEBUG_VM_PRINT("OP_CALL_R executed");
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
                            break;
                        }
                        
                        // Check if we have room for another call frame
                        if (vm.frameCount >= FRAMES_MAX) {
                            vm_set_register_safe(resultReg, BOOL_VAL(false));
                            break;
                        }
                        
                        // Create new call frame with closure context
                        CallFrame* frame = &vm.frames[vm.frameCount++];
                        frame->returnAddress = vm.ip;
                        frame->previousChunk = vm.chunk;
                        frame->baseRegister = resultReg;

                        // Determine parameter base register
                        uint8_t paramBase = 256 - function->arity;
                        if (paramBase < 1) paramBase = 1;
                        frame->parameterBaseRegister = (uint16_t)paramBase;

                        // Save frame and temp registers
                        const int temp_reg_start = 192; // MP_TEMP_REG_START
                        const int temp_reg_count = 48;  // R192-R239
                        frame->savedRegisterCount = 64 + temp_reg_count;
                        for (int i = 0; i < 64; i++) {
                            frame->savedRegisters[i] = vm_get_register_safe(FRAME_REG_START + i);
                        }
                        for (int i = 0; i < temp_reg_count; i++) {
                            frame->savedRegisters[64 + i] = vm_get_register_safe(temp_reg_start + i);
                        }

                        // Set up closure context (closure in register 0)
                        vm_set_register_safe(0, funcValue);  // Store closure in register 0 for upvalue access

                        // Copy arguments to parameter registers
                        for (int i = 0; i < argCount; i++) {
                            vm_set_register_safe(paramBase + i, vm_get_register_safe(firstArgReg + i));
                        }

                        // Switch to function's bytecode
                        vm.chunk = function->chunk;
                        vm.ip = function->chunk->code;

                        break;
                    } else if (IS_I32(funcValue)) {
                        int functionIndex = AS_I32(funcValue);
                        
                        if (functionIndex < 0 || functionIndex >= vm.functionCount) {
                            vm_set_register_safe(resultReg, BOOL_VAL(false));
                            break;
                        }
                        
                        Function* function = &vm.functions[functionIndex];
                        
                        // Check arity
                        if (argCount != function->arity) {
                            vm_set_register_safe(resultReg, BOOL_VAL(false));
                            break;
                        }
                        
                        // Check if we have room for another call frame
                        if (vm.frameCount >= FRAMES_MAX) {
                            vm_set_register_safe(resultReg, BOOL_VAL(false));
                            break;
                        }
                        
                        // Create new call frame with optimized parameter allocation
                        CallFrame* frame = &vm.frames[vm.frameCount++];
                        frame->returnAddress = vm.ip;
                        frame->previousChunk = vm.chunk;
                        frame->baseRegister = resultReg;
                        frame->register_count = argCount;
                        frame->functionIndex = functionIndex;
                        
                        // Simple parameter base calculation to match compiler
                        uint8_t paramBase = 256 - function->arity;
                        if (paramBase < 1) paramBase = 1;
                        frame->parameterBaseRegister = (uint16_t)paramBase;
                        
                        // Save all frame registers (R256-R319) AND temp registers (R192-R239) to prevent corruption during recursive calls
                        // CRITICAL FIX: Also save temp registers to fix fibonacci/recursive function register corruption
                        int temp_reg_start = 192;  // MP_TEMP_REG_START
                        int temp_reg_count = 48;   // R192-R239 (48 registers)
                        frame->savedRegisterCount = 64 + temp_reg_count;  // FRAME_REGISTERS + TEMP_REGISTERS
                        
                        // Save frame registers (R256-R319)
                        for (int i = 0; i < 64; i++) {
                            frame->savedRegisters[i] = vm_get_register_safe(FRAME_REG_START + i);
                            // Debug: Print saved values for first few registers
                            if (i < 8) {
                                DEBUG_VM_PRINT("SAVE FRAME R%d (type=%d)", FRAME_REG_START + i, vm_get_register_safe(FRAME_REG_START + i).type);
                                if (vm_get_register_safe(FRAME_REG_START + i).type == VALUE_I32) {
                                    DEBUG_VM_PRINT("  value = %d\n", AS_I32(vm_get_register_safe(FRAME_REG_START + i)));
                                }
                            }
                        }
                        
                        // Save temp registers (R192-R239) - CRITICAL FIX for recursive function register corruption
                        for (int i = 0; i < temp_reg_count; i++) {
                            frame->savedRegisters[64 + i] = vm_get_register_safe(temp_reg_start + i);
                            // Debug: Print saved temp register values for first few
                            if (i < 8) {
                                DEBUG_VM_PRINT("SAVE TEMP R%d (type=%d)", temp_reg_start + i, vm_get_register_safe(temp_reg_start + i).type);
                                if (vm_get_register_safe(temp_reg_start + i).type == VALUE_I32) {
                                    DEBUG_VM_PRINT("  value = %d\n", AS_I32(vm_get_register_safe(temp_reg_start + i)));
                                }
                            }
                        }
                        
                        // Copy arguments to parameter registers
                        for (int i = 0; i < argCount; i++) {
                            vm_set_register_safe(paramBase + i, vm_get_register_safe(firstArgReg + i));
                        }
                        
                        // Switch to function's chunk
                        vm.chunk = function->chunk;
                        vm.ip = function->chunk->code + function->start;
                        
                    } else if (IS_FUNCTION(funcValue)) {
                        // Calling a function object directly
                        ObjFunction* objFunction = AS_FUNCTION(funcValue);
                        
                        // Check arity
                        if (argCount != objFunction->arity) {
                            vm_set_register_safe(resultReg, BOOL_VAL(false));
                            break;
                        }
                        
                        // Check if we have room for another call frame
                        if (vm.frameCount >= FRAMES_MAX) {
                            vm_set_register_safe(resultReg, BOOL_VAL(false));
                            break;
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
                            vm_set_register_safe(paramBase + i, vm_get_register_safe(firstArgReg + i));
                        }
                        
                        // Switch to function's bytecode
                        vm.chunk = objFunction->chunk;
                        vm.ip = objFunction->chunk->code;
                        
                    } else {
                        vm_set_register_safe(resultReg, BOOL_VAL(false));
                    }
                    break;
                }
                case OP_TAIL_CALL_R: {
                    uint8_t funcReg = READ_BYTE();
                    uint8_t firstArgReg = READ_BYTE();
                    uint8_t argCount = READ_BYTE();
                    uint8_t resultReg = READ_BYTE();
                    
                    Value funcValue = vm_get_register_safe(funcReg);
                    
                    if (IS_I32(funcValue)) {
                        int functionIndex = AS_I32(funcValue);
                        
                        if (functionIndex < 0 || functionIndex >= vm.functionCount) {
                            vm_set_register_safe(resultReg, BOOL_VAL(false));
                            break;
                        }
                        
                        Function* function = &vm.functions[functionIndex];
                        
                        // Check arity
                        if (argCount != function->arity) {
                            vm_set_register_safe(resultReg, BOOL_VAL(false));
                            break;
                        }
                        
                        // For tail calls, we reuse the current frame instead of creating a new one
                        // This prevents stack growth in recursive calls
                        
                        // Copy arguments to function's frame registers
                        // We need to be careful about overlapping registers
                        Value tempArgs[256];
                        for (int i = 0; i < argCount; i++) {
                            tempArgs[i] = vm_get_register_safe(firstArgReg + i);
                        }
                        
                        // Clear the current frame's parameter registers first, then set new ones
                        for (int i = 0; i < argCount; i++) {
                            uint16_t frame_reg_id = FRAME_REG_START + i;
                            set_register(&vm.register_file, frame_reg_id, tempArgs[i]);
                            vm_set_register_safe(200 + i, tempArgs[i]);  // Use safe parameter register range
                        }
                        
                        // Switch to function's chunk - reuse current frame
                        vm.chunk = function->chunk;
                        vm.ip = function->chunk->code + function->start;
                        
                    } else {
                        vm_set_register_safe(resultReg, BOOL_VAL(false));
                    }
                    break;
                }
                case OP_RETURN_R: {
                    uint8_t reg = READ_BYTE();
                    Value returnValue = vm_get_register_safe(reg);

                    if (vm.frameCount > 0) {
                        CallFrame* frame = &vm.frames[--vm.frameCount];
                        
                        const int temp_reg_start = 192;
                        const int temp_reg_count = 48;

                        if (frame->savedRegisterCount == 64 + temp_reg_count) {
                            for (int i = 0; i < 64; i++) {
                                vm_set_register_safe(FRAME_REG_START + i, frame->savedRegisters[i]);
                            }
                            for (int i = 0; i < temp_reg_count; i++) {
                                vm_set_register_safe(temp_reg_start + i, frame->savedRegisters[64 + i]);
                            }
                        } else if (frame->savedRegisterCount == 31) {
                            for (int i = 0; i < 15; i++) {
                                vm_set_register_safe(65 + i, frame->savedRegisters[i]);
                            }
                            for (int i = 0; i < 16; i++) {
                                vm_set_register_safe(240 + i, frame->savedRegisters[15 + i]);
                            }
                        } else if (frame->savedRegisterCount == 31 + temp_reg_count) {
                            for (int i = 0; i < 15; i++) {
                                vm_set_register_safe(65 + i, frame->savedRegisters[i]);
                            }
                            for (int i = 0; i < 16; i++) {
                                vm_set_register_safe(240 + i, frame->savedRegisters[15 + i]);
                            }
                            for (int i = 0; i < temp_reg_count; i++) {
                                vm_set_register_safe(temp_reg_start + i, frame->savedRegisters[31 + i]);
                            }
                        } else {
                            for (int i = 0; i < frame->savedRegisterCount; i++) {
                                vm_set_register_safe(frame->savedRegisterStart + i, frame->savedRegisters[i]);
                            }
                        }
                        
                        vm.chunk = frame->previousChunk;
                        vm.ip = frame->returnAddress;

                        // Store return value in the result register
                        vm_set_register_safe(frame->baseRegister, returnValue);
                    } else {
                        // Top-level return
                        vm.lastExecutionTime = get_time_vm() - start_time;
                        RETURN(INTERPRET_OK);
                    }
                    break;
                }

                case OP_RETURN_VOID: {
                    if (vm.frameCount > 0) {
                        CallFrame* frame = &vm.frames[--vm.frameCount];
                        
                        const int temp_reg_start = 192;
                        const int temp_reg_count = 48;

                        if (frame->savedRegisterCount == 64 + temp_reg_count) {
                            for (int i = 0; i < 64; i++) {
                                vm_set_register_safe(FRAME_REG_START + i, frame->savedRegisters[i]);
                            }
                            for (int i = 0; i < temp_reg_count; i++) {
                                vm_set_register_safe(temp_reg_start + i, frame->savedRegisters[64 + i]);
                            }
                        } else if (frame->savedRegisterCount == 31) {
                            for (int i = 0; i < 15; i++) {
                                vm_set_register_safe(65 + i, frame->savedRegisters[i]);
                            }
                            for (int i = 0; i < 16; i++) {
                                vm_set_register_safe(240 + i, frame->savedRegisters[15 + i]);
                            }
                        } else if (frame->savedRegisterCount == 31 + temp_reg_count) {
                            for (int i = 0; i < 15; i++) {
                                vm_set_register_safe(65 + i, frame->savedRegisters[i]);
                            }
                            for (int i = 0; i < 16; i++) {
                                vm_set_register_safe(240 + i, frame->savedRegisters[15 + i]);
                            }
                            for (int i = 0; i < temp_reg_count; i++) {
                                vm_set_register_safe(temp_reg_start + i, frame->savedRegisters[31 + i]);
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
                    break;
                }

                // Phase 1: Frame register operations
                case OP_LOAD_FRAME: {
                    uint8_t reg = READ_BYTE();
                    uint8_t frame_offset = READ_BYTE();
                    uint16_t frame_reg_id = FRAME_REG_START + frame_offset;
                    Value* src = get_register(&vm.register_file, frame_reg_id);
                    vm_set_register_safe(reg, *src);
                    break;
                }

                case OP_LOAD_SPILL: {
                    uint8_t reg = READ_BYTE();
                    uint8_t spill_id_high = READ_BYTE();
                    uint8_t spill_id_low = READ_BYTE();
                    uint16_t spill_id = (spill_id_high << 8) | spill_id_low;
                    Value* src = get_register(&vm.register_file, spill_id);
                    vm_set_register_safe(reg, *src);
                    break;
                }

                case OP_STORE_SPILL: {
                    uint8_t spill_id_high = READ_BYTE();
                    uint8_t spill_id_low = READ_BYTE();
                    uint8_t reg = READ_BYTE();
                    uint16_t spill_id = (spill_id_high << 8) | spill_id_low;
                    Value value = vm_get_register_safe(reg);
                    set_register(&vm.register_file, spill_id, value);
                    break;
                }

                case OP_STORE_FRAME: {
                    uint8_t frame_offset = READ_BYTE();
                    uint8_t reg = READ_BYTE();
                    uint16_t frame_reg_id = FRAME_REG_START + frame_offset;
                    set_register(&vm.register_file, frame_reg_id, vm_get_register_safe(reg));
                    break;
                }

                case OP_ENTER_FRAME: {
                    uint8_t frame_size = READ_BYTE();
                    (void)frame_size; // Frame size is implicit in current implementation
                    allocate_frame(&vm.register_file);
                    break;
                }

                case OP_EXIT_FRAME: {
                    deallocate_frame(&vm.register_file);
                    break;
                }

                case OP_MOVE_FRAME: {
                    uint8_t dst_offset = READ_BYTE();
                    uint8_t src_offset = READ_BYTE();
                    uint16_t dst_reg_id = FRAME_REG_START + dst_offset;
                    uint16_t src_reg_id = FRAME_REG_START + src_offset;
                    Value* src = get_register(&vm.register_file, src_reg_id);
                    set_register(&vm.register_file, dst_reg_id, *src);
                    break;
                }

                case OP_CLOSURE_R: {
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
                            closure->upvalues[i] = captureUpvalue(&vm_get_register_safe(index));
                        } else {
                            ObjClosure* enclosing = AS_CLOSURE(vm_get_register_safe(0)); // Current closure
                            closure->upvalues[i] = enclosing->upvalues[index];
                        }
                    }
                    
                    vm_set_register_safe(dstReg, CLOSURE_VAL(closure));
                    break;
                }

                case OP_GET_UPVALUE_R: {
                    uint8_t dstReg = READ_BYTE();
                    uint8_t upvalueIndex = READ_BYTE();
                    
                    ObjClosure* closure = AS_CLOSURE(vm_get_register_safe(0)); // Current closure
                    vm_set_register_safe(dstReg, *closure->upvalues[upvalueIndex]->location);
                    break;
                }

                case OP_SET_UPVALUE_R: {
                    uint8_t upvalueIndex = READ_BYTE();
                    uint8_t valueReg = READ_BYTE();
                    
                    ObjClosure* closure = AS_CLOSURE(vm_get_register_safe(0)); // Current closure
                    *closure->upvalues[upvalueIndex]->location = vm_get_register_safe(valueReg);
                    break;
                }

                case OP_CLOSE_UPVALUE_R: {
                    uint8_t localReg = READ_BYTE();
                    closeUpvalues(&vm_get_register_safe(localReg));
                    break;
                }

                // Short jump optimizations for performance  
                case OP_JUMP_SHORT: {
                    uint8_t offset = READ_BYTE();
                    if (!CF_JUMP_SHORT(offset)) {
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    break;
                }

                case OP_JUMP_BACK_SHORT: {
                    uint8_t offset = READ_BYTE();
                    if (!CF_JUMP_BACK_SHORT(offset)) {
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    break;
                }

                case OP_JUMP_IF_NOT_SHORT: {
                    uint8_t reg = READ_BYTE();
                    uint8_t offset = READ_BYTE();
                    if (!CF_JUMP_IF_NOT_SHORT(reg, offset)) {
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    break;
                }

                case OP_LOOP_SHORT: {
                    uint8_t offset = READ_BYTE();
                    if (!CF_LOOP_SHORT(offset)) {
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    break;
                }

                // Typed arithmetic operations for maximum performance (bypass Value boxing)
                case OP_ADD_I32_TYPED: {
                    handle_add_i32_typed();
                    break;
                }

                case OP_SUB_I32_TYPED: {
                    handle_sub_i32_typed();
                    break;
                }

                case OP_MUL_I32_TYPED: {
                    handle_mul_i32_typed();
                    break;
                }

                case OP_DIV_I32_TYPED: {
                    handle_div_i32_typed();
                    break;
                }

                case OP_MOD_I32_TYPED: {
                    handle_mod_i32_typed();
                    break;
                }

                // Additional typed operations (I64, F64, comparisons, loads, moves)
                case OP_ADD_I64_TYPED: {
                    handle_add_i64_typed();
                    break;
                }

                case OP_SUB_I64_TYPED: {
                    handle_sub_i64_typed();
                    break;
                }

                case OP_MUL_I64_TYPED: {
                    handle_mul_i64_typed();
                    break;
                }

                case OP_DIV_I64_TYPED: {
                    handle_div_i64_typed();
                    break;
                }

                case OP_MOD_I64_TYPED: {
                    handle_mod_i64_typed();
                    break;
                }

                case OP_ADD_F64_TYPED: {
                    handle_add_f64_typed();
                    break;
                }

                case OP_SUB_F64_TYPED: {
                    handle_sub_f64_typed();
                    break;
                }

                case OP_MUL_F64_TYPED: {
                    handle_mul_f64_typed();
                    break;
                }

                case OP_DIV_F64_TYPED: {
                    handle_div_f64_typed();
                    break;
                }

                case OP_MOD_F64_TYPED: {
                    handle_mod_f64_typed();
                    break;
                }

                // U32 Typed Operations
                case OP_ADD_U32_TYPED: {
                    handle_add_u32_typed();
                    break;
                }

                case OP_SUB_U32_TYPED: {
                    handle_sub_u32_typed();
                    break;
                }

                case OP_MUL_U32_TYPED: {
                    handle_mul_u32_typed();
                    break;
                }

                case OP_DIV_U32_TYPED: {
                    handle_div_u32_typed();
                    break;
                }

                case OP_MOD_U32_TYPED: {
                    handle_mod_u32_typed();
                    break;
                }

                // U64 Typed Operations
                case OP_ADD_U64_TYPED: {
                    handle_add_u64_typed();
                    break;
                }

                case OP_SUB_U64_TYPED: {
                    handle_sub_u64_typed();
                    break;
                }

                case OP_MUL_U64_TYPED: {
                    handle_mul_u64_typed();
                    break;
                }

                case OP_DIV_U64_TYPED: {
                    handle_div_u64_typed();
                    break;
                }

                case OP_MOD_U64_TYPED: {
                    handle_mod_u64_typed();
                    break;
                }

                // TODO: Removed mixed-type op for Rust-style strict typing

                case OP_LT_I32_TYPED: {
                    VM_TYPED_CMP_OP(i32_regs, <);
                    break;
                }

                case OP_LE_I32_TYPED: {
                    VM_TYPED_CMP_OP(i32_regs, <=);
                    break;
                }

                case OP_GT_I32_TYPED: {
                    VM_TYPED_CMP_OP(i32_regs, >);
                    break;
                }

                case OP_GE_I32_TYPED: {
                    VM_TYPED_CMP_OP(i32_regs, >=);
                    break;
                }

                case OP_LT_I64_TYPED: {
                    VM_TYPED_CMP_OP(i64_regs, <);
                    break;
                }

                case OP_LE_I64_TYPED: {
                    VM_TYPED_CMP_OP(i64_regs, <=);
                    break;
                }

                case OP_GT_I64_TYPED: {
                    VM_TYPED_CMP_OP(i64_regs, >);
                    break;
                }

                case OP_GE_I64_TYPED: {
                    VM_TYPED_CMP_OP(i64_regs, >=);
                    break;
                }

                case OP_LT_F64_TYPED: {
                    VM_TYPED_CMP_OP(f64_regs, <);
                    break;
                }

                case OP_LE_F64_TYPED: {
                    VM_TYPED_CMP_OP(f64_regs, <=);
                    break;
                }

                case OP_GT_F64_TYPED: {
                    VM_TYPED_CMP_OP(f64_regs, >);
                    break;
                }

                case OP_GE_F64_TYPED: {
                    VM_TYPED_CMP_OP(f64_regs, >=);
                    break;
                }

                case OP_LT_U32_TYPED: {
                    VM_TYPED_CMP_OP(u32_regs, <);
                    break;
                }

                case OP_LE_U32_TYPED: {
                    VM_TYPED_CMP_OP(u32_regs, <=);
                    break;
                }

                case OP_GT_U32_TYPED: {
                    VM_TYPED_CMP_OP(u32_regs, >);
                    break;
                }

                case OP_GE_U32_TYPED: {
                    VM_TYPED_CMP_OP(u32_regs, >=);
                    break;
                }

                case OP_LT_U64_TYPED: {
                    VM_TYPED_CMP_OP(u64_regs, <);
                    break;
                }

                case OP_LE_U64_TYPED: {
                    VM_TYPED_CMP_OP(u64_regs, <=);
                    break;
                }

                case OP_GT_U64_TYPED: {
                    VM_TYPED_CMP_OP(u64_regs, >);
                    break;
                }

                case OP_GE_U64_TYPED: {
                    VM_TYPED_CMP_OP(u64_regs, >=);
                    break;
                }

                case OP_LOAD_I32_CONST: {
                    handle_load_i32_const();
                    break;
                }

                case OP_LOAD_I64_CONST: {
                    handle_load_i64_const();
                    break;
                }

                case OP_LOAD_F64_CONST: {
                    handle_load_f64_const();
                    break;
                }

                case OP_MOVE_I32: {
                    handle_move_i32();
                    break;
                }

                case OP_MOVE_I64: {
                    handle_move_i64();
                    break;
                }

                case OP_MOVE_F64: {
                    handle_move_f64();
                    break;
                }

                case OP_TIME_STAMP: {
                    uint8_t dst = READ_BYTE();
                    
                    // Get high-precision timestamp in seconds
                    double timestamp = builtin_time_stamp();
                    
                    // Store in typed register and regular register for compatibility
                    vm.typed_regs.f64_regs[dst] = timestamp;
                    vm.typed_regs.reg_types[dst] = REG_TYPE_F64;
                    vm_set_register_safe(dst, F64_VAL(timestamp));

                    break;
                }

                // Fused instructions for optimized loops and arithmetic
                case OP_ADD_I32_IMM: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    int32_t imm = *(int32_t*)vm.ip;
                    vm.ip += 4;

                    Value src_value = vm_get_register_safe(src);
                    if (!IS_I32(src_value)) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operand must be i32");
                    }

                    int32_t result = AS_I32(src_value) + imm;
                    vm_store_i32_typed_hot(dst, result);
                    break;
                }

                case OP_SUB_I32_IMM: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    int32_t imm = *(int32_t*)vm.ip;
                    vm.ip += 4;

                    if (!IS_I32(vm_get_register_safe(src))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operand must be i32");
                    }

                    int32_t result = AS_I32(vm_get_register_safe(src)) - imm;
                    vm_set_register_safe(dst, I32_VAL(result));
                    break;
                }

                case OP_MUL_I32_IMM: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    int32_t imm = *(int32_t*)vm.ip;
                    vm.ip += 4;

                    if (!IS_I32(vm_get_register_safe(src))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operand must be i32");
                    }

                    int32_t result = AS_I32(vm_get_register_safe(src)) * imm;
                    vm_set_register_safe(dst, I32_VAL(result));
                    break;
                }

                case OP_CMP_I32_IMM: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    int32_t imm = *(int32_t*)vm.ip;
                    vm.ip += 4;

                    bool result = vm.typed_regs.i32_regs[src] < imm;
                    vm_store_bool_register(dst, result);
                    break;
                }

                case OP_INC_CMP_JMP: {
                    bool has_chunk = vm.chunk && vm.chunk->code;
                    size_t opcode_offset =
                        has_chunk ? (size_t)((vm.ip - vm.chunk->code) - 1) : 0;
                    uint8_t reg = READ_BYTE();
                    uint8_t limit_reg = READ_BYTE();
                    int16_t offset = READ_SHORT();

                    bool monotonic_hint = false;
                    if (has_chunk && vm.chunk->monotonic_range_flags &&
                        opcode_offset < (size_t)vm.chunk->count) {
                        monotonic_hint =
                            vm.chunk->monotonic_range_flags[opcode_offset] != 0;
                    }

                    if (monotonic_hint && vm_typed_reg_in_range(reg) &&
                        vm_typed_reg_in_range(limit_reg) &&
                        vm.typed_regs.reg_types[reg] == REG_TYPE_I32 &&
                        vm.typed_regs.reg_types[limit_reg] == REG_TYPE_I32) {
                        bool should_continue = false;
                        if (vm_exec_monotonic_inc_cmp_i32(reg, limit_reg,
                                                          &should_continue)) {
                            if (should_continue) {
                            vm.ip += offset;
                        }
                        break;
                    }
                }                    if (vm_exec_inc_i32_checked(reg)) {
                        if (vm_typed_reg_in_range(limit_reg) &&
                            vm.typed_regs.reg_types[limit_reg] == REG_TYPE_I32) {
                            if (vm.typed_regs.i32_regs[reg] < vm.typed_regs.i32_regs[limit_reg]) {
                                vm.ip += offset;
                            }
                            break;
                        }

                        Value limit = vm_get_register_safe(limit_reg);
                        if (!IS_I32(limit)) {
                            VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(),
                                            "Operands must be i32");
                        }

                        if (vm.typed_regs.i32_regs[reg] < AS_I32(limit)) {
                            vm.ip += offset;
                        }
                        break;
                    }

                    Value counter = vm_get_register_safe(reg);
                    Value limit = vm_get_register_safe(limit_reg);
                    if (!IS_I32(counter) || !IS_I32(limit)) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(),
                                        "Operands must be i32");
                    }

                    int32_t incremented = AS_I32(counter) + 1;
                    store_i32_register(reg, incremented);
                    if (incremented < AS_I32(limit)) {
                        vm.ip += offset;
                    }
                    break;
                }

                case OP_DEC_CMP_JMP: {
                    uint8_t reg = READ_BYTE();
                    uint8_t zero_test = READ_BYTE();
                    int16_t offset = READ_SHORT();

                    int32_t decremented = vm.typed_regs.i32_regs[reg] - 1;
                    vm_store_i32_typed_hot(reg, decremented);
                    if (decremented > vm.typed_regs.i32_regs[zero_test]) {
                        vm.ip += offset;
                    }
                    break;
                }

                case OP_MUL_ADD_I32: {
                    uint8_t dst = READ_BYTE();
                    uint8_t mul1 = READ_BYTE();
                    uint8_t mul2 = READ_BYTE();
                    uint8_t add = READ_BYTE();

                    int32_t result =
                        vm.typed_regs.i32_regs[mul1] * vm.typed_regs.i32_regs[mul2] +
                        vm.typed_regs.i32_regs[add];
                    vm_store_i32_typed_hot(dst, result);
                    break;
                }

                case OP_HALT:
                    vm.lastExecutionTime = get_time_vm() - start_time;
                    vm.isShuttingDown = true;  // Set shutdown flag before returning
                    RETURN(INTERPRET_OK);

                // Extended opcodes for 16-bit register access (Phase 2)
                case OP_LOAD_CONST_EXT: {
                    handle_load_const_ext();
                    break;
                }

                case OP_MOVE_EXT: {
                    handle_move_ext();
                    break;
                }

                case OP_STORE_EXT: {
                    // TODO: Implement handle_store_ext()  
                    VM_ERROR_RETURN(ERROR_RUNTIME, CURRENT_LOCATION(), "OP_STORE_EXT not implemented yet");
                    break;
                }

                case OP_LOAD_EXT: {
                    // TODO: Implement handle_load_ext()
                    VM_ERROR_RETURN(ERROR_RUNTIME, CURRENT_LOCATION(), "OP_LOAD_EXT not implemented yet");
                    break;
                }

                default:
                    VM_ERROR_RETURN(ERROR_RUNTIME, CURRENT_LOCATION(), "Unknown opcode: %d", instruction);
            }
            continue;

        handle_runtime_error:
            if (!vm_handle_pending_error()) {
                RETURN(INTERPRET_RUNTIME_ERROR);
            }
            continue;
        }
    #undef RETURN
}
#endif // !USE_COMPUTED_GOTO
