// Orus Language Project
//  ---------------------------------------------------------------------------
//  File: src/vm/runtime/builtin_istype.c
//  Author: Jordy Orel KONDA
//  Description: Implements the builtin istype routine that compares a value's runtime type name against a provided string.

#include "runtime/builtins.h"

#include <string.h>

#include "builtin_type_common.h"
#include "vm/vm_string_ops.h"

bool builtin_istype(Value value, Value type_identifier, Value* out_value) {
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

        bool matches = false;
        if (IS_STRING(type_identifier)) {
            ObjString* expected = AS_STRING(type_identifier);
            const char* expected_chars = expected ? string_get_chars(expected) : NULL;
            if (!expected_chars) {
                expected_chars = "";
            }
            matches = (strcmp(label_chars, expected_chars) == 0);
        }

        reallocate(label_chars, label_capacity, 0);
        *out_value = BOOL_VAL(matches);
        return true;
    }

    const char* label = builtin_value_type_label(value);
    if (!label) {
        label = "unknown";
    }

    bool matches = false;
    if (IS_STRING(type_identifier)) {
        ObjString* expected = AS_STRING(type_identifier);
        const char* expected_chars = expected ? string_get_chars(expected) : NULL;
        if (!expected_chars) {
            expected_chars = "";
        }
        matches = (strcmp(label, expected_chars) == 0);
    }

    *out_value = BOOL_VAL(matches);
    return true;
}
