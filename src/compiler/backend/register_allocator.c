//  Orus Language Project
//  ---------------------------------------------------------------------------
//  File: src/compiler/backend/register_allocator.c
//  Author: Jordy Orel KONDA
//  Copyright (c) 2023 Jordy Orel KONDA
//  License: MIT License (see LICENSE file in the project root)
//  Description: Implements register allocation strategies that map SSA values to VM egisters.


#include "compiler/register_allocator.h"
#include "internal/logging.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Internal legacy allocator used by the dual system facade.
typedef struct MultiPassRegisterAllocator {
    bool global_regs[GLOBAL_REGISTERS];
    bool frame_regs[FRAME_REGISTERS];
    bool temp_regs[TEMP_REGISTERS];
    bool module_regs[MODULE_REGISTERS];

    bool scope_temp_regs[MP_SCOPE_LEVEL_COUNT][8];
    int current_scope_level;

    bool typed_residency_hint[REGISTER_COUNT];

    int next_global;
    int next_frame;
    int next_temp;
    int next_module;

    int temp_stack[TEMP_REGISTERS];
    int temp_stack_top;
} MultiPassRegisterAllocator;

typedef struct RegisterBank {
    RegisterBankKind kind;
    RegisterType physical_type;
    const char* name;
    bool regs[256];
} RegisterBank;

typedef struct DualRegisterAllocator {
    MultiPassRegisterAllocator* legacy_allocator;

    bool standard_regs[256];

    RegisterBank banks[REG_BANK_COUNT];

    RegisterAllocation allocations[256];
    int allocation_count;

    int arithmetic_operation_count;
    bool prefer_typed_registers;
} DualRegisterAllocator;

typedef struct RegisterBankDefinition {
    RegisterBankKind kind;
    RegisterType type;
    const char* name;
} RegisterBankDefinition;

static const RegisterBankDefinition REGISTER_BANK_DEFINITIONS[REG_BANK_COUNT] = {
    [REG_BANK_TYPED_I32] = {REG_BANK_TYPED_I32, REG_TYPE_I32, "i32"},
    [REG_BANK_TYPED_I64] = {REG_BANK_TYPED_I64, REG_TYPE_I64, "i64"},
    [REG_BANK_TYPED_F64] = {REG_BANK_TYPED_F64, REG_TYPE_F64, "f64"},
    [REG_BANK_TYPED_U32] = {REG_BANK_TYPED_U32, REG_TYPE_U32, "u32"},
    [REG_BANK_TYPED_U64] = {REG_BANK_TYPED_U64, REG_TYPE_U64, "u64"},
    [REG_BANK_TYPED_BOOL] = {REG_BANK_TYPED_BOOL, REG_TYPE_BOOL, "bool"},
};

// Logging helpers scoped to the register allocator implementation
#ifndef REGISTER_ALLOCATOR_DEBUG
#define REGISTER_ALLOCATOR_DEBUG 0
#endif

#if REGISTER_ALLOCATOR_DEBUG
#define REGISTER_ALLOCATOR_LOG(fmt, ...)   LOG_DEBUG(fmt, ##__VA_ARGS__)
#define REGISTER_ALLOCATOR_WARN(fmt, ...)  LOG_WARN(fmt, ##__VA_ARGS__)
#define REGISTER_ALLOCATOR_ERROR(fmt, ...) LOG_ERROR(fmt, ##__VA_ARGS__)
#else
#define REGISTER_ALLOCATOR_LOG(fmt, ...)   ((void)0)
#define REGISTER_ALLOCATOR_WARN(fmt, ...)  LOG_WARN(fmt, ##__VA_ARGS__)
#define REGISTER_ALLOCATOR_ERROR(fmt, ...) LOG_ERROR(fmt, ##__VA_ARGS__)
#endif

// Forward declarations for internal helpers used before their definitions.
static void mp_free_temp_register(MultiPassRegisterAllocator* allocator, int reg);
static bool mp_has_typed_residency_hint(const MultiPassRegisterAllocator* allocator, int reg);
static RegisterAllocation* allocate_standard_register(DualRegisterAllocator* allocator,
                                                      RegisterType type,
                                                      int scope_preference);
static void init_register_banks(DualRegisterAllocator* allocator);
static RegisterBank* get_register_bank(DualRegisterAllocator* allocator, RegisterBankKind kind);
static RegisterBank* find_bank_for_type(DualRegisterAllocator* allocator, RegisterType type);
static int allocate_physical_from_bank(RegisterBank* bank);
static void free_physical_from_bank(RegisterBank* bank, int physical_id);
static RegisterAllocation* allocate_register_from_bank(DualRegisterAllocator* allocator,
                                                       RegisterBankKind kind);
