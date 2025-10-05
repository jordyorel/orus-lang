/*
 * Orus Language Project
 * ---------------------------------------------------------------------------
 * File: include/runtime/builtins.h
 * Author: Jordy Orel KONDA
 * Copyright (c) 2025 Jordy Orel KONDA
 * License: MIT License (see LICENSE file in the project root)
 * Description: Declares runtime built-in functions and registration helpers for the
 *              VM.
 */

#ifndef ORUS_BUILTINS_H
#define ORUS_BUILTINS_H

#include "vm/vm.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/**
 * Print the provided values to standard output.
 *
 * @param args    Array of values to print.
 * @param count   Number of values in the array.
 * @param newline When true, append a newline after printing.
 */
void builtin_print(Value* args, int count, bool newline);

/**
 * Read a line of input from standard input, optionally displaying a prompt.
 *
 * @param args      Optional array containing a single prompt value.
 * @param count     Number of values provided in {@code args} (0 or 1).
 * @param out_value Receives the captured input as a string value.
 * @return true on success, false if input was unavailable or invalid arguments
 *         were supplied.
 */
bool builtin_input(Value* args, int count, Value* out_value);

/**
 * Obtain the textual runtime type for a value.
 *
 * @param value     Value whose runtime type name is requested.
 * @param out_value Receives the resulting string value on success.
 * @return true when the type name could be produced.
 */
bool builtin_typeof(Value value, Value* out_value);

/**
 * Determine whether a value's runtime type matches the provided identifier.
 *
 * @param value          Value to inspect.
 * @param type_identifier Expected type name as a string value.
 * @param out_value      Receives a boolean value indicating the result.
 * @return true on success, false when the comparison could not be performed.
 */
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

/**
 * Create a range iterator object mirroring Python-style semantics.
 *
 * @param args      Array of arguments describing the range bounds.
 * @param count     Number of arguments supplied (1..3).
 * @param out_value Receives the constructed range iterator on success.
 * @return true when the range arguments were valid and the iterator was created.
 */
bool builtin_range(Value* args, int count, Value* out_value);

/**
 * Push a value onto an array, growing the backing store when needed.
 *
 * @param array_value Value containing the target array.
 * @param element     Value to append.
 * @return true when the value was appended successfully.
 */
bool builtin_array_push(Value array_value, Value element);

/**
 * Pop a value from the end of an array.
 *
 * @param array_value Value containing the target array.
 * @param out_value   Receives the popped value on success.
 * @return true when a value was popped successfully.
 */
bool builtin_array_pop(Value array_value, Value* out_value);

/**
 * Produce a sorted copy of the provided array.
 *
 * @param array_value Value referencing the array to sort.
 * @param out_value   Receives the new sorted array on success.
 * @return true when sorting succeeded and {@code out_value} was populated.
 */
bool builtin_sorted(Value array_value, Value* out_value);

/**
 * Obtain a monotonic timestamp in seconds.
 *
 * The epoch is unspecified but monotonically increasing for the
 * duration of the process.
 */
double builtin_timestamp(void);

/**
 * Compare two values for equality and report detailed diagnostics when they
 * differ. Used by the integration tests to validate interpreter behaviour.
 *
 * @param label     Human-readable label describing the assertion context.
 * @param actual    The value observed during execution.
 * @param expected  The expected value.
 * @param out_message Optional pointer that receives an allocated failure
 *                    message when the assertion fails. The caller owns the
 *                    returned string and must free it with {@code free()}.
 * @return true when the values are equal, false otherwise.
 */
bool builtin_assert_eq(Value label, Value actual, Value expected, char** out_message);

#endif // ORUS_BUILTINS_H
