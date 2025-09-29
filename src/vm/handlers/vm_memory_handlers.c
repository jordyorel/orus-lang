/*
 * Orus Language Project
 * ---------------------------------------------------------------------------
 * File: src/vm/handlers/vm_memory_handlers.c
 * Author: Jordy Orel KONDA
 * Copyright (c) 2025 Jordy Orel KONDA
 * License: MIT License (see LICENSE file in the project root)
 * Description: Implements memory opcode handlers for load, store, and allocation
 *              operations.
 */

/*
 * File: src/vm/handlers/vm_memory_handlers.c
 * High-performance memory operation handlers for the Orus VM
 * 
 * Design Philosophy:
 * - Static inline functions for zero-cost abstraction
 * - Preserve computed-goto dispatch performance
 * - Clean separation of memory operations from dispatch logic
 * - Maintain exact same behavior as original implementations
 */

#include "vm/vm_opcode_handlers.h"
#include "vm/vm_dispatch.h"
#include "vm/vm_comparison.h"
#include "vm/vm_loop_fastpaths.h"
#include "runtime/builtins.h"

// Frame-aware register access functions for proper local variable isolation

// ====== Basic Load Operation Handlers ======

void handle_load_const(void) {
    uint8_t reg = READ_BYTE();
    uint16_t constantIndex = READ_SHORT();
    vm_set_register_safe(reg, READ_CONSTANT(constantIndex));
}

// Extended constant loading for 16-bit register IDs (Phase 2.1)
void handle_load_const_ext(void) {
    uint16_t reg = READ_SHORT();      // 16-bit register ID
    uint16_t constantIndex = READ_SHORT(); // 16-bit constant index
    Value constant = READ_CONSTANT(constantIndex);
    
    // Use VM register file for extended register access
    set_register(&vm.register_file, reg, constant);
}

// Extended register move for 16-bit register IDs (Phase 2.2)
void handle_move_ext(void) {
    uint16_t dst_reg = READ_SHORT();  // 16-bit destination register
    uint16_t src_reg = READ_SHORT();  // 16-bit source register
    
    // Get value from source register using VM register file
    Value* src_value = get_register(&vm.register_file, src_reg);
    if (src_value) {
        // Move to destination register
        set_register(&vm.register_file, dst_reg, *src_value);
    }
}


void handle_load_true(void) {
    uint8_t reg = READ_BYTE();
    vm_store_bool_register(reg, true);
}

void handle_load_false(void) {
    uint8_t reg = READ_BYTE();
    vm_store_bool_register(reg, false);
}

// ====== Register Move Operation Handler ======

void handle_move_reg(void) {
    uint8_t dst = READ_BYTE();
    uint8_t src = READ_BYTE();

    if (vm_typed_reg_in_range(src)) {
        switch (vm.typed_regs.reg_types[src]) {
            case REG_TYPE_I32: {
                int32_t cached;
                if (vm_try_read_i32_typed(src, &cached)) {
                    store_i32_register(dst, cached);
                    return;
                }
                break;
            }
            case REG_TYPE_I64: {
                int64_t cached;
                if (vm_try_read_i64_typed(src, &cached)) {
                    store_i64_register(dst, cached);
                    return;
                }
                break;
            }
            case REG_TYPE_U32: {
                uint32_t cached;
                if (vm_try_read_u32_typed(src, &cached)) {
                    store_u32_register(dst, cached);
                    return;
                }
                break;
            }
            case REG_TYPE_U64: {
                uint64_t cached;
                if (vm_try_read_u64_typed(src, &cached)) {
                    store_u64_register(dst, cached);
                    return;
                }
                break;
            }
            case REG_TYPE_F64: {
                double cached;
                if (vm_try_read_f64_typed(src, &cached)) {
                    store_f64_register(dst, cached);
                    return;
                }
                break;
            }
            case REG_TYPE_BOOL: {
                bool cached;
                if (vm_try_read_bool_typed(src, &cached)) {
                    store_bool_register(dst, cached);
                    return;
                }
                break;
            }
            case REG_TYPE_HEAP:
            case REG_TYPE_NONE:
            default:
                break;
        }
    }

    // Use frame-aware register access for proper local variable isolation
    Value value = vm_get_register_safe(src);
    switch (value.type) {
        case VAL_I32:
            vm_cache_i32_typed(src, AS_I32(value));
            store_i32_register(dst, AS_I32(value));
            break;
        case VAL_I64:
            vm_cache_i64_typed(src, AS_I64(value));
            store_i64_register(dst, AS_I64(value));
            break;
        case VAL_U32:
            vm_cache_u32_typed(src, AS_U32(value));
            store_u32_register(dst, AS_U32(value));
            break;
        case VAL_U64:
            vm_cache_u64_typed(src, AS_U64(value));
            store_u64_register(dst, AS_U64(value));
            break;
        case VAL_F64:
            vm_cache_f64_typed(src, AS_F64(value));
            store_f64_register(dst, AS_F64(value));
            break;
        case VAL_BOOL:
            vm_cache_bool_typed(src, AS_BOOL(value));
            store_bool_register(dst, AS_BOOL(value));
            break;
        default:
            vm_set_register_safe(dst, value);
            break;
    }
}

