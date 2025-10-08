// Orus Language Project
// ---------------------------------------------------------------------------
// File: src/vm/runtime/core_fs_intrinsics.c
// Author: Orus Contributors
// Description: Filesystem intrinsic bindings exposed to the runtime.

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "runtime/core_intrinsics.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <sys/types.h>
#endif

#include "runtime/core_bytes.h"
#include "runtime/core_fs_handles.h"
#include "runtime/memory.h"
#include "vm/vm_string_ops.h"

static bool value_to_size(Value value, size_t* out) {
    if (!out) {
        return false;
    }

    switch (value.type) {
        case VAL_I32:
            if (AS_I32(value) < 0) {
                return false;
            }
            *out = (size_t)AS_I32(value);
            return true;
        case VAL_I64:
            if (AS_I64(value) < 0 ||
                (uint64_t)AS_I64(value) > (uint64_t)SIZE_MAX) {
                return false;
            }
            *out = (size_t)AS_I64(value);
            return true;
        case VAL_U32:
            *out = (size_t)AS_U32(value);
            return true;
        case VAL_U64:
            if (AS_U64(value) > (uint64_t)SIZE_MAX) {
                return false;
            }
            *out = (size_t)AS_U64(value);
            return true;
        case VAL_F64: {
            double v = AS_F64(value);
            if (!isfinite(v) || v < 0.0 || v > (double)SIZE_MAX) {
                return false;
            }
            double truncated = floor(v);
            if (truncated != v) {
                return false;
            }
            *out = (size_t)truncated;
            return true;
        }
        default:
            return false;
    }
}

static bool value_to_int64(Value value, int64_t* out) {
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
        case VAL_U64:
            if (AS_U64(value) > (uint64_t)INT64_MAX) {
                return false;
            }
            *out = (int64_t)AS_U64(value);
            return true;
        case VAL_F64: {
            double v = AS_F64(value);
            if (!isfinite(v) || v < (double)INT64_MIN || v > (double)INT64_MAX) {
                return false;
            }
            double truncated = floor(v);
            if (truncated != v) {
                return false;
            }
            *out = (int64_t)truncated;
            return true;
        }
        default:
            return false;
    }
}

static bool value_to_int(Value value, int* out) {
    int64_t temp = 0;
    if (!out || !value_to_int64(value, &temp)) {
        return false;
    }

    if (temp < INT_MIN || temp > INT_MAX) {
        return false;
    }

    *out = (int)temp;
    return true;
}

static Value allocate_file_value(FILE* handle, Value path_value) {
    ObjString* path_str = IS_STRING(path_value) ? AS_STRING(path_value) : NULL;
    return vm_file_wrap_handle(handle, path_str, true);
}

static Value make_empty_bytes(void) {
    return BYTES_VAL(allocateByteBuffer(0));
}

static bool buffer_from_value(Value value, const uint8_t** data, size_t* length) {
    if (!data || !length) {
        return false;
    }

    if (IS_BYTES(value)) {
        ObjByteBuffer* buffer = AS_BYTES(value);
        *data = (buffer && buffer->length > 0) ? buffer->data : NULL;
        *length = buffer ? buffer->length : 0;
        return true;
    }

    if (IS_STRING(value)) {
        ObjString* string = AS_STRING(value);
        *data = (const uint8_t*)(string ? string_get_chars(string) : NULL);
        *length = string ? (size_t)string->length : 0;
        return true;
    }

    return false;
}

static Value vm_core_fs_open(int argCount, Value* args) {
    if (!args || argCount < 1 || !IS_STRING(args[0])) {
        return FILE_VAL(NULL);
    }

    Value path_value = args[0];
    ObjString* path_str = AS_STRING(path_value);
    const char* path_chars = path_str ? string_get_chars(path_str) : NULL;
    const char* mode_chars = "r";

    if (argCount > 1 && IS_STRING(args[1])) {
        ObjString* mode = AS_STRING(args[1]);
        mode_chars = mode ? string_get_chars(mode) : mode_chars;
    }

    if (!path_chars || !mode_chars) {
        return FILE_VAL(NULL);
    }

    FILE* handle = fopen(path_chars, mode_chars);
    if (!handle) {
        return FILE_VAL(NULL);
    }

    return allocate_file_value(handle, path_value);
}

static Value vm_core_fs_close(int argCount, Value* args) {
    (void)argCount;
    if (!args || argCount < 1) {
        return BOOL_VAL(false);
    }

    return BOOL_VAL(vm_file_close_value(args[0]));
}

static Value vm_core_fs_is_open(int argCount, Value* args) {
    (void)argCount;
    if (!args || argCount < 1) {
        return BOOL_VAL(false);
    }
    return BOOL_VAL(vm_file_value_is_open(args[0]));
}

