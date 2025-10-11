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

#ifndef FUNCTION_SPECIALIZATION_THRESHOLD
#define FUNCTION_SPECIALIZATION_THRESHOLD 512ULL
#endif

extern VM vm;

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
        vm.jit_cache.count++;
        return slot;
    }

    return NULL;
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

    if (slot->entry.code_ptr && slot->entry.code_ptr != entry->code_ptr) {
        orus_jit_backend_release_entry(vm.jit_backend, &slot->entry);
        memset(&slot->entry, 0, sizeof(slot->entry));
    }

    slot->entry = *entry;
    slot->generation = ++vm.jit_cache.next_generation;

    entry->code_ptr = NULL;
    entry->entry_point = NULL;
    entry->code_capacity = 0;
    entry->code_size = 0;
    entry->debug_name = NULL;

    return slot->generation;
}

void
vm_jit_invalidate_entry(const JITDeoptTrigger* trigger) {
    if (!trigger) {
        return;
    }

    vm.jit_deopt_count++;

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
        if (trigger->generation != 0 && slot->generation != trigger->generation) {
            continue;
        }
        vm_jit_cache_reset_slot(slot);
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

    if (function->deopt_stub_chunk && function->deopt_stub_chunk->code &&
        function->deopt_stub_chunk->count > 0) {
        uint8_t param_count = function->deopt_stub_chunk->code[0];
        uint16_t base = frame->parameterBaseRegister;
        TypedRegisterWindow* window = vm_active_typed_window();
        if (!window) {
            window = &vm.typed_regs.root_window;
        }

        for (uint8_t i = 0; i < param_count; ++i) {
            uint16_t reg = (uint16_t)(base + i);
            if (!vm_typed_reg_in_range(reg) || !window) {
                continue;
            }

            if (!typed_window_slot_live(window, reg)) {
                continue;
            }

            uint8_t reg_type = window->reg_types[reg];
            if (reg_type != REG_TYPE_NONE) {
                vm_clear_typed_register_slot(reg, reg_type);
            }
        }
    }

    if (function->deopt_handler) {
        function->deopt_handler(function);
    }
}