static RegisterBankKind register_bank_kind_from_type(RegisterType type);

// ====== DUAL REGISTER SYSTEM IMPLEMENTATION ======

MultiPassRegisterAllocator* init_mp_register_allocator(void) {
    MultiPassRegisterAllocator* allocator = malloc(sizeof(MultiPassRegisterAllocator));
    if (!allocator) return NULL;
    
    // Initialize all registers as free
    memset(allocator->global_regs, false, sizeof(allocator->global_regs));
    memset(allocator->frame_regs, false, sizeof(allocator->frame_regs));
    memset(allocator->temp_regs, false, sizeof(allocator->temp_regs));
    memset(allocator->module_regs, false, sizeof(allocator->module_regs));
    memset(allocator->typed_residency_hint, false, sizeof(allocator->typed_residency_hint));
    
    // Initialize scope-aware temp registers
    memset(allocator->scope_temp_regs, false, sizeof(allocator->scope_temp_regs));
    allocator->current_scope_level = 0;
    
    // Initialize allocation pointers
    allocator->next_global = MP_GLOBAL_REG_START;
    allocator->next_frame = MP_FRAME_REG_START;
    allocator->next_temp = MP_TEMP_REG_START;
    allocator->next_module = MP_MODULE_REG_START;
    
    // Initialize temp stack for register reuse
    allocator->temp_stack_top = -1;
    
    return allocator;
}

void free_mp_register_allocator(MultiPassRegisterAllocator* allocator) {
    if (allocator) {
        free(allocator);
    }
}

int mp_allocate_global_register(MultiPassRegisterAllocator* allocator) {
    if (!allocator) return -1;

    // Find next free global register
    for (int i = 0; i < GLOBAL_REGISTERS; i++) {
        if (!allocator->global_regs[i]) {
            allocator->global_regs[i] = true;
            return MP_GLOBAL_REG_START + i;
        }
    }

    // No free global registers
    REGISTER_ALLOCATOR_WARN("[REGISTER_ALLOCATOR] Warning: No free global registers\n");
    return -1;
}

bool mp_reserve_global_register(MultiPassRegisterAllocator* allocator, int reg) {
    if (!allocator) {
        return false;
    }

    if (reg < MP_GLOBAL_REG_START || reg > MP_GLOBAL_REG_END) {
        return false;
    }

    int index = reg - MP_GLOBAL_REG_START;
    allocator->global_regs[index] = true;
    return true;
}

int mp_allocate_frame_register(MultiPassRegisterAllocator* allocator) {
    if (!allocator) return -1;
    
    // Find next free frame register starting from R64 (MP_FRAME_REG_START)
    // Parameter registers occupy the upper range (R240-R255), so we start at 64 for locals
    for (int i = 0; i < FRAME_REGISTERS; i++) {  // Frame register window
        if (!allocator->frame_regs[i]) {
            allocator->frame_regs[i] = true;
            return MP_FRAME_REG_START + i;  // R64 + i
        }
    }
    
    // No free frame registers
    REGISTER_ALLOCATOR_WARN("[REGISTER_ALLOCATOR] Warning: No free frame registers\n");
    return -1;
}

// Reset frame registers for function compilation isolation
void mp_reset_frame_registers(MultiPassRegisterAllocator* allocator) {
    if (!allocator) return;
    
    // Reset all frame register tracking
    for (int i = 0; i < FRAME_REGISTERS; i++) {
        allocator->frame_regs[i] = false;
    }

    // Reset frame allocation counter
    allocator->next_frame = MP_FRAME_REG_START;
    
    REGISTER_ALLOCATOR_LOG("[REGISTER_ALLOCATOR] Reset frame registers for new function\n");
}

int mp_allocate_temp_register(MultiPassRegisterAllocator* allocator) {
    if (!allocator) return -1;

    // Find next free temp register (don't reuse from stack for better register isolation)
    // This prevents register conflicts in nested expressions
    for (int i = 0; i < TEMP_REGISTERS; i++) {
        if (!allocator->temp_regs[i]) {
            allocator->temp_regs[i] = true;
            int reg = MP_TEMP_REG_START + i;
            REGISTER_ALLOCATOR_LOG("[REGISTER_ALLOCATOR] Allocated temp register R%d (sequential allocation)\n", reg);
            return reg;
        }
    }

    // If no sequential register available, try to reuse from stack
    if (allocator->temp_stack_top >= 0) {
        int reused_reg = allocator->temp_stack[allocator->temp_stack_top--];
        REGISTER_ALLOCATOR_LOG("[REGISTER_ALLOCATOR] Reusing temp register R%d (from stack)\n", reused_reg);
        return reused_reg;
    }

    // No free temp registers
    REGISTER_ALLOCATOR_ERROR("[REGISTER_ALLOCATOR] Error: No free temp registers (register spill needed)\n");
    return -1;
}

