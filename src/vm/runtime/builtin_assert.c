// Orus Language Project
//  ---------------------------------------------------------------------------
//  File: src/vm/runtime/builtin_assert.c
//  Author: Jordy Orel KONDA
//  Description: Implements the assert_eq builtin used by the test suite to validate interpreter behaviour at runtime.
 

#include "runtime/builtins.h"

#include "runtime/memory.h"
#include "vm/vm.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char* data;
    size_t length;
    size_t capacity;
} AssertStringBuilder;

static bool sb_init(AssertStringBuilder* sb) {
    if (!sb) {
        return false;
    }
    sb->capacity = 128;
    sb->length = 0;
    sb->data = (char*)malloc(sb->capacity);
    if (!sb->data) {
        sb->capacity = 0;
        return false;
    }
    sb->data[0] = '\0';
    return true;
}

static bool sb_reserve(AssertStringBuilder* sb, size_t additional) {
    if (!sb || !sb->data) {
        return false;
    }
    size_t required = sb->length + additional + 1;
    if (required <= sb->capacity) {
        return true;
    }
    size_t new_capacity = sb->capacity ? sb->capacity : 16;
    while (new_capacity < required) {
        new_capacity *= 2;
    }
    char* new_data = (char*)realloc(sb->data, new_capacity);
    if (!new_data) {
        return false;
    }
    sb->data = new_data;
    sb->capacity = new_capacity;
    return true;
}

static bool sb_append(AssertStringBuilder* sb, const char* text) {
    if (!text) {
        text = "";
    }
    size_t len = strlen(text);
    if (!sb_reserve(sb, len)) {
        return false;
    }
    memcpy(sb->data + sb->length, text, len);
    sb->length += len;
    sb->data[sb->length] = '\0';
    return true;
}

static bool sb_append_char(AssertStringBuilder* sb, char c) {
    if (!sb_reserve(sb, 1)) {
        return false;
    }
    sb->data[sb->length++] = c;
    sb->data[sb->length] = '\0';
    return true;
}

static bool sb_append_format(AssertStringBuilder* sb, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    va_list copy;
    va_copy(copy, args);
    int needed = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (needed < 0) {
        va_end(args);
        return false;
    }
    if (!sb_reserve(sb, (size_t)needed)) {
        va_end(args);
        return false;
    }
    vsnprintf(sb->data + sb->length, sb->capacity - sb->length, fmt, args);
    sb->length += (size_t)needed;
    va_end(args);
    return true;
}

static void sb_free(AssertStringBuilder* sb) {
    if (!sb) {
        return;
    }
    free(sb->data);
    sb->data = NULL;
    sb->length = 0;
    sb->capacity = 0;
}

static bool deep_value_equal(Value a, Value b);
static bool append_value_repr(AssertStringBuilder* sb, Value value);

static bool append_array_repr(AssertStringBuilder* sb, ObjArray* array) {
    if (!sb_append_char(sb, '[')) {
        return false;
    }
    if (!array || array->length == 0) {
        return sb_append_char(sb, ']');
    }
    for (int i = 0; i < array->length; i++) {
        if (i > 0) {
            if (!sb_append(sb, ", ")) {
                return false;
            }
        }
        if (!append_value_repr(sb, array->elements[i])) {
            return false;
        }
    }
    return sb_append_char(sb, ']');
}

static bool append_enum_repr(AssertStringBuilder* sb, ObjEnumInstance* inst) {
    if (!inst) {
        return sb_append(sb, "<enum>");
    }
    const char* type_name =
        (inst->typeName) ? obj_string_chars(inst->typeName) : "<enum>";
    const char* variant_name =
        (inst->variantName) ? obj_string_chars(inst->variantName) : "<variant>";
    if (!sb_append(sb, type_name)) {
        return false;
    }
    if (!sb_append_char(sb, '.')) {
        return false;
    }
    if (!sb_append(sb, variant_name)) {
        return false;
    }
    ObjArray* payload = inst->payload;
    if (!payload || payload->length == 0) {
        return true;
    }
    if (!sb_append_char(sb, '(')) {
        return false;
    }
    for (int i = 0; i < payload->length; i++) {
        if (i > 0) {
            if (!sb_append(sb, ", ")) {
                return false;
            }
        }
        if (!append_value_repr(sb, payload->elements[i])) {
            return false;
        }
    }
    return sb_append_char(sb, ')');
}

static bool append_string_repr(AssertStringBuilder* sb, ObjString* str) {
    if (!sb_append_char(sb, '"')) {
        return false;
    }
    if (str) {
        const char* chars = obj_string_chars(str);
        for (int i = 0; chars && i < str->length; i++) {
            char c = chars[i];
            switch (c) {
                case '\\':
                    if (!sb_append(sb, "\\\\")) return false;
                    break;
                case '\n':
                    if (!sb_append(sb, "\\n")) return false;
                    break;
                case '\t':
                    if (!sb_append(sb, "\\t")) return false;
                    break;
                case '"':
                    if (!sb_append(sb, "\\\"")) return false;
                    break;
                default:
                    if (!sb_append_char(sb, c)) return false;
                    break;
            }
        }
    }
    return sb_append_char(sb, '"');
}

