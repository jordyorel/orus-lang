// Orus Language Project
// ---------------------------------------------------------------------------
// File: src/vm/runtime/builtin_array_repeat.c
// Description: Implements array repetition logic used by the `array * count` operator.

#include "runtime/builtins.h"
#include "runtime/memory.h"

#include <limits.h>
#include <stdbool.h>

static bool extract_repeat_count(Value value, int64_t* out_count) {
    if (!out_count) {
        return false;
    }

    int64_t count = 0;
    if (IS_I32(value)) {
        count = AS_I32(value);
    } else if (IS_I64(value)) {
        count = AS_I64(value);
    } else if (IS_U32(value)) {
        count = (int64_t)AS_U32(value);
    } else if (IS_U64(value)) {
        uint64_t raw = AS_U64(value);
        if (raw > (uint64_t)INT64_MAX) {
            return false;
        }
        count = (int64_t)raw;
    } else {
        return false;
    }

    if (count < 0) {
        return false;
    }

    *out_count = count;
    return true;
}

bool builtin_array_repeat(Value array_value, Value count_value, Value* out_value) {
    if (!out_value) {
        return false;
    }

    if (!IS_ARRAY(array_value)) {
        return false;
    }

    ObjArray* source = AS_ARRAY(array_value);
    if (!source) {
        return false;
    }

    int64_t repeat = 0;
    if (!extract_repeat_count(count_value, &repeat)) {
        return false;
    }

    int length = source->length;
    if (repeat == 0 || length == 0) {
        ObjArray* empty = allocateArray(0);
        if (!empty) {
            return false;
        }
        empty->length = 0;
        *out_value = ARRAY_VAL(empty);
        return true;
    }

    if (repeat > INT_MAX) {
        return false;
    }

    int64_t total = (int64_t)length * repeat;
    if (total > INT_MAX) {
        return false;
    }

    ObjArray* result = allocateArray((int)total);
    if (!result) {
        return false;
    }

    arrayEnsureCapacity(result, (int)total);

    for (int64_t i = 0; i < repeat; i++) {
        for (int j = 0; j < length; j++) {
            result->elements[result->length++] = source->elements[j];
        }
    }

    *out_value = ARRAY_VAL(result);
    return true;
}
