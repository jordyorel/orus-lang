/*
 * Orus Language Project
 * ---------------------------------------------------------------------------
 * File: include/compiler/register_allocator.h
 * Author: Jordy Orel KONDA
 * Copyright (c) 2025 Jordy Orel KONDA
 * License: MIT License (see LICENSE file in the project root)
 * Description: Declares register allocation routines for mapping compiler temporaries
 *              to VM registers.
 */

#ifndef REGISTER_ALLOCATOR_H
#define REGISTER_ALLOCATOR_H

#include <stdbool.h>
#include <stdint.h>

#include "vm/vm.h"  // For RegisterType enum

#ifndef REGISTER_ALLOCATOR_DEBUG
#define REGISTER_ALLOCATOR_DEBUG 0
#endif

// Multi-pass compiler register ranges (mirrors VM register layout)
#define MP_GLOBAL_REG_START    GLOBAL_REG_START
#define MP_GLOBAL_REG_END      (GLOBAL_REG_START + GLOBAL_REGISTERS - 1)
#define MP_FRAME_REG_START     FRAME_REG_START
#define MP_FRAME_REG_END       (FRAME_REG_START + FRAME_REGISTERS - 1)
#define MP_TEMP_REG_START      TEMP_REG_START
#define MP_TEMP_REG_END        (TEMP_REG_START + TEMP_REGISTERS - 1)
#define MP_MODULE_REG_START    MODULE_REG_START
#define MP_MODULE_REG_END      (MODULE_REG_START + MODULE_REGISTERS - 1)

#define MP_SCOPE_LEVEL_COUNT   (TEMP_REGISTERS / 8)

// Register allocation strategies
typedef enum RegisterStrategy {
    REG_STRATEGY_STANDARD,    // Use vm.registers[] with OP_*_R instructions
    REG_STRATEGY_TYPED,       // Use vm.typed_regs.* with OP_*_TYPED instructions
    REG_STRATEGY_AUTO         // Compiler decides based on usage pattern
} RegisterStrategy;

// Kinds of register banks managed by the dual allocator
typedef enum RegisterBankKind {
    REG_BANK_INVALID = -1,
    REG_BANK_TYPED_I32 = 0,
    REG_BANK_TYPED_I64,
    REG_BANK_TYPED_F64,
    REG_BANK_TYPED_U32,
    REG_BANK_TYPED_U64,
    REG_BANK_TYPED_BOOL,
    REG_BANK_COUNT
} RegisterBankKind;

// Register allocation record
typedef struct RegisterAllocation {
    int logical_id;              // R0-R255 logical register ID (for standard)
    RegisterType physical_type;  // Which physical bank (REG_TYPE_I32, etc.)
    int physical_id;             // Physical register within typed bank (0-255)
    RegisterStrategy strategy;   // Which instruction set to use
    bool is_active;              // Whether allocation is currently active
} RegisterAllocation;

// Opaque dual allocator handle
typedef struct DualRegisterAllocator DualRegisterAllocator;

// ====== COMPILER FACADE HELPERS ======

// Initialization and cleanup
DualRegisterAllocator* compiler_create_allocator(void);
void compiler_destroy_allocator(DualRegisterAllocator* allocator);

// Standard register allocation helpers
int compiler_alloc_global(DualRegisterAllocator* allocator);
int compiler_alloc_frame(DualRegisterAllocator* allocator);
int compiler_alloc_temp(DualRegisterAllocator* allocator);
int compiler_alloc_consecutive_temps(DualRegisterAllocator* allocator, int count);
int compiler_alloc_module(DualRegisterAllocator* allocator);

// Scope-aware helpers
int compiler_alloc_scoped_temp(DualRegisterAllocator* allocator, int scope_level);
void compiler_enter_scope(DualRegisterAllocator* allocator);
void compiler_exit_scope(DualRegisterAllocator* allocator);
void compiler_free_scoped_temp(DualRegisterAllocator* allocator, int reg, int scope_level);

// Register release helpers
void compiler_free_register(DualRegisterAllocator* allocator, int reg);
void compiler_free_temp(DualRegisterAllocator* allocator, int reg);

// Residency hints
void compiler_set_typed_residency_hint(DualRegisterAllocator* allocator, int reg, bool persistent);
bool compiler_has_typed_residency_hint(const DualRegisterAllocator* allocator, int reg);

// Utilities
void compiler_reserve_global(DualRegisterAllocator* allocator, int reg);
void compiler_reset_frame_registers(DualRegisterAllocator* allocator);
bool compiler_is_register_free(DualRegisterAllocator* allocator, int reg);
const char* compiler_register_type_name(int reg);

// Typed/dual allocation helpers
RegisterAllocation* compiler_alloc_typed(DualRegisterAllocator* allocator, RegisterBankKind bank_kind);
RegisterAllocation* compiler_alloc_smart(DualRegisterAllocator* allocator,
                                         RegisterType type,
                                         bool is_arithmetic_hot_path);
void compiler_free_allocation(DualRegisterAllocator* allocator, RegisterAllocation* allocation);

// Diagnostics
bool is_arithmetic_heavy_context(DualRegisterAllocator* allocator);
const char* register_strategy_name(RegisterStrategy strategy);
void compiler_print_register_allocation_stats(DualRegisterAllocator* allocator);

#endif // REGISTER_ALLOCATOR_H

