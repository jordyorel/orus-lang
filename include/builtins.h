#ifndef ORUS_BUILTINS_H
#define ORUS_BUILTINS_H

#include "vm.h"
#include <stdbool.h>

// Print multiple values with optional newline.
void builtin_print(Value* args, int count, bool newline);

#endif // ORUS_BUILTINS_H
