/*
 * Orus Language Project
 * ---------------------------------------------------------------------------
 * File: include/vm/vm_validation.h
 * Author: Jordy Orel KONDA
 * Copyright (c) 2025 Jordy Orel KONDA
 * License: MIT License (see LICENSE file in the project root)
 * Description: Declares validation helpers that verify bytecode and runtime
 *              invariants.
 */

#ifndef VM_VALIDATION_H
#define VM_VALIDATION_H

#include <stdbool.h>
#include "vm.h"

// Validate that a register index is within valid range.
// Returns true if valid, false otherwise and reports ERROR_INDEX.
bool validate_register_index(int index);

// Validate that the VM call frame count does not exceed the limit.
// Returns true if valid, false otherwise and reports ERROR_RUNTIME.
bool validate_frame_count(int count);

#endif // VM_VALIDATION_H
