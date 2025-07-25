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

// Bridge functions for safe register access across legacy and hierarchical systems
// static inline Value vm_get_register_safe(uint16_t id) {
//     if (id < 256) {
//         // Legacy global registers
//         return vm.registers[id];
//     } else {
//         // Use register file for frame/spill registers
//         Value* reg_ptr = get_register(&vm.register_file, id);
//         return reg_ptr ? *reg_ptr : NIL_VAL;
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
                printf("        ");
                for (int i = 0; i < 8; i++) {
                    printf("[ R%d: ", i);
                    printValue(vm.registers[i]);
                    printf(" ]");
                }
                printf("\n");

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

                case OP_LOAD_NIL: {
                    handle_load_nil();
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
                    if (IS_STRING(vm.registers[src1]) || IS_STRING(vm.registers[src2])) {
                        // Convert both operands to strings and concatenate
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
                        break;
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
                    break;
                }

                case OP_SUB_I32_R: {
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
                    vm.registers[dst] = I32_VAL(a - b);
#else
                    // Strict same-type arithmetic only (after coercion)
                    if (IS_I32(val1)) {
                        int32_t a = AS_I32(val1);
                        int32_t b = AS_I32(val2);
                        vm.registers[dst] = I32_VAL(a - b);
                    } else if (IS_I64(val1)) {
                        int64_t a = AS_I64(val1);
                        int64_t b = AS_I64(val2);
                        vm.registers[dst] = I64_VAL(a - b);
                    } else if (IS_U32(val1)) {
                        uint32_t a = AS_U32(val1);
                        uint32_t b = AS_U32(val2);
                        vm.registers[dst] = U32_VAL(a - b);
                    } else if (IS_U64(val1)) {
                        uint64_t a = AS_U64(val1);
                        uint64_t b = AS_U64(val2);
                        vm.registers[dst] = U64_VAL(a - b);
                    } else if (IS_F64(val1)) {
                        double a = AS_F64(val1);
                        double b = AS_F64(val2);
                        vm.registers[dst] = F64_VAL(a - b);
                    }
#endif
                    break;
                }

                case OP_MUL_I32_R: {
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
                    break;
                }

                case OP_DIV_I32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (vm.registers[src1].type != vm.registers[src2].type) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be the same type. Use 'as' for explicit type conversion.");
                    }

                    if (!(IS_I32(vm.registers[src1]) || IS_I64(vm.registers[src1]) ||
                          IS_U32(vm.registers[src1]) || IS_U64(vm.registers[src1]) || IS_F64(vm.registers[src1]))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be numeric (i32, i64, u32, u64, or f64)");
                    }

                    if (IS_I32(vm.registers[src1])) {
                        HANDLE_I32_OVERFLOW_DIV(AS_I32(vm.registers[src1]), AS_I32(vm.registers[src2]), dst);
                    } else if (IS_I64(vm.registers[src1])) {
                        HANDLE_I64_OVERFLOW_DIV(AS_I64(vm.registers[src1]), AS_I64(vm.registers[src2]), dst);
                    } else if (IS_U32(vm.registers[src1])) {
                        HANDLE_U32_OVERFLOW_DIV(AS_U32(vm.registers[src1]), AS_U32(vm.registers[src2]), dst);
                    } else if (IS_U64(vm.registers[src1])) {
                        HANDLE_U64_OVERFLOW_DIV(AS_U64(vm.registers[src1]), AS_U64(vm.registers[src2]), dst);
                    } else {
                        HANDLE_F64_OVERFLOW_DIV(AS_F64(vm.registers[src1]), AS_F64(vm.registers[src2]), dst);
                    }
                    break;
                }

                case OP_MOD_I32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (vm.registers[src1].type != vm.registers[src2].type) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be the same type. Use 'as' for explicit type conversion.");
                    }

                    if (!(IS_I32(vm.registers[src1]) || IS_I64(vm.registers[src1]) ||
                          IS_U32(vm.registers[src1]) || IS_U64(vm.registers[src1]) || IS_F64(vm.registers[src1]))) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be numeric (i32, i64, u32, u64, or f64)");
                    }

                    if (IS_I32(vm.registers[src1])) {
                        HANDLE_I32_OVERFLOW_MOD(AS_I32(vm.registers[src1]), AS_I32(vm.registers[src2]), dst);
                    } else if (IS_I64(vm.registers[src1])) {
                        HANDLE_I64_OVERFLOW_MOD(AS_I64(vm.registers[src1]), AS_I64(vm.registers[src2]), dst);
                    } else if (IS_U32(vm.registers[src1])) {
                        HANDLE_U32_OVERFLOW_MOD(AS_U32(vm.registers[src1]), AS_U32(vm.registers[src2]), dst);
                    } else if (IS_U64(vm.registers[src1])) {
                        HANDLE_U64_OVERFLOW_MOD(AS_U64(vm.registers[src1]), AS_U64(vm.registers[src2]), dst);
                    } else {
                        HANDLE_F64_OVERFLOW_MOD(AS_F64(vm.registers[src1]), AS_F64(vm.registers[src2]), dst);
                    }
                    break;
                }

                case OP_INC_I32_R: {
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
                    break;
                }

                case OP_DEC_I32_R: {
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
                    break;
                }

                case OP_NEG_I32_R: {
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
                    break;
                }

                // I64 arithmetic operations
                case OP_ADD_I64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (!IS_I64(vm.registers[src1]) ||
                        !IS_I64(vm.registers[src2])) {
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
                    break;
                }

                case OP_SUB_I64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (!IS_I64(vm.registers[src1]) ||
                        !IS_I64(vm.registers[src2])) {
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
                    break;
                }

                case OP_MUL_I64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (!IS_I64(vm.registers[src1]) ||
                        !IS_I64(vm.registers[src2])) {
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
                    break;
                }

                case OP_DIV_I64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (!IS_I64(vm.registers[src1]) ||
                        !IS_I64(vm.registers[src2])) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be i64");
                    }

                    int64_t b = AS_I64(vm.registers[src2]);
                    if (b == 0) {
                        VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Division by zero");
                    }

                    vm.registers[dst] = I64_VAL(AS_I64(vm.registers[src1]) / b);
                    break;
                }

                case OP_MOD_I64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (!IS_I64(vm.registers[src1]) ||
                        !IS_I64(vm.registers[src2])) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be i64");
                    }

                    int64_t b = AS_I64(vm.registers[src2]);
                    if (b == 0) {
                        VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Division by zero");
                    }

                    vm.registers[dst] = I64_VAL(AS_I64(vm.registers[src1]) % b);
                    break;
                }

                case OP_ADD_U32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (!IS_U32(vm.registers[src1]) || !IS_U32(vm.registers[src2])) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be u32");
                    }

                    uint32_t a = AS_U32(vm.registers[src1]);
                    uint32_t b = AS_U32(vm.registers[src2]);
                    
                    // Check for overflow: if a + b < a, then overflow occurred
                    if (UINT32_MAX - a < b) {
                        VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "u32 addition overflow");
                    }

                    vm.registers[dst] = U32_VAL(a + b);
                    break;
                }

                case OP_SUB_U32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (!IS_U32(vm.registers[src1]) || !IS_U32(vm.registers[src2])) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be u32");
                    }

                    uint32_t a = AS_U32(vm.registers[src1]);
                    uint32_t b = AS_U32(vm.registers[src2]);
                    
                    // Check for underflow: if a < b, then underflow would occur
                    if (a < b) {
                        VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "u32 subtraction underflow");
                    }

                    vm.registers[dst] = U32_VAL(a - b);
                    break;
                }

                case OP_MUL_U32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();

                    if (!IS_U32(vm.registers[src1]) || !IS_U32(vm.registers[src2])) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be u32");
                    }

                    uint32_t a = AS_U32(vm.registers[src1]);
                    uint32_t b = AS_U32(vm.registers[src2]);
                    
                    // Check for multiplication overflow: if a != 0 && result / a != b
                    if (a != 0 && b > UINT32_MAX / a) {
                        VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "u32 multiplication overflow");
                    }

                    vm.registers[dst] = U32_VAL(a * b);
                    break;
                }

                case OP_DIV_U32_R: {
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
                    break;
                }

                case OP_MOD_U32_R: {
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
                    break;
                }

                case OP_ADD_U64_R: {
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
                    break;
                }

                case OP_SUB_U64_R: {
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
                    break;
                }

                case OP_MUL_U64_R: {
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
                    break;
                }

                case OP_DIV_U64_R: {
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
                    break;
                }

                case OP_MOD_U64_R: {
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
                    break;
                }

                case OP_I32_TO_I64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    if (!IS_I32(vm.registers[src])) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be i32");
                    }
                    vm.registers[dst] = I64_VAL((int64_t)AS_I32(vm.registers[src]));
                    break;
                }

                case OP_I32_TO_U32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    if (!IS_I32(vm.registers[src])) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be i32");
                    }
                    vm.registers[dst] = U32_VAL((uint32_t)AS_I32(vm.registers[src]));
                    break;
                }

                case OP_I32_TO_BOOL_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    if (!IS_I32(vm.registers[src])) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be i32");
                    }
                    // Convert i32 to bool: 0 -> false, non-zero -> true
                    vm.registers[dst] = BOOL_VAL(AS_I32(vm.registers[src]) != 0);
                    break;
                }

                case OP_U32_TO_I32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    if (!IS_U32(vm.registers[src])) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be u32");
                    }
                    vm.registers[dst] = I32_VAL((int32_t)AS_U32(vm.registers[src]));
                    break;
                }

                case OP_F64_TO_U32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    if (!IS_F64(vm.registers[src])) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be f64");
                    }
                    double val = AS_F64(vm.registers[src]);
                    if (val < 0.0 || val > (double)UINT32_MAX) {
                        VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "f64 value out of u32 range");
                    }
                    vm.registers[dst] = U32_VAL((uint32_t)val);
                    break;
                }

                case OP_U32_TO_F64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    if (!IS_U32(vm.registers[src])) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be u32");
                    }
                    vm.registers[dst] = F64_VAL((double)AS_U32(vm.registers[src]));
                    break;
                }

                case OP_I32_TO_U64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    if (!IS_I32(vm.registers[src])) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be i32");
                    }
                    int32_t val = AS_I32(vm.registers[src]);
                    if (val < 0) {
                        VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Cannot convert negative i32 to u64");
                    }
                    vm.registers[dst] = U64_VAL((uint64_t)val);
                    break;
                }

                case OP_I64_TO_U64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    if (!IS_I64(vm.registers[src])) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be i64");
                    }
                    int64_t val = AS_I64(vm.registers[src]);
                    if (val < 0) {
                        VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "Cannot convert negative i64 to u64");
                    }
                    vm.registers[dst] = U64_VAL((uint64_t)val);
                    break;
                }

                case OP_U64_TO_I32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    if (!IS_U64(vm.registers[src])) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be u64");
                    }
                    uint64_t val = AS_U64(vm.registers[src]);
                    if (val > (uint64_t)INT32_MAX) {
                        VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "u64 value too large for i32");
                    }
                    vm.registers[dst] = I32_VAL((int32_t)val);
                    break;
                }

                case OP_U64_TO_I64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    if (!IS_U64(vm.registers[src])) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be u64");
                    }
                    uint64_t val = AS_U64(vm.registers[src]);
                    if (val > (uint64_t)INT64_MAX) {
                        VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "u64 value too large for i64");
                    }
                    vm.registers[dst] = I64_VAL((int64_t)val);
                    break;
                }

                case OP_U32_TO_U64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    if (!IS_U32(vm.registers[src])) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be u32");
                    }
                    vm.registers[dst] = U64_VAL((uint64_t)AS_U32(vm.registers[src]));
                    break;
                }

                case OP_U64_TO_U32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    if (!IS_U64(vm.registers[src])) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be u64");
                    }
                    uint64_t val = AS_U64(vm.registers[src]);
                    if (val > (uint64_t)UINT32_MAX) {
                        VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "u64 value too large for u32");
                    }
                    vm.registers[dst] = U32_VAL((uint32_t)val);
                    break;
                }

                case OP_F64_TO_U64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    if (!IS_F64(vm.registers[src])) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be f64");
                    }
                    double val = AS_F64(vm.registers[src]);
                    if (val < 0.0 || val > (double)UINT64_MAX) {
                        VM_ERROR_RETURN(ERROR_VALUE, CURRENT_LOCATION(), "f64 value out of u64 range");
                    }
                    vm.registers[dst] = U64_VAL((uint64_t)val);
                    break;
                }

                case OP_U64_TO_F64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    if (!IS_U64(vm.registers[src])) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be u64");
                    }
                    vm.registers[dst] = F64_VAL((double)AS_U64(vm.registers[src]));
                    break;
                }

                // F64 Arithmetic Operations
                case OP_ADD_F64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    if (!IS_F64(vm.registers[src1]) || !IS_F64(vm.registers[src2])) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be f64");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    vm.registers[dst] = F64_VAL(AS_F64(vm.registers[src1]) + AS_F64(vm.registers[src2]));
                    break;
                }

                case OP_SUB_F64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    if (!IS_F64(vm.registers[src1]) || !IS_F64(vm.registers[src2])) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be f64");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    vm.registers[dst] = F64_VAL(AS_F64(vm.registers[src1]) - AS_F64(vm.registers[src2]));
                    break;
                }

                case OP_MUL_F64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    if (!IS_F64(vm.registers[src1]) || !IS_F64(vm.registers[src2])) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be f64");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    vm.registers[dst] = F64_VAL(AS_F64(vm.registers[src1]) * AS_F64(vm.registers[src2]));
                    break;
                }

                case OP_DIV_F64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    if (!IS_F64(vm.registers[src1]) || !IS_F64(vm.registers[src2])) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be f64");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    double a = AS_F64(vm.registers[src1]);
                    double b = AS_F64(vm.registers[src2]);
                    
                    // IEEE 754 compliant: division by zero produces infinity, not error
                    double result = a / b;
                    
                    // The result may be infinity, -infinity, or NaN
                    // These are valid f64 values according to IEEE 754
                    vm.registers[dst] = F64_VAL(result);
                    break;
                }

                case OP_MOD_F64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    if (!IS_F64(vm.registers[src1]) || !IS_F64(vm.registers[src2])) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be f64");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    double a = AS_F64(vm.registers[src1]);
                    double b = AS_F64(vm.registers[src2]);
                    
                    // IEEE 754 compliant: use fmod for floating point modulo
                    double result = fmod(a, b);
                    
                    // The result may be infinity, -infinity, or NaN
                    // These are valid f64 values according to IEEE 754
                    vm.registers[dst] = F64_VAL(result);
                    break;
                }

                // Bitwise Operations
                case OP_AND_I32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    if (!IS_I32(vm.registers[src1]) || !IS_I32(vm.registers[src2])) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be i32");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    vm.registers[dst] = I32_VAL(AS_I32(vm.registers[src1]) & AS_I32(vm.registers[src2]));
                    break;
                }

                case OP_OR_I32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    if (!IS_I32(vm.registers[src1]) || !IS_I32(vm.registers[src2])) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be i32");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    vm.registers[dst] = I32_VAL(AS_I32(vm.registers[src1]) | AS_I32(vm.registers[src2]));
                    break;
                }

                case OP_XOR_I32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    if (!IS_I32(vm.registers[src1]) || !IS_I32(vm.registers[src2])) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be i32");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    vm.registers[dst] = I32_VAL(AS_I32(vm.registers[src1]) ^ AS_I32(vm.registers[src2]));
                    break;
                }

                case OP_NOT_I32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    if (!IS_I32(vm.registers[src])) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operand must be i32");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    vm.registers[dst] = I32_VAL(~AS_I32(vm.registers[src]));
                    break;
                }

                case OP_SHL_I32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    if (!IS_I32(vm.registers[src1]) || !IS_I32(vm.registers[src2])) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be i32");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    vm.registers[dst] = I32_VAL(AS_I32(vm.registers[src1]) << AS_I32(vm.registers[src2]));
                    break;
                }

                case OP_SHR_I32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src1 = READ_BYTE();
                    uint8_t src2 = READ_BYTE();
                    if (!IS_I32(vm.registers[src1]) || !IS_I32(vm.registers[src2])) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be i32");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    vm.registers[dst] = I32_VAL(AS_I32(vm.registers[src1]) >> AS_I32(vm.registers[src2]));
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
                    if (!IS_I32(vm.registers[src])) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be i32");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    vm.registers[dst] = F64_VAL((double)AS_I32(vm.registers[src]));
                    break;
                }

                case OP_I64_TO_F64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    if (!IS_I64(vm.registers[src])) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be i64");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    vm.registers[dst] = F64_VAL((double)AS_I64(vm.registers[src]));
                    break;
                }

                case OP_F64_TO_I32_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    if (!IS_F64(vm.registers[src])) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be f64");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    vm.registers[dst] = I32_VAL((int32_t)AS_F64(vm.registers[src]));
                    break;
                }

                case OP_F64_TO_I64_R: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    if (!IS_F64(vm.registers[src])) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Source must be f64");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    vm.registers[dst] = I64_VAL((int64_t)AS_F64(vm.registers[src]));
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
                    } else if (IS_NIL(vm.registers[src1])) {
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
                    } else if (IS_NIL(vm.registers[src2])) {
                        right_bool = false;
                    } else {
                        right_bool = true; // Objects, strings, etc. are truthy
                    }

                    vm.registers[dst] = BOOL_VAL(left_bool && right_bool);
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
                    } else if (IS_NIL(vm.registers[src1])) {
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
                    } else if (IS_NIL(vm.registers[src2])) {
                        right_bool = false;
                    } else {
                        right_bool = true; // Objects, strings, etc. are truthy
                    }

                    vm.registers[dst] = BOOL_VAL(left_bool || right_bool);
                    break;
                }

                case OP_NOT_BOOL_R: {
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
                    } else if (IS_NIL(vm.registers[src])) {
                        src_bool = false;
                    } else {
                        src_bool = true; // Objects, strings, etc. are truthy
                    }

                    vm.registers[dst] = BOOL_VAL(!src_bool);
                    break;
                }

                case OP_CONCAT_R: {
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
                    break;
                }

                case OP_TO_STRING_R: {
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
                        break;
                    } else {
                        snprintf(buffer, sizeof(buffer), "nil");
                    }
                    
                    ObjString* result = allocateString(buffer, (int)strlen(buffer));
                    vm.registers[dst] = STRING_VAL(result);
                    break;
                }

                // Control flow
                case OP_JUMP: {
                    uint16_t offset = READ_SHORT();
                    CF_JUMP(offset);
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

                case OP_LOOP: {
                    uint16_t offset = READ_SHORT();
                    CF_LOOP(offset);
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
                            vm.registers[resultReg] = NIL_VAL;
                            break;
                        }
                        
                        // Check if we have room for another call frame
                        if (vm.frameCount >= FRAMES_MAX) {
                            vm.registers[resultReg] = NIL_VAL;
                            break;
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
                        
                        break;
                    } else if (IS_I32(funcValue)) {
                        int functionIndex = AS_I32(funcValue);
                        
                        if (functionIndex < 0 || functionIndex >= vm.functionCount) {
                            vm.registers[resultReg] = NIL_VAL;
                            break;
                        }
                        
                        Function* function = &vm.functions[functionIndex];
                        
                        // Check arity
                        if (argCount != function->arity) {
                            vm.registers[resultReg] = NIL_VAL;
                            break;
                        }
                        
                        // Check if we have room for another call frame
                        if (vm.frameCount >= FRAMES_MAX) {
                            vm.registers[resultReg] = NIL_VAL;
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
                        
                    } else if (IS_FUNCTION(funcValue)) {
                        // Calling a function object directly
                        ObjFunction* objFunction = AS_FUNCTION(funcValue);
                        
                        // Check arity
                        if (argCount != objFunction->arity) {
                            vm.registers[resultReg] = NIL_VAL;
                            break;
                        }
                        
                        // Check if we have room for another call frame
                        if (vm.frameCount >= FRAMES_MAX) {
                            vm.registers[resultReg] = NIL_VAL;
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
                            vm.registers[paramBase + i] = vm.registers[firstArgReg + i];
                        }
                        
                        // Switch to function's bytecode
                        vm.chunk = objFunction->chunk;
                        vm.ip = objFunction->chunk->code;
                        
                    } else {
                        vm.registers[resultReg] = NIL_VAL;
                    }
                    break;
                }
                case OP_TAIL_CALL_R: {
                    uint8_t funcReg = READ_BYTE();
                    uint8_t firstArgReg = READ_BYTE();
                    uint8_t argCount = READ_BYTE();
                    uint8_t resultReg = READ_BYTE();
                    
                    Value funcValue = vm.registers[funcReg];
                    
                    if (IS_I32(funcValue)) {
                        int functionIndex = AS_I32(funcValue);
                        
                        if (functionIndex < 0 || functionIndex >= vm.functionCount) {
                            vm.registers[resultReg] = NIL_VAL;
                            break;
                        }
                        
                        Function* function = &vm.functions[functionIndex];
                        
                        // Check arity
                        if (argCount != function->arity) {
                            vm.registers[resultReg] = NIL_VAL;
                            break;
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
                        vm.registers[resultReg] = NIL_VAL;
                    }
                    break;
                }
                case OP_RETURN_R: {
                    uint8_t reg = READ_BYTE();
                    Value returnValue = vm.registers[reg];

                    if (vm.frameCount > 0) {
                        CallFrame* frame = &vm.frames[--vm.frameCount];
                        
                        // Restore saved registers using dynamic parameter base and bridge functions
                        for (int i = 0; i < frame->savedRegisterCount; i++) {
                            vm_set_register_safe(frame->parameterBaseRegister + i, frame->savedRegisters[i]);
                        }
                        
                        vm.chunk = frame->previousChunk;
                        vm.ip = frame->returnAddress;

                        // Store return value in the result register
                        vm.registers[frame->baseRegister] = returnValue;
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
                        
                        // Restore saved registers using dynamic parameter base and bridge functions
                        for (int i = 0; i < frame->savedRegisterCount; i++) {
                            vm_set_register_safe(frame->parameterBaseRegister + i, frame->savedRegisters[i]);
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
                    vm.registers[reg] = *src;
                    break;
                }

                case OP_LOAD_SPILL: {
                    uint8_t reg = READ_BYTE();
                    uint8_t spill_id_high = READ_BYTE();
                    uint8_t spill_id_low = READ_BYTE();
                    uint16_t spill_id = (spill_id_high << 8) | spill_id_low;
                    Value* src = get_register(&vm.register_file, spill_id);
                    vm.registers[reg] = *src;
                    break;
                }

                case OP_STORE_SPILL: {
                    uint8_t spill_id_high = READ_BYTE();
                    uint8_t spill_id_low = READ_BYTE();
                    uint8_t reg = READ_BYTE();
                    uint16_t spill_id = (spill_id_high << 8) | spill_id_low;
                    Value value = vm.registers[reg];
                    set_register(&vm.register_file, spill_id, value);
                    break;
                }

                case OP_STORE_FRAME: {
                    uint8_t frame_offset = READ_BYTE();
                    uint8_t reg = READ_BYTE();
                    uint16_t frame_reg_id = FRAME_REG_START + frame_offset;
                    set_register(&vm.register_file, frame_reg_id, vm.registers[reg]);
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
                    
                    Value functionValue = vm.registers[functionReg];
                    if (!IS_FUNCTION(functionValue)) {
                        VM_ERROR_RETURN(ERROR_RUNTIME, CURRENT_LOCATION(), "Expected function for closure creation");
                    }
                    
                    ObjFunction* function = AS_FUNCTION(functionValue);
                    ObjClosure* closure = allocateClosure(function);
                    
                    for (int i = 0; i < upvalueCount; i++) {
                        uint8_t isLocal = READ_BYTE();
                        uint8_t index = READ_BYTE();
                        
                        if (isLocal) {
                            closure->upvalues[i] = captureUpvalue(&vm.registers[index]);
                        } else {
                            ObjClosure* enclosing = AS_CLOSURE(vm.registers[0]); // Current closure
                            closure->upvalues[i] = enclosing->upvalues[index];
                        }
                    }
                    
                    vm.registers[dstReg] = CLOSURE_VAL(closure);
                    break;
                }

                case OP_GET_UPVALUE_R: {
                    uint8_t dstReg = READ_BYTE();
                    uint8_t upvalueIndex = READ_BYTE();
                    
                    ObjClosure* closure = AS_CLOSURE(vm.registers[0]); // Current closure
                    vm.registers[dstReg] = *closure->upvalues[upvalueIndex]->location;
                    break;
                }

                case OP_SET_UPVALUE_R: {
                    uint8_t upvalueIndex = READ_BYTE();
                    uint8_t valueReg = READ_BYTE();
                    
                    ObjClosure* closure = AS_CLOSURE(vm.registers[0]); // Current closure
                    *closure->upvalues[upvalueIndex]->location = vm.registers[valueReg];
                    break;
                }

                case OP_CLOSE_UPVALUE_R: {
                    uint8_t localReg = READ_BYTE();
                    closeUpvalues(&vm.registers[localReg]);
                    break;
                }

                // Short jump optimizations for performance  
                case OP_JUMP_SHORT: {
                    uint8_t offset = READ_BYTE();
                    CF_JUMP_SHORT(offset);
                    break;
                }

                case OP_JUMP_BACK_SHORT: {
                    uint8_t offset = READ_BYTE();
                    CF_JUMP_BACK_SHORT(offset);
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
                    CF_LOOP_SHORT(offset);
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
                    vm.registers[dst] = F64_VAL(timestamp);

                    break;
                }

                // Fused instructions for optimized loops and arithmetic
                case OP_ADD_I32_IMM: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    int32_t imm = *(int32_t*)vm.ip;
                    vm.ip += 4;

                    if (!IS_I32(vm.registers[src])) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operand must be i32");
                    }

                    int32_t result = AS_I32(vm.registers[src]) + imm;
                    vm.registers[dst] = I32_VAL(result);
                    break;
                }

                case OP_SUB_I32_IMM: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    int32_t imm = *(int32_t*)vm.ip;
                    vm.ip += 4;

                    if (!IS_I32(vm.registers[src])) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operand must be i32");
                    }

                    int32_t result = AS_I32(vm.registers[src]) - imm;
                    vm.registers[dst] = I32_VAL(result);
                    break;
                }

                case OP_MUL_I32_IMM: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    int32_t imm = *(int32_t*)vm.ip;
                    vm.ip += 4;

                    if (!IS_I32(vm.registers[src])) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operand must be i32");
                    }

                    int32_t result = AS_I32(vm.registers[src]) * imm;
                    vm.registers[dst] = I32_VAL(result);
                    break;
                }

                case OP_CMP_I32_IMM: {
                    uint8_t dst = READ_BYTE();
                    uint8_t src = READ_BYTE();
                    int32_t imm = *(int32_t*)vm.ip;
                    vm.ip += 4;

                    vm.typed_regs.bool_regs[dst] = vm.typed_regs.i32_regs[src] < imm;
                    vm.typed_regs.reg_types[dst] = REG_TYPE_BOOL;
                    break;
                }

                case OP_INC_CMP_JMP: {
                    uint8_t reg = READ_BYTE();
                    uint8_t limit_reg = READ_BYTE();
                    int16_t offset = READ_SHORT();

                    if (!IS_I32(vm.registers[reg]) || !IS_I32(vm.registers[limit_reg])) {
                        VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), "Operands must be i32");
                    }

                    int32_t incremented = AS_I32(vm.registers[reg]) + 1;
                    vm.registers[reg] = I32_VAL(incremented);
                    if (incremented < AS_I32(vm.registers[limit_reg])) {
                        vm.ip += offset;
                    }
                    break;
                               }

                case OP_DEC_CMP_JMP: {
                    uint8_t reg = READ_BYTE();
                    uint8_t zero_test = READ_BYTE();
                    int16_t offset = READ_SHORT();

                    if (--vm.typed_regs.i32_regs[reg] > vm.typed_regs.i32_regs[zero_test]) {
                        vm.ip += offset;
                    }
                    break;
                }

                case OP_MUL_ADD_I32: {
                    uint8_t dst = READ_BYTE();
                    uint8_t mul1 = READ_BYTE();
                    uint8_t mul2 = READ_BYTE();
                    uint8_t add = READ_BYTE();

                    vm.typed_regs.i32_regs[dst] =
                        vm.typed_regs.i32_regs[mul1] * vm.typed_regs.i32_regs[mul2] +
                        vm.typed_regs.i32_regs[add];
                    vm.typed_regs.reg_types[dst] = REG_TYPE_I32;
                    break;
                }

                case OP_HALT:
                    vm.lastExecutionTime = get_time_vm() - start_time;
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
                    vm.lastExecutionTime = get_time_vm() - start_time;
                    RETURN(INTERPRET_RUNTIME_ERROR);
            }

            if (IS_ERROR(vm.lastError)) {
                // Handle runtime errors
                if (vm.tryFrameCount > 0) {
                    // Exception handling
                    TryFrame frame = vm.tryFrames[--vm.tryFrameCount];
                    vm.ip = frame.handler;
                    vm.globals[frame.varIndex] = vm.lastError;
                    vm.lastError = NIL_VAL;
                } else {
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }
            }
        }
    #undef RETURN
}
#endif // !USE_COMPUTED_GOTO
