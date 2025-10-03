// Orus Language Project
// ---------------------------------------------------------------------------
// File: src/vm/runtime/builtin_type_of.c
// Author: JORDY OREL KONDA
// Description: Implements the builtin type_of routine that reports the runtime type name of a value.

#include "runtime/builtins.h"
#include "runtime/memory.h"

#include <string.h>

#include "builtin_type_common.h"

bool builtin_type_of(Value value, Value* out_value) {
    if (!out_value) {
        return false;
    }

    if (IS_ERROR(value)) {
        char* label_chars = NULL;
        size_t label_length = 0;
        size_t label_capacity = 0;
        if (!builtin_alloc_error_label(value, &label_chars, &label_length,
                                       &label_capacity)) {
            return false;
        }

        ObjString* type_name =
            allocateStringFromBuffer(label_chars, label_capacity, (int)label_length);
        if (!type_name) {
            reallocate(label_chars, label_capacity, 0);
            return false;
        }

        *out_value = STRING_VAL(type_name);
        return true;
    }

    const char* label = builtin_value_type_label(value);
    if (!label) {
        label = "unknown";
    }

    ObjString* type_name = allocateString(label, (int)strlen(label));
    if (!type_name) {
        return false;
    }

    *out_value = STRING_VAL(type_name);
    return true;
}