static bool append_value_repr(AssertStringBuilder* sb, Value value) {
    switch (value.type) {
        case VAL_BOOL:
            return sb_append(sb, AS_BOOL(value) ? "true" : "false");
        case VAL_I32:
            return sb_append_format(sb, "%d", AS_I32(value));
        case VAL_I64:
            return sb_append_format(sb, "%lld", (long long)AS_I64(value));
        case VAL_U32:
            return sb_append_format(sb, "%u", AS_U32(value));
        case VAL_U64:
            return sb_append_format(sb, "%llu", (unsigned long long)AS_U64(value));
        case VAL_F64:
            return sb_append_format(sb, "%g", AS_F64(value));
        case VAL_STRING:
            return append_string_repr(sb, AS_STRING(value));
        case VAL_ARRAY:
            return append_array_repr(sb, AS_ARRAY(value));
        case VAL_ENUM:
            return append_enum_repr(sb, AS_ENUM(value));
        case VAL_RANGE_ITERATOR:
            return sb_append(sb, "<range>");
        case VAL_ARRAY_ITERATOR:
            return sb_append(sb, "<array-iter>");
        case VAL_ERROR:
            if (AS_ERROR(value) && AS_ERROR(value)->message) {
                return sb_append_format(sb, "Error(%s)",
                                         obj_string_chars(AS_ERROR(value)->message));
            }
            return sb_append(sb, "Error");
        case VAL_FUNCTION:
            return sb_append(sb, "<function>");
        case VAL_CLOSURE:
            return sb_append(sb, "<closure>");
        default:
            return sb_append(sb, "<value>");
    }
}

static bool deep_value_equal(Value a, Value b) {
    if (a.type != b.type) {
        return false;
    }
    switch (a.type) {
        case VAL_ARRAY: {
            ObjArray* left = AS_ARRAY(a);
            ObjArray* right = AS_ARRAY(b);
            if (!left || !right) {
                return left == right;
            }
            if (left->length != right->length) {
                return false;
            }
            for (int i = 0; i < left->length; i++) {
                if (!deep_value_equal(left->elements[i], right->elements[i])) {
                    return false;
                }
            }
            return true;
        }
        case VAL_ENUM: {
            ObjEnumInstance* left = AS_ENUM(a);
            ObjEnumInstance* right = AS_ENUM(b);
            if (!left || !right) {
                return left == right;
            }
            if (left->typeName != right->typeName ||
                left->variantIndex != right->variantIndex) {
                return false;
            }
            ObjArray* left_payload = left->payload;
            ObjArray* right_payload = right->payload;
            int left_len = left_payload ? left_payload->length : 0;
            int right_len = right_payload ? right_payload->length : 0;
            if (left_len != right_len) {
                return false;
            }
            for (int i = 0; i < left_len; i++) {
                if (!deep_value_equal(left_payload->elements[i],
                                      right_payload->elements[i])) {
                    return false;
                }
            }
            return true;
        }
        default:
            return valuesEqual(a, b);
    }
}

static char* duplicate_cstring(const char* text) {
    if (!text) {
        text = "";
    }
    size_t len = strlen(text);
    char* copy = (char*)malloc(len + 1);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, text, len + 1);
    return copy;
}

bool builtin_assert_eq(Value label, Value actual, Value expected, char** out_message) {
    if (out_message) {
        *out_message = NULL;
    }

    const char* label_text = NULL;
    char* label_owned = NULL;
    if (IS_STRING(label) && AS_STRING(label)) {
        label_text = obj_string_chars(AS_STRING(label));
    } else {
        AssertStringBuilder label_builder;
        if (!sb_init(&label_builder)) {
            if (out_message) {
                *out_message = duplicate_cstring("assert_eq failed: out of memory");
            }
            return false;
        }
        if (!append_value_repr(&label_builder, label)) {
            sb_free(&label_builder);
            if (out_message) {
                *out_message = duplicate_cstring("assert_eq failed: unable to render label");
            }
            return false;
        }
        label_owned = label_builder.data;
        label_text = label_owned;
    }

    bool equal = deep_value_equal(actual, expected);
    if (equal) {
        free(label_owned);
        return true;
    }

    AssertStringBuilder message;
    if (!sb_init(&message)) {
        free(label_owned);
        if (out_message) {
            *out_message = duplicate_cstring("assert_eq failed (out of memory)");
        }
        return false;
    }

    if (!sb_append(&message, "assert_eq failed")) {
        sb_free(&message);
        free(label_owned);
        if (out_message) {
            *out_message = duplicate_cstring("assert_eq failed");
        }
        return false;
    }

    if (label_text && label_text[0] != '\0') {
        if (!sb_append(&message, " (")) {
            sb_free(&message);
            free(label_owned);
            if (out_message) {
                *out_message = duplicate_cstring("assert_eq failed");
            }
            return false;
        }
        if (!sb_append(&message, label_text)) {
            sb_free(&message);
            free(label_owned);
            if (out_message) {
                *out_message = duplicate_cstring("assert_eq failed");
            }
            return false;
        }
        if (!sb_append_char(&message, ')')) {
            sb_free(&message);
            free(label_owned);
            if (out_message) {
                *out_message = duplicate_cstring("assert_eq failed");
            }
            return false;
        }
    }

    if (!sb_append(&message, "\n  expected: ")) {
        sb_free(&message);
        free(label_owned);
        if (out_message) {
            *out_message = duplicate_cstring("assert_eq failed");
        }
        return false;
    }
    if (!append_value_repr(&message, expected)) {
        sb_free(&message);
        free(label_owned);
        if (out_message) {
            *out_message = duplicate_cstring("assert_eq failed");
        }
        return false;
    }

    if (!sb_append(&message, "\n  actual:   ")) {
        sb_free(&message);
        free(label_owned);
        if (out_message) {
            *out_message = duplicate_cstring("assert_eq failed");
        }
        return false;
    }
    if (!append_value_repr(&message, actual)) {
        sb_free(&message);
        free(label_owned);
        if (out_message) {
            *out_message = duplicate_cstring("assert_eq failed");
        }
        return false;
    }

    if (out_message) {
        *out_message = message.data;
    } else {
        sb_free(&message);
    }

    free(label_owned);
    return false;
}
