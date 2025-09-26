/*
 * Orus Language Project
 * ---------------------------------------------------------------------------
 * File: src/vm/register_file.c
 * Author: Jordy Orel KONDA
 * Copyright (c) 2025 Jordy Orel KONDA
 * License: MIT License (see LICENSE file in the project root)
 * Description: Implements the VM register file storing execution state for each
 *              frame.
 */

// register_file.c - Register File Architecture Implementation
// Implementation of hierarchical register windows with dynamic spilling

#include "vm/vm.h"
#include "vm/vm_constants.h"
#include "vm/spill_manager.h"
#include "vm/module_manager.h"
#include "vm/register_cache.h"
#include "runtime/memory.h"
#include <string.h>
#include <assert.h>

// Forward declarations
bool is_spilled_register(uint16_t id);
bool is_module_register(uint16_t id);

// Phase 1: Internal fast register access with branch prediction hints (used by cache)
Value* get_register_internal(RegisterFile* rf, uint16_t id) {
    // Fast path: Global registers (0-255)
    if (__builtin_expect(id < GLOBAL_REGISTERS, 1)) {
        return &rf->globals[id];
    }
    
    // Frame registers (256-319)
    if (__builtin_expect(id >= FRAME_REG_START && id < FRAME_REG_START + FRAME_REGISTERS, 1)) {
        if (rf->current_frame) {
            return &rf->current_frame->registers[id - FRAME_REG_START];
        }
        // Fallback to global if no current frame
        return &rf->globals[id % GLOBAL_REGISTERS];
    }
    
    // Temp registers (320-351)
    if (id < TEMP_REG_START + TEMP_REGISTERS) {
        return &rf->temps[id - TEMP_REG_START];
    }
    
    // Module registers (352-479)
    if (rf->module_manager && is_module_register(id)) {
        uint8_t module_id = (id - MODULE_REG_START) / MODULE_REGISTERS;
        uint16_t reg_offset = (id - MODULE_REG_START) % MODULE_REGISTERS;
        return get_module_register(rf->module_manager, module_id, reg_offset);
    }
    
    // Spilled registers (480+) - HashMap lookup
    if (rf->spilled_registers && is_spilled_register(id)) {
        static Value spilled_value;
        if (unspill_register_value(rf->spilled_registers, id, &spilled_value)) {
            return &spilled_value;
        }
    }
    
    // Fallback to global array (error case)
    return &rf->globals[id % GLOBAL_REGISTERS];
}

// Cached register access function (public interface)
Value* get_register(RegisterFile* rf, uint16_t id) {
    // Use cache if available, otherwise fall back to direct access
    if (rf->cache) {
        return cached_get_register(rf->cache, rf, id);
    }
    return get_register_internal(rf, id);
}

void set_register_internal(RegisterFile* rf, uint16_t id, Value value) {
    // Fast path: Global registers (0-255)
    if (__builtin_expect(id < GLOBAL_REGISTERS, 1)) {
        rf->globals[id] = value;
        return;
    }
    
    // Frame registers (256-319)
    if (__builtin_expect(id >= FRAME_REG_START && id < FRAME_REG_START + FRAME_REGISTERS, 1)) {
        if (rf->current_frame) {
            rf->current_frame->registers[id - FRAME_REG_START] = value;
            return;
        }
        // Fallback to global if no current frame
        rf->globals[id % GLOBAL_REGISTERS] = value;
        return;
    }
    
    // Temp registers (320-351)
    if (id < TEMP_REG_START + TEMP_REGISTERS) {
        rf->temps[id - TEMP_REG_START] = value;
        return;
    }
    
    // Phase 3: Module registers (352-479)
    if (rf->module_manager && is_module_register(id)) {
        uint8_t module_id = (id - MODULE_REG_START) / MODULE_REGISTERS;
        uint16_t reg_offset = (id - MODULE_REG_START) % MODULE_REGISTERS;
        set_module_register(rf->module_manager, module_id, reg_offset, value);
        return;
    }
    
    // Phase 2: Spilled registers (480+) - Store in HashMap
    if (rf->spilled_registers && is_spilled_register(id)) {
        SpillManager* spill_mgr = rf->spilled_registers;
        set_spill_register_value(spill_mgr, id, value);
        return;
    }
    
    // Fallback to global array (error case)
    rf->globals[id % GLOBAL_REGISTERS] = value;
}

