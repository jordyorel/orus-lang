// Orus Language Project
// ---------------------------------------------------------------------------
// File: src/vm/runtime/builtin_number.c
// Author: Jordy Orel KONDA
// Description: Implements numeric parsing helpers for builtin int() and float().


#include "runtime/builtins.h"

#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <stdarg.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LITERAL_PREVIEW 48

static const char* value_type_name(ValueType type) {
    switch (type) {
        case VAL_BOOL: return "bool";
        case VAL_I32: return "i32";
        case VAL_I64: return "i64";
        case VAL_U32: return "u32";
        case VAL_U64: return "u64";
        case VAL_F64: return "f64";
        case VAL_STRING: return "string";
        case VAL_ARRAY: return "array";
        case VAL_ENUM: return "enum";
        case VAL_ERROR: return "error";
        case VAL_RANGE_ITERATOR: return "range_iterator";
        case VAL_ARRAY_ITERATOR: return "array_iterator";
        default: return "value";
    }
}

static void format_string_preview(const ObjString* string, char* buffer, size_t size) {
    if (!buffer || size == 0) {
        return;
    }

    const char* chars = obj_string_chars((ObjString*)string);
    if (!string || !chars) {
        snprintf(buffer, size, "<null string>");
        return;
    }

    size_t limit = (size > 4) ? size - 4 : size;
    size_t copy_len = (size_t)string->length;
    bool truncated = false;
    if (copy_len > limit) {
        copy_len = limit;
        truncated = true;
    }

    if (truncated) {
        snprintf(buffer, size, "%.*s...", (int)copy_len, chars);
    } else {
        snprintf(buffer, size, "%.*s", (int)copy_len, chars);
    }
}

static bool string_contains_decimal_hint(const ObjString* string) {
    if (!string) {
        return false;
    }

    const char* chars = obj_string_chars((ObjString*)string);
    if (!chars) {
        return false;
    }

    for (int i = 0; i < string->length; i++) {
        char c = chars[i];
        if (c == '.' || c == 'e' || c == 'E') {
            return true;
        }
    }

    return false;
}

static bool parse_int_string(const ObjString* string, int32_t* out_value, bool* out_overflow) {
    if (!string || !out_value || !out_overflow) {
        return false;
    }

    const char* start = obj_string_chars((ObjString*)string);
    if (!start) {
        return false;
    }

    errno = 0;
    char* end = NULL;
    long value = strtol(start, &end, 10);

    if (errno == ERANGE || value < INT32_MIN || value > INT32_MAX) {
        *out_overflow = true;
        return false;
    }

    while (end && *end != '\0' && isspace((unsigned char)*end)) {
        end++;
    }

    if (!end || *end != '\0') {
        *out_overflow = false;
        return false;
    }

    *out_overflow = false;
    *out_value = (int32_t)value;
    return true;
}

static bool parse_float_string(const ObjString* string, double* out_value, bool* out_overflow) {
    if (!string || !out_value || !out_overflow) {
        return false;
    }

    const char* start = obj_string_chars((ObjString*)string);
    if (!start) {
        return false;
    }

    errno = 0;
    char* end = NULL;
    double value = strtod(start, &end);

    if (errno == ERANGE || !isfinite(value)) {
        *out_overflow = true;
        return false;
    }

    while (end && *end != '\0' && isspace((unsigned char)*end)) {
        end++;
    }

    if (!end || *end != '\0') {
        *out_overflow = false;
        return false;
    }

    *out_overflow = false;
    *out_value = value;
    return true;
}

static void write_message(char* message, size_t size, const char* format, ...) {
    if (!message || size == 0 || !format) {
        return;
    }

    va_list args;
    va_start(args, format);
    vsnprintf(message, size, format, args);
    va_end(args);
}

