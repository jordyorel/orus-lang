#include "compiler/register_allocator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Disable all debug output for clean program execution
#define REGISTER_ALLOCATOR_DEBUG 0
#if REGISTER_ALLOCATOR_DEBUG == 0
#define printf(...) ((void)0)
#endif

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
    for (int i = 0; i < 64; i++) {
        if (!allocator->global_regs[i]) {
            allocator->global_regs[i] = true;
            return MP_GLOBAL_REG_START + i;
        }
    }

    // No free global registers
    printf("[REGISTER_ALLOCATOR] Warning: No free global registers\n");
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
    for (int i = 0; i < 128; i++) {  // R64-R191 range
        if (!allocator->frame_regs[i]) {
            allocator->frame_regs[i] = true;
            return MP_FRAME_REG_START + i;  // R64 + i
        }
    }
    
    // No free frame registers
    printf("[REGISTER_ALLOCATOR] Warning: No free frame registers\n");
    return -1;
}

// Reset frame registers for function compilation isolation
void mp_reset_frame_registers(MultiPassRegisterAllocator* allocator) {
    if (!allocator) return;
    
    // Reset all frame register tracking
    for (int i = 0; i < 128; i++) {
        allocator->frame_regs[i] = false;
    }
    
    // Reset frame allocation counter
    allocator->next_frame = MP_FRAME_REG_START;
    
    printf("[REGISTER_ALLOCATOR] Reset frame registers for new function\n");
}

int mp_allocate_temp_register(MultiPassRegisterAllocator* allocator) {
    if (!allocator) return -1;
    
    // Find next free temp register (don't reuse from stack for better register isolation)
    // This prevents register conflicts in nested expressions
    for (int i = 0; i < 48; i++) {
        if (!allocator->temp_regs[i]) {
            allocator->temp_regs[i] = true;
            int reg = MP_TEMP_REG_START + i;
            printf("[REGISTER_ALLOCATOR] Allocated temp register R%d (sequential allocation)\n", reg);
            return reg;
        }
    }
    
    // If no sequential register available, try to reuse from stack  
    if (allocator->temp_stack_top >= 0) {
        int reused_reg = allocator->temp_stack[allocator->temp_stack_top--];
        printf("[REGISTER_ALLOCATOR] Reusing temp register R%d (from stack)\n", reused_reg);
        return reused_reg;
    }
    
    // No free temp registers
    printf("[REGISTER_ALLOCATOR] Error: No free temp registers (register spill needed)\n");
    return -1;
}

int mp_allocate_module_register(MultiPassRegisterAllocator* allocator) {
    if (!allocator) return -1;
    
    // Find next free module register
    for (int i = 0; i < 16; i++) {
        if (!allocator->module_regs[i]) {
            allocator->module_regs[i] = true;
            return MP_MODULE_REG_START + i;
        }
    }
    
    // No free module registers
    printf("[REGISTER_ALLOCATOR] Warning: No free module registers\n");
    return -1;
}

// ============= SCOPE-AWARE REGISTER ALLOCATION (NEW) =============

int mp_allocate_scoped_temp_register(MultiPassRegisterAllocator* allocator, int scope_level) {
    if (!allocator) return -1;
    if (scope_level < 0 || scope_level >= 6) {
        printf("[REGISTER_ALLOCATOR] Error: Invalid scope level %d (must be 0-5)\n", scope_level);
        return -1;
    }
    
    // Each scope level gets 8 registers: R192+scope_level*8 to R192+scope_level*8+7
    int base_reg = MP_TEMP_REG_START + (scope_level * 8);
    
    // Find next free register in this scope level
    for (int i = 0; i < 8; i++) {
        if (!allocator->scope_temp_regs[scope_level][i]) {
            allocator->scope_temp_regs[scope_level][i] = true;
            int reg = base_reg + i;
            printf("[REGISTER_ALLOCATOR] Allocated scoped temp register R%d (scope level %d, slot %d)\n", 
                   reg, scope_level, i);
            return reg;
        }
    }
    
    // No free registers in this scope level
    printf("[REGISTER_ALLOCATOR] Warning: No free temp registers in scope level %d\n", scope_level);
    return -1;
}