int mp_allocate_consecutive_temp_registers(MultiPassRegisterAllocator* allocator, int count) {
    if (!allocator || count <= 0 || count > TEMP_REGISTERS) {
        REGISTER_ALLOCATOR_ERROR("[REGISTER_ALLOCATOR] Error: Invalid consecutive allocation request (%d)\n", count);
        return -1;
    }

    for (int start = 0; start <= TEMP_REGISTERS - count; start++) {
        bool available = true;
        for (int offset = 0; offset < count; offset++) {
            if (allocator->temp_regs[start + offset]) {
                available = false;
                break;
            }
        }

        if (available) {
            for (int offset = 0; offset < count; offset++) {
                allocator->temp_regs[start + offset] = true;
            }
            int reg = MP_TEMP_REG_START + start;
            REGISTER_ALLOCATOR_LOG("[REGISTER_ALLOCATOR] Allocated consecutive temp registers R%d-R%d\n",
                   reg, reg + count - 1);
            return reg;
        }
    }

    REGISTER_ALLOCATOR_ERROR("[REGISTER_ALLOCATOR] Error: Unable to allocate %d consecutive temp registers\n", count);
    return -1;
}

int mp_allocate_module_register(MultiPassRegisterAllocator* allocator) {
    if (!allocator) return -1;
    
    // Find next free module register
    for (int i = 0; i < MODULE_REGISTERS; i++) {
        if (!allocator->module_regs[i]) {
            allocator->module_regs[i] = true;
            return MP_MODULE_REG_START + i;
        }
    }
    
    // No free module registers
    REGISTER_ALLOCATOR_WARN("[REGISTER_ALLOCATOR] Warning: No free module registers\n");
    return -1;
}

// ============= SCOPE-AWARE REGISTER ALLOCATION (NEW) =============

int mp_allocate_scoped_temp_register(MultiPassRegisterAllocator* allocator, int scope_level) {
    if (!allocator) return -1;
    if (scope_level < 0 || scope_level >= MP_SCOPE_LEVEL_COUNT) {
        REGISTER_ALLOCATOR_ERROR("[REGISTER_ALLOCATOR] Error: Invalid scope level %d (must be 0-%d)\n",
               scope_level, MP_SCOPE_LEVEL_COUNT - 1);
        return -1;
    }
    
    // Each scope level gets 8 registers: R192+scope_level*8 to R192+scope_level*8+7
    int base_reg = MP_TEMP_REG_START + (scope_level * 8);
    
    // Find next free register in this scope level
    for (int i = 0; i < 8; i++) {
        if (!allocator->scope_temp_regs[scope_level][i]) {
            allocator->scope_temp_regs[scope_level][i] = true;
            int reg = base_reg + i;
            REGISTER_ALLOCATOR_LOG("[REGISTER_ALLOCATOR] Allocated scoped temp register R%d (scope level %d, slot %d)\n", 
                   reg, scope_level, i);
            return reg;
        }
    }
    
    // No free registers in this scope level
    REGISTER_ALLOCATOR_WARN("[REGISTER_ALLOCATOR] Warning: No free temp registers in scope level %d\n", scope_level);
    return -1;
}

void mp_enter_scope(MultiPassRegisterAllocator* allocator) {
    if (!allocator) return;
    
    if (allocator->current_scope_level < MP_SCOPE_LEVEL_COUNT - 1) {
        allocator->current_scope_level++;
        REGISTER_ALLOCATOR_LOG("[REGISTER_ALLOCATOR] Entered scope level %d\n", allocator->current_scope_level);
    } else {
        REGISTER_ALLOCATOR_WARN("[REGISTER_ALLOCATOR] Warning: Maximum scope depth (%d) reached\n",
               MP_SCOPE_LEVEL_COUNT - 1);
    }
}

void mp_exit_scope(MultiPassRegisterAllocator* allocator) {
    if (!allocator) return;
    
    if (allocator->current_scope_level > 0) {
        // Free all registers in current scope level
        int scope = allocator->current_scope_level;
        for (int i = 0; i < 8; i++) {
            allocator->scope_temp_regs[scope][i] = false;
        }

        REGISTER_ALLOCATOR_LOG("[REGISTER_ALLOCATOR] Exited scope level %d (freed %d registers)\n", scope, 8);
        allocator->current_scope_level--;
    } else {
        REGISTER_ALLOCATOR_WARN("[REGISTER_ALLOCATOR] Warning: Already at root scope level\n");
    }
}

