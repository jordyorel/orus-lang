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