void mp_enter_scope(MultiPassRegisterAllocator* allocator) {
    if (!allocator) return;
    
    if (allocator->current_scope_level < 5) {
        allocator->current_scope_level++;
        printf("[REGISTER_ALLOCATOR] Entered scope level %d\n", allocator->current_scope_level);
    } else {
        printf("[REGISTER_ALLOCATOR] Warning: Maximum scope depth (5) reached\n");
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
        
        printf("[REGISTER_ALLOCATOR] Exited scope level %d (freed %d registers)\n", scope, 8);
        allocator->current_scope_level--;
    } else {
        printf("[REGISTER_ALLOCATOR] Warning: Already at root scope level\n");
    }
}

void mp_free_scoped_temp_register(MultiPassRegisterAllocator* allocator, int reg, int scope_level) {
    if (!allocator) return;
    if (scope_level < 0 || scope_level >= 6) return;
    
    int base_reg = MP_TEMP_REG_START + (scope_level * 8);
    if (reg >= base_reg && reg < base_reg + 8) {
        int slot = reg - base_reg;
        allocator->scope_temp_regs[scope_level][slot] = false;
        printf("[REGISTER_ALLOCATOR] Freed scoped temp register R%d (scope level %d, slot %d)\n", 
               reg, scope_level, slot);
    }
}

void mp_free_register(MultiPassRegisterAllocator* allocator, int reg) {
    if (!allocator) return;

    if (mp_has_typed_residency_hint(allocator, reg)) {
        printf("[REGISTER_ALLOCATOR] Skipped freeing R%d due to typed residency hint\n", reg);
        return;
    }

    // Determine register type and free accordingly
    if (reg >= MP_GLOBAL_REG_START && reg <= MP_GLOBAL_REG_END) {
        int index = reg - MP_GLOBAL_REG_START;
        allocator->global_regs[index] = false;
        printf("[REGISTER_ALLOCATOR] Freed global register R%d\n", reg);
    }
    else if (reg >= MP_FRAME_REG_START && reg <= MP_FRAME_REG_END) {
        int index = reg - MP_FRAME_REG_START;
        allocator->frame_regs[index] = false;
        printf("[REGISTER_ALLOCATOR] Freed frame register R%d\n", reg);
    }
    else if (reg >= MP_TEMP_REG_START && reg <= MP_TEMP_REG_END) {
        mp_free_temp_register(allocator, reg);
    }
    else if (reg >= MP_MODULE_REG_START && reg <= MP_MODULE_REG_END) {
        int index = reg - MP_MODULE_REG_START;
        allocator->module_regs[index] = false;
        printf("[REGISTER_ALLOCATOR] Freed module register R%d\n", reg);
    }
    else {
        printf("[REGISTER_ALLOCATOR] Warning: Invalid register R%d cannot be freed\n", reg);
    }
}

void mp_free_temp_register(MultiPassRegisterAllocator* allocator, int reg) {
    if (!allocator) {
        return;
    }

    if (mp_has_typed_residency_hint(allocator, reg)) {
        printf("[REGISTER_ALLOCATOR] Skipped freeing temp R%d due to typed residency hint\n", reg);
        return;
    }

    if (reg < MP_TEMP_REG_START || reg > MP_TEMP_REG_END) {
        printf("[REGISTER_ALLOCATOR] Warning: Invalid temp register R%d\n", reg);
        return;
    }

    int index = reg - MP_TEMP_REG_START;
    allocator->temp_regs[index] = false;
    
    // Add to reuse stack (LIFO for better cache locality)
    if (allocator->temp_stack_top < 47) {
        allocator->temp_stack[++allocator->temp_stack_top] = reg;
        printf("[REGISTER_ALLOCATOR] Freed temp register R%d (added to reuse stack)\n", reg);
    } else {
        printf("[REGISTER_ALLOCATOR] Freed temp register R%d (reuse stack full)\n", reg);
    }
}