void mp_free_scoped_temp_register(MultiPassRegisterAllocator* allocator, int reg, int scope_level) {
    if (!allocator) return;
    if (scope_level < 0 || scope_level >= MP_SCOPE_LEVEL_COUNT) return;

    int base_reg = MP_TEMP_REG_START + (scope_level * 8);
    if (reg >= base_reg && reg < base_reg + 8) {
        int slot = reg - base_reg;
        allocator->scope_temp_regs[scope_level][slot] = false;
        REGISTER_ALLOCATOR_LOG("[REGISTER_ALLOCATOR] Freed scoped temp register R%d (scope level %d, slot %d)\n", 
               reg, scope_level, slot);
    }
}

void mp_free_register(MultiPassRegisterAllocator* allocator, int reg) {
    if (!allocator) return;

    if (mp_has_typed_residency_hint(allocator, reg)) {
        REGISTER_ALLOCATOR_LOG("[REGISTER_ALLOCATOR] Skipped freeing R%d due to typed residency hint\n", reg);
        return;
    }

    // Determine register type and free accordingly
    if (reg >= MP_GLOBAL_REG_START && reg <= MP_GLOBAL_REG_END) {
        int index = reg - MP_GLOBAL_REG_START;
        allocator->global_regs[index] = false;
        REGISTER_ALLOCATOR_LOG("[REGISTER_ALLOCATOR] Freed global register R%d\n", reg);
    }
    else if (reg >= MP_FRAME_REG_START && reg <= MP_FRAME_REG_END) {
        int index = reg - MP_FRAME_REG_START;
        allocator->frame_regs[index] = false;
        REGISTER_ALLOCATOR_LOG("[REGISTER_ALLOCATOR] Freed frame register R%d\n", reg);
    }
    else if (reg >= MP_TEMP_REG_START && reg <= MP_TEMP_REG_END) {
        mp_free_temp_register(allocator, reg);
    }
    else if (reg >= MP_MODULE_REG_START && reg <= MP_MODULE_REG_END) {
        int index = reg - MP_MODULE_REG_START;
        allocator->module_regs[index] = false;
        REGISTER_ALLOCATOR_LOG("[REGISTER_ALLOCATOR] Freed module register R%d\n", reg);
    }
    else {
        REGISTER_ALLOCATOR_WARN("[REGISTER_ALLOCATOR] Warning: Invalid register R%d cannot be freed\n", reg);
    }
}

static void mp_free_temp_register(MultiPassRegisterAllocator* allocator, int reg) {
    if (!allocator) {
        return;
    }

    if (mp_has_typed_residency_hint(allocator, reg)) {
        REGISTER_ALLOCATOR_LOG("[REGISTER_ALLOCATOR] Skipped freeing temp R%d due to typed residency hint\n", reg);
        return;
    }

    if (reg < MP_TEMP_REG_START || reg > MP_TEMP_REG_END) {
        REGISTER_ALLOCATOR_WARN("[REGISTER_ALLOCATOR] Warning: Invalid temp register R%d\n", reg);
        return;
    }

    int index = reg - MP_TEMP_REG_START;
    allocator->temp_regs[index] = false;
    
    // Add to reuse stack (LIFO for better cache locality)
    if (allocator->temp_stack_top < TEMP_REGISTERS - 1) {
        allocator->temp_stack[++allocator->temp_stack_top] = reg;
        REGISTER_ALLOCATOR_LOG("[REGISTER_ALLOCATOR] Freed temp register R%d (added to reuse stack)\n", reg);
    } else {
        REGISTER_ALLOCATOR_LOG("[REGISTER_ALLOCATOR] Freed temp register R%d (reuse stack full)\n", reg);
    }
}

void mp_set_typed_residency_hint(MultiPassRegisterAllocator* allocator, int reg, bool persistent) {
    if (!allocator || reg < 0 || reg >= REGISTER_COUNT) {
        return;
    }
    allocator->typed_residency_hint[reg] = persistent;
}

static bool mp_has_typed_residency_hint(const MultiPassRegisterAllocator* allocator, int reg) {
    if (!allocator || reg < 0 || reg >= REGISTER_COUNT) {
        return false;
    }
    return allocator->typed_residency_hint[reg];
}

