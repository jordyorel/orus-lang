/*
 * Orus Language Project
 * ---------------------------------------------------------------------------
 * File: src/vm/core/vm_tagged_union.c
 * Author: Jordy Orel KONDA
 * Copyright (c) 2025 Jordy Orel KONDA
 * License: MIT License (see LICENSE file in the project root)
 * Description: Implements the tagged union operations for dynamic value
 *              representation.
 */

#include "vm_internal.h"
#include "vm/vm_tagged_union.h"
#include "runtime/memory.h"
#include "vm/vm_string_ops.h"

#include <string.h>

static bool copy_payload_to_array(const TaggedUnionSpec* spec, ObjArray** out_array) {
    if (!out_array) {
        return false;
    }

    *out_array = NULL;

    if (!spec || spec->payload_count <= 0) {
        return true;
    }

    if (!spec->payload) {
        return false;
    }

    ObjArray* array = allocateArray(spec->payload_count);
    if (!array || !array->elements) {
        return false;
    }

    for (int i = 0; i < spec->payload_count; i++) {
        array->elements[i] = spec->payload[i];
    }
    array->length = spec->payload_count;

    *out_array = array;
    return true;
}

bool vm_make_tagged_union(const TaggedUnionSpec* spec, Value* out_value) {
    if (!spec || !out_value || !spec->type_name) {
        return false;
    }

    ObjString* type_name = intern_string(spec->type_name, (int)strlen(spec->type_name));
    if (!type_name) {
        return false;
    }

    ObjString* variant_name = NULL;
    if (spec->variant_name && spec->variant_name[0] != '\0') {
        variant_name = intern_string(spec->variant_name, (int)strlen(spec->variant_name));
        if (!variant_name) {
            return false;
        }
    }

    ObjArray* payload = NULL;
    if (!copy_payload_to_array(spec, &payload)) {
        return false;
    }

    ObjEnumInstance* instance = allocateEnumInstance(type_name, variant_name, spec->variant_index, payload);
    if (!instance) {
        return false;
    }

    *out_value = ENUM_VAL(instance);
    return true;
}

bool vm_result_ok(Value inner, Value* out_value) {
    TaggedUnionSpec spec = {
        .type_name = "Result",
        .variant_name = "Ok",
        .variant_index = 0,
        .payload = &inner,
        .payload_count = 1,
    };
    return vm_make_tagged_union(&spec, out_value);
}

bool vm_result_err(Value error_value, Value* out_value) {
    TaggedUnionSpec spec = {
        .type_name = "Result",
        .variant_name = "Err",
        .variant_index = 1,
        .payload = &error_value,
        .payload_count = 1,
    };
    return vm_make_tagged_union(&spec, out_value);
}
