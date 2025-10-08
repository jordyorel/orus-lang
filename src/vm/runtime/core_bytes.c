// Orus Language Project
// ---------------------------------------------------------------------------
// File: src/vm/runtime/core_bytes.c
// Author: Orus Contributors
// Description: Runtime helpers and intrinsic bindings for the byte buffer type.

#include "runtime/core_bytes.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "runtime/memory.h"
#include "vm/vm_string_ops.h"

static bool double_to_integral_size(double value, size_t* out) {
    if (!out || isnan(value) || !isfinite(value)) {
        return false;
    }

    if (value <= 0.0) {
        *out = 0;
        return true;
    }

    if (value > (double)SIZE_MAX) {
        return false;
    }

    double truncated = floor(value);
    if (truncated != value) {
        return false;
    }

    *out = (size_t)truncated;
    return true;
}

static bool value_to_size(Value value, size_t* out) {
    if (!out) {
        return false;
    }

    switch (value.type) {
        case VAL_BOOL:
            *out = AS_BOOL(value) ? 1u : 0u;
            return true;
        case VAL_I32: {
            int32_t v = AS_I32(value);
            if (v <= 0) {
                *out = 0;
                return true;
            }
            *out = (size_t)v;
            return true;
        }
        case VAL_I64: {
            int64_t v = AS_I64(value);
            if (v <= 0) {
                *out = 0;
                return true;
            }
            if ((uint64_t)v > (uint64_t)SIZE_MAX) {
                return false;
            }
            *out = (size_t)v;
            return true;
        }
        case VAL_U32:
            *out = (size_t)AS_U32(value);
            return true;
        case VAL_U64: {
            uint64_t v = AS_U64(value);
            if (v > (uint64_t)SIZE_MAX) {
                return false;
            }
            *out = (size_t)v;
            return true;
        }
        case VAL_F64:
            return double_to_integral_size(AS_F64(value), out);
        case VAL_NUMBER:
            return double_to_integral_size(value.as.number, out);
        default:
            return false;
    }
}

static bool double_to_byte(double value, uint8_t* out) {
    if (!out || isnan(value) || !isfinite(value)) {
        return false;
    }

    if (value < 0.0 || value > 255.0) {
        return false;
    }

    double truncated = floor(value);
    if (truncated != value) {
        return false;
    }

    *out = (uint8_t)truncated;
    return true;
}

static bool value_to_byte(Value value, uint8_t* out) {
    if (!out) {
        return false;
    }

    switch (value.type) {
        case VAL_BOOL:
            *out = AS_BOOL(value) ? 1u : 0u;
            return true;
        case VAL_I32: {
            int32_t v = AS_I32(value);
            if (v < 0 || v > 255) {
                return false;
            }
            *out = (uint8_t)v;
            return true;
        }
        case VAL_I64: {
            int64_t v = AS_I64(value);
            if (v < 0 || v > 255) {
                return false;
            }
            *out = (uint8_t)v;
            return true;
        }
        case VAL_U32: {
            uint32_t v = AS_U32(value);
            if (v > 255u) {
                return false;
            }
            *out = (uint8_t)v;
            return true;
        }
        case VAL_U64: {
            uint64_t v = AS_U64(value);
            if (v > 255u) {
                return false;
            }
            *out = (uint8_t)v;
            return true;
        }
        case VAL_F64:
            return double_to_byte(AS_F64(value), out);
        case VAL_NUMBER:
            return double_to_byte(value.as.number, out);
        default:
            return false;
    }
}

ObjByteBuffer* vm_bytes_from_string_object(ObjString* string) {
    if (!string) {
        return allocateByteBuffer(0);
    }

    if (string->length <= 0) {
        return allocateByteBuffer(0);
    }

    const char* chars = string_get_chars(string);
    if (!chars) {
        return allocateByteBuffer(0);
    }

    return allocateByteBufferCopy((const uint8_t*)chars, (size_t)string->length);
}

ObjString* vm_bytes_to_string_object(const ObjByteBuffer* buffer) {
    if (!buffer || buffer->length == 0 || !buffer->data) {
        return allocateString("", 0);
    }

    return allocateString((const char*)buffer->data, (int)buffer->length);
}

Value vm_core_bytes_alloc(int argCount, Value* args) {
    size_t length = 0;
    if (!args || argCount < 1 || !value_to_size(args[0], &length)) {
        return BYTES_VAL(allocateByteBuffer(0));
    }

    return BYTES_VAL(allocateByteBuffer(length));
}

Value vm_core_bytes_alloc_fill(int argCount, Value* args) {
    size_t length = 0;
    uint8_t fill = 0;
    if (!args || argCount < 2 || !value_to_size(args[0], &length) ||
        !value_to_byte(args[1], &fill)) {
        return BYTES_VAL(allocateByteBuffer(0));
    }

    return BYTES_VAL(allocateByteBufferFilled(length, fill));
}

Value vm_core_bytes_slice(int argCount, Value* args) {
    if (!args || argCount < 1 || !IS_BYTES(args[0])) {
        return BYTES_VAL(allocateByteBuffer(0));
    }

    ObjByteBuffer* source = AS_BYTES(args[0]);
    if (!source) {
        return BYTES_VAL(allocateByteBuffer(0));
    }

    size_t start = 0;
    size_t length = 0;
    bool has_start = (argCount > 1) && value_to_size(args[1], &start);
    bool has_length = (argCount > 2) && value_to_size(args[2], &length);

    if (!has_start) {
        start = 0;
    }
    if (!has_length) {
        length = source->length > start ? source->length - start : 0;
    }

    return BYTES_VAL(allocateByteBufferSlice(source, start, length));
}

Value vm_core_bytes_from_string(int argCount, Value* args) {
    if (!args || argCount < 1 || !IS_STRING(args[0])) {
        return BYTES_VAL(allocateByteBuffer(0));
    }

    ObjString* string = AS_STRING(args[0]);
    return BYTES_VAL(vm_bytes_from_string_object(string));
}

Value vm_core_bytes_to_string(int argCount, Value* args) {
    if (!args || argCount < 1 || !IS_BYTES(args[0])) {
        return STRING_VAL(allocateString("", 0));
    }

    ObjByteBuffer* buffer = AS_BYTES(args[0]);
    return STRING_VAL(vm_bytes_to_string_object(buffer));
}