void mp_set_typed_residency_hint(MultiPassRegisterAllocator* allocator, int reg, bool persistent) {
    if (!allocator || reg < 0 || reg >= REGISTER_COUNT) {
        return;
    }
    allocator->typed_residency_hint[reg] = persistent;
}

bool mp_has_typed_residency_hint(const MultiPassRegisterAllocator* allocator, int reg) {
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
    
    // Initialize typed register tracking (R0-R255 per type)
    memset(allocator->typed_i32_regs, false, sizeof(allocator->typed_i32_regs));
    memset(allocator->typed_i64_regs, false, sizeof(allocator->typed_i64_regs));
    memset(allocator->typed_f64_regs, false, sizeof(allocator->typed_f64_regs));
    memset(allocator->typed_u32_regs, false, sizeof(allocator->typed_u32_regs));
    memset(allocator->typed_u64_regs, false, sizeof(allocator->typed_u64_regs));
    memset(allocator->typed_bool_regs, false, sizeof(allocator->typed_bool_regs));
    
    // Initialize allocation tracking
    memset(allocator->allocations, 0, sizeof(allocator->allocations));
    allocator->allocation_count = 0;
    
    // Initialize performance heuristics
    allocator->arithmetic_operation_count = 0;
    allocator->prefer_typed_registers = true;  // Start with optimization enabled
    
    printf("[DUAL_REGISTER_ALLOCATOR] Initialized with typed register optimization enabled\n");
    return allocator;
}

void free_dual_register_allocator(DualRegisterAllocator* allocator) {
    if (!allocator) return;
    
    free_mp_register_allocator(allocator->legacy_allocator);
    free(allocator);
}

// Helper function to check if a register type is numeric and benefits from typed registers
static bool is_numeric_type_for_typed_regs(RegisterType type) {
    return (type == REG_TYPE_I32 || type == REG_TYPE_I64 || 
            type == REG_TYPE_F64 || type == REG_TYPE_U32 || 
            type == REG_TYPE_U64 || type == REG_TYPE_BOOL);
}

// Helper function to get next available typed register for a specific type
static int find_free_typed_register(DualRegisterAllocator* allocator, RegisterType type) {
    bool* regs = NULL;
    
    switch (type) {
        case REG_TYPE_I32:  regs = allocator->typed_i32_regs;  break;
        case REG_TYPE_I64:  regs = allocator->typed_i64_regs;  break;
        case REG_TYPE_F64:  regs = allocator->typed_f64_regs;  break;
        case REG_TYPE_U32:  regs = allocator->typed_u32_regs;  break;
        case REG_TYPE_U64:  regs = allocator->typed_u64_regs;  break;
        case REG_TYPE_BOOL: regs = allocator->typed_bool_regs; break;
        default: return -1;
    }
    
    // Find first free register in the bank (R0-R255)
    for (int i = 0; i < 256; i++) {
        if (!regs[i]) {
            regs[i] = true;
            return i;
        }
    }
    
    return -1;  // No free typed registers
}

RegisterAllocation* allocate_typed_register(DualRegisterAllocator* allocator, RegisterType type) {
    if (!allocator || !is_numeric_type_for_typed_regs(type)) return NULL;
    
    int physical_id = find_free_typed_register(allocator, type);
    if (physical_id == -1) {
        printf("[DUAL_REGISTER_ALLOCATOR] No free typed registers for type %d, falling back to standard\n", type);
        return allocate_standard_register(allocator, type, MP_TEMP_REG_START);
    }
    
    // Create allocation record
    if (allocator->allocation_count >= 256) {
        printf("[DUAL_REGISTER_ALLOCATOR] Warning: Maximum allocations reached\n");
        return NULL;
    }
    
    RegisterAllocation* allocation = &allocator->allocations[allocator->allocation_count++];
    allocation->logical_id = -1;  // Not applicable for typed registers
    allocation->physical_type = type;
    allocation->physical_id = physical_id;
    allocation->strategy = REG_STRATEGY_TYPED;
    allocation->is_active = true;
    
    printf("[DUAL_REGISTER_ALLOCATOR] Allocated typed register: type=%d, physical_id=%d\n", type, physical_id);
    return allocation;
}

