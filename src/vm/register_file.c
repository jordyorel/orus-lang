// Orus Language Project
// ---------------------------------------------------------------------------
// File: src/vm/register_file.c
// Author: Jordy Orel KONDA
// Copyright (c) 2025 Jordy Orel KONDA
// License: MIT License (see LICENSE file in the project root)
// Description: Implements the VM register file storing execution state for each frame.

// register_file.c - Register File Architecture Implementation
// Implementation of hierarchical register windows with dynamic spilling

#include "vm/vm.h"
#include "vm/vm_constants.h"
#include "vm/spill_manager.h"
#include "vm/module_manager.h"
#include "vm/register_cache.h"
#include "vm/vm_comparison.h"
#include "runtime/memory.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>

// Forward declarations
bool is_spilled_register(uint16_t id);
bool is_module_register(uint16_t id);

static void reset_frame_value_storage(CallFrame* frame) {
    if (!frame) {
        return;
    }

    for (size_t i = 0; i < FRAME_REGISTERS; ++i) {
        frame->registers[i] = BOOL_VAL(false);
    }

    for (size_t i = 0; i < TEMP_REGISTERS; ++i) {
        frame->temps[i] = BOOL_VAL(false);
    }
}

static void reset_frame_metadata(CallFrame* frame) {
    if (!frame) {
        return;
    }

    frame->parent = NULL;
    frame->next = NULL;
    frame->typed_window = NULL;
    frame->previous_typed_window = NULL;
    frame->typed_window_version = 0;
    frame->frame_base = FRAME_REG_START;
    frame->temp_base = TEMP_REG_START;
    frame->temp_count = 0;
    frame->spill_base = SPILL_REG_START;
    frame->spill_count = 0;
    frame->register_count = 0;
    frame->module_id = 0;
    frame->flags = 0;
    frame->returnAddress = NULL;
    frame->previousChunk = NULL;
    frame->resultRegister = FRAME_REG_START;
    frame->parameterBaseRegister = FRAME_REG_START;
    frame->functionIndex = UINT16_MAX;
}

static inline uint16_t typed_window_select_bit(uint64_t mask) {
#if defined(__GNUC__) || defined(__clang__)
    return (uint16_t)__builtin_ctzll(mask);
#else
    uint16_t index = 0;
    while ((mask & 1u) == 0u) {
        mask >>= 1;
        ++index;
    }
    return index;
#endif
}

static inline uint64_t typed_window_mask_for_range(uint16_t start, uint16_t end, uint16_t word_index) {
    uint16_t word_start = (uint16_t)(word_index * 64);
    uint16_t word_end = (uint16_t)(word_start + 64);
    if (start >= word_end || end <= word_start) {
        return 0;
    }
    uint16_t local_start = start > word_start ? (uint16_t)(start - word_start) : 0;
    uint16_t local_end = end < word_end ? (uint16_t)(end - word_start) : 64;
    uint64_t lower_mask = local_start == 0 ? 0 : ((uint64_t)1 << local_start) - 1;
    uint64_t upper_mask = local_end >= 64 ? UINT64_MAX : ((uint64_t)1 << local_end) - 1;
    return upper_mask & ~lower_mask;
}

static void typed_window_copy_slot(TypedRegisterWindow* dst, const TypedRegisterWindow* src, uint16_t index);

static void typed_window_clear_live_range(TypedRegisterWindow* window, uint16_t start, uint16_t end) {
    if (!window || start >= end) {
        return;
    }

    uint16_t start_word = (uint16_t)(start >> 6);
    uint16_t end_word = (uint16_t)((end + 63) >> 6);
    if (end_word > TYPED_WINDOW_LIVE_WORDS) {
        end_word = TYPED_WINDOW_LIVE_WORDS;
    }

    for (uint16_t word = start_word; word < end_word; ++word) {
        uint64_t range_mask = typed_window_mask_for_range(start, end, word);
        uint64_t live = window->live_mask[word] & range_mask;
        window->live_mask[word] &= ~range_mask;
        window->dirty_mask[word] &= ~range_mask;
        while (live) {
            uint16_t bit = typed_window_select_bit(live);
            uint16_t index = (uint16_t)(word * 64 + bit);
            window->reg_types[index] = REG_TYPE_NONE;
            typed_window_clear_dirty(window, index);
            if (window->heap_regs) {
                window->heap_regs[index] = typed_window_default_boxed_value();
            }
            live &= live - 1;
        }
    }
}