static Value vm_core_fs_read(int argCount, Value* args) {
    if (!args || argCount < 2) {
        return make_empty_bytes();
    }

    FILE* handle = vm_file_borrow_handle(args[0]);
    if (!handle) {
        return make_empty_bytes();
    }

    size_t length = 0;
    if (!value_to_size(args[1], &length) || length == 0) {
        return make_empty_bytes();
    }

    ObjByteBuffer* buffer = allocateByteBuffer(length);
    if (!buffer || !buffer->data) {
        return make_empty_bytes();
    }

    size_t read = fread(buffer->data, 1, length, handle);
    if (read < length && ferror(handle)) {
        clearerr(handle);
        buffer->length = 0;
    } else {
        buffer->length = read;
    }

    return BYTES_VAL(buffer);
}

static Value vm_core_fs_write(int argCount, Value* args) {
    if (!args || argCount < 2) {
        return I64_VAL(0);
    }

    FILE* handle = vm_file_borrow_handle(args[0]);
    if (!handle) {
        return I64_VAL(0);
    }

    const uint8_t* data = NULL;
    size_t length = 0;
    if (!buffer_from_value(args[1], &data, &length) || length == 0) {
        return I64_VAL(0);
    }

    size_t written = fwrite(data, 1, length, handle);
    if (written < length && ferror(handle)) {
        clearerr(handle);
    }

    return I64_VAL((int64_t)written);
}

static Value vm_core_fs_seek(int argCount, Value* args) {
    if (!args || argCount < 2) {
        return BOOL_VAL(false);
    }

    FILE* handle = vm_file_borrow_handle(args[0]);
    if (!handle) {
        return BOOL_VAL(false);
    }

    int64_t offset = 0;
    if (!value_to_int64(args[1], &offset)) {
        return BOOL_VAL(false);
    }

    int origin = SEEK_SET;
    if (argCount > 2 && !value_to_int(args[2], &origin)) {
        return BOOL_VAL(false);
    }

    int result = 0;
#if defined(_WIN32)
    result = _fseeki64(handle, offset, origin);
#else
    result = fseeko(handle, (off_t)offset, origin);
#endif
    return BOOL_VAL(result == 0);
}

static Value vm_core_fs_tell(int argCount, Value* args) {
    (void)argCount;
    if (!args || argCount < 1) {
        return I64_VAL(-1);
    }

    FILE* handle = vm_file_borrow_handle(args[0]);
    if (!handle) {
        return I64_VAL(-1);
    }

#if defined(_WIN32)
    __int64 position = _ftelli64(handle);
    if (position < 0) {
        return I64_VAL(-1);
    }
    return I64_VAL((int64_t)position);
#else
    off_t position = ftello(handle);
    if (position < 0) {
        return I64_VAL(-1);
    }
    return I64_VAL((int64_t)position);
#endif
}

static Value vm_core_fs_flush(int argCount, Value* args) {
    (void)argCount;
    if (!args || argCount < 1) {
        return BOOL_VAL(false);
    }

    FILE* handle = vm_file_borrow_handle(args[0]);
    if (!handle) {
        return BOOL_VAL(false);
    }

    return BOOL_VAL(fflush(handle) == 0);
}

const IntrinsicSignatureInfo core_fs_intrinsic_signature_table[] = {
    {"__fs_open", 2, {TYPE_STRING, TYPE_STRING}, TYPE_ANY},
    {"__fs_close", 1, {TYPE_ANY}, TYPE_BOOL},
    {"__fs_is_open", 1, {TYPE_ANY}, TYPE_BOOL},
    {"__fs_read", 2, {TYPE_ANY, TYPE_I64}, TYPE_BYTES},
    {"__fs_write", 2, {TYPE_ANY, TYPE_BYTES}, TYPE_I64},
    {"__fs_seek", 3, {TYPE_ANY, TYPE_I64, TYPE_I32}, TYPE_BOOL},
    {"__fs_tell", 1, {TYPE_ANY}, TYPE_I64},
    {"__fs_flush", 1, {TYPE_ANY}, TYPE_BOOL},
};

const size_t core_fs_intrinsic_signature_table_count =
    sizeof(core_fs_intrinsic_signature_table) /
    sizeof(core_fs_intrinsic_signature_table[0]);

const IntrinsicBinding core_fs_intrinsic_bindings[] = {
    {"__fs_open", vm_core_fs_open},
    {"__fs_close", vm_core_fs_close},
    {"__fs_is_open", vm_core_fs_is_open},
    {"__fs_read", vm_core_fs_read},
    {"__fs_write", vm_core_fs_write},
    {"__fs_seek", vm_core_fs_seek},
    {"__fs_tell", vm_core_fs_tell},
    {"__fs_flush", vm_core_fs_flush},
};

const size_t core_fs_intrinsic_bindings_count =
    sizeof(core_fs_intrinsic_bindings) /
    sizeof(core_fs_intrinsic_bindings[0]);