// Phase 4: Cached register set function (public interface)
void set_register(RegisterFile* rf, uint16_t id, Value value) {
    // Use cache if available, otherwise fall back to direct access
    if (rf->cache) {
        cached_set_register(rf->cache, rf, id, value);
    } else {
        set_register_internal(rf, id, value);
    }
}

// Phase 1: Call frame management
CallFrame* allocate_frame(RegisterFile* rf) {
    // Allocate new frame from heap (for now - could use frame pool later)
    CallFrame* frame = (CallFrame*)malloc(sizeof(CallFrame));
    if (!frame) return NULL;
    
    // Initialize frame
    memset(frame->registers, 0, sizeof(frame->registers));
    frame->parent = rf->current_frame;
    frame->next = rf->frame_stack;
    frame->register_count = 0;
    frame->spill_start = 0;
    frame->module_id = 0;
    frame->flags = 0;
    
    // Legacy compatibility
    frame->returnAddress = NULL;
    frame->previousChunk = NULL;
    frame->baseRegister = 0;
    frame->functionIndex = 0;
    frame->savedRegisterCount = 0;
    memset(frame->savedRegisters, 0, sizeof(frame->savedRegisters));
    
    // Update register file
    rf->frame_stack = frame;
    rf->current_frame = frame;
    
    return frame;
}

void deallocate_frame(RegisterFile* rf) {
    if (!rf->current_frame) return;
    
    CallFrame* frame = rf->current_frame;
    
    // Update register file
    rf->current_frame = frame->parent;
    rf->frame_stack = frame->next;
    
    // Free frame
    free(frame);
}

// Phase 1: Initialize register file
void init_register_file(RegisterFile* rf) {
    // Initialize global registers to NIL
    for (int i = 0; i < GLOBAL_REGISTERS; i++) {
        rf->globals[i] = BOOL_VAL(false);
    }
    
    // Initialize temp registers to NIL
    for (int i = 0; i < TEMP_REGISTERS; i++) {
        rf->temps[i] = BOOL_VAL(false);
    }
    
    // Initialize frame management
    rf->current_frame = NULL;
    rf->frame_stack = NULL;
    
    // Phase 2: Initialize spill management
    rf->spilled_registers = create_spill_manager();
    rf->metadata = NULL;  // TODO: Initialize metadata array if needed
    
    // Phase 3: Initialize module management
    rf->module_manager = create_module_manager();
    
    // Phase 4: Initialize register cache
    rf->cache = NULL;  // Cache is created on-demand via enable_register_caching()
}

// Phase 1: Cleanup register file
void free_register_file(RegisterFile* rf) {
    // Deallocate all frames
    while (rf->current_frame) {
        deallocate_frame(rf);
    }
    
    // Phase 2: Free spill area
    if (rf->spilled_registers) {
        free_spill_manager(rf->spilled_registers);
        rf->spilled_registers = NULL;
    }
    
    // Phase 3: Free module management
    if (rf->module_manager) {
        free_module_manager(rf->module_manager);
        rf->module_manager = NULL;
    }
    
    // Phase 4: Free register cache
    if (rf->cache) {
        free_register_cache(rf->cache);
        rf->cache = NULL;
    }
    
    if (rf->metadata) {
        free(rf->metadata);
        rf->metadata = NULL;
    }
}

// Phase 1: Frame register allocation for compiler
uint16_t allocate_frame_register(RegisterFile* rf) {
    if (!rf->current_frame) {
        // Allocate frame if none exists
        if (!allocate_frame(rf)) {
            return 0; // Error: couldn't allocate frame
        }
    }
    
    if (rf->current_frame->register_count >= FRAME_REGISTERS) {
        // Frame full - with 255 registers, this should be extremely rare
        return 0; // Error: frame full
    }
    
    uint16_t reg_id = FRAME_REG_START + rf->current_frame->register_count;
    rf->current_frame->register_count++;
    
    return reg_id;
}