bool mp_is_register_free(MultiPassRegisterAllocator* allocator, int reg) {
    if (!allocator) return false;
    
    if (reg >= MP_GLOBAL_REG_START && reg <= MP_GLOBAL_REG_END) {
        return !allocator->global_regs[reg - MP_GLOBAL_REG_START];
    }
    else if (reg >= MP_FRAME_REG_START && reg <= MP_FRAME_REG_END) {
        return !allocator->frame_regs[reg - MP_FRAME_REG_START];
    }
    else if (reg >= MP_TEMP_REG_START && reg <= MP_TEMP_REG_END) {
        return !allocator->temp_regs[reg - MP_TEMP_REG_START];
    }
    else if (reg >= MP_MODULE_REG_START && reg <= MP_MODULE_REG_END) {
        return !allocator->module_regs[reg - MP_MODULE_REG_START];
    }
    
    return false;  // Invalid register
}

const char* mp_register_type_name(int reg) {
    if (reg >= MP_GLOBAL_REG_START && reg <= MP_GLOBAL_REG_END) return "GLOBAL";
    if (reg >= MP_FRAME_REG_START && reg <= MP_FRAME_REG_END) return "FRAME";  
    if (reg >= MP_TEMP_REG_START && reg <= MP_TEMP_REG_END) return "TEMP";
    if (reg >= MP_MODULE_REG_START && reg <= MP_MODULE_REG_END) return "MODULE";
    return "INVALID";
}

// ====== DUAL REGISTER ALLOCATOR IMPLEMENTATION ======

DualRegisterAllocator* init_dual_register_allocator(void) {
    DualRegisterAllocator* allocator = malloc(sizeof(DualRegisterAllocator));
    if (!allocator) return NULL;
    
    // Initialize legacy allocator for compatibility
    allocator->legacy_allocator = init_mp_register_allocator();
    if (!allocator->legacy_allocator) {
        free(allocator);
        return NULL;
    }
    
    // Initialize standard register tracking (R0-R255)
    memset(allocator->standard_regs, false, sizeof(allocator->standard_regs));

    // Initialize typed register banks
    init_register_banks(allocator);
    
    // Initialize allocation tracking
    memset(allocator->allocations, 0, sizeof(allocator->allocations));
    allocator->allocation_count = 0;
    
    // Initialize performance heuristics
    allocator->arithmetic_operation_count = 0;
    allocator->prefer_typed_registers = true;  // Start with optimization enabled
    
    REGISTER_ALLOCATOR_LOG("[DUAL_REGISTER_ALLOCATOR] Initialized with typed register optimization enabled\n");
    return allocator;
}

void free_dual_register_allocator(DualRegisterAllocator* allocator) {
    if (!allocator) return;

    free_mp_register_allocator(allocator->legacy_allocator);
    free(allocator);
}

static void init_register_banks(DualRegisterAllocator* allocator) {
    if (!allocator) {
        return;
    }

    for (int i = 0; i < REG_BANK_COUNT; i++) {
        const RegisterBankDefinition* def = &REGISTER_BANK_DEFINITIONS[i];
        RegisterBank* bank = &allocator->banks[i];
        bank->kind = def->kind;
        bank->physical_type = def->type;
        bank->name = def->name;
        memset(bank->regs, 0, sizeof(bank->regs));
    }
}

static RegisterBank* get_register_bank(DualRegisterAllocator* allocator, RegisterBankKind kind) {
    if (!allocator || kind < 0 || kind >= REG_BANK_COUNT) {
        return NULL;
    }
    return &allocator->banks[kind];
}

static RegisterBank* find_bank_for_type(DualRegisterAllocator* allocator, RegisterType type) {
    if (!allocator) {
        return NULL;
    }

    for (int i = 0; i < REG_BANK_COUNT; i++) {
        if (allocator->banks[i].physical_type == type) {
            return &allocator->banks[i];
        }
    }
    return NULL;
}

static RegisterBankKind register_bank_kind_from_type(RegisterType type) {
    for (int i = 0; i < REG_BANK_COUNT; i++) {
        if (REGISTER_BANK_DEFINITIONS[i].type == type) {
            return REGISTER_BANK_DEFINITIONS[i].kind;
        }
    }
    return REG_BANK_INVALID;
}

static int allocate_physical_from_bank(RegisterBank* bank) {
    if (!bank) {
        return -1;
    }

    for (int i = 0; i < 256; i++) {
        if (!bank->regs[i]) {
            bank->regs[i] = true;
            return i;
        }
    }
    return -1;
}

