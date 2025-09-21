/*
 * File: src/vm/handlers/vm_control_flow_handlers.c
 * High-performance control flow operation handlers for the Orus VM
 * 
 * Design Philosophy:
 * - Static inline functions for zero-cost abstraction
 * - Preserve computed-goto dispatch performance
 * - Clean separation of control flow operations from dispatch logic
 * - Maintain exact same behavior as original implementations
 */

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