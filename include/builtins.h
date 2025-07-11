#ifndef ORUS_BUILTINS_H
#define ORUS_BUILTINS_H

#include "vm.h"
#include <stdbool.h>
#include <stdint.h>

// Print multiple values with optional newline.
void builtin_print(Value* args, int count, bool newline);

// High-precision timestamp function
// Returns milliseconds since an arbitrary but monotonic starting point
int32_t builtin_time_stamp(void);

#endif // ORUS_BUILTINS_H