static void free_physical_from_bank(RegisterBank* bank, int physical_id) {
    if (!bank) {
        return;
    }

    if (physical_id >= 0 && physical_id < 256) {
        bank->regs[physical_id] = false;
    }
}

static RegisterAllocation* allocate_register_from_bank(DualRegisterAllocator* allocator,
                                                       RegisterBankKind kind) {
    RegisterBank* bank = get_register_bank(allocator, kind);
    if (!bank) {
        return NULL;
    }

    int physical_id = allocate_physical_from_bank(bank);
    if (physical_id == -1) {
        return NULL;
    }

    if (allocator->allocation_count >= 256) {
        REGISTER_ALLOCATOR_WARN("[DUAL_REGISTER_ALLOCATOR] Warning: Maximum allocations reached\n");
        free_physical_from_bank(bank, physical_id);
        return NULL;
    }

    RegisterAllocation* allocation = &allocator->allocations[allocator->allocation_count++];
    allocation->logical_id = -1;
    allocation->physical_type = bank->physical_type;
    allocation->physical_id = physical_id;
    allocation->strategy = REG_STRATEGY_TYPED;
    allocation->is_active = true;

    REGISTER_ALLOCATOR_LOG(
        "[DUAL_REGISTER_ALLOCATOR] Allocated typed register bank=%s (type=%d) physical_id=%d\n",
        bank->name,
        bank->physical_type,
        physical_id);

    return allocation;
}

// Helper function to check if a register type is numeric and benefits from typed registers
static bool is_numeric_type_for_typed_regs(RegisterType type) {
    return register_bank_kind_from_type(type) != REG_BANK_INVALID;
}

static RegisterAllocation* allocate_typed_register(DualRegisterAllocator* allocator,
                                                   RegisterBankKind bank_kind) {
    if (!allocator) {
        return NULL;
    }

    RegisterBank* bank = get_register_bank(allocator, bank_kind);
    if (!bank) {
        return NULL;
    }

    RegisterAllocation* allocation = allocate_register_from_bank(allocator, bank_kind);
    if (allocation) {
        return allocation;
    }

    REGISTER_ALLOCATOR_WARN(
        "[DUAL_REGISTER_ALLOCATOR] No free typed registers in bank %s (type=%d), falling back to standard\n",
        bank->name,
        bank->physical_type);
    return allocate_standard_register(allocator, bank->physical_type, MP_TEMP_REG_START);
}

static RegisterAllocation* allocate_standard_register(DualRegisterAllocator* allocator,
                                                      RegisterType type,
                                                      int scope_preference) {
    if (!allocator) return NULL;
    
    // Use legacy allocator to get a standard register based on scope preference
    int logical_id = -1;
    
    if (scope_preference >= MP_GLOBAL_REG_START && scope_preference <= MP_GLOBAL_REG_END) {
        logical_id = mp_allocate_global_register(allocator->legacy_allocator);
    } else if (scope_preference >= MP_FRAME_REG_START && scope_preference <= MP_FRAME_REG_END) {
        logical_id = mp_allocate_frame_register(allocator->legacy_allocator);
    } else if (scope_preference >= MP_TEMP_REG_START && scope_preference <= MP_TEMP_REG_END) {
        logical_id = mp_allocate_temp_register(allocator->legacy_allocator);
    } else {
        // Default to temp register
        logical_id = mp_allocate_temp_register(allocator->legacy_allocator);
    }
    
    if (logical_id == -1) {
        REGISTER_ALLOCATOR_ERROR("[DUAL_REGISTER_ALLOCATOR] Failed to allocate standard register\n");
        return NULL;
    }
    
    // Mark as used in standard register tracking
    if (logical_id >= 0 && logical_id < 256) {
        allocator->standard_regs[logical_id] = true;
    }
    
    // Create allocation record
    if (allocator->allocation_count >= 256) {
        REGISTER_ALLOCATOR_WARN("[DUAL_REGISTER_ALLOCATOR] Warning: Maximum allocations reached\n");
        return NULL;
    }
    
    RegisterAllocation* allocation = &allocator->allocations[allocator->allocation_count++];
    allocation->logical_id = logical_id;
    allocation->physical_type = type;
    allocation->physical_id = -1;  // Not applicable for standard registers
    allocation->strategy = REG_STRATEGY_STANDARD;
    allocation->is_active = true;
    
    REGISTER_ALLOCATOR_LOG("[DUAL_REGISTER_ALLOCATOR] Allocated standard register: logical_id=%d, type=%d\n", logical_id, type);
    return allocation;
}

