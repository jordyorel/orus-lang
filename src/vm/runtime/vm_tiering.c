// Orus Language Project
// ---------------------------------------------------------------------------
// File: src/vm/runtime/vm_tiering.c
// Description: Implements tier selection logic for specialized bytecode and
//              provides default deoptimization handling.

#include "vm/vm_tiering.h"
#include "vm/vm.h"
#include "vm/vm_profiling.h"
#include "vm/vm_comparison.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define VM_FUSION_PATCH_COOLDOWN 4096u

#ifndef FUNCTION_SPECIALIZATION_THRESHOLD
#define FUNCTION_SPECIALIZATION_THRESHOLD 512ULL
#endif

extern VM vm;

static uint64_t g_tiering_instruction_tick_counter = 0;

typedef bool (*VMFusionMiniHandler)(VMFusionPatch* patch);

extern bool vm_dispatch_execute_fused_window(VMFusionPatch* patch);

static VMFusionPatch*
vm_fusion_find_patch(const uint8_t* start_ip) {
    if (!start_ip) {
        return NULL;
    }

    for (size_t i = 0; i < vm.fusion_patch_count && i < VM_MAX_FUSION_PATCHES; ++i) {
        VMFusionPatch* patch = &vm.fusion_patches[i];
        if (patch->start_ip == start_ip) {
            return patch;
        }
    }

    return NULL;
}

static size_t
vm_fusion_oldest_patch_index(void) {
    size_t index = 0;
    uint64_t oldest = UINT64_MAX;

    for (size_t i = 0; i < VM_MAX_FUSION_PATCHES; ++i) {
        VMFusionPatch* patch = &vm.fusion_patches[i];
        if (!patch->start_ip) {
            return i;
        }
        if (patch->last_activation < oldest) {
            oldest = patch->last_activation;
            index = i;
        }
    }

    return index;
}

static VMFusionPatch*
vm_fusion_acquire_patch(const VMHotWindowDescriptor* window) {
    if (!window || !window->start_ip) {
        return NULL;
    }

    VMFusionPatch* existing = vm_fusion_find_patch(window->start_ip);
    if (existing) {
        return existing;
    }

    size_t slot = 0;
    if (vm.fusion_patch_count < VM_MAX_FUSION_PATCHES) {
        slot = vm.fusion_patch_count++;
    } else {
        slot = vm_fusion_oldest_patch_index();
    }

    VMFusionPatch* patch = &vm.fusion_patches[slot];
    memset(patch, 0, sizeof(*patch));
    return patch;
}

static void
vm_jit_cache_reset_slot(JITEntryCacheSlot* slot) {
    if (!slot) {
        return;
    }

    if (slot->entry.code_ptr) {
        orus_jit_backend_release_entry(vm.jit_backend, &slot->entry);
        memset(&slot->entry, 0, sizeof(slot->entry));
    }

    if (slot->occupied && vm.jit_cache.count > 0) {
        vm.jit_cache.count--;
    }

    slot->function_index = UINT16_MAX;
    slot->loop_index = UINT16_MAX;
    slot->generation = 0;
    slot->occupied = false;
    slot->warmup_recorded = false;
}

static void
vm_jit_apply_warmup_backoff(FunctionId function,
                            LoopId loop,
                            bool escalate_backoff) {
    if (loop == UINT16_MAX) {
        return;
    }

    HotPathSample* sample = &vm.profile[loop];
    sample->func = function;
    sample->loop = loop;
    sample->warmup_level = 0;
    sample->suppressed_triggers = 0;

    if (escalate_backoff) {
        if (sample->cooldown_exponent < ORUS_JIT_WARMUP_MAX_BACKOFF) {
            sample->cooldown_exponent++;
        }
        if (sample->hit_count > ORUS_JIT_WARMUP_PARTIAL_RESET) {
            sample->hit_count = ORUS_JIT_WARMUP_PARTIAL_RESET;
        }
    } else {
        sample->cooldown_exponent = 0;
    }

    uint64_t now = vm.ticks;
    uint64_t cooldown =
        orus_jit_warmup_compute_cooldown(sample->cooldown_exponent);
    sample->last_threshold_tick = now;
    if (cooldown > UINT64_MAX - now) {
        sample->cooldown_until_tick = UINT64_MAX;
    } else {
        sample->cooldown_until_tick = now + cooldown;
    }
}

static uint64_t
vm_jit_cache_next_generation(void) {
    vm.jit_cache.next_generation++;
    if (vm.jit_cache.next_generation == 0) {
        vm.jit_cache.next_generation = 1;
    }
    return vm.jit_cache.next_generation;
}

