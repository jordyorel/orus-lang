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
 * Obtain a monotonic timestamp in milliseconds.
 *
 * The epoch is unspecified but monotonically increasing for the
 * duration of the process.
 */
int32_t builtin_time_stamp(void);

#endif // ORUS_BUILTINS_H