// Phase 1: Temporary register allocation
uint16_t allocate_temp_register(RegisterFile* rf) {
    (void)rf; // Suppress unused parameter warning
    // Simple linear allocation for temps (could be more sophisticated)
    static uint8_t next_temp = 0;
    
    if (next_temp >= TEMP_REGISTERS) {
        next_temp = 0; // Wrap around (assumes short-lived usage)
    }
    
    return TEMP_REG_START + next_temp++;
}

// Phase 1: Register type checking for debugging
bool is_global_register(uint16_t id) {
    return id < FRAME_REG_START;
}

bool is_frame_register(uint16_t id) {
    return id >= FRAME_REG_START && id < TEMP_REG_START;
}

bool is_temp_register(uint16_t id) {
    return id >= TEMP_REG_START && id < MODULE_REG_START;
}

bool is_module_register(uint16_t id) {
    return id >= MODULE_REG_START && id < SPILL_REG_START;
}

bool is_spilled_register(uint16_t id) {
    return id >= SPILL_REG_START;
}

// Phase 2: Spilling functions implementation
void spill_register(RegisterFile* rf, uint16_t id) {
    if (!rf->spilled_registers) return;
    
    Value* value = get_register(rf, id);
    SpillManager* spill_mgr = rf->spilled_registers;
    spill_register_value(spill_mgr, *value);
}

void unspill_register(RegisterFile* rf, uint16_t id) {
    if (!rf->spilled_registers) return;
    
    SpillManager* spill_mgr = rf->spilled_registers;
    Value value;
    if (unspill_register_value(spill_mgr, id, &value)) {
        // Register was successfully unspilled
        // In a more sophisticated implementation, we would move it back to a physical register
        remove_spilled_register(spill_mgr, id);
    }
}

bool register_file_needs_spilling(RegisterFile* rf) {
    if (!rf->spilled_registers) return false;
    
    SpillManager* spill_mgr = rf->spilled_registers;
    return needs_spilling(spill_mgr);
}

// Phase 2: Register pressure analysis (simple implementation)
uint16_t allocate_spilled_register(RegisterFile* rf, Value value) {
    if (!rf->spilled_registers) return 0;
    
    SpillManager* spill_mgr = rf->spilled_registers;
    return spill_register_value(spill_mgr, value);
}

// Phase 2: Get spilling statistics
void get_register_file_stats(RegisterFile* rf, size_t* global_used, size_t* frame_used, size_t* temp_used, size_t* spilled_count) {
    if (global_used) {
        *global_used = 0;
        for (int i = 0; i < GLOBAL_REGISTERS; i++) {
            if (rf->globals[i].type != VAL_BOOL) (*global_used)++;
        }
    }
    
    if (frame_used) {
        *frame_used = rf->current_frame ? rf->current_frame->register_count : 0;
    }
    
    if (temp_used) {
        *temp_used = 0;
        for (int i = 0; i < TEMP_REGISTERS; i++) {
            if (rf->temps[i].type != VAL_BOOL) (*temp_used)++;
        }
    }
    
    if (spilled_count && rf->spilled_registers) {
        SpillManager* spill_mgr = rf->spilled_registers;
        get_spill_stats(spill_mgr, spilled_count, NULL);
    } else if (spilled_count) {
        *spilled_count = 0;
    }
}

// Phase 4: Cache integration functions
void enable_register_caching(RegisterFile* rf) {
    if (!rf || rf->cache) return; // Already enabled
    
    rf->cache = create_register_cache();
    if (rf->cache) {
        rf->cache->caching_enabled = true;
    }
}

void disable_register_caching(RegisterFile* rf) {
    if (!rf || !rf->cache) return;
    
    rf->cache->caching_enabled = false;
}

void flush_register_file_cache(RegisterFile* rf) {
    if (!rf || !rf->cache) return;
    
    flush_register_cache(rf->cache, rf);
}

void print_register_cache_stats(RegisterFile* rf) {
    if (!rf || !rf->cache) {
        printf("Register cache not available\n");
        return;
    }
    
    print_cache_stats(rf->cache);
}