static void typed_window_copy_live_range(TypedRegisterWindow* dst, const TypedRegisterWindow* src,
                                         uint16_t start, uint16_t end) {
    if (!dst || !src || start >= end) {
        return;
    }

    uint16_t start_word = (uint16_t)(start >> 6);
    uint16_t end_word = (uint16_t)((end + 63) >> 6);
    if (end_word > TYPED_WINDOW_LIVE_WORDS) {
        end_word = TYPED_WINDOW_LIVE_WORDS;
    }

    for (uint16_t word = start_word; word < end_word; ++word) {
        uint64_t range_mask = typed_window_mask_for_range(start, end, word);
        uint64_t live = src->live_mask[word] & range_mask;
        uint64_t dirty = (src->dirty_mask[word] & range_mask) & live;
        dst->live_mask[word] = (dst->live_mask[word] & ~range_mask) | live;
        dst->dirty_mask[word] = (dst->dirty_mask[word] & ~range_mask) | dirty;

        uint64_t update_bits = range_mask;
        while (update_bits) {
            uint16_t bit = typed_window_select_bit(update_bits);
            uint16_t index = (uint16_t)(word * 64 + bit);
            uint64_t mask_bit = (uint64_t)1 << bit;
            bool is_live = (live & mask_bit) != 0;
            bool is_dirty = (dirty & mask_bit) != 0;

            if (is_live) {
                typed_window_copy_slot(dst, src, index);
                dst->dirty[index] = is_dirty;
            } else {
                dst->dirty[index] = false;
            }

            update_bits &= update_bits - 1;
        }
    }
}

static void typed_window_initialize_storage(TypedRegisterWindow* window) {
    if (!window) {
        return;
    }

    memset(window, 0, sizeof(*window));
    window->generation = 0;
    typed_window_reset_live_mask(window);
    for (uint16_t i = 0; i < TYPED_REGISTER_WINDOW_SIZE; ++i) {
        window->reg_types[i] = REG_TYPE_NONE;
    }
    window->next = NULL;
}

static void typed_window_copy_slot(TypedRegisterWindow* dst, const TypedRegisterWindow* src, uint16_t index) {
    dst->i32_regs[index] = src->i32_regs[index];
    dst->i64_regs[index] = src->i64_regs[index];
    dst->u32_regs[index] = src->u32_regs[index];
    dst->u64_regs[index] = src->u64_regs[index];
    dst->f64_regs[index] = src->f64_regs[index];
    dst->bool_regs[index] = src->bool_regs[index];
    dst->reg_types[index] = src->reg_types[index];
    if (src->reg_types[index] == REG_TYPE_HEAP && src->heap_regs) {
        Value* dst_heap = typed_window_ensure_heap_storage(dst);
        if (dst_heap) {
            dst_heap[index] = src->heap_regs[index];
        }
    } else if (dst->heap_regs) {
        dst->heap_regs[index] = typed_window_default_boxed_value();
    }
}

static void typed_window_sync_shared_ranges(TypedRegisterWindow* dst, const TypedRegisterWindow* src) {
    if (!dst || !src) {
        return;
    }

    uint16_t shared_limit = FRAME_REG_START < TYPED_REGISTER_WINDOW_SIZE ? FRAME_REG_START : TYPED_REGISTER_WINDOW_SIZE;
    typed_window_copy_live_range(dst, src, 0, shared_limit);

    uint16_t module_end = MODULE_REG_START + MODULE_REGISTERS;
    if (MODULE_REG_START < TYPED_REGISTER_WINDOW_SIZE) {
        if (module_end > TYPED_REGISTER_WINDOW_SIZE) {
            module_end = TYPED_REGISTER_WINDOW_SIZE;
        }
        typed_window_copy_live_range(dst, src, MODULE_REG_START, module_end);
    }
}

static void typed_registers_bind_window(TypedRegisterWindow* window) {
    if (!window) {
        window = &vm.typed_regs.root_window;
    }

    vm.typed_regs.active_window = window;
    vm.typed_regs.i32_regs = window->i32_regs;
    vm.typed_regs.i64_regs = window->i64_regs;
    vm.typed_regs.u32_regs = window->u32_regs;
    vm.typed_regs.u64_regs = window->u64_regs;
    vm.typed_regs.f64_regs = window->f64_regs;
    vm.typed_regs.bool_regs = window->bool_regs;
    vm.typed_regs.heap_regs = window->heap_regs;
    vm.typed_regs.dirty = window->dirty;
    vm.typed_regs.dirty_mask = window->dirty_mask;
    vm.typed_regs.reg_types = window->reg_types;
}