static JITEntryCacheSlot*
vm_jit_cache_find_slot(FunctionId function, LoopId loop) {
    if (!vm.jit_cache.slots || vm.jit_cache.capacity == 0) {
        return NULL;
    }

    for (size_t i = 0; i < vm.jit_cache.capacity; ++i) {
        JITEntryCacheSlot* slot = &vm.jit_cache.slots[i];
        if (!slot->occupied) {
            continue;
        }
        if (slot->function_index == function && slot->loop_index == loop) {
            return slot;
        }
    }

    return NULL;
}

static bool
vm_jit_cache_ensure_capacity(size_t min_capacity) {
    if (vm.jit_cache.capacity >= min_capacity) {
        return true;
    }

    size_t new_capacity = vm.jit_cache.capacity ? vm.jit_cache.capacity : 4u;
    while (new_capacity < min_capacity) {
        new_capacity *= 2u;
    }

    JITEntryCacheSlot* slots =
        (JITEntryCacheSlot*)realloc(vm.jit_cache.slots,
                                    new_capacity * sizeof(JITEntryCacheSlot));
    if (!slots) {
        return false;
    }

    for (size_t i = vm.jit_cache.capacity; i < new_capacity; ++i) {
        memset(&slots[i], 0, sizeof(JITEntryCacheSlot));
        slots[i].function_index = UINT16_MAX;
        slots[i].loop_index = UINT16_MAX;
    }

    vm.jit_cache.slots = slots;
    vm.jit_cache.capacity = new_capacity;
    return true;
}

static JITEntryCacheSlot*
vm_jit_cache_acquire_slot(FunctionId function, LoopId loop) {
    JITEntryCacheSlot* slot = vm_jit_cache_find_slot(function, loop);
    if (slot) {
        return slot;
    }

    if (vm.jit_cache.count >= vm.jit_cache.capacity) {
        if (!vm_jit_cache_ensure_capacity(vm.jit_cache.count + 1u)) {
            return NULL;
        }
    }

    for (size_t i = 0; i < vm.jit_cache.capacity; ++i) {
        slot = &vm.jit_cache.slots[i];
        if (slot->occupied) {
            continue;
        }
        slot->function_index = function;
        slot->loop_index = loop;
        slot->occupied = true;
        slot->warmup_recorded = false;
        vm.jit_cache.count++;
        return slot;
    }

    return NULL;
}

void
vm_tiering_request_window_fusion(const VMHotWindowDescriptor* window) {
    if (!window || !window->start_ip || window->length == 0 ||
        window->length > VM_MAX_FUSION_WINDOW) {
        return;
    }

    VMFusionPatch* patch = vm_fusion_acquire_patch(window);
    if (!patch) {
        return;
    }

    patch->start_ip = window->start_ip;
    patch->length = window->length;
    memcpy(patch->opcodes, window->opcodes, window->length);
    patch->handler = (void*)vm_dispatch_execute_fused_window;
    patch->active = true;
    patch->metadata_requested = true;
    patch->hot_hits = 0;
    patch->last_activation = g_tiering_instruction_tick_counter;
}

bool
vm_tiering_try_execute_fused(const uint8_t* start_ip, uint8_t opcode) {
    VMFusionPatch* patch = vm_fusion_find_patch(start_ip);
    if (!patch || !patch->active || !patch->handler) {
        return false;
    }

    if (patch->length == 0 || patch->opcodes[0] != opcode) {
        return false;
    }

    VMFusionMiniHandler handler = (VMFusionMiniHandler)patch->handler;
    uint8_t* original_ip = vm.ip;
    bool handled = handler(patch);
    if (!handled) {
        vm.ip = original_ip;
        patch->active = false;
        return false;
    }

    if (patch->hot_hits < UINT64_MAX) {
        patch->hot_hits++;
    }
    patch->last_activation = g_tiering_instruction_tick_counter;
    return true;
}

void
vm_tiering_instruction_tick(uint64_t instruction_index) {
    g_tiering_instruction_tick_counter = instruction_index;

    if (instruction_index == 0 ||
        (instruction_index % VM_FUSION_PATCH_COOLDOWN) != 0) {
        return;
    }

    for (size_t i = 0; i < vm.fusion_patch_count && i < VM_MAX_FUSION_PATCHES; ++i) {
        VMFusionPatch* patch = &vm.fusion_patches[i];
        if (!patch->active || !patch->start_ip) {
            continue;
        }
        if (instruction_index - patch->last_activation > VM_FUSION_PATCH_COOLDOWN) {
            patch->active = false;
        }
    }
}