BuiltinParseResult builtin_parse_int(Value input, Value* out_value,
                                     char* message, size_t message_size) {
    if (!out_value) {
        write_message(message, message_size, "int() internal error: missing output pointer");
        return BUILTIN_PARSE_INVALID;
    }

    if (message && message_size > 0) {
        message[0] = '\0';
    }

    switch (input.type) {
        case VAL_I32:
            *out_value = input;
            return BUILTIN_PARSE_OK;

        case VAL_I64: {
            int64_t value = AS_I64(input);
            if (value < INT32_MIN || value > INT32_MAX) {
                write_message(message, message_size,
                              "int() overflow: %lld is out of range for i32",
                              (long long)value);
                return BUILTIN_PARSE_OVERFLOW;
            }
            *out_value = I32_VAL((int32_t)value);
            return BUILTIN_PARSE_OK;
        }

        case VAL_U32: {
            uint32_t value = AS_U32(input);
            if (value > (uint32_t)INT32_MAX) {
                write_message(message, message_size,
                              "int() overflow: %u is out of range for i32",
                              value);
                return BUILTIN_PARSE_OVERFLOW;
            }
            *out_value = I32_VAL((int32_t)value);
            return BUILTIN_PARSE_OK;
        }

        case VAL_U64: {
            uint64_t value = AS_U64(input);
            if (value > (uint64_t)INT32_MAX) {
                write_message(message, message_size,
                              "int() overflow: %llu is out of range for i32",
                              (unsigned long long)value);
                return BUILTIN_PARSE_OVERFLOW;
            }
            *out_value = I32_VAL((int32_t)value);
            return BUILTIN_PARSE_OK;
        }

        case VAL_F64: {
            double value = AS_F64(input);
            if (!isfinite(value)) {
                write_message(message, message_size,
                              "int() overflow: value is not finite");
                return BUILTIN_PARSE_OVERFLOW;
            }
            double integral;
            double fractional = modf(value, &integral);
            if (fabs(fractional) > DBL_EPSILON) {
                write_message(message, message_size,
                              "int() argument must be a whole number, got f64 %g",
                              value);
                return BUILTIN_PARSE_INVALID;
            }
            if (integral < INT32_MIN || integral > INT32_MAX) {
                write_message(message, message_size,
                              "int() overflow: %g is out of range for i32",
                              value);
                return BUILTIN_PARSE_OVERFLOW;
            }
            *out_value = I32_VAL((int32_t)integral);
            return BUILTIN_PARSE_OK;
        }

        case VAL_STRING: {
            ObjString* string = AS_STRING(input);
            int32_t parsed = 0;
            bool overflow = false;
            if (parse_int_string(string, &parsed, &overflow)) {
                *out_value = I32_VAL(parsed);
                return BUILTIN_PARSE_OK;
            }

            char preview[MAX_LITERAL_PREVIEW + 8];
            format_string_preview(string, preview, sizeof(preview));

            if (overflow) {
                write_message(message, message_size,
                              "int() overflow: \"%s\" is out of range for i32",
                              preview);
                return BUILTIN_PARSE_OVERFLOW;
            }

            if (string_contains_decimal_hint(string)) {
                write_message(message, message_size,
                              "int() argument must be an integer string (decimals are not allowed). "
                              "Use float() to parse decimal values, got \"%s\"",
                              preview);
            } else {
                write_message(message, message_size,
                              "int() argument must be an integer string, got \"%s\"",
                              preview);
            }
            return BUILTIN_PARSE_INVALID;
        }

        default:
            write_message(message, message_size,
                          "int() argument must be a string or number, got %s",
                          value_type_name(input.type));
            return BUILTIN_PARSE_INVALID;
    }
}

BuiltinParseResult builtin_parse_float(Value input, Value* out_value,
                                       char* message, size_t message_size) {
    if (!out_value) {
        write_message(message, message_size, "float() internal error: missing output pointer");
        return BUILTIN_PARSE_INVALID;
    }

    if (message && message_size > 0) {
        message[0] = '\0';
    }

    switch (input.type) {
        case VAL_F64:
            *out_value = input;
            return BUILTIN_PARSE_OK;

        case VAL_I32:
            *out_value = F64_VAL((double)AS_I32(input));
            return BUILTIN_PARSE_OK;

        case VAL_I64:
            *out_value = F64_VAL((double)AS_I64(input));
            return BUILTIN_PARSE_OK;

        case VAL_U32:
            *out_value = F64_VAL((double)AS_U32(input));
            return BUILTIN_PARSE_OK;

        case VAL_U64:
            *out_value = F64_VAL((double)AS_U64(input));
            return BUILTIN_PARSE_OK;

        case VAL_STRING: {
            ObjString* string = AS_STRING(input);
            double parsed = 0.0;
            bool overflow = false;
            if (parse_float_string(string, &parsed, &overflow)) {
                *out_value = F64_VAL(parsed);
                return BUILTIN_PARSE_OK;
            }

            char preview[MAX_LITERAL_PREVIEW + 8];
            format_string_preview(string, preview, sizeof(preview));

            if (overflow) {
                write_message(message, message_size,
                              "float() overflow: \"%s\" is out of range for f64",
                              preview);
                return BUILTIN_PARSE_OVERFLOW;
            }

            write_message(message, message_size,
                          "float() argument must be a number, got \"%s\"",
                          preview);
            return BUILTIN_PARSE_INVALID;
        }

        default:
            write_message(message, message_size,
                          "float() argument must be a string or number, got %s",
                          value_type_name(input.type));
            return BUILTIN_PARSE_INVALID;
    }
}

#undef MAX_LITERAL_PREVIEW
