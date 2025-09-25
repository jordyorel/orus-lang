#ifndef REGISTER_ALLOCATOR_H
#define REGISTER_ALLOCATOR_H

#include <stdint.h>
#include <stdbool.h>
#include "vm/vm.h"  // For RegisterType enum

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

typedef struct MultiPassRegisterAllocator {
    // Register usage tracking (updated to match new ranges)
    bool global_regs[GLOBAL_REGISTERS];       // R0-R63 usage
    bool frame_regs[FRAME_REGISTERS];         // R64-R191 usage
    bool temp_regs[TEMP_REGISTERS];           // R192-R239 usage (matches TEMP_REGISTERS)
    bool module_regs[MODULE_REGISTERS];       // R240-R255 usage (module registers)

    // Scope-aware temp register allocation (updated ranges)
    bool scope_temp_regs[MP_SCOPE_LEVEL_COUNT][8]; // Scope-aware temp allocation blocks
    int current_scope_level;     // Current nesting level (0-(MP_SCOPE_LEVEL_COUNT-1))

    // Residency hints to keep typed values alive across hot backedges
    bool typed_residency_hint[REGISTER_COUNT];
    
    // Allocation state
    int next_global;           // Next available global register
    int next_frame;            // Next available frame register
    int next_temp;             // Next available temp register
    int next_module;           // Next available module register
    
    // Stack for temp register reuse
    int temp_stack[TEMP_REGISTERS];        // Reusable temp registers (updated for new range)
    int temp_stack_top;        // Top of temp stack
} MultiPassRegisterAllocator;

// Core allocation functions
MultiPassRegisterAllocator* init_mp_register_allocator(void);
void free_mp_register_allocator(MultiPassRegisterAllocator* allocator);

// Register allocation by type
int mp_allocate_global_register(MultiPassRegisterAllocator* allocator);
int mp_allocate_frame_register(MultiPassRegisterAllocator* allocator);
bool mp_reserve_global_register(MultiPassRegisterAllocator* allocator, int reg);

void mp_reset_frame_registers(MultiPassRegisterAllocator* allocator);
int mp_allocate_temp_register(MultiPassRegisterAllocator* allocator);
int mp_allocate_consecutive_temp_registers(MultiPassRegisterAllocator* allocator, int count);
int mp_allocate_module_register(MultiPassRegisterAllocator* allocator);

// Scope-aware register allocation (NEW)
int mp_allocate_scoped_temp_register(MultiPassRegisterAllocator* allocator, int scope_level);
void mp_enter_scope(MultiPassRegisterAllocator* allocator);
void mp_exit_scope(MultiPassRegisterAllocator* allocator);
void mp_free_scoped_temp_register(MultiPassRegisterAllocator* allocator, int reg, int scope_level);

// Register deallocation
void mp_free_register(MultiPassRegisterAllocator* allocator, int reg);
void mp_free_temp_register(MultiPassRegisterAllocator* allocator, int reg);

// Residency hint helpers
void mp_set_typed_residency_hint(MultiPassRegisterAllocator* allocator, int reg, bool persistent);
bool mp_has_typed_residency_hint(const MultiPassRegisterAllocator* allocator, int reg);

// Utilities
bool mp_is_register_free(MultiPassRegisterAllocator* allocator, int reg);
const char* mp_register_type_name(int reg);

// ====== DUAL REGISTER SYSTEM IMPLEMENTATION ======

// Register allocation strategies
typedef enum RegisterStrategy {
    REG_STRATEGY_STANDARD,    // Use vm.registers[] with OP_*_R instructions
    REG_STRATEGY_TYPED,      // Use vm.typed_regs.* with OP_*_TYPED instructions  
    REG_STRATEGY_AUTO        // Compiler decides based on usage pattern
} RegisterStrategy;

// Register allocation record
typedef struct RegisterAllocation {
    int logical_id;           // R0-R255 logical register ID (for standard)
    RegisterType physical_type; // Which physical bank (REG_TYPE_I32, etc.)
    int physical_id;          // Physical register within typed bank (0-255)
    RegisterStrategy strategy; // Which instruction set to use
    bool is_active;           // Whether allocation is currently active
} RegisterAllocation;

// Enhanced register allocator with dual system support
typedef struct DualRegisterAllocator {
    // Legacy allocator for compatibility
    MultiPassRegisterAllocator* legacy_allocator;
    
    // Standard register tracking (R0-R255) - for general purpose
    bool standard_regs[256];
    
    // Typed register tracking (R0-R255 per type) - for performance
    bool typed_i32_regs[256];
    bool typed_i64_regs[256];
    bool typed_f64_regs[256];
    bool typed_u32_regs[256];
    bool typed_u64_regs[256];
    bool typed_bool_regs[256];
    
    // Allocation tracking
    RegisterAllocation allocations[256];
    int allocation_count;
    
    // Performance heuristics
    int arithmetic_operation_count;  // Track arithmetic intensity
    bool prefer_typed_registers;     // Heuristic: prefer typed when beneficial
} DualRegisterAllocator;

// ====== DUAL REGISTER ALLOCATOR API ======

// Initialization and cleanup
DualRegisterAllocator* init_dual_register_allocator(void);
void free_dual_register_allocator(DualRegisterAllocator* allocator);

// Smart register allocation
RegisterAllocation* allocate_register_smart(DualRegisterAllocator* allocator, 
                                           RegisterType type, 
                                           bool is_arithmetic_hot_path);

RegisterAllocation* allocate_typed_register(DualRegisterAllocator* allocator, 
                                          RegisterType type);

RegisterAllocation* allocate_standard_register(DualRegisterAllocator* allocator, 
                                             RegisterType type, 
                                             int scope_preference);

// Register deallocation
void free_register_allocation(DualRegisterAllocator* allocator, 
                            RegisterAllocation* allocation);

// Utilities
bool is_arithmetic_heavy_context(DualRegisterAllocator* allocator);
const char* register_strategy_name(RegisterStrategy strategy);
void print_register_allocation_stats(DualRegisterAllocator* allocator);

#endif // REGISTER_ALLOCATOR_H