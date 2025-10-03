
// Orus Language Project
// ---------------------------------------------------------------------------
// File: src/vm/runtime/builtin_sorted.c
// Author: Jordy Orel KONDA
// Description: Implements the sorted() builtin that returns a sorted copy of an array.


#include "runtime/builtins.h"
#include "runtime/memory.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

static ValueType g_sorted_element_type = VAL_BOOL;

#define TIMSORT_MIN_MERGE 32

static inline int value_compare(const Value* a, const Value* b);
static void timsort(Value* values, int length);

static int compare_bool(const Value* a, const Value* b) {
    bool left = AS_BOOL(*a);
    bool right = AS_BOOL(*b);
    return (int)left - (int)right;
}

static int compare_i32(const Value* a, const Value* b) {
    int32_t left = AS_I32(*a);
    int32_t right = AS_I32(*b);
    if (left < right) return -1;
    if (left > right) return 1;
    return 0;
}

static int compare_i64(const Value* a, const Value* b) {
    int64_t left = AS_I64(*a);
    int64_t right = AS_I64(*b);
    if (left < right) return -1;
    if (left > right) return 1;
    return 0;
}

static int compare_u32(const Value* a, const Value* b) {
    uint32_t left = AS_U32(*a);
    uint32_t right = AS_U32(*b);
    if (left < right) return -1;
    if (left > right) return 1;
    return 0;
}

static int compare_u64(const Value* a, const Value* b) {
    uint64_t left = AS_U64(*a);
    uint64_t right = AS_U64(*b);
    if (left < right) return -1;
    if (left > right) return 1;
    return 0;
}

static int compare_f64(const Value* a, const Value* b) {
    double left = AS_F64(*a);
    double right = AS_F64(*b);

    bool left_nan = isnan(left);
    bool right_nan = isnan(right);
    if (left_nan && right_nan) return 0;
    if (left_nan) return 1;
    if (right_nan) return -1;

    if (left < right) return -1;
    if (left > right) return 1;
    return 0;
}

static int compare_string(const Value* a, const Value* b) {
    ObjString* left = AS_STRING(*a);
    ObjString* right = AS_STRING(*b);
    if (left == right) return 0;
    if (!left) return right ? -1 : 0;
    if (!right) return 1;
    return strcmp(left->chars, right->chars);
}

static int compare_values(const void* lhs, const void* rhs) {
    const Value* a = (const Value*)lhs;
    const Value* b = (const Value*)rhs;

    switch (g_sorted_element_type) {
        case VAL_BOOL:
            return compare_bool(a, b);
        case VAL_I32:
            return compare_i32(a, b);
        case VAL_I64:
            return compare_i64(a, b);
        case VAL_U32:
            return compare_u32(a, b);
        case VAL_U64:
            return compare_u64(a, b);
        case VAL_F64:
        case VAL_NUMBER:
            return compare_f64(a, b);
        case VAL_STRING:
            return compare_string(a, b);
        default:
            return 0;
    }
}

static inline int value_compare(const Value* a, const Value* b) {
    return compare_values(a, b);
}

static int min_run_length(int n) {
    int r = 0;
    while (n >= TIMSORT_MIN_MERGE) {
        r |= n & 1;
        n >>= 1;
    }
    return n + r;
}

static void insertion_sort(Value* values, int left, int right) {
    for (int i = left + 1; i <= right; i++) {
        Value key = values[i];
        int j = i - 1;
        while (j >= left && value_compare(&key, &values[j]) < 0) {
            values[j + 1] = values[j];
            j--;
        }
        values[j + 1] = key;
    }
}

static void merge(Value* values, Value* buffer, int left, int mid, int right) {
    const int left_length = mid - left + 1;
    const int right_length = right - mid;

    memcpy(buffer, &values[left], (size_t)left_length * sizeof(Value));

    int idx_left = 0;
    int idx_right = 0;
    int idx = left;
    const int right_start = mid + 1;

    while (idx_left < left_length && idx_right < right_length) {
        if (value_compare(&buffer[idx_left], &values[right_start + idx_right]) <= 0) {
            values[idx++] = buffer[idx_left++];
        } else {
            values[idx++] = values[right_start + idx_right++];
        }
    }

    while (idx_left < left_length) {
        values[idx++] = buffer[idx_left++];
    }
}

static void timsort(Value* values, int length) {
    if (length < 2) {
        return;
    }

    const int min_run = min_run_length(length);

    for (int start = 0; start < length; start += min_run) {
        int end = start + min_run - 1;
        if (end >= length) {
            end = length - 1;
        }
        insertion_sort(values, start, end);
    }

    Value* buffer = GROW_ARRAY(Value, NULL, 0, length);
    if (!buffer) {
        insertion_sort(values, 0, length - 1);
        return;
    }

    for (int size = min_run; size < length; size *= 2) {
        for (int left = 0; left < length; left += size * 2) {
            int mid = left + size - 1;
            if (mid >= length - 1) {
                break;
            }

            int right = left + size * 2 - 1;
            if (right >= length) {
                right = length - 1;
            }

            if (value_compare(&values[mid], &values[mid + 1]) <= 0) {
                continue;
            }

            merge(values, buffer, left, mid, right);
        }
    }

    FREE_ARRAY(Value, buffer, length);
}

static bool validate_elements(const ObjArray* array, ValueType* out_type) {
    if (!array || !out_type) {
        return false;
    }

    if (array->length == 0) {
        *out_type = VAL_BOOL;
        return true;
    }

    ValueType first_type = array->elements[0].type;
    switch (first_type) {
        case VAL_BOOL:
        case VAL_I32:
        case VAL_I64:
        case VAL_U32:
        case VAL_U64:
        case VAL_F64:
        case VAL_NUMBER:
        case VAL_STRING:
            break;
        default:
            return false;
    }

    for (int i = 1; i < array->length; i++) {
        if (array->elements[i].type != first_type) {
            return false;
        }
    }

    *out_type = first_type;
    return true;
}

bool builtin_sorted(Value array_value, Value* out_value) {
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

    ValueType element_type;
    if (!validate_elements(source, &element_type)) {
        return false;
    }

    ObjArray* result = allocateArray(source->length);
    if (!result) {
        return false;
    }

    if (source->length > 0) {
        arrayEnsureCapacity(result, source->length);
        for (int i = 0; i < source->length; i++) {
            result->elements[i] = source->elements[i];
        }
    }

    result->length = source->length;

    if (result->length > 1) {
        g_sorted_element_type = element_type;
        timsort(result->elements, result->length);
    }

    *out_value = ARRAY_VAL(result);
    return true;
}
