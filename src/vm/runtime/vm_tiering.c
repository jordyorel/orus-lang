// Orus Language Project
// ---------------------------------------------------------------------------
// File: src/vm/runtime/vm_tiering.c
// Description: Implements tier selection logic for specialized bytecode and
//              provides default deoptimization handling.

#include "vm/vm_tiering.h"
#include "vm/vm.h"
#include "vm/vm_profiling.h"
#include <stdio.h>

#ifndef FUNCTION_SPECIALIZATION_THRESHOLD
#define FUNCTION_SPECIALIZATION_THRESHOLD 512ULL
#endif

extern VM vm;

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

void vm_default_deopt_stub(Function* function) {
    if (!function) {
        return;
    }

    if (function->tier == FUNCTION_TIER_SPECIALIZED) {
        function->tier = FUNCTION_TIER_BASELINE;
        function->specialization_hits = 0;
        if (function->debug_name) {
            fprintf(stderr,
                    "[tiering] Deoptimized function '%s', reverting to baseline bytecode\n",
                    function->debug_name);
        }
    }
}