RegisterAllocation* allocate_standard_register(DualRegisterAllocator* allocator, RegisterType type, int scope_preference) {
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
        printf("[DUAL_REGISTER_ALLOCATOR] Failed to allocate standard register\n");
        return NULL;
    }
    
    // Mark as used in standard register tracking
    if (logical_id >= 0 && logical_id < 256) {
        allocator->standard_regs[logical_id] = true;
    }
    
    // Create allocation record
    if (allocator->allocation_count >= 256) {
        printf("[DUAL_REGISTER_ALLOCATOR] Warning: Maximum allocations reached\n");
        return NULL;
    }
    
    RegisterAllocation* allocation = &allocator->allocations[allocator->allocation_count++];
    allocation->logical_id = logical_id;
    allocation->physical_type = type;
    allocation->physical_id = -1;  // Not applicable for standard registers
    allocation->strategy = REG_STRATEGY_STANDARD;
    allocation->is_active = true;
    
    printf("[DUAL_REGISTER_ALLOCATOR] Allocated standard register: logical_id=%d, type=%d\n", logical_id, type);
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
        
        RegisterAllocation* typed_alloc = allocate_typed_register(allocator, type);
        if (typed_alloc) {
            printf("[DUAL_REGISTER_ALLOCATOR] Smart allocation chose TYPED register for performance\n");
            return typed_alloc;
        }
        
        // Fallback to standard if typed allocation failed
        printf("[DUAL_REGISTER_ALLOCATOR] Typed allocation failed, falling back to standard\n");
    }
    
    // Use standard registers for general-purpose or non-numeric operations
    int scope_preference = is_arithmetic_hot_path ? MP_TEMP_REG_START : MP_FRAME_REG_START;
    RegisterAllocation* standard_alloc = allocate_standard_register(allocator, type, scope_preference);
    
    if (standard_alloc) {
        printf("[DUAL_REGISTER_ALLOCATOR] Smart allocation chose STANDARD register\n");
    }
    
    return standard_alloc;
}

void free_register_allocation(DualRegisterAllocator* allocator, RegisterAllocation* allocation) {
    if (!allocator || !allocation || !allocation->is_active) return;
    
    if (allocation->strategy == REG_STRATEGY_TYPED) {
        // Free typed register
        bool* regs = NULL;
        
        switch (allocation->physical_type) {
            case REG_TYPE_I32:  regs = allocator->typed_i32_regs;  break;
            case REG_TYPE_I64:  regs = allocator->typed_i64_regs;  break;
            case REG_TYPE_F64:  regs = allocator->typed_f64_regs;  break;
            case REG_TYPE_U32:  regs = allocator->typed_u32_regs;  break;
            case REG_TYPE_U64:  regs = allocator->typed_u64_regs;  break;
            case REG_TYPE_BOOL: regs = allocator->typed_bool_regs; break;
            default: break;
        }
        
        if (regs && allocation->physical_id >= 0 && allocation->physical_id < 256) {
            regs[allocation->physical_id] = false;
            printf("[DUAL_REGISTER_ALLOCATOR] Freed typed register: type=%d, physical_id=%d\n",
                   allocation->physical_type, allocation->physical_id);
        }
    } else if (allocation->strategy == REG_STRATEGY_STANDARD) {
        // Free standard register
        if (allocation->logical_id >= 0 && allocation->logical_id < 256) {
            allocator->standard_regs[allocation->logical_id] = false;
            mp_free_register(allocator->legacy_allocator, allocation->logical_id);
            printf("[DUAL_REGISTER_ALLOCATOR] Freed standard register: logical_id=%d\n", allocation->logical_id);
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
    
    printf("[DUAL_REGISTER_ALLOCATOR] Stats: %d typed, %d standard, %d arithmetic ops\n", 
           typed_count, standard_count, allocator->arithmetic_operation_count);
    
    // Explicitly mark variables as used to avoid compiler warnings
    (void)typed_count;
    (void)standard_count;
}