// ====== Global Variable Operation Handlers ======

void handle_load_global(void) {
    uint8_t reg = READ_BYTE();
    uint8_t globalIndex = READ_BYTE();
    if (globalIndex >= vm.variableCount || vm.globalTypes[globalIndex] == NULL) {
        runtimeError(ERROR_NAME, (SrcLocation){NULL,0,0}, "Undefined variable");
        return;
    }
    vm_set_register_safe(reg, vm.globals[globalIndex]);
}

void handle_store_global(void) {
    uint8_t globalIndex = READ_BYTE();
    uint8_t reg = READ_BYTE();
    
    // CREATIVE SOLUTION: Type safety enforcement with intelligent literal coercion
    // This maintains single-pass design while being flexible for compatible types
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
            
            runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Type mismatch: cannot assign value to variable of type '%s'. Use 'as' for explicit conversion.",
                        expectedTypeName);
            return;
        }
        
        // Store the coerced value
        vm.globals[globalIndex] = coercedValue;
    } else {
        // No declared type, store as-is
        vm.globals[globalIndex] = valueToStore;
    }
}

// ====== Typed Constant Load Handlers ======

void handle_load_i32_const(void) {
    uint8_t reg = READ_BYTE();
    uint16_t constantIndex = READ_SHORT();
    Value constant = READ_CONSTANT(constantIndex);
    if (!IS_I32(constant)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Constant must be i32");
        return;
    }
    vm_store_i32_register(reg, AS_I32(constant));
}

void handle_load_i64_const(void) {
    uint8_t reg = READ_BYTE();
    uint16_t constantIndex = READ_SHORT();
    // Store in the standard register system where comparison operations expect values
    Value constant = READ_CONSTANT(constantIndex);
    if (!IS_I64(constant)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Constant must be i64");
        return;
    }
    vm_store_i64_register(reg, AS_I64(constant));
}

void handle_load_u32_const(void) {
    uint8_t reg = READ_BYTE();
    uint16_t constantIndex = READ_SHORT();
    // Store in the standard register system where comparison operations expect values
    Value constant = READ_CONSTANT(constantIndex);
    if (!IS_U32(constant)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Constant must be u32");
        return;
    }
    vm_store_u32_register(reg, AS_U32(constant));
}

void handle_load_u64_const(void) {
    uint8_t reg = READ_BYTE();
    uint16_t constantIndex = READ_SHORT();
    // Store in the standard register system where comparison operations expect values
    Value constant = READ_CONSTANT(constantIndex);
    if (!IS_U64(constant)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Constant must be u64");
        return;
    }
    vm_store_u64_register(reg, AS_U64(constant));
}

void handle_load_f64_const(void) {
    uint8_t reg = READ_BYTE();
    uint16_t constantIndex = READ_SHORT();
    // Store in the standard register system where comparison operations expect values
    Value constant = READ_CONSTANT(constantIndex);
    if (!IS_F64(constant)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Constant must be f64");
        return;
    }
    vm_store_f64_register(reg, AS_F64(constant));
}

// ====== Typed Move Operation Handlers ======

