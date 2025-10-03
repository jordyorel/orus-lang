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

static inline void handle_jump_short(void) {
    uint8_t offset = READ_BYTE();
    CF_JUMP_SHORT(offset);
}

static inline void handle_jump_back_short(void) {
    uint8_t offset = READ_BYTE();
    CF_JUMP_BACK_SHORT(offset);
}

static inline void handle_jump_if_not_short(void) {
    uint8_t reg = READ_BYTE();
    uint8_t offset = READ_BYTE();
    if (!CF_JUMP_IF_NOT_SHORT(reg, offset)) {
        return;
    }
}

static inline void handle_loop_short(void) {
    uint8_t offset = READ_BYTE();
    CF_LOOP_SHORT(offset);
}

// ====== Extended Jump Operation Handlers ======

static inline void handle_jump_if_false(void) {
    uint8_t reg = READ_BYTE();
    uint16_t offset = READ_SHORT();
    if (!CF_JUMP_IF_NOT(reg, offset)) {
        return;
    }
}

static inline void handle_jump_if_true(void) {
    uint8_t reg = READ_BYTE();
    uint16_t offset = READ_SHORT();
    if (!CF_JUMP_IF(reg, offset)) {
        return;
    }
}

// ====== Long Jump Operation Handlers ======

static inline void handle_jump_long(void) {
    uint16_t offset = READ_SHORT();
    CF_JUMP(offset);
}

static inline void handle_jump_if_not_long(void) {
    uint8_t reg = READ_BYTE();
    uint16_t offset = READ_SHORT();
    if (!CF_JUMP_IF_NOT(reg, offset)) {
        return;
    }
}

static inline void handle_loop_long(void) {
    uint16_t offset = READ_SHORT();
    CF_LOOP(offset);
}