//  Orus Language Project

#ifndef ORUS_BUILTINS_H
#define ORUS_BUILTINS_H

#include "vm/vm.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>


void builtin_print(Value* args, int count, bool newline);


bool builtin_input(Value* args, int count, Value* out_value);

bool builtin_typeof(Value value, Value* out_value);

bool builtin_istype(Value value, Value type_identifier, Value* out_value);

typedef enum {
    BUILTIN_PARSE_OK = 0,
    BUILTIN_PARSE_INVALID,
    BUILTIN_PARSE_OVERFLOW
} BuiltinParseResult;

BuiltinParseResult builtin_parse_int(Value input, Value* out_value,
                                     char* message, size_t message_size);
BuiltinParseResult builtin_parse_float(Value input, Value* out_value,
                                       char* message, size_t message_size);

bool builtin_range(Value* args, int count, Value* out_value);

bool builtin_array_push(Value array_value, Value element);

bool builtin_array_pop(Value array_value, Value* out_value);

bool builtin_array_repeat(Value array_value, Value count_value, Value* out_value);

bool builtin_sorted(Value array_value, Value* out_value);

double builtin_timestamp(void);

bool builtin_assert_eq(Value label, Value actual, Value expected, char** out_message);

#endif // ORUS_BUILTINS_H
