/*
 * Orus Language Project
 * ---------------------------------------------------------------------------
 * File: src/vm/core/vm_validation.c
 * Author: Jordy Orel KONDA
 * Copyright (c) 2025 Jordy Orel KONDA
 * License: MIT License (see LICENSE file in the project root)
 * Description: Validates VM state and bytecode invariants prior to execution.
 */

#include "vm_internal.h"
#include "vm/vm_validation.h"

// Validate that the given register index is within bounds.
bool validate_register_index(int index) {
    if (index < 0 || index >= REGISTER_COUNT) {
        runtimeError(ERROR_INDEX, CURRENT_LOCATION(),
                     "Register index %d out of bounds (limit: %d)",
                     index, REGISTER_COUNT);
        return false;
    }
    return true;
}

// Validate that the number of call frames stays under the limit.
bool validate_frame_count(int count) {
    if (count < 0 || count >= FRAMES_MAX) {
        runtimeError(ERROR_RUNTIME, CURRENT_LOCATION(),
                     "Call frame count %d exceeds maximum %d",
                     count, FRAMES_MAX);
        return false;
    }
    return true;
}