void handle_move_i32(void) {
    uint8_t dst = READ_BYTE();
    uint8_t src = READ_BYTE();
    Value src_val = vm_get_register_safe(src);
    if (!IS_I32(src_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Source register must contain i32 value");
        return;
    }
    vm_cache_i32_typed(src, AS_I32(src_val));
    vm_store_i32_register(dst, AS_I32(src_val));
}

void handle_move_i64(void) {
    uint8_t dst = READ_BYTE();
    uint8_t src = READ_BYTE();
    // Read from the standard register system where OP_LOAD_CONST stores values
    Value src_val = vm_get_register_safe(src);
    if (!IS_I64(src_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Source register must contain i64 value");
        return;
    }
    vm_cache_i64_typed(src, AS_I64(src_val));
    vm_store_i64_register(dst, AS_I64(src_val));
}

void handle_move_u32(void) {
    uint8_t dst = READ_BYTE();
    uint8_t src = READ_BYTE();
    Value src_val = vm_get_register_safe(src);
    if (!IS_U32(src_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Source register must contain u32 value");
        return;
    }
    vm_cache_u32_typed(src, AS_U32(src_val));
    vm_store_u32_register(dst, AS_U32(src_val));
}

void handle_move_u64(void) {
    uint8_t dst = READ_BYTE();
    uint8_t src = READ_BYTE();
    Value src_val = vm_get_register_safe(src);
    if (!IS_U64(src_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Source register must contain u64 value");
        return;
    }
    vm_cache_u64_typed(src, AS_U64(src_val));
    vm_store_u64_register(dst, AS_U64(src_val));
}

void handle_move_f64(void) {
    uint8_t dst = READ_BYTE();
    uint8_t src = READ_BYTE();
    // Read from the standard register system where OP_LOAD_CONST stores values
    Value src_val = vm_get_register_safe(src);
    if (!IS_F64(src_val)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Source register must contain f64 value");
        return;
    }
    vm_cache_f64_typed(src, AS_F64(src_val));
    vm_store_f64_register(dst, AS_F64(src_val));
}

// ====== Print Operation Handlers ======

void handle_input(void) {
    uint8_t dst = READ_BYTE();
    uint8_t arg_count = READ_BYTE();
    uint8_t prompt_reg = READ_BYTE();

    if (arg_count > 1) {
        SrcLocation loc = {vm.filePath, vm.currentLine, vm.currentColumn};
        runtimeError(ERROR_ARGUMENT, loc, "input() accepts at most one argument");
        return;
    }

    Value args_storage[1];
    Value* args_ptr = NULL;
    if (arg_count == 1) {
        args_storage[0] = vm_get_register_safe(prompt_reg);
        args_ptr = args_storage;
    }

    Value result;
    if (!builtin_input(args_ptr, (int)arg_count, &result)) {
        SrcLocation loc = {vm.filePath, vm.currentLine, vm.currentColumn};
        runtimeError(ERROR_EOF, loc, "input() reached end of file");
        return;
    }

    vm_set_register_safe(dst, result);
}

void handle_range(void) {
    uint8_t dst = READ_BYTE();
    uint8_t arg_count = READ_BYTE();
    uint8_t first_reg = READ_BYTE();
    uint8_t second_reg = READ_BYTE();
    uint8_t third_reg = READ_BYTE();

    if (arg_count < 1 || arg_count > 3) {
        SrcLocation loc = {vm.filePath, vm.currentLine, vm.currentColumn};
        runtimeError(ERROR_ARGUMENT, loc, "range() expects between 1 and 3 arguments");
        return;
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
        SrcLocation loc = {vm.filePath, vm.currentLine, vm.currentColumn};
        runtimeError(ERROR_ARGUMENT, loc, "Invalid arguments provided to range()");
        return;
    }

    vm_typed_iterator_invalidate(dst);
    vm_set_register_safe(dst, result);

    if (!vm.config.force_boxed_iterators && IS_RANGE_ITERATOR(result)) {
        ObjRangeIterator* iterator = AS_RANGE_ITERATOR(result);
        if (iterator) {
            vm_typed_iterator_bind_range(dst, iterator->current, iterator->end, iterator->step);
        }
    }
}

void handle_print(void) {
    uint8_t reg = READ_BYTE();
    Value temp_value = vm_get_register_safe(reg);
    builtin_print(&temp_value, 1, true);
}

void handle_print_multi(void) {
    uint8_t first = READ_BYTE();
    uint8_t count = READ_BYTE();
    uint8_t nl = READ_BYTE();
    
    // Validate bounds to avoid out-of-range register access
    if ((int)first + (int)count > 256) {
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL,0,0},
                     "PRINT_MULTI out of bounds: first=%d, count=%d",
                     first, count);
        return;
    }

    // Copy values to temporary array using frame-aware access
    Value temp_values[256];  // Max possible count
    for (int i = 0; i < count; i++) {
        temp_values[i] = vm_get_register_safe((uint16_t)(first + i));
    }
    builtin_print(temp_values, count, nl != 0);
}

void handle_print_no_nl(void) {
    uint8_t reg = READ_BYTE();
    Value temp_value = vm_get_register_safe(reg);
    builtin_print(&temp_value, 1, false);
}

void handle_parse_int(void) {
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
        SrcLocation loc = {vm.filePath, vm.currentLine, vm.currentColumn};
        runtimeError(ERROR_CONVERSION, loc, "%s", text);
        return;
    }

    vm_set_register_safe(dst, result);
}

void handle_parse_float(void) {
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
        SrcLocation loc = {vm.filePath, vm.currentLine, vm.currentColumn};
        runtimeError(ERROR_CONVERSION, loc, "%s", text);
        return;
    }

    vm_set_register_safe(dst, result);
}

void handle_type_of(void) {
    uint8_t dst = READ_BYTE();
    uint8_t value_reg = READ_BYTE();

    Value value = vm_get_register_safe(value_reg);
    Value result;
    if (!builtin_type_of(value, &result)) {
        SrcLocation loc = {vm.filePath, vm.currentLine, vm.currentColumn};
        runtimeError(ERROR_RUNTIME, loc, "type_of() internal error");
        return;
    }

    vm_set_register_safe(dst, result);
}

void handle_is_type(void) {
    uint8_t dst = READ_BYTE();
    uint8_t value_reg = READ_BYTE();
    uint8_t type_reg = READ_BYTE();

    Value value = vm_get_register_safe(value_reg);
    Value type_identifier = vm_get_register_safe(type_reg);
    Value result;
    if (!builtin_is_type(value, type_identifier, &result)) {
        SrcLocation loc = {vm.filePath, vm.currentLine, vm.currentColumn};
        runtimeError(ERROR_RUNTIME, loc, "is_type() internal error");
        return;
    }

    vm_set_register_safe(dst, result);
}

// ====== Utility Operation Handlers ======

void handle_halt(void) {
    // Halt operation - handled by dispatch loop
    // This is a placeholder for consistency
}

void handle_time_stamp(void) {
    // Time stamp operation - implementation depends on VM requirements
    // This is a placeholder for consistency
}