void register_file_reconcile_active_window(void) {
    TypedRegisterWindow* window = vm.typed_regs.active_window ? vm.typed_regs.active_window : &vm.typed_regs.root_window;
    if (!window) {
        return;
    }

    for (uint16_t word = 0; word < TYPED_WINDOW_LIVE_WORDS; ++word) {
        uint64_t dirty = window->dirty_mask[word];
        while (dirty) {
            uint16_t bit = typed_window_select_bit(dirty);
            uint16_t index = (uint16_t)(word * 64 + bit);
            vm_reconcile_typed_register(index);
            dirty &= dirty - 1;
        }
    }
}

static TypedRegisterWindow* typed_registers_acquire_window(void) {
    TypedRegisterWindow* window = vm.typed_regs.free_windows;
    if (window) {
        vm.typed_regs.free_windows = window->next;
    } else {
        window = (TypedRegisterWindow*)malloc(sizeof(TypedRegisterWindow));
        if (!window) {
            return NULL;
        }
        typed_window_initialize_storage(window);
    }

    window->next = NULL;
    return window;
}

static void typed_registers_release_window(TypedRegisterWindow* window) {
    if (!window) {
        return;
    }

    typed_window_reset_live_mask(window);
    window->next = vm.typed_regs.free_windows;
    vm.typed_regs.free_windows = window;
}

static void clear_typed_window_frame(TypedRegisterWindow* window) {
    if (!window) {
        return;
    }

    uint16_t limit = MODULE_REG_START < TYPED_REGISTER_WINDOW_SIZE ? MODULE_REG_START : TYPED_REGISTER_WINDOW_SIZE;
    typed_window_clear_live_range(window, FRAME_REG_START, limit);
}

void register_file_clear_active_typed_frame(void) {
    TypedRegisterWindow* window = vm.typed_regs.active_window;
    if (!window) {
        window = &vm.typed_regs.root_window;
    }
    clear_typed_window_frame(window);
}

void register_file_reset_active_frame_storage(void) {
    CallFrame* frame = vm.register_file.current_frame;
    if (!frame) {
        return;
    }
    reset_frame_value_storage(frame);
    frame->register_count = 0;
    frame->temp_count = 0;
}

