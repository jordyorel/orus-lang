/*
 * Orus Language Project
 * ---------------------------------------------------------------------------
 * File: src/vm/runtime/builtin_array_push.c
 * Author: Jordy Orel KONDA
 * Description: Implements the builtin routine to append values to arrays.
 */

#include "runtime/builtins.h"
#include "runtime/memory.h"

bool builtin_array_push(Value array_value, Value element) {
    if (!IS_ARRAY(array_value)) {
        return false;
    }

    ObjArray* array = AS_ARRAY(array_value);
    return arrayPush(array, element);
}
