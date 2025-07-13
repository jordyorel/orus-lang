#ifndef ORUS_BUILTINS_H
#define ORUS_BUILTINS_H

#include "vm.h"
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
 * Obtain a monotonic timestamp in milliseconds.
 *
 * The epoch is unspecified but monotonically increasing for the
 * duration of the process.
 */
int32_t builtin_time_stamp(void);

#endif // ORUS_BUILTINS_H
