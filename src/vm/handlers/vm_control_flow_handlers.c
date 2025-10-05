// Orus Language Project
// ---------------------------------------------------------------------------
// File: src/vm/handlers/vm_control_flow_handlers.c
// Author: Jordy Orel KONDA
// Copyright (c) 2023 Jordy Orel KONDA
// License: MIT License (see LICENSE file in the project root)
// Description: Implements control-flow opcode handlers managing jumps and branches.



#include "vm/vm_opcode_handlers.h"
#include "vm/vm_dispatch.h"
#include "vm/vm_control_flow.h"

// ====== Jump Operation Handlers ======

bool handle_jump_short(void) {
    uint8_t offset = READ_BYTE();
    return CF_JUMP_SHORT(offset);
}

bool handle_jump_back_short(void) {
    uint8_t offset = READ_BYTE();
    return CF_JUMP_BACK_SHORT(offset);
}

bool handle_jump_if_not_short(void) {
    uint8_t reg = READ_BYTE();
    uint8_t offset = READ_BYTE();
    return CF_JUMP_IF_NOT_SHORT(reg, offset);
}

bool handle_loop_short(void) {
    uint8_t offset = READ_BYTE();

    // Hot path detection: Profile short loop iterations (tight loops)
    if (g_profiling.isActive && (g_profiling.enabledFlags & PROFILE_HOT_PATHS)) {
        static uint64_t short_loop_iterations = 0;
        short_loop_iterations++;
        profileHotPath((void*)(vm.ip - vm.chunk->code), short_loop_iterations);
    }

    return CF_LOOP_SHORT(offset);
}

// ====== Long Jump Operation Handlers ======

bool handle_jump_long(void) {
    uint16_t offset = READ_SHORT();
    return CF_JUMP(offset);
}

bool handle_jump_if_not_long(void) {
    uint8_t reg = READ_BYTE();
    uint16_t offset = READ_SHORT();
    return CF_JUMP_IF_NOT(reg, offset);
}

bool handle_loop_long(void) {
    uint16_t offset = READ_SHORT();

    // Hot path detection: Profile loop iterations
    if (g_profiling.isActive && (g_profiling.enabledFlags & PROFILE_HOT_PATHS)) {
        static uint64_t loop_iterations = 0;
        loop_iterations++;
        profileHotPath((void*)(vm.ip - vm.chunk->code), loop_iterations);
    }

    return CF_LOOP(offset);
}