RegisterAllocation* allocate_register_smart(DualRegisterAllocator* allocator, RegisterType type, bool is_arithmetic_hot_path) {
    if (!allocator) return NULL;
    
    // Increment arithmetic operation counter for heuristics
    if (is_arithmetic_hot_path) {
        allocator->arithmetic_operation_count++;
    }
    
    // Smart decision: use typed registers for arithmetic-heavy numeric operations
    if (is_arithmetic_hot_path &&
        is_numeric_type_for_typed_regs(type) &&
        allocator->prefer_typed_registers) {

        RegisterBankKind bank_kind = register_bank_kind_from_type(type);
        if (bank_kind != REG_BANK_INVALID) {
            RegisterAllocation* typed_alloc = allocate_typed_register(allocator, bank_kind);
            if (typed_alloc) {
                REGISTER_ALLOCATOR_LOG("[DUAL_REGISTER_ALLOCATOR] Smart allocation chose TYPED register for performance\n");
                return typed_alloc;
            }
        }

        // Fallback to standard if typed allocation failed
        REGISTER_ALLOCATOR_WARN("[DUAL_REGISTER_ALLOCATOR] Typed allocation failed, falling back to standard\n");
    }
    
    // Use standard registers for general-purpose or non-numeric operations
    int scope_preference = is_arithmetic_hot_path ? MP_TEMP_REG_START : MP_FRAME_REG_START;
    RegisterAllocation* standard_alloc = allocate_standard_register(allocator, type, scope_preference);
    
    if (standard_alloc) {
        REGISTER_ALLOCATOR_LOG("[DUAL_REGISTER_ALLOCATOR] Smart allocation chose STANDARD register\n");
    }
    
    return standard_alloc;
}

void free_register_allocation(DualRegisterAllocator* allocator, RegisterAllocation* allocation) {
    if (!allocator || !allocation || !allocation->is_active) return;
    
    if (allocation->strategy == REG_STRATEGY_TYPED) {
        RegisterBank* bank = find_bank_for_type(allocator, allocation->physical_type);
        if (bank) {
            free_physical_from_bank(bank, allocation->physical_id);
            REGISTER_ALLOCATOR_LOG(
                "[DUAL_REGISTER_ALLOCATOR] Freed typed register bank=%s (type=%d) physical_id=%d\n",
                bank->name,
                allocation->physical_type,
                allocation->physical_id);
        } else {
            REGISTER_ALLOCATOR_WARN(
                "[DUAL_REGISTER_ALLOCATOR] Unable to locate bank for typed register type=%d\n",
                allocation->physical_type);
        }
    } else if (allocation->strategy == REG_STRATEGY_STANDARD) {
        // Free standard register
        if (allocation->logical_id >= 0 && allocation->logical_id < 256) {
            allocator->standard_regs[allocation->logical_id] = false;
            mp_free_register(allocator->legacy_allocator, allocation->logical_id);
            REGISTER_ALLOCATOR_LOG("[DUAL_REGISTER_ALLOCATOR] Freed standard register: logical_id=%d\n", allocation->logical_id);
        }
    }
    
    allocation->is_active = false;
}

// Utility functions
bool is_arithmetic_heavy_context(DualRegisterAllocator* allocator) {
    if (!allocator) return false;
    return allocator->arithmetic_operation_count > 10;  // Heuristic threshold
}

const char* register_strategy_name(RegisterStrategy strategy) {
    switch (strategy) {
        case REG_STRATEGY_STANDARD: return "STANDARD";
        case REG_STRATEGY_TYPED:    return "TYPED";
        case REG_STRATEGY_AUTO:     return "AUTO";
        default:                    return "UNKNOWN";
    }
}

void print_register_allocation_stats(DualRegisterAllocator* allocator) {
    if (!allocator) return;
    
    int typed_count = 0, standard_count = 0;
    
    for (int i = 0; i < allocator->allocation_count; i++) {
        if (allocator->allocations[i].is_active) {
            if (allocator->allocations[i].strategy == REG_STRATEGY_TYPED) {
                typed_count++;
            } else {
                standard_count++;
            }
        }
    }
    
    REGISTER_ALLOCATOR_LOG("[DUAL_REGISTER_ALLOCATOR] Stats: %d typed, %d standard, %d arithmetic ops\n", 
           typed_count, standard_count, allocator->arithmetic_operation_count);
    
    // Explicitly mark variables as used to avoid compiler warnings
    (void)typed_count;
    (void)standard_count;
}

// ====== COMPILER FACADE HELPERS ======

DualRegisterAllocator* compiler_create_allocator(void) {
    return init_dual_register_allocator();
}

