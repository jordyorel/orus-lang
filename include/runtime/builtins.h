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
 * @param separator String to use between values (NULL for default space).
 */
void builtin_print(Value* args, int count, bool newline, const char* separator);

/**
 * Print with separator from a Value (for runtime separator support).
 *
 * @param args   Array of values to print.
 * @param count  Number of values in the array.
 * @param newline When true, append a newline after printing.
 * @param separator_value Value containing separator string.
 */
void builtin_print_with_sep_value(Value* args, int count, bool newline, Value separator_value);

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