// Internal fast register access with branch prediction hints (used by cache)
Value* get_register_internal(RegisterFile* rf, uint16_t id) {
    // Fast path: Global registers (0-63)
    if (__builtin_expect(id < GLOBAL_REGISTERS, 1)) {
        return &rf->globals[id];
    }

    // Frame registers (64-191)
    if (__builtin_expect(id >= FRAME_REG_START && id < FRAME_REG_START + FRAME_REGISTERS, 1)) {
        if (rf->current_frame) {
            return &rf->current_frame->registers[id - FRAME_REG_START];
        }

        // When no frame is active, fall back to the legacy register storage so we don't
        // alias unrelated global slots. This keeps register 1:1 semantics intact during
        // bootstrapping and host-driven execution.
        if (id < REGISTER_COUNT) {
            return &vm.registers[id];
        }
    }

    // Temp registers (192-239)
    if (id >= TEMP_REG_START && id < TEMP_REG_START + TEMP_REGISTERS) {
        Value* temp_bank = rf->temps ? rf->temps : rf->temps_root;
        return &temp_bank[id - TEMP_REG_START];
    }

    // Module registers (240-255)
    if (id >= MODULE_REG_START && id < MODULE_REG_START + MODULE_REGISTERS) {
        if (rf->module_manager && is_module_register(id)) {
            uint8_t module_id = (id - MODULE_REG_START) / MODULE_REGISTERS;
            uint16_t reg_offset = (id - MODULE_REG_START) % MODULE_REGISTERS;
            Value* slot = get_module_register(rf->module_manager, module_id, reg_offset);
            if (slot) {
                return slot;
            }
        }

        if (id < REGISTER_COUNT) {
            return &vm.registers[id];
        }
    }

    // Spilled registers (256+)
    if (rf->spilled_registers && is_spilled_register(id)) {
        static Value spilled_value;
        if (unspill_register_value(rf->spilled_registers, id, &spilled_value)) {
            return &spilled_value;
        }
    }

    // Final fallback – provide a stable pointer within the legacy array.
    return &vm.registers[id % REGISTER_COUNT];
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
    // Fast path: Global registers (0-63)
    if (__builtin_expect(id < GLOBAL_REGISTERS, 1)) {
        rf->globals[id] = value;
        if (id < REGISTER_COUNT) {
            vm.registers[id] = value;
        }
        return;
    }

    // Frame registers (64-191)
    if (__builtin_expect(id >= FRAME_REG_START && id < FRAME_REG_START + FRAME_REGISTERS, 1)) {
        if (rf->current_frame) {
            uint16_t slot = (uint16_t)(id - FRAME_REG_START);
            assert(slot < FRAME_REGISTERS);
            rf->current_frame->registers[slot] = value;
            uint16_t new_count = (uint16_t)(slot + 1);
            if (new_count > rf->current_frame->register_count) {
                rf->current_frame->register_count = new_count;
            }
        } else if (id < REGISTER_COUNT) {
            vm.registers[id] = value;
        }

        if (id < REGISTER_COUNT) {
            vm.registers[id] = value;
        }
        return;
    }

    // Temp registers (192-239)
    if (id >= TEMP_REG_START && id < TEMP_REG_START + TEMP_REGISTERS) {
        Value* temp_bank = rf->temps ? rf->temps : rf->temps_root;
        uint16_t slot = (uint16_t)(id - TEMP_REG_START);
        assert(slot < TEMP_REGISTERS);
        temp_bank[slot] = value;
        if (rf->current_frame && rf->temps == rf->current_frame->temps) {
            uint16_t new_count = (uint16_t)(slot + 1);
            if (new_count > rf->current_frame->temp_count) {
                rf->current_frame->temp_count = new_count;
            }
        }
        if (id < REGISTER_COUNT) {
            vm.registers[id] = value;
        }
        return;
    }

    // Module registers (240-255)
    if (id >= MODULE_REG_START && id < MODULE_REG_START + MODULE_REGISTERS) {
        if (rf->module_manager && is_module_register(id)) {
            uint8_t module_id = (id - MODULE_REG_START) / MODULE_REGISTERS;
            uint16_t reg_offset = (id - MODULE_REG_START) % MODULE_REGISTERS;
            set_module_register(rf->module_manager, module_id, reg_offset, value);
        }
        if (id < REGISTER_COUNT) {
            vm.registers[id] = value;
        }
        return;
    }

    // Spilled registers (256+)
    if (rf->spilled_registers && is_spilled_register(id)) {
        SpillManager* spill_mgr = rf->spilled_registers;
        set_spill_register_value(spill_mgr, id, value);
        return;
    }

    if (id < REGISTER_COUNT) {
        vm.registers[id] = value;
        return;
    }

    // Fallback: map to globals deterministically for out-of-range ids
    rf->globals[id % GLOBAL_REGISTERS] = value;
}

// Cached register set function (public interface)
void set_register(RegisterFile* rf, uint16_t id, Value value) {
    // Use cache if available, otherwise fall back to direct access
    if (rf->cache) {
        cached_set_register(rf->cache, rf, id, value);
    } else {
        set_register_internal(rf, id, value);
    }
}

// Call frame management
CallFrame* allocate_frame(RegisterFile* rf) {
    if (!rf) {
        return NULL;
    }

    CallFrame* frame = rf->free_frames;
    if (!frame) {
        return NULL;
    }
    rf->free_frames = frame->next;
    reset_frame_metadata(frame);

    TypedRegisterWindow* parent_window =
        vm.typed_regs.active_window ? vm.typed_regs.active_window : &vm.typed_regs.root_window;
    TypedRegisterWindow* new_window = typed_registers_acquire_window();
    if (!new_window) {
        frame->next = rf->free_frames;
        rf->free_frames = frame;
        return NULL;
    }

    frame->parent = rf->current_frame;

    typed_window_reset_live_mask(new_window);
    typed_window_sync_shared_ranges(new_window, parent_window);
    new_window->generation = ++vm.typed_regs.window_version;
    frame->typed_window = new_window;
    frame->previous_typed_window = parent_window;
    frame->typed_window_version = new_window->generation;

    frame->next = rf->frame_stack;
    rf->frame_stack = frame;
    rf->current_frame = frame;
    rf->temps = frame->temps;

    typed_registers_bind_window(new_window);
    vm.typed_regs.active_depth++;
    clear_typed_window_frame(new_window);

    vm.frameCount++;

    return frame;
}

