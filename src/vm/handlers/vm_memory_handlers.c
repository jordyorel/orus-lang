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
#include "runtime/builtins.h"

// ====== Basic Load Operation Handlers ======

static inline void handle_load_const(void) {
    uint8_t reg = READ_BYTE();
    uint16_t constantIndex = READ_SHORT();
    vm.registers[reg] = READ_CONSTANT(constantIndex);
}

// Extended constant loading for 16-bit register IDs (Phase 2.1)
static inline void handle_load_const_ext(void) {
    uint16_t reg = READ_SHORT();      // 16-bit register ID
    uint16_t constantIndex = READ_SHORT(); // 16-bit constant index
    Value constant = READ_CONSTANT(constantIndex);
    
    // Use VM register file for extended register access
    set_register(&vm.register_file, reg, constant);
}

// Extended register move for 16-bit register IDs (Phase 2.2)
static inline void handle_move_ext(void) {
    uint16_t dst_reg = READ_SHORT();  // 16-bit destination register
    uint16_t src_reg = READ_SHORT();  // 16-bit source register
    
    // Get value from source register using VM register file
    Value* src_value = get_register(&vm.register_file, src_reg);
    if (src_value) {
        // Move to destination register
        set_register(&vm.register_file, dst_reg, *src_value);
    }
}

static inline void handle_load_nil(void) {
    uint8_t reg = READ_BYTE();
    vm.registers[reg] = NIL_VAL;
}

static inline void handle_load_true(void) {
    uint8_t reg = READ_BYTE();
    vm.registers[reg] = BOOL_VAL(true);
}

static inline void handle_load_false(void) {
    uint8_t reg = READ_BYTE();
    vm.registers[reg] = BOOL_VAL(false);
}

// ====== Register Move Operation Handler ======

static inline void handle_move_reg(void) {
    uint8_t dst = READ_BYTE();
    uint8_t src = READ_BYTE();
    vm.registers[dst] = vm.registers[src];
}

// ====== Global Variable Operation Handlers ======

static inline void handle_load_global(void) {
    uint8_t reg = READ_BYTE();
    uint8_t globalIndex = READ_BYTE();
    if (globalIndex >= vm.variableCount || vm.globalTypes[globalIndex] == NULL) {
        runtimeError(ERROR_NAME, (SrcLocation){NULL,0,0}, "Undefined variable");
        return;
    }
    vm.registers[reg] = vm.globals[globalIndex];
}

static inline void handle_store_global(void) {
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

static inline void handle_load_i32_const(void) {
    uint8_t reg = READ_BYTE();
    uint16_t constantIndex = READ_SHORT();
    vm.typed_regs.i32_regs[reg] = READ_CONSTANT(constantIndex).as.i32;
    vm.typed_regs.reg_types[reg] = REG_TYPE_I32;
}

static inline void handle_load_i64_const(void) {
    uint8_t reg = READ_BYTE();
    uint16_t constantIndex = READ_SHORT();
    vm.typed_regs.i64_regs[reg] = READ_CONSTANT(constantIndex).as.i64;
    vm.typed_regs.reg_types[reg] = REG_TYPE_I64;
}

static inline void handle_load_u32_const(void) {
    uint8_t reg = READ_BYTE();
    uint16_t constantIndex = READ_SHORT();
    vm.typed_regs.u32_regs[reg] = READ_CONSTANT(constantIndex).as.u32;
    vm.typed_regs.reg_types[reg] = REG_TYPE_U32;
}

static inline void handle_load_u64_const(void) {
    uint8_t reg = READ_BYTE();
    uint16_t constantIndex = READ_SHORT();
    vm.typed_regs.u64_regs[reg] = READ_CONSTANT(constantIndex).as.u64;
    vm.typed_regs.reg_types[reg] = REG_TYPE_U64;
}

static inline void handle_load_f64_const(void) {
    uint8_t reg = READ_BYTE();
    uint16_t constantIndex = READ_SHORT();
    vm.typed_regs.f64_regs[reg] = READ_CONSTANT(constantIndex).as.f64;
    vm.typed_regs.reg_types[reg] = REG_TYPE_F64;
}

// ====== Typed Move Operation Handlers ======

static inline void handle_move_i32(void) {
    uint8_t dst = READ_BYTE();
    uint8_t src = READ_BYTE();
    vm.typed_regs.i32_regs[dst] = vm.typed_regs.i32_regs[src];
    vm.typed_regs.reg_types[dst] = REG_TYPE_I32;
}

static inline void handle_move_i64(void) {
    uint8_t dst = READ_BYTE();
    uint8_t src = READ_BYTE();
    vm.typed_regs.i64_regs[dst] = vm.typed_regs.i64_regs[src];
    vm.typed_regs.reg_types[dst] = REG_TYPE_I64;
}

static inline void handle_move_u32(void) {
    uint8_t dst = READ_BYTE();
    uint8_t src = READ_BYTE();
    vm.typed_regs.u32_regs[dst] = vm.typed_regs.u32_regs[src];
    vm.typed_regs.reg_types[dst] = REG_TYPE_U32;
}

static inline void handle_move_u64(void) {
    uint8_t dst = READ_BYTE();
    uint8_t src = READ_BYTE();
    vm.typed_regs.u64_regs[dst] = vm.typed_regs.u64_regs[src];
    vm.typed_regs.reg_types[dst] = REG_TYPE_U64;
}

static inline void handle_move_f64(void) {
    uint8_t dst = READ_BYTE();
    uint8_t src = READ_BYTE();
    vm.typed_regs.f64_regs[dst] = vm.typed_regs.f64_regs[src];
    vm.typed_regs.reg_types[dst] = REG_TYPE_F64;
}

// ====== Print Operation Handlers ======

static inline void handle_print(void) {
    uint8_t reg = READ_BYTE();
    builtin_print(&vm.registers[reg], 1, true, NULL);
}

static inline void handle_print_multi(void) {
    uint8_t first = READ_BYTE();
    uint8_t count = READ_BYTE();
    uint8_t nl = READ_BYTE();
    builtin_print(&vm.registers[first], count, nl != 0, NULL);
}

static inline void handle_print_multi_sep(void) {
    uint8_t first = READ_BYTE();
    uint8_t count = READ_BYTE();
    uint8_t sep_reg = READ_BYTE();
    uint8_t nl = READ_BYTE();
    builtin_print_with_sep_value(&vm.registers[first], count, nl != 0, vm.registers[sep_reg]);
}

static inline void handle_print_no_nl(void) {
    uint8_t reg = READ_BYTE();
    builtin_print(&vm.registers[reg], 1, false, NULL);
}

// ====== Utility Operation Handlers ======

static inline void handle_halt(void) {
    // Halt operation - handled by dispatch loop
    // This is a placeholder for consistency
}

static inline void handle_time_stamp(void) {
    // Time stamp operation - implementation depends on VM requirements
    // This is a placeholder for consistency
}