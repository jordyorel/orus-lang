/*
 * Orus Language Project
 * ---------------------------------------------------------------------------
 * File: src/vm/runtime/builtin_range.c
 * Author: OpenAI Assistant
 * Description: Implements the range() builtin producing integer iterators.
 */

#include "runtime/builtins.h"
#include "runtime/memory.h"

#include <limits.h>

static bool value_to_i64(Value value, int64_t* out) {
    if (!out) {
        return false;
    }

    switch (value.type) {
        case VAL_I32:
            *out = (int64_t)AS_I32(value);
            return true;
        case VAL_I64:
            *out = AS_I64(value);
            return true;
        case VAL_U32:
            *out = (int64_t)AS_U32(value);
            return true;
        case VAL_U64: {
            uint64_t raw = AS_U64(value);
            if (raw > (uint64_t)INT64_MAX) {
                return false;
            }
            *out = (int64_t)raw;
            return true;
        }
        default:
            break;
    }

    return false;
}

bool builtin_range(Value* args, int count, Value* out_value) {
    if (!out_value) {
        return false;
    }

    if (count < 1 || count > 3) {
        return false;
    }

    if (count > 0 && !args) {
        return false;
    }

    int64_t start = 0;
    int64_t stop = 0;
    int64_t step = 1;

    if (count == 1) {
        if (!value_to_i64(args[0], &stop)) {
            return false;
        }
    } else if (count == 2) {
        if (!value_to_i64(args[0], &start) || !value_to_i64(args[1], &stop)) {
            return false;
        }
    } else {
        if (!value_to_i64(args[0], &start) ||
            !value_to_i64(args[1], &stop) ||
            !value_to_i64(args[2], &step)) {
            return false;
        }
    }

    if (step == 0) {
        return false;
    }

    ObjRangeIterator* iterator = allocateRangeIterator(start, stop, step);
    if (!iterator) {
        return false;
    }

    *out_value = RANGE_ITERATOR_VAL(iterator);
    return true;
}
