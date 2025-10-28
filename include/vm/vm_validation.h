// Orus Language Project

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