void deallocate_frame(RegisterFile* rf) {
    if (!rf || !rf->current_frame) {
        return;
    }

    CallFrame* frame = rf->current_frame;

    TypedRegisterWindow* parent_window =
        frame->previous_typed_window ? frame->previous_typed_window : &vm.typed_regs.root_window;
    TypedRegisterWindow* window_to_release = frame->typed_window;

    typed_window_sync_shared_ranges(parent_window, window_to_release);
    typed_registers_bind_window(parent_window);
    if (vm.typed_regs.active_depth > 0) {
        vm.typed_regs.active_depth--;
    }
    typed_registers_release_window(window_to_release);

    CallFrame* parent = frame->parent;
    CallFrame* next_frame = frame->next;

    rf->current_frame = parent;
    rf->frame_stack = next_frame;
    rf->temps = rf->current_frame ? rf->current_frame->temps : rf->temps_root;

    reset_frame_metadata(frame);
    frame->next = rf->free_frames;
    rf->free_frames = frame;

    if (vm.frameCount > 0) {
        vm.frameCount--;
    }
}

// Initialize register file
void init_register_file(RegisterFile* rf) {
    // Initialize global registers to NIL
    for (int i = 0; i < GLOBAL_REGISTERS; i++) {
        rf->globals[i] = BOOL_VAL(false);
    }
    
    // Initialize temp registers to NIL
    for (int i = 0; i < TEMP_REGISTERS; i++) {
        rf->temps_root[i] = BOOL_VAL(false);
    }
    rf->temps = rf->temps_root;

    // Initialize frame management
    rf->current_frame = NULL;
    rf->frame_stack = NULL;
    rf->free_frames = NULL;
    for (int i = FRAMES_MAX - 1; i >= 0; --i) {
        CallFrame* frame = &vm.frames[i];
        reset_frame_value_storage(frame);
        reset_frame_metadata(frame);
        frame->temp_count = TEMP_REGISTERS;
        frame->next = rf->free_frames;
        rf->free_frames = frame;
    }
    vm.frameCount = 0;

    // Initialize spill management
    rf->spilled_registers = create_spill_manager();
    rf->metadata = NULL;  // TODO: Initialize metadata array if needed
    
    // Initialize module management
    rf->module_manager = create_module_manager();
    
    // Initialize register cache
    rf->cache = NULL;  // Cache is created on-demand via enable_register_caching()
}

// Cleanup register file
void free_register_file(RegisterFile* rf) {
    // Deallocate all frames
    while (rf->current_frame) {
        deallocate_frame(rf);
    }
    
    // Free spill area
    if (rf->spilled_registers) {
        free_spill_manager(rf->spilled_registers);
        rf->spilled_registers = NULL;
    }
    
    // Free module management
    if (rf->module_manager) {
        free_module_manager(rf->module_manager);
        rf->module_manager = NULL;
    }
    
    // Free register cache
    if (rf->cache) {
        free_register_cache(rf->cache);
        rf->cache = NULL;
    }

    if (rf->metadata) {
        free(rf->metadata);
        rf->metadata = NULL;
    }

    TypedRegisterWindow* window = vm.typed_regs.free_windows;
    while (window) {
        TypedRegisterWindow* next = window->next;
        free(window);
        window = next;
    }
    vm.typed_regs.free_windows = NULL;
    typed_registers_bind_window(&vm.typed_regs.root_window);
    vm.typed_regs.active_depth = 0;
}

// Frame register allocation for compiler
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

// Temporary register allocation
uint16_t allocate_temp_register(RegisterFile* rf) {
    (void)rf; // Suppress unused parameter warning
    // Simple linear allocation for temps (could be more sophisticated)
    static uint8_t next_temp = 0;
    
    if (next_temp >= TEMP_REGISTERS) {
        next_temp = 0; // Wrap around (assumes short-lived usage)
    }
    
    return TEMP_REG_START + next_temp++;
}

// Register type checking for debugging
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

// Spilling functions implementation
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

// Register pressure analysis (simple implementation)
uint16_t allocate_spilled_register(RegisterFile* rf, Value value) {
    if (!rf->spilled_registers) return 0;
    
    SpillManager* spill_mgr = rf->spilled_registers;
    return spill_register_value(spill_mgr, value);
}

// Get spilling statistics
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

// Cache integration functions
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