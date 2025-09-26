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

/**
 * Print the provided values to standard output.
 *
 * @param args   Array of values to print.
 * @param count  Number of values in the array.
 * @param newline When true, append a newline after printing.
 */
void builtin_print(Value* args, int count, bool newline);

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
 * Obtain a monotonic timestamp in milliseconds.
 *
 * The epoch is unspecified but monotonically increasing for the
 * duration of the process.
 */
double builtin_time_stamp();

#endif // ORUS_BUILTINS_H
