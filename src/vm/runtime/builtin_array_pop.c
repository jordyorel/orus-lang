/*
 * Orus Language Project
 * ---------------------------------------------------------------------------
 * File: src/vm/runtime/builtin_array_pop.c
 * Author: Jordy Orel KONDA
 * Description: Implements the builtin routine to pop values from arrays.
 */

#include "runtime/builtins.h"
#include "runtime/memory.h"

bool builtin_array_pop(Value array_value, Value* out_value) {
    if (!IS_ARRAY(array_value)) {
        return false;
    }

    ObjArray* array = AS_ARRAY(array_value);
    return arrayPop(array, out_value);
}
