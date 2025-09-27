/*
 * Orus Language Project
 * ---------------------------------------------------------------------------
 * File: src/vm/runtime/builtin_type_common.h
 * Author: OpenAI Assistant
 * Description: Shared helpers for builtin type inspection routines.
 */

#ifndef ORUS_BUILTIN_TYPE_COMMON_H
#define ORUS_BUILTIN_TYPE_COMMON_H

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

#endif /* ORUS_BUILTIN_TYPE_COMMON_H */
