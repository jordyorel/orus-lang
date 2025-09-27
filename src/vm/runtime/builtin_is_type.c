/*
 * Orus Language Project
 * ---------------------------------------------------------------------------
 * File: src/vm/runtime/builtin_is_type.c
 * Author: OpenAI Assistant
 * Description: Implements the builtin is_type routine that compares a value's
 *              runtime type name against a provided string.
 */

#include "runtime/builtins.h"

#include <string.h>

#include "builtin_type_common.h"

bool builtin_is_type(Value value, Value type_identifier, Value* out_value) {
    if (!out_value) {
        return false;
    }

    const char* label = builtin_value_type_label(value);
    if (!label) {
        label = "unknown";
    }

    bool matches = false;
    if (IS_STRING(type_identifier)) {
        ObjString* expected = AS_STRING(type_identifier);
        const char* expected_chars = (expected && expected->chars) ? expected->chars : "";
        matches = (strcmp(label, expected_chars) == 0);
    }

    *out_value = BOOL_VAL(matches);
    return true;
}