void
vm_tiering_invalidate_all_fusions(void) {
    for (size_t i = 0; i < VM_MAX_FUSION_PATCHES; ++i) {
        VMFusionPatch* patch = &vm.fusion_patches[i];
        if (!patch->start_ip) {
            continue;
        }
        memset(patch, 0, sizeof(*patch));
    }
    vm.fusion_patch_count = 0;
    vm.fusion_generation++;
}

JITEntry*
vm_jit_lookup_entry(FunctionId function, LoopId loop) {
    JITEntryCacheSlot* slot = vm_jit_cache_find_slot(function, loop);
    if (!slot || !slot->entry.code_ptr) {
        return NULL;
    }
    return &slot->entry;
}

uint64_t
vm_jit_install_entry(FunctionId function, LoopId loop, JITEntry* entry) {
    if (!entry || !entry->code_ptr) {
        return 0;
    }

    JITEntryCacheSlot* slot = vm_jit_cache_acquire_slot(function, loop);
    if (!slot) {
        orus_jit_backend_release_entry(vm.jit_backend, entry);
        memset(entry, 0, sizeof(*entry));
        return 0;
    }

    bool had_entry = slot->entry.code_ptr != NULL;
    bool replaced_code = had_entry && slot->entry.code_ptr != entry->code_ptr;
    if (replaced_code) {
        orus_jit_backend_release_entry(vm.jit_backend, &slot->entry);
        memset(&slot->entry, 0, sizeof(slot->entry));
    }

    bool reused_code = had_entry && !replaced_code;
    slot->entry = *entry;
    slot->generation = vm_jit_cache_next_generation();
    slot->warmup_recorded = reused_code ? slot->warmup_recorded : false;

    entry->code_ptr = NULL;
    entry->entry_point = NULL;
    entry->code_capacity = 0;
    entry->code_size = 0;
    entry->debug_name = NULL;

    if (loop != UINT16_MAX) {
        vm.jit_loop_blocklist[loop] = false;
        vm_jit_apply_warmup_backoff(function, loop, false);
    }

    return slot->generation;
}

void
vm_jit_invalidate_entry(const JITDeoptTrigger* trigger) {
    if (!trigger) {
        return;
    }

    vm.jit_deopt_count++;
    vm_tiering_invalidate_all_fusions();

    if (trigger->function_index == UINT16_MAX) {
        vm_jit_flush_entries();
        return;
    }

    for (size_t i = 0; i < vm.jit_cache.capacity; ++i) {
        JITEntryCacheSlot* slot = &vm.jit_cache.slots[i];
        if (!slot->occupied) {
            continue;
        }
        if (slot->function_index != trigger->function_index) {
            continue;
        }
        if (trigger->loop_index != UINT16_MAX &&
            slot->loop_index != trigger->loop_index) {
            continue;
        }
        FunctionId function_index = slot->function_index;
        LoopId loop_index = slot->loop_index;
        if (trigger->generation != 0 && slot->generation != trigger->generation) {
            continue;
        }
        vm_jit_cache_reset_slot(slot);
        vm_jit_apply_warmup_backoff(function_index, loop_index, true);
    }
}

void
vm_jit_flush_entries(void) {
    if (!vm.jit_cache.slots) {
        return;
    }

    for (size_t i = 0; i < vm.jit_cache.capacity; ++i) {
        vm_jit_cache_reset_slot(&vm.jit_cache.slots[i]);
    }
    vm.jit_cache.count = 0;
    memset(vm.jit_loop_blocklist, 0, sizeof(vm.jit_loop_blocklist));
    vm_tiering_invalidate_all_fusions();
}

static bool function_guard_allows_specialization(Function* function) {
    if (!function || function->tier != FUNCTION_TIER_SPECIALIZED) {
        return false;
    }

    if (!function->specialized_chunk) {
        return false;
    }

    if (!(g_profiling.enabledFlags & PROFILE_FUNCTION_CALLS) || !g_profiling.isActive) {
        return true;
    }

    uint64_t current_hits = getFunctionHitCount(function, false);
    if (current_hits == 0 && function->specialization_hits == 0) {
        return false;
    }

    uint64_t reference = function->specialization_hits;
    if (reference == 0) {
        reference = FUNCTION_SPECIALIZATION_THRESHOLD;
    }

    // Allow specialization to remain active while hotness remains above 25% of
    // the recorded profiling signal. Once it cools below that, request a deopt.
    return current_hits >= (reference / 4);
}

Chunk* vm_select_function_chunk(Function* function) {
    if (!function) {
        return NULL;
    }

    if (function_guard_allows_specialization(function)) {
        return function->specialized_chunk;
    }

    if (function->tier == FUNCTION_TIER_SPECIALIZED && function->deopt_handler) {
        function->deopt_handler(function);
    }

    return function->chunk;
}