void compiler_destroy_allocator(DualRegisterAllocator* allocator) {
    free_dual_register_allocator(allocator);
}

int compiler_alloc_global(DualRegisterAllocator* allocator) {
    if (!allocator) return -1;
    return mp_allocate_global_register(allocator->legacy_allocator);
}

int compiler_alloc_frame(DualRegisterAllocator* allocator) {
    if (!allocator) return -1;
    return mp_allocate_frame_register(allocator->legacy_allocator);
}

int compiler_alloc_temp(DualRegisterAllocator* allocator) {
    if (!allocator) return -1;
    return mp_allocate_temp_register(allocator->legacy_allocator);
}

int compiler_alloc_consecutive_temps(DualRegisterAllocator* allocator, int count) {
    if (!allocator) return -1;
    return mp_allocate_consecutive_temp_registers(allocator->legacy_allocator, count);
}

int compiler_alloc_module(DualRegisterAllocator* allocator) {
    if (!allocator) return -1;
    return mp_allocate_module_register(allocator->legacy_allocator);
}

int compiler_alloc_scoped_temp(DualRegisterAllocator* allocator, int scope_level) {
    if (!allocator) return -1;
    return mp_allocate_scoped_temp_register(allocator->legacy_allocator, scope_level);
}

void compiler_enter_scope(DualRegisterAllocator* allocator) {
    if (!allocator) return;
    mp_enter_scope(allocator->legacy_allocator);
}

void compiler_exit_scope(DualRegisterAllocator* allocator) {
    if (!allocator) return;
    mp_exit_scope(allocator->legacy_allocator);
}

void compiler_free_scoped_temp(DualRegisterAllocator* allocator, int reg, int scope_level) {
    if (!allocator) return;
    mp_free_scoped_temp_register(allocator->legacy_allocator, reg, scope_level);
}

void compiler_free_register(DualRegisterAllocator* allocator, int reg) {
    if (!allocator) return;
    mp_free_register(allocator->legacy_allocator, reg);
}

void compiler_free_temp(DualRegisterAllocator* allocator, int reg) {
    if (!allocator) return;

    // Some call sites defensively attempt to free registers that ultimately
    // resolved to global or frame allocations (for example, when a previously
    // computed value is reused). Those registers are not managed by the temp
    // allocator, so forwarding them would trigger noisy warnings and offer no
    // benefit. Guard the call here so that only genuine temp registers are
    // passed through to the multipass allocator.
    if (reg < MP_TEMP_REG_START || reg > MP_TEMP_REG_END) {
        return;
    }

    mp_free_temp_register(allocator->legacy_allocator, reg);
}

void compiler_set_typed_residency_hint(DualRegisterAllocator* allocator, int reg, bool persistent) {
    if (!allocator) return;
    mp_set_typed_residency_hint(allocator->legacy_allocator, reg, persistent);
}

bool compiler_has_typed_residency_hint(const DualRegisterAllocator* allocator, int reg) {
    if (!allocator) return false;
    return mp_has_typed_residency_hint(allocator->legacy_allocator, reg);
}

void compiler_reserve_global(DualRegisterAllocator* allocator, int reg) {
    if (!allocator) return;
    mp_reserve_global_register(allocator->legacy_allocator, reg);
}

void compiler_reset_frame_registers(DualRegisterAllocator* allocator) {
    if (!allocator) return;
    mp_reset_frame_registers(allocator->legacy_allocator);
}

bool compiler_is_register_free(DualRegisterAllocator* allocator, int reg) {
    if (!allocator) return false;
    return mp_is_register_free(allocator->legacy_allocator, reg);
}

const char* compiler_register_type_name(int reg) {
    return mp_register_type_name(reg);
}

RegisterAllocation* compiler_alloc_typed(DualRegisterAllocator* allocator, RegisterBankKind bank_kind) {
    if (!allocator) return NULL;
    return allocate_typed_register(allocator, bank_kind);
}

RegisterAllocation* compiler_alloc_smart(DualRegisterAllocator* allocator,
                                         RegisterType type,
                                         bool is_arithmetic_hot_path) {
    if (!allocator) return NULL;
    return allocate_register_smart(allocator, type, is_arithmetic_hot_path);
}

void compiler_free_allocation(DualRegisterAllocator* allocator, RegisterAllocation* allocation) {
    if (!allocator) return;
    free_register_allocation(allocator, allocation);
}

void compiler_print_register_allocation_stats(DualRegisterAllocator* allocator) {
    print_register_allocation_stats(allocator);
}

