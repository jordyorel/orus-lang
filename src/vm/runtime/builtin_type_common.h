// Orus Language Project
// ---------------------------------------------------------------------------
// File: src/vm/runtime/builtin_type_common.h
// Author: JORDY OREL KONDA
// Description: Shared helpers for builtin type inspection routines.


#ifndef ORUS_BUILTIN_TYPE_COMMON_H
#define ORUS_BUILTIN_TYPE_COMMON_H

#include <stddef.h>
#include <string.h>

#include "runtime/memory.h"
#include "vm/vm.h"

static inline const char* builtin_value_type_label(Value value) {
    switch (value.type) {
        case VAL_BOOL: return "bool";
        case VAL_I32: return "i32";
        case VAL_I64: return "i64";
        case VAL_U32: return "u32";
        case VAL_U64: return "u64";
        case VAL_F64: return "f64";
        case VAL_NUMBER: return "number";
        case VAL_STRING: return "string";
        case VAL_ARRAY: return "array";
        case VAL_ENUM: {
            ObjEnumInstance* instance = AS_ENUM(value);
            if (instance && instance->typeName && instance->typeName->chars) {
                return instance->typeName->chars;
            }
            return "enum";
        }
        case VAL_ERROR: return "error";
        case VAL_RANGE_ITERATOR: return "range_iterator";
        case VAL_ARRAY_ITERATOR: return "array_iterator";
        case VAL_FUNCTION: return "function";
        case VAL_CLOSURE: return "function";
        default: return "unknown";
    }
}

static inline const char* builtin_error_type_name(ErrorType type) {
    switch (type) {
        case ERROR_RUNTIME: return "runtime error";
        case ERROR_TYPE: return "type error";
        case ERROR_NAME: return "name error";
        case ERROR_INDEX: return "index error";
        case ERROR_KEY: return "key error";
        case ERROR_VALUE: return "value error";
        case ERROR_CONVERSION: return "conversion error";
        case ERROR_ARGUMENT: return "argument error";
        case ERROR_IMPORT: return "import error";
        case ERROR_ATTRIBUTE: return "attribute error";
        case ERROR_UNIMPLEMENTED: return "unimplemented error";
        case ERROR_SYNTAX: return "syntax error";
        case ERROR_INDENT: return "indentation error";
        case ERROR_TAB: return "tab error";
        case ERROR_RECURSION: return "recursion error";
        case ERROR_IO: return "io error";
        case ERROR_OS: return "os error";
        case ERROR_EOF: return "eof error";
        default: return "error";
    }
}

static inline bool builtin_alloc_error_label(
    Value value,
    char** out_chars,
    size_t* out_length,
    size_t* out_capacity) {
    if (!IS_ERROR(value) || !out_chars || !out_length || !out_capacity) {
        return false;
    }

    ObjError* error = AS_ERROR(value);
    if (!error) {
        return false;
    }

    const char* type_name = builtin_error_type_name(error->type);
    if (!type_name) {
        type_name = "error";
    }

    const char* message =
        (error->message && error->message->chars) ? error->message->chars : "";

    size_t type_len = strlen(type_name);
    size_t message_len = strlen(message);

    bool include_space = message_len > 0;
    size_t total_len = type_len + (include_space ? 1 + message_len : 0);
    size_t capacity = total_len + 1;

    char* buffer = (char*)reallocate(NULL, 0, capacity);
    if (!buffer) {
        return false;
    }

    size_t offset = 0;
    if (type_len > 0) {
        memcpy(buffer + offset, type_name, type_len);
        offset += type_len;
    }

    if (include_space) {
        buffer[offset++] = ' ';
        memcpy(buffer + offset, message, message_len);
        offset += message_len;
    }

    buffer[offset] = '\0';

    *out_chars = buffer;
    *out_length = total_len;
    *out_capacity = capacity;
    return true;
}

#endif /* ORUS_BUILTIN_TYPE_COMMON_H */