static void
vm_fallback_to_interpreter(Function* function) {
    if (!function || !function->chunk) {
        return;
    }

    if (!function->specialized_chunk || !function->specialized_chunk->code) {
        return;
    }

    if (!vm.chunk || vm.chunk != function->specialized_chunk) {
        return;
    }

    if (!vm.register_file.current_frame) {
        return;
    }

    uint8_t* specialized_code = function->specialized_chunk->code;
    uint8_t* baseline_code = function->chunk->code;
    if (!specialized_code || !baseline_code) {
        return;
    }

    if (vm.ip < specialized_code + function->start) {
        vm.chunk = function->chunk;
        vm.ip = baseline_code + function->start;
        return;
    }

    size_t specialized_offset = (size_t)(vm.ip - (specialized_code + function->start));
    size_t baseline_limit = 0;
    if (function->chunk->count > function->start) {
        baseline_limit = (size_t)(function->chunk->count - function->start);
    }

    if (specialized_offset > baseline_limit) {
        specialized_offset = baseline_limit;
    }

    vm.chunk = function->chunk;
    vm.ip = baseline_code + function->start + specialized_offset;
}

void vm_default_deopt_stub(Function* function) {
    if (!function) {
        return;
    }

    if (function->tier == FUNCTION_TIER_SPECIALIZED) {
        function->tier = FUNCTION_TIER_BASELINE;
        function->specialization_hits = 0;
        vm_fallback_to_interpreter(function);
        if (function->debug_name) {
            fprintf(stderr,
                    "[tiering] Deoptimized function '%s', reverting to baseline bytecode\n",
                    function->debug_name);
        }
    }
}

static void
vm_clear_typed_window_range(TypedRegisterWindow* window, uint16_t start, uint16_t end) {
    if (!window) {
        return;
    }

    if (start >= VM_TYPED_REGISTER_LIMIT) {
        return;
    }

    if (end > VM_TYPED_REGISTER_LIMIT) {
        end = VM_TYPED_REGISTER_LIMIT;
    }

    for (uint16_t reg = start; reg < end; ++reg) {
        if (!typed_window_slot_live(window, reg)) {
            continue;
        }

        vm_reconcile_typed_register(reg);
        typed_window_clear_dirty(window, reg);
        window->reg_types[reg] = REG_TYPE_NONE;
        typed_window_clear_live(window, reg);
    }
}

static void
vm_apply_typed_deopt_landing_pad(Function* function, CallFrame* frame) {
    if (!frame) {
        return;
    }

    TypedRegisterWindow* window = frame->typed_window;
    if (!window) {
        window = vm_active_typed_window();
    }
    if (!window) {
        window = &vm.typed_regs.root_window;
    }
    if (!window) {
        return;
    }

    uint16_t frame_start = frame->frame_base;
    uint16_t frame_end = frame_start;
    if (frame->register_count > 0) {
        uint32_t computed_end = (uint32_t)frame_start + (uint32_t)frame->register_count;
        frame_end = computed_end > VM_TYPED_REGISTER_LIMIT ? VM_TYPED_REGISTER_LIMIT
                                                           : (uint16_t)computed_end;
    }
    vm_clear_typed_window_range(window, frame_start, frame_end);

    if (function && function->arity > 0) {
        uint16_t param_start = frame->parameterBaseRegister;
        uint32_t computed_param_end = (uint32_t)param_start + (uint32_t)function->arity;
        uint16_t param_end = computed_param_end > VM_TYPED_REGISTER_LIMIT ? VM_TYPED_REGISTER_LIMIT
                                                                          : (uint16_t)computed_param_end;
        vm_clear_typed_window_range(window, param_start, param_end);
    }

    if (frame->temp_count > 0) {
        uint16_t temp_start = frame->temp_base;
        uint32_t computed_temp_end = (uint32_t)temp_start + (uint32_t)frame->temp_count;
        uint16_t temp_end = computed_temp_end > VM_TYPED_REGISTER_LIMIT ? VM_TYPED_REGISTER_LIMIT
                                                                        : (uint16_t)computed_temp_end;
        vm_clear_typed_window_range(window, temp_start, temp_end);
    }
}

void vm_handle_type_error_deopt(void) {
    CallFrame* frame = vm.register_file.current_frame;
    if (!frame) {
        return;
    }

    if (frame->functionIndex == UINT16_MAX || frame->functionIndex >= (uint16_t)vm.functionCount) {
        return;
    }

    Function* function = &vm.functions[frame->functionIndex];
    if (!function || function->tier != FUNCTION_TIER_SPECIALIZED) {
        return;
    }

    vm_apply_typed_deopt_landing_pad(function, frame);

    if (function->deopt_handler) {
        function->deopt_handler(function);
    }
}
