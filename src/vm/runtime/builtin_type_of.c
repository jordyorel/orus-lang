/*
 * Orus Language Project
 * ---------------------------------------------------------------------------
 * File: src/vm/runtime/builtin_type_of.c
 * Author: OpenAI Assistant
 * Description: Implements the builtin type_of routine that reports the runtime
 *              type name of a value.
 */

#include "runtime/builtins.h"
#include "runtime/memory.h"

#include <string.h>

#include "builtin_type_common.h"

bool builtin_type_of(Value value, Value* out_value) {
    if (!out_value) {
        return false;
